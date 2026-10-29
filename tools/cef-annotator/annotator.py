#!/usr/bin/env python3
"""
Steam CEF Annotator Daemon & CLI
Manages target discovery, WebSocket lifecycle, isolated/main world injection,
hardened binding callbacks, element screenshots, and dual storage (Vibe MCP + local JSON).
"""

import argparse
import base64
import json
import os
import random
import re
import signal
import string
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
import uuid
import websocket

LOCAL_DIR = os.path.dirname(os.path.abspath(__file__))
ANNOTATIONS_FILE = os.path.join(LOCAL_DIR, "annotations.json")
OVERLAY_JS_PATH = os.path.join(LOCAL_DIR, "overlay.js")
VIBE_SERVER_URL = "http://127.0.0.1:3846"
RATE_LIMIT_MAX_PER_SEC = 5

def generate_random_token(prefix="__annot_", length=8):
    return prefix + ''.join(random.choices(string.ascii_lowercase + string.digits, k=length))

def probe_json_targets():
    """Probes IPv4 (127.0.0.1) and IPv6 ([::1]) on port 8080."""
    for host in ("127.0.0.1", "[::1]"):
        url = f"http://{host}:8080/json"
        try:
            req = urllib.request.Request(url, headers={"User-Agent": "CEF-Annotator"})
            with urllib.request.urlopen(req, timeout=1.5) as r:
                return json.loads(r.read().decode())
        except Exception:
            continue
    return None

def extract_shell_route_hint(targets):
    """Finds the shell tracking target to extract route hint (e.g. /library/home)."""
    for t in targets:
        url = urllib.parse.unquote(t.get("url", ""))
        if "<!--tracking:" in url:
            m = re.search(r"<!--tracking:[^:]*:([^>]+)-->", url)
            if m:
                return m.group(1).strip()
    return ""

def load_local_annotations():
    if not os.path.exists(ANNOTATIONS_FILE):
        return []
    try:
        with open(ANNOTATIONS_FILE, "r", encoding="utf-8") as f:
            return json.load(f)
    except Exception as e:
        print(f"[Warn] Failed reading {ANNOTATIONS_FILE}: {e}", file=sys.stderr)
        return []

def save_local_annotations(annotations):
    """Atomic write via temporary file and rename."""
    tmp_fd, tmp_path = tempfile.mkstemp(dir=LOCAL_DIR, prefix="annotations_", suffix=".tmp")
    try:
        with os.fdopen(tmp_fd, "w", encoding="utf-8") as f:
            json.dump(annotations, f, indent=2)
        os.replace(tmp_path, ANNOTATIONS_FILE)
    except Exception as e:
        if os.path.exists(tmp_path):
            os.remove(tmp_path)
        raise e

def post_to_vibe_server(annotation, screenshot_bytes=None):
    """Posts annotation and optional screenshot attachment to Vibe server."""
    # 1. POST annotation
    annot_url = f"{VIBE_SERVER_URL}/api/annotations"
    try:
        data = json.dumps(annotation).encode("utf-8")
        req = urllib.request.Request(annot_url, data=data, headers={"Content-Type": "application/json"})
        with urllib.request.urlopen(req, timeout=2.0) as resp:
            pass
    except Exception as e:
        print(f"[Warn] Vibe server POST /api/annotations failed: {e}", file=sys.stderr)
        return False

    # 2. POST screenshot attachment if provided
    if screenshot_bytes:
        att_url = f"{VIBE_SERVER_URL}/api/annotations/{annotation['id']}/attachments"
        try:
            req = urllib.request.Request(
                att_url,
                data=screenshot_bytes,
                headers={
                    "Content-Type": "image/png",
                    "x-attachment-kind": "capture"
                }
            )
            with urllib.request.urlopen(req, timeout=3.0) as resp:
                pass
        except Exception as e:
            print(f"[Warn] Vibe server attachment POST failed: {e}", file=sys.stderr)

    return True

def delete_from_vibe_server(annot_id):
    """Deletes annotation from Vibe server by ID."""
    annot_url = f"{VIBE_SERVER_URL}/api/annotations/{annot_id}"
    try:
        req = urllib.request.Request(annot_url, method="DELETE")
        with urllib.request.urlopen(req, timeout=2.0) as resp:
            pass
        return True
    except Exception as e:
        print(f"[Warn] Vibe server DELETE {annot_url} failed: {e}", file=sys.stderr)
        return False

class TargetSession:
    """Manages CDP session on a single target WebSocket."""
    def __init__(self, target_info, world_mode, route_hint, initial_annotations):
        self.target_id = target_info["id"]
        self.ws_url = target_info["webSocketDebuggerUrl"]
        self.title = target_info.get("title", "")
        self.url = target_info.get("url", "")
        self.world_mode = world_mode  # 'main' or 'isolated'
        self.route_hint = route_hint
        self.initial_annotations = initial_annotations

        self.ws = None
        self.msg_id = 0
        self.binding_name = generate_random_token("__annot_")
        self.guard_name = generate_random_token("__guard_")
        self.host_id = generate_random_token("__cef_annot_host_")
        self.api_name = generate_random_token("__cef_annot_api_")
        self.script_identifier = None
        self.isolated_context_id = None
        self.main_context_id = None
        self.root_frame_id = None
        self.valid_context_ids = set()
        self.event_queue = []
        self.is_attached = False

        # Rate limiting
        self.save_timestamps = []

    def connect(self):
        self.ws = websocket.create_connection(self.ws_url, timeout=5.0)
        self.ws.settimeout(0.1)

    def send_cmd(self, method, params=None, timeout=4.0):
        self.msg_id += 1
        cid = self.msg_id
        req = {"id": cid, "method": method}
        if params:
            req["params"] = params
        self.ws.send(json.dumps(req))

        start = time.time()
        while time.time() - start < timeout:
            try:
                raw = self.ws.recv()
                msg = json.loads(raw)
                if msg.get("id") == cid:
                    return msg
                else:
                    self.event_queue.append(msg)
            except websocket.WebSocketTimeoutException:
                continue
        raise TimeoutError(f"Timeout waiting for command {method} (id={cid})")

    def attach_and_inject(self):
        self.send_cmd("Page.enable")
        self.send_cmd("Runtime.enable")

        # Get root frame ID
        ft = self.send_cmd("Page.getFrameTree")
        self.root_frame_id = ft["result"]["frameTree"]["frame"]["id"]

        # Check shell population if in main world mode
        if self.world_mode == "main":
            body_res = self.send_cmd("Runtime.evaluate", {
                "expression": "document.body ? document.body.innerHTML.length : 0",
                "returnByValue": True
            })
            body_len = body_res.get("result", {}).get("result", {}).get("value", 0)
            if body_len < 1000:
                print(f"[Skip] Shell target {self.target_id[:8]} body length {body_len} < 1000 (not populated yet)")
                return

        # Read template overlay.js
        with open(OVERLAY_JS_PATH, "r", encoding="utf-8") as f:
            template = f.read()

        filtered_annots = [
            a for a in self.initial_annotations
            if a.get("url") and (
                (self.route_hint and a.get("element_context", {}).get("pageContext", {}).get("routeHint") == self.route_hint) or
                (not self.route_hint and a.get("url", "").split("?")[0] == self.url.split("?")[0])
            )
        ]

        # Template substitutions
        script_source = template.replace("__GUARD_NAME__", self.guard_name)
        script_source = script_source.replace("__BINDING_NAME__", self.binding_name)
        script_source = script_source.replace("__HOST_ID__", self.host_id)
        script_source = script_source.replace("__API_NAME__", self.api_name)
        script_source = script_source.replace("__TARGET_ID__", self.target_id)
        script_source = script_source.replace("__WORLD_MODE__", self.world_mode)
        script_source = script_source.replace("__ROUTE_HINT__", self.route_hint or "")
        script_source = script_source.replace("__INITIAL_ANNOTATIONS__", json.dumps(filtered_annots))

        if self.world_mode == "isolated":
            world_name = "SteamAnnotator"
            # 1. Register script to evaluate on new document in isolated world
            doc_res = self.send_cmd("Page.addScriptToEvaluateOnNewDocument", {
                "source": script_source,
                "worldName": world_name
            })
            self.script_identifier = doc_res.get("result", {}).get("identifier")

            # 2. Add binding scoped to isolated world
            self.send_cmd("Runtime.addBinding", {
                "name": self.binding_name,
                "executionContextName": world_name
            })

            # 3. Create isolated world on current root frame and evaluate immediately
            iso_res = self.send_cmd("Page.createIsolatedWorld", {
                "frameId": self.root_frame_id,
                "worldName": world_name,
                "grantUniversalAccess": False
            })
            self.isolated_context_id = iso_res["result"]["executionContextId"]
            self.valid_context_ids.add(self.isolated_context_id)

            # Evaluate script in isolated context
            self.send_cmd("Runtime.evaluate", {
                "expression": script_source,
                "contextId": self.isolated_context_id,
                "returnByValue": True
            })

            # Security verification
            main_check = self.send_cmd("Runtime.evaluate", {
                "expression": f"typeof window['{self.binding_name}']",
                "returnByValue": True
            })
            iso_check = self.send_cmd("Runtime.evaluate", {
                "expression": f"typeof window['{self.binding_name}']",
                "contextId": self.isolated_context_id,
                "returnByValue": True
            })
            main_typeof = main_check.get("result", {}).get("result", {}).get("value")
            iso_typeof = iso_check.get("result", {}).get("result", {}).get("value")

            if main_typeof != "undefined" or iso_typeof != "function":
                print(f"[Warn] Isolated world check failed: main={main_typeof}, iso={iso_typeof}", file=sys.stderr)

        else:
            # Main world mode (Steam shell)
            try:
                self.send_cmd("Runtime.evaluate", {
                    "expression": "Object.keys(window).filter(k => k.startsWith('__cef_annot_api_')).forEach(k => { try { window[k].cleanup(); } catch(e){} delete window[k]; }); document.querySelectorAll('[id^=__cef_annot_host]').forEach(el => el.remove());",
                    "returnByValue": True
                }, timeout=1.0)
            except Exception:
                pass

            doc_res = self.send_cmd("Page.addScriptToEvaluateOnNewDocument", {
                "source": script_source
            })
            self.script_identifier = doc_res.get("result", {}).get("identifier")

            self.send_cmd("Runtime.addBinding", {
                "name": self.binding_name
            })

            eval_res = self.send_cmd("Runtime.evaluate", {
                "expression": script_source,
                "returnByValue": True
            })

            # Harvest top-level main context from initial events
            for msg in self.event_queue:
                if msg.get("method") == "Runtime.executionContextCreated":
                    ctx = msg.get("params", {}).get("context", {})
                    aux = ctx.get("auxData", {})
                    if aux.get("frameId") == self.root_frame_id and aux.get("isDefault", True):
                        self.main_context_id = ctx.get("id")
                        self.valid_context_ids.add(ctx.get("id"))

        self.is_attached = True
        print(f"[Attached] Target {self.target_id[:8]} ({self.world_mode} mode): {self.title or self.url[:40]}")

    def update_pins(self, all_annotations):
        """Pushes updated annotations list to overlay."""
        if not self.is_attached:
            return
        api_name = self.api_name
        payload = json.dumps(all_annotations)
        expr = f"window['{api_name}']?.updatePins({payload})"
        params = {"expression": expr, "returnByValue": True}
        if self.world_mode == "isolated" and self.isolated_context_id:
            params["contextId"] = self.isolated_context_id
        try:
            self.send_cmd("Runtime.evaluate", params, timeout=1.0)
        except Exception:
            pass

    def update_route_hint(self, new_route):
        """Updates route hint and re-filters pins on the shell."""
        if not self.is_attached or self.world_mode != "main":
            return
        self.route_hint = new_route
        api_name = self.api_name
        expr = f"window['{api_name}']?.setRouteHint({json.dumps(new_route)})"
        try:
            self.send_cmd("Runtime.evaluate", {"expression": expr, "returnByValue": True}, timeout=1.0)
        except Exception:
            pass

    def send_heartbeat(self):
        """Refreshes overlay heartbeat timestamp."""
        if not self.is_attached:
            return
        api_name = self.api_name
        expr = f"window['{api_name}']?.heartbeat()"
        params = {"expression": expr, "returnByValue": True}
        if self.world_mode == "isolated" and self.isolated_context_id:
            params["contextId"] = self.isolated_context_id
        try:
            self.send_cmd("Runtime.evaluate", params, timeout=1.0)
        except Exception:
            pass

    def toggle_overlay(self):
        """Toggles annotate mode via external API."""
        if not self.is_attached:
            return
        api_name = self.api_name
        expr = f"window['{api_name}']?.toggle()"
        params = {"expression": expr, "returnByValue": True}
        if self.world_mode == "isolated" and self.isolated_context_id:
            params["contextId"] = self.isolated_context_id
        try:
            self.send_cmd("Runtime.evaluate", params, timeout=1.5)
        except Exception as e:
            print(f"[Warn] Failed toggle on target {self.target_id[:8]}: {e}", file=sys.stderr)

    def capture_element_screenshot(self, rect):
        """Hides overlay host, captures clip screenshot, and restores overlay."""
        api_name = self.api_name
        eval_params = {"returnByValue": True}
        if self.world_mode == "isolated" and self.isolated_context_id:
            eval_params["contextId"] = self.isolated_context_id

        # 1. Hide overlay
        eval_params["expression"] = f"window['{api_name}']?.hideForScreenshot()"
        try:
            self.send_cmd("Runtime.evaluate", eval_params, timeout=1.0)
        except Exception:
            pass

        screenshot_bytes = None
        try:
            # Check rect validity
            x = rect.get("x", 0)
            y = rect.get("y", 0)
            w = rect.get("width", 0)
            h = rect.get("height", 0)
            if w > 0 and h > 0 and (x + w) > 0 and (y + h) > 0:
                shot_res = self.send_cmd("Page.captureScreenshot", {
                    "format": "png",
                    "clip": {
                        "x": x,
                        "y": y,
                        "width": w,
                        "height": h,
                        "scale": 1
                    }
                }, timeout=3.0)
                data = shot_res.get("result", {}).get("data")
                if data:
                    screenshot_bytes = base64.b64decode(data)
        except Exception as e:
            print(f"[Warn] Screenshot capture failed: {e}", file=sys.stderr)
        finally:
            # 2. Restore overlay
            eval_params["expression"] = f"window['{api_name}']?.restoreAfterScreenshot()"
            try:
                self.send_cmd("Runtime.evaluate", eval_params, timeout=1.0)
            except Exception:
                pass

        return screenshot_bytes

    def cleanup(self):
        """Removes overlay host, binding, and new-document script."""
        if not self.ws:
            return
        try:
            api_name = self.api_name
            expr = f"window['{api_name}']?.cleanup(); delete window['{api_name}']; delete window['{self.binding_name}']; Object.keys(window).filter(k => k.startsWith('__annot_') || k.startsWith('__cef_annot_')).forEach(k => delete window[k]); document.querySelectorAll('[id^=__cef_annot_host], [id^=__cef_annotator_host]').forEach(el => el.remove());"
            eval_params = {"expression": expr, "returnByValue": True}
            if self.world_mode == "isolated" and self.isolated_context_id:
                eval_params["contextId"] = self.isolated_context_id
            self.send_cmd("Runtime.evaluate", eval_params, timeout=1.0)
            
            # Clean all tracked isolated contexts and main world
            for cid in list(self.valid_context_ids):
                try:
                    self.send_cmd("Runtime.evaluate", {
                        "expression": f"delete window['{api_name}']; delete window['{self.binding_name}']; Object.keys(window).filter(k => k.startsWith('__annot_') || k.startsWith('__cef_annot_')).forEach(k => delete window[k]);",
                        "contextId": cid,
                        "returnByValue": True
                    }, timeout=0.5)
                except Exception:
                    pass

            if self.world_mode == "isolated":
                self.send_cmd("Runtime.evaluate", {"expression": "document.querySelectorAll('[id^=__cef_annot_host], [id^=__cef_annotator_host]').forEach(el => el.remove()); Object.keys(window).filter(k => k.startsWith('__annot_') || k.startsWith('__cef_annot_')).forEach(k => delete window[k]);", "returnByValue": True}, timeout=1.0)
        except Exception:
            pass

        try:
            self.send_cmd("Runtime.removeBinding", {"name": self.binding_name}, timeout=1.0)
        except Exception:
            pass

        if self.script_identifier:
            try:
                self.send_cmd("Page.removeScriptToEvaluateOnNewDocument", {"identifier": self.script_identifier}, timeout=1.0)
            except Exception:
                pass

        try:
            self.ws.close()
        except Exception:
            pass
        self.is_attached = False
        print(f"[Detached] Cleaned up target {self.target_id[:8]}")


class AnnotatorDaemon:
    """Manages active targets, polling loop, payload validation, and storage."""
    def __init__(self, match_pattern=None):
        self.match_pattern = re.compile(match_pattern) if match_pattern else None
        self.sessions = {}  # target_id -> TargetSession
        self.running = False
        self.last_poll_time = 0
        self.last_heartbeat_time = 0

    def select_targets(self, targets):
        """Filters targets into candidate list with world mode."""
        selected = []
        route_hint = extract_shell_route_hint(targets)

        for t in targets:
            tid = t.get("id")
            title = t.get("title", "")
            url = t.get("url", "")
            ttype = t.get("type", "")

            if ttype != "page" or not t.get("webSocketDebuggerUrl"):
                continue

            # CLI regex match override
            if self.match_pattern:
                if self.match_pattern.search(title) or self.match_pattern.search(url):
                    mode = "main" if "steamloopback.host" in url or "about:blank" in url else "isolated"
                    selected.append((t, mode, route_hint))
                continue

            # Default match:
            # 1. Main Shell: title "Steam" or createflags=18
            if title == "Steam" or "createflags=18" in url:
                selected.append((t, "main", route_hint))
            # 2. Store target: store.steampowered.com
            elif "store.steampowered.com" in url:
                selected.append((t, "isolated", route_hint))

        return selected

    def poll_and_sync_targets(self):
        targets = probe_json_targets()
        if targets is None:
            return

        candidates = self.select_targets(targets)
        active_ids = {t["id"] for t, _, _ in candidates}

        # Drop closed / disappeared targets
        for tid in list(self.sessions.keys()):
            if tid not in active_ids:
                print(f"[Drop] Target {tid[:8]} disappeared.")
                self.sessions[tid].cleanup()
                del self.sessions[tid]

        # Attach to new targets
        all_annots = load_local_annotations()
        for t, mode, route_hint in candidates:
            tid = t["id"]
            if tid not in self.sessions:
                session = TargetSession(t, mode, route_hint, all_annots)
                try:
                    session.connect()
                    session.attach_and_inject()
                    self.sessions[tid] = session
                except Exception as e:
                    print(f"[Error] Failed attaching to target {tid[:8]}: {e}", file=sys.stderr)
                    session.cleanup()
            else:
                self.sessions[tid].url = t.get("url", "")
                self.sessions[tid].title = t.get("title", "")
                if self.sessions[tid].world_mode == "main" and self.sessions[tid].route_hint != route_hint:
                    print(f"[Route Change] Shell route changed: {self.sessions[tid].route_hint} -> {route_hint}")
                    self.sessions[tid].update_route_hint(route_hint)

    def process_incoming_events(self):
        """Reads non-blocking incoming CDP events on all active WebSockets."""
        for tid, session in list(self.sessions.items()):
            if not session.ws:
                continue

            messages = list(session.event_queue)
            session.event_queue.clear()

            try:
                while True:
                    raw = session.ws.recv()
                    messages.append(json.loads(raw))
            except websocket.WebSocketTimeoutException:
                pass
            except (websocket.WebSocketConnectionClosedException, ConnectionResetError):
                print(f"[Disconnect] WebSocket closed on target {tid[:8]}")
                session.cleanup()
                del self.sessions[tid]
                continue
            except Exception as e:
                print(f"[Error] Event processing error on {tid[:8]}: {e}", file=sys.stderr)
                continue

            for msg in messages:
                method = msg.get("method")
                params = msg.get("params", {})

                if method == "Runtime.bindingCalled":
                    self.handle_binding_called(session, params)
                elif method == "Runtime.executionContextCreated":
                    ctx = params.get("context", {})
                    aux = ctx.get("auxData", {})
                    frame_id = aux.get("frameId")
                    cid = ctx.get("id")
                    if frame_id == session.root_frame_id:
                        if session.world_mode == "isolated" and ctx.get("name") == "SteamAnnotator":
                            session.isolated_context_id = cid
                            session.valid_context_ids.add(cid)
                        elif session.world_mode == "main" and aux.get("isDefault", True):
                            session.main_context_id = cid
                            session.valid_context_ids.add(cid)
                elif method == "Runtime.executionContextDestroyed":
                    destroyed_id = params.get("executionContextId")
                    session.valid_context_ids.discard(destroyed_id)
                    if session.isolated_context_id == destroyed_id:
                        session.isolated_context_id = None
                    if session.main_context_id == destroyed_id:
                        session.main_context_id = None

    def handle_binding_called(self, session, params):
        """Hardened validation and persistence of incoming annotation payload."""
        # Check binding name
        if params.get("name") != session.binding_name:
            return

        # Context verification & top-level-frame check
        ctx_id = params.get("executionContextId")
        if ctx_id not in session.valid_context_ids:
            print(f"[Security Rejected] Untrusted or non-top-frame contextId: {ctx_id} (valid top-level: {session.valid_context_ids})", file=sys.stderr)
            return

        raw_payload = params.get("payload", "")
        # Payload size cap: max 64 KB
        if len(raw_payload.encode("utf-8")) > 65536:
            print(f"[Security Rejected] Oversized payload: {len(raw_payload)} bytes", file=sys.stderr)
            return

        # Rate limiting: max 5 saves/sec
        now = time.time()
        session.save_timestamps = [t for t in session.save_timestamps if now - t < 1.0]
        if len(session.save_timestamps) >= RATE_LIMIT_MAX_PER_SEC:
            print("[Security Rejected] Rate limit exceeded (>5 saves/sec)", file=sys.stderr)
            return
        session.save_timestamps.append(now)

        try:
            data = json.loads(raw_payload)
        except Exception:
            print("[Security Rejected] Payload is not valid JSON", file=sys.stderr)
            return

        if data.get("action") == "delete_all":
            all_annots = load_local_annotations()
            count = len(all_annots)
            deleted_from_vibe = 0
            for a in all_annots:
                aid = a.get("id")
                if aid and delete_from_vibe_server(aid):
                    deleted_from_vibe += 1
            save_local_annotations([])
            print(f"[Deleted All] Cleared {count} annotation(s) from local store ({deleted_from_vibe} removed from Vibe server)")
            for s in self.sessions.values():
                s.update_pins([])
            return

        if data.get("action") != "save_annotation":
            return

        # Strict schema & type check
        comment = str(data.get("comment", "")).strip()[:5000]
        elem_ctx = data.get("element_context")
        if not isinstance(elem_ctx, dict):
            print("[Security Rejected] element_context must be a dict", file=sys.stderr)
            return

        # Build clean annotation object
        annot_id = str(uuid.uuid4())
        page_ctx = elem_ctx.get("pageContext", {})
        real_url = page_ctx.get("url") or session.url
        if session.world_mode == "main" and session.route_hint:
            # Use real url with route hint instead of about:blank createflags
            real_url = f"https://steamloopback.host{session.route_hint}"

        rect = elem_ctx.get("rect", {})

        # Screenshot capture
        screenshot_bytes = session.capture_element_screenshot(rect)
        if not screenshot_bytes:
            elem_ctx["screenshot_skipped"] = "Rect empty, off-screen, or capture failed"

        mode = str(data.get("mode", "comment"))
        pending_changes = data.get("pending_changes")
        variant_instructions = data.get("variant_instructions")

        annotation = {
            "id": annot_id,
            "url": real_url,
            "comment": comment,
            "status": "pending",
            "mode": mode,
            "element_context": elem_ctx,
            "created_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            "attachments": []
        }
        if pending_changes and isinstance(pending_changes, dict):
            annotation["pending_changes"] = pending_changes
            annotation["cleanup_required"] = True
        if variant_instructions and isinstance(variant_instructions, dict):
            annotation["variant_instructions"] = variant_instructions

        # 1. Local dual storage
        all_annots = load_local_annotations()
        all_annots.append(annotation)
        save_local_annotations(all_annots)
        print(f"[Saved] Annotation {annot_id[:8]} written to {ANNOTATIONS_FILE}")

        # 2. Vibe server storage
        post_to_vibe_server(annotation, screenshot_bytes)

        # 3. Update pins across all sessions
        for s in self.sessions.values():
            s.update_pins(all_annots)

    def run(self):
        self.running = True
        print("[CEF Annotator] Daemon started. Press Ctrl+C to exit.")
        while self.running:
            now = time.time()
            if now - self.last_poll_time > 2.0:
                self.poll_and_sync_targets()
                self.last_poll_time = now

            if now - self.last_heartbeat_time > 2.0:
                for s in self.sessions.values():
                    s.send_heartbeat()
                self.last_heartbeat_time = now

            self.process_incoming_events()
            time.sleep(0.05)

        self.stop()

    def stop(self):
        print("\n[CEF Annotator] Shutting down cleanly...")
        self.running = False
        for tid, s in list(self.sessions.items()):
            s.cleanup()
        self.sessions.clear()
        print("[CEF Annotator] Shutdown complete.")

# --- CLI Subcommands ---

def ensure_vibe_server():
    """Checks if Vibe server is reachable; if not, attempts to auto-start it."""
    try:
        req = urllib.request.Request(f"{VIBE_SERVER_URL}/health")
        with urllib.request.urlopen(req, timeout=0.8) as resp:
            if resp.status == 200:
                return True
    except Exception:
        pass

    print("[Vibe Server] Port 3846 not responding. Auto-starting vibe-annotations-server...")
    try:
        subprocess.Popen(
            ["npx", "vibe-annotations-server", "start"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            preexec_fn=os.setpgrp
        )
        for _ in range(25):
            time.sleep(0.2)
            try:
                req = urllib.request.Request(f"{VIBE_SERVER_URL}/health")
                with urllib.request.urlopen(req, timeout=0.5) as resp:
                    if resp.status == 200:
                        print("[Vibe Server] Ready on http://127.0.0.1:3846")
                        return True
            except Exception:
                pass
        print("[Vibe Server] Started in background, continuing...")
        return True
    except Exception as e:
        print(f"[Warn] Could not auto-start vibe-annotations-server: {e}", file=sys.stderr)
        return False

def cmd_run(args):
    ensure_vibe_server()
    daemon = AnnotatorDaemon(match_pattern=args.match)

    def sig_handler(sig, frame):
        daemon.running = False

    signal.signal(signal.SIGINT, sig_handler)
    signal.signal(signal.SIGTERM, sig_handler)
    daemon.run()

def cmd_toggle(args):
    targets = probe_json_targets()
    if not targets:
        print("[Error] No active Steam CEF targets found on port 8080.", file=sys.stderr)
        return 1

    count = 0
    for t in targets:
        title = t.get("title", "")
        url = t.get("url", "")
        if title == "Steam" or "createflags=18" in url or "store.steampowered.com" in url:
            tid_prefix = t["id"][:8]
            try:
                ws = websocket.create_connection(t["webSocketDebuggerUrl"], timeout=2.0)
                toggle_expr = "Object.keys(window).filter(k => k.startsWith('__cef_annot_api_')).forEach(k => window[k]?.toggle())"
                # Try toggle in main world
                ws.send(json.dumps({
                    "id": 1,
                    "method": "Runtime.evaluate",
                    "params": {"expression": toggle_expr, "returnByValue": True}
                }))
                ws.recv()
                # Also try isolated contexts (context IDs 3..15)
                for cid in range(3, 15):
                    ws.send(json.dumps({
                        "id": cid,
                        "method": "Runtime.evaluate",
                        "params": {
                            "expression": toggle_expr,
                            "contextId": cid,
                            "returnByValue": True
                        }
                    }))
                    try:
                        ws.recv()
                    except Exception:
                        pass
                ws.close()
                count += 1
                print(f"[Toggle] Sent toggle to target {tid_prefix} ({title or url[:30]})")
            except Exception as e:
                print(f"[Warn] Toggle failed on {t['id'][:8]}: {e}", file=sys.stderr)
    return 0 if count > 0 else 1

def cmd_list(args):
    annots = load_local_annotations()
    if not annots:
        print("No annotations found in local store.")
        return 0

    print(f"Total annotations: {len(annots)}\n")
    for i, a in enumerate(annots, 1):
        aid = a.get("id", "")
        url = a.get("url", "")
        comment = a.get("comment", "")
        created = a.get("created_at", "")
        ctx = a.get("element_context", {})
        tag = ctx.get("tag", "")
        react_name = ctx.get("react", {}).get("componentName")
        selectors = ctx.get("selectors", {}).get("priority", [])
        best_sel = selectors[0] if selectors else "(none)"

        print(f"[{i}] ID: {aid}")
        print(f"    URL:      {url}")
        print(f"    Created:  {created}")
        print(f"    Element:  <{tag}> | React: {react_name or 'None'} | Selector: {best_sel}")
        print(f"    Comment:  {comment}")
        print("-" * 60)
    return 0

def cmd_done(args):
    annot_id = args.id.strip()
    annots = load_local_annotations()
    existing = [a for a in annots if a.get("id") == annot_id or a.get("id", "").startswith(annot_id)]

    if not existing:
        print(f"[Error] Annotation '{annot_id}' not found.", file=sys.stderr)
        return 1

    full_id = existing[0]["id"]
    updated = [a for a in annots if a.get("id") != full_id]
    save_local_annotations(updated)
    print(f"[Done] Removed {full_id} from {ANNOTATIONS_FILE}")

    # Remove from Vibe server if running
    if delete_from_vibe_server(full_id):
        print(f"[Done] Deleted {full_id} from Vibe server")
    return 0

def cmd_clear(args):
    local_annots = load_local_annotations()
    count = len(local_annots)
    deleted_from_vibe = 0

    # Delete only cef-annotator's own annotations from Vibe server
    for a in local_annots:
        aid = a.get("id")
        if aid and delete_from_vibe_server(aid):
            deleted_from_vibe += 1

    save_local_annotations([])
    print(f"[Clear] Cleared {count} annotation(s) from {ANNOTATIONS_FILE} (removed {deleted_from_vibe} from Vibe server)")
    return 0

def cmd_status(args):
    print("=== CEF Annotator Status ===")
    # 1. Target discovery check
    targets = probe_json_targets()
    if targets is None:
        print("[Targets] Port 8080: UNREACHABLE (Is Steam running with -cef-enable-debugging?)")
    else:
        print(f"[Targets] Port 8080: REACHABLE ({len(targets)} targets discovered)")
        for t in targets:
            tid = t.get("id", "")[:8]
            title = t.get("title", "")
            url = t.get("url", "")
            if title == "Steam" or "createflags=18" in url:
                print(f"  * [Shell Target]    {tid}: title='{title}', url='{url[:50]}'")
            elif "store.steampowered.com" in url:
                print(f"  * [Store Target]    {tid}: title='{title}', url='{url[:50]}'")
            elif "<!--tracking:" in url:
                print(f"  * [Tracking Target] {tid}: url='{url[:50]}'")

    # 2. Vibe server check
    try:
        with urllib.request.urlopen(f"{VIBE_SERVER_URL}/health", timeout=1.0) as r:
            vibe_status = f"REACHABLE ({r.status})"
    except Exception as e:
        vibe_status = f"UNREACHABLE ({e})"
    print(f"[Vibe Server] {VIBE_SERVER_URL}: {vibe_status}")

    # 3. Local annotations store
    annots = load_local_annotations()
    print(f"[Local Store] {ANNOTATIONS_FILE}: {len(annots)} annotation(s)")
    return 0

def main():
    parser = argparse.ArgumentParser(description="Steam CEF Annotator CLI & Daemon")
    subparsers = parser.add_subparsers(dest="command", required=True)

    # run
    p_run = subparsers.add_parser("run", help="Run the annotator daemon")
    p_run.add_argument("--match", type=str, help="Regex to filter target title/URL", default=None)
    p_run.set_defaults(func=cmd_run)

    # toggle
    p_toggle = subparsers.add_parser("toggle", help="Toggle annotate mode on active targets")
    p_toggle.set_defaults(func=cmd_toggle)

    # list
    p_list = subparsers.add_parser("list", help="List saved annotations")
    p_list.set_defaults(func=cmd_list)

    # done
    p_done = subparsers.add_parser("done", help="Mark annotation resolved and delete")
    p_done.add_argument("id", type=str, help="Annotation ID (or prefix)")
    p_done.set_defaults(func=cmd_done)

    # clear
    p_clear = subparsers.add_parser("clear", help="Clear all saved annotations")
    p_clear.set_defaults(func=cmd_clear)

    # status
    p_status = subparsers.add_parser("status", help="Show system and target status")
    p_status.set_defaults(func=cmd_status)

    args = parser.parse_args()
    return args.func(args)

if __name__ == "__main__":
    sys.exit(main() or 0)
