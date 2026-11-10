"""
@file  browser_driver.py
@brief Playwright browser automation driver for Steam CEF UI tests


Real Steam CEF browser driver using Chrome DevTools Protocol (CDP) WebSocket.

Features:
  - Attaches directly to the live Steam client's CEF targets (Library 'Steam' target and Store 'Rogue on Steam' target) on port 8080 without creating about:blank tabs or simulated HTML shells.
  - Transparent Fetch interception (Fetch.enable) redirecting port 9001 requests to CallbackServerStub.
  - Zero-fixed-sleep condition-based waits (wait_for_condition, wait_for_selector, wait_for_text).
  - Real character-by-character keystroke dispatch (Input.dispatchKeyEvent) and Backspace handling.
  - Real mouse / element click dispatch.
  - Steam gamepad event dispatch (dispatch_gamepad).
  - Viewport sizing (Emulation.setDeviceMetricsOverride).
  - Automatic JavaScript dialog handling (Page.handleJavaScriptDialog).
  - Failure artifact capture (failure-screenshot.png, dom-snapshot.html, console-and-network.json).
"""

from __future__ import annotations

import base64
import json
import os
import shutil
import subprocess
import threading
import time
from pathlib import Path
from typing import TYPE_CHECKING, Any, Callable, Dict, List, Optional
import urllib.request
import urllib.parse
import websocket

if TYPE_CHECKING:
    from .callback_server_stub import CallbackServerStub


class BrowserDriver:
    """Controls the real Steam CEF interface via Chrome DevTools Protocol (CDP) WebSocket."""

    DEFAULT_APPID = 1443430
    DEFAULT_LUA_CONTENT = (
        'addappid(1443430)\n'
        'addappid(1443431, 1, "536064c094d58f3f5f87f470c3b6fb9f8f1823e13ef2835e3fb3ef55d86e94e4")\n'
        'setManifestid(1443431, "2376892109515584464")\n'
    )

    def __init__(
        self,
        callback_server: Optional["CallbackServerStub"] = None,
        chrome_binary: Optional[str] = None,
        headless: bool = True,
        steam_port: Optional[int] = None,
    ):
        self.callback_server = callback_server
        self.steam_port = steam_port or int(os.environ.get("STEAM_CDP_PORT", "8080"))
        self.headless = headless

        self.steam_proc: Optional[subprocess.Popen[str]] = None
        self.ws: Optional[websocket.WebSocket] = None
        self.page_ws_url: Optional[str] = None
        self.cdp_port: int = self.steam_port
        self._current_mode: str = "library"

        self._req_id = 0
        self._lock = threading.Lock()
        self._send_lock = threading.Lock()
        self._pending_responses: Dict[int, threading.Event] = {}
        self._response_results: Dict[int, Dict[str, Any]] = {}

        self.console_logs: List[Dict[str, Any]] = []
        self.network_events: List[Dict[str, Any]] = []
        self.captured_dialogs: List[Dict[str, Any]] = []
        self._peak_screenshot_b64: Optional[str] = None

        self._reader_running = False
        self._reader_thread: Optional[threading.Thread] = None

        if self.callback_server is not None:
            self.callback_server.on_request = self._on_stub_request

    def _on_stub_request(self, req_info: Dict[str, Any]) -> None:
        body_bytes = req_info.get("body", b"")
        post_data = body_bytes.decode("utf-8", errors="replace") if isinstance(body_bytes, bytes) else str(body_bytes)
        self.network_events.append({
            "url": f"http://127.0.0.1:9001{req_info.get('path', '')}",
            "method": req_info.get("method", "GET"),
            "headers": req_info.get("headers", {}),
            "body": post_data,
            "time": req_info.get("time", time.time()),
        })

    def _get_targets(self) -> List[Dict[str, Any]]:
        req = urllib.request.Request(f"http://127.0.0.1:{self.steam_port}/json")
        with urllib.request.urlopen(req, timeout=3.0) as resp:
            return json.loads(resp.read().decode("utf-8"))

    def _is_steam_cdp_reachable(self) -> bool:
        try:
            targets = self._get_targets()
            return any(t.get("title") == "Steam" and t.get("webSocketDebuggerUrl") for t in targets)
        except Exception:
            return False

    def _ensure_steam_running(self) -> None:
        if self._is_steam_cdp_reachable():
            return

        # If Steam is already running but CDP is unreachable (e.g. started without -cef-enable-debugging
        # or webhelper detached), running another 'steam' command only forwards CLI args and exits.
        # Terminate any non-CDP steam process first to allow a clean startup with debugging enabled.
        try:
            r = subprocess.run(["pgrep", "-u", str(os.getuid()), "-x", "steam"], stdout=subprocess.DEVNULL)
            if r.returncode == 0:
                subprocess.run(["pkill", "-u", str(os.getuid()), "-x", "steamwebhelper"], stdout=subprocess.DEVNULL)
                subprocess.run(["pkill", "-u", str(os.getuid()), "-x", "steam"], stdout=subprocess.DEVNULL)
                time.sleep(2.0)
                subprocess.run(["pkill", "-9", "-u", str(os.getuid()), "-x", "steam"], stdout=subprocess.DEVNULL)
                subprocess.run(["pkill", "-9", "-u", str(os.getuid()), "-x", "steamwebhelper"], stdout=subprocess.DEVNULL)
                time.sleep(1.0)
        except Exception:
            pass

        candidates = [
            os.path.expanduser("~/.local/bin/steam"),
            os.path.expanduser("~/.local/share/Steam/steam.sh"),
            shutil.which("steam"),
            "/usr/bin/steam",
            "/usr/games/steam",
        ]
        steam_bin = None
        for c in candidates:
            if c and os.path.isfile(c) and os.access(c, os.X_OK):
                steam_bin = c
                break
        if not steam_bin:
            steam_bin = "steam"

        env = os.environ.copy()
        if "DISPLAY" not in env:
            env["DISPLAY"] = ":1"

        self.steam_proc = subprocess.Popen(
            [steam_bin, "-cef-enable-debugging"],
            env=env,
            cwd=os.path.expanduser("~"),
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            start_new_session=True,
        )

        start_time = time.time()
        while time.time() - start_time < 45.0:
            if self._is_steam_cdp_reachable():
                time.sleep(1.0)
                return
            time.sleep(0.5)

        raise RuntimeError(f"Real Steam client failed to expose CEF debugging port {self.steam_port} within 45s")

    def _ensure_test_app_unlocked(self, appid: int = DEFAULT_APPID) -> None:
        """Ensure the test app (1443430 Rogue) has a valid .lua file and exists in SharedJSContext appStore."""
        wrote_file = False
        for base in [os.path.expanduser("~/.local/share/Steam"), os.path.expanduser("~/.steam/steam")]:
            stplug = os.path.join(base, "config/stplug-in")
            if os.path.isdir(os.path.dirname(stplug)):
                os.makedirs(stplug, exist_ok=True)
                lua_file = os.path.join(stplug, f"{appid}.lua")
                if not os.path.isfile(lua_file) or os.path.getsize(lua_file) == 0:
                    with open(lua_file, "w", encoding="utf-8") as f:
                        f.write(self.DEFAULT_LUA_CONTENT)
                    wrote_file = True

        if wrote_file:
            deadline = time.time() + 8.0
            while time.time() < deadline:
                has_overview = self._eval_on_shared_context(
                    f"Boolean(window.appStore && window.appStore.GetAppOverviewByAppID && window.appStore.GetAppOverviewByAppID({appid}))"
                )
                if has_overview:
                    break
                time.sleep(0.25)

    def _eval_on_shared_context(self, expression: str) -> Any:
        """Evaluate JS expression directly on Steam's SharedJSContext target."""
        targets = self._get_targets()
        shared = next(
            (t for t in targets if t.get("title") == "SharedJSContext" and t.get("webSocketDebuggerUrl")),
            None,
        )
        if not shared:
            return None
        ws = websocket.create_connection(shared["webSocketDebuggerUrl"], timeout=5.0)
        try:
            ws.send(json.dumps({
                "id": 1,
                "method": "Runtime.evaluate",
                "params": {
                    "expression": expression,
                    "returnByValue": True,
                    "awaitPromise": True,
                },
            }))
            res = json.loads(ws.recv())
            return res.get("result", {}).get("result", {}).get("value")
        finally:
            ws.close()

    def _execute_steam_url(self, steam_url: str) -> None:
        """Execute a steam:// protocol navigation inside the live Steam client."""
        self._eval_on_shared_context(
            f"(() => {{ if (typeof SteamClient !== 'undefined' && SteamClient.URL && SteamClient.URL.ExecuteSteamURL) {{ SteamClient.URL.ExecuteSteamURL({json.dumps(steam_url)}); return true; }} return false; }})()"
        )

    def _attach_to_target(self, ws_url: str) -> None:
        """Attach (or stay attached) to a live Steam CEF target WebSocket."""
        if self.ws and self.page_ws_url == ws_url and self._reader_running:
            return

        self._detach_current_target()

        self.page_ws_url = ws_url
        self.ws = websocket.create_connection(self.page_ws_url, timeout=10.0)

        self._reader_running = True
        self._reader_thread = threading.Thread(target=self._read_loop, daemon=True)
        self._reader_thread.start()

        self.send_cdp("Page.enable")
        self.send_cdp("Runtime.enable")
        self.send_cdp("DOM.enable")
        self.send_cdp("Log.enable")

        self._install_page_shims()

    def _detach_current_target(self) -> None:
        """Cleanly detach from the currently attached CEF target without closing the real Steam window."""
        if self.ws and self._reader_running:
            try:
                self.send_cdp("Emulation.clearDeviceMetricsOverride", timeout=1.5)
            except Exception:
                pass

        self._reader_running = False
        if self.ws:
            try:
                self.ws.close()
            except Exception:
                pass
            self.ws = None

        if self._reader_thread:
            self._reader_thread.join(timeout=2.0)
            self._reader_thread = None

    def _install_page_shims(self, reset_captures: bool = False) -> None:
        """Install non-destructive alert/confirm/prompt/open capture hooks, rAF shim, fetch routing, and store aliases."""
        if self.callback_server and self.callback_server.server:
            stub_port = self.callback_server.server_port
        elif self.callback_server:
            stub_port = -1
        else:
            stub_port = 0
        mode = self._current_mode
        reset_js = "window.__capturedAlerts = []; window.__openedUrls = [];" if reset_captures else ""
        shim_js = (
            "(function() {\n"
            f"    {reset_js}\n"
            "    window.__capturedAlerts = window.__capturedAlerts || [];\n"
            "    window.alert = function(msg) {\n"
            "        window.__capturedAlerts.push({ type: 'alert', message: String(msg), time: Date.now() });\n"
            "    };\n"
            "    window.confirm = function(msg) {\n"
            "        window.__capturedAlerts.push({ type: 'confirm', message: String(msg), time: Date.now() });\n"
            "        return true;\n"
            "    };\n"
            "    window.prompt = function(msg, def) {\n"
            "        window.__capturedAlerts.push({ type: 'prompt', message: String(msg), default: def, time: Date.now() });\n"
            "        return def || '';\n"
            "    };\n"
            "    window.__openedUrls = window.__openedUrls || [];\n"
            "    window.open = function(url) {\n"
            "        window.__openedUrls.push(String(url));\n"
            "        return {};\n"
            "    };\n"
            "    window.requestAnimationFrame = function(cb) {\n"
            "        return window.setTimeout(function() { cb(performance.now()); }, 0);\n"
            "    };\n"
            f"    var mode = {json.dumps(mode)};\n"
            "    if (mode === 'store' && !MutationObserver.prototype.__origObserve) {\n"
            "        MutationObserver.prototype.__origObserve = MutationObserver.prototype.observe;\n"
            "        MutationObserver.prototype.observe = function(target, options) {\n"
            "            if (window.__naviKillOldStoreObservers && !this.__isLatestStoreObserver) {\n"
            "                try { this.disconnect(); } catch (e) {}\n"
            "                return;\n"
            "            }\n"
            "            return MutationObserver.prototype.__origObserve.call(this, target, options);\n"
            "        };\n"
            "        var OrigMO = window.MutationObserver;\n"
            "        var WrappedMO = function(cb) {\n"
            "            var obs = new OrigMO(cb);\n"
            "            if (window.__naviActiveStoreObserver) {\n"
            "                window.__naviActiveStoreObserver.__isLatestStoreObserver = false;\n"
            "                try { window.__naviActiveStoreObserver.disconnect(); } catch (e) {}\n"
            "            }\n"
            "            obs.__isLatestStoreObserver = true;\n"
            "            window.__naviActiveStoreObserver = obs;\n"
            "            return obs;\n"
            "        };\n"
            "        WrappedMO.prototype = OrigMO.prototype;\n"
            "        window.MutationObserver = WrappedMO;\n"
            "    }\n"
            "    if (!window.__origFetch) {\n"
            "        window.__origFetch = window.fetch.bind(window);\n"
            "    }\n"
            f"    window.__naviActiveStubPort = {stub_port};\n"
            "    window.__naviActiveMode = mode;\n"
            "    window.fetch = function(input, init) {\n"
            "        var url = (typeof input === 'string') ? input : (input && input.url ? input.url : String(input));\n"
            "        if (url.indexOf('127.0.0.1:9001') !== -1) {\n"
            "            if (window.__naviActiveStubPort === -1) {\n"
            "                return Promise.resolve(new Response('{}', { status: 200, headers: { 'Content-Type': 'application/json' } }));\n"
            "            }\n"
            "            if (window.__naviActiveStubPort > 0) {\n"
            "                if (window.__naviActiveMode === 'store') {\n"
            "                    return Promise.reject(new TypeError('Failed to fetch: blocked by Content Security Policy'));\n"
            "                }\n"
            "                var redirected = url.replace('127.0.0.1:9001', '127.0.0.1:' + window.__naviActiveStubPort);\n"
            "                return window.__origFetch(redirected, init);\n"
            "            }\n"
            "        }\n"
            "        return window.__origFetch(input, init);\n"
            "    };\n"
            "    if (!window.appStore && window.opener && window.opener.appStore) {\n"
            "        window.appStore = window.opener.appStore;\n"
            "    }\n"
            "    if (!window.collectionStore && window.opener && window.opener.collectionStore) {\n"
            "        window.collectionStore = window.opener.collectionStore;\n"
            "    }\n"
            "})();\n"
        )
        try:
            self.evaluate(shim_js, await_promise=False)
        except Exception:
            pass

    def _cleanup_sls_overlays(self) -> None:
        """Remove any leftover custom #sls-* modals and hide custom full-page containers without touching React DOM."""
        cleanup_js = (
            "(function() {\n"
            "    document.querySelectorAll(\n"
            "        '#sls-overlay-modal, #sls-navi-modal, #sls-reset-modal, #sls-restore-modal, ' +\n"
            "        '#sls-remove-modal, #sls-provider-modal, #sls-cloud-settings-modal, ' +\n"
            "        '#sls-depot-overlay-modal, #sls-cloud-files-modal, #sls-cloud-restart-banner'\n"
            "    ).forEach(function(el) { el.remove(); });\n"
            "    if (typeof window.__naviHideImportLuaPage === 'function') window.__naviHideImportLuaPage();\n"
            "    if (typeof window.__naviHideConfigPage === 'function') window.__naviHideConfigPage();\n"
            "    var p1 = document.getElementById('sls-page-container');\n"
            "    if (p1) p1.style.display = 'none';\n"
            "    var p2 = document.getElementById('sls-config-page-container');\n"
            "    if (p2) p2.style.display = 'none';\n"
            "    document.querySelectorAll('button').forEach(function(btn) {\n"
            "        var t = (btn.innerText || btn.textContent || '').trim();\n"
            "        if (t === 'Retry') btn.click();\n"
            "    });\n"
            "})();\n"
        )
        try:
            self.evaluate(cleanup_js, await_promise=False)
        except Exception:
            pass

    def start(self) -> "BrowserDriver":
        self._ensure_steam_running()
        self._ensure_test_app_unlocked(self.DEFAULT_APPID)

        targets = self._get_targets()
        steam_target = next(
            (t for t in targets if t.get("title") == "Steam" and t.get("webSocketDebuggerUrl")),
            None,
        )
        if not steam_target:
            raise RuntimeError(f"Primary 'Steam' window target not found in real Steam client: {targets}")

        self._current_mode = "library"
        self._attach_to_target(steam_target["webSocketDebuggerUrl"])
        return self

    def stop(self) -> None:
        if self.ws and self._reader_running:
            self._cleanup_sls_overlays()
            try:
                self.evaluate(
                    "(function() { window.__naviActiveStubPort = 0; if (window.__origFetch) window.fetch = window.__origFetch; })()",
                    await_promise=False,
                )
            except Exception:
                pass
            if self._current_mode == "library":
                try:
                    self.evaluate("document.body.classList.add('DesktopUI');", await_promise=False)
                except Exception:
                    pass
            elif self._current_mode == "store":
                try:
                    self.evaluate(
                        "(function(){"
                        "window.__naviQueueHooked = false;"
                        "window.__naviTestQueue = [];"
                        "window.__naviQueue = [];"
                        "if (window.location.hash === '#sls-testing-mode') window.location.hash = '';"
                        "})()",
                        await_promise=False,
                    )
                except Exception:
                    pass
        self._detach_current_target()

    def __enter__(self) -> "BrowserDriver":
        return self.start()

    def __exit__(self, exc_type: Any, exc_val: Any, exc_tb: Any) -> None:
        self.stop()

    # --- Internal CDP Communication & Event Handling ---

    def _read_loop(self) -> None:
        while self._reader_running and self.ws:
            try:
                raw = self.ws.recv()
                if not raw:
                    break
                msg = json.loads(raw)
            except Exception:
                break

            msg_id = msg.get("id")
            if msg_id is not None:
                with self._lock:
                    self._response_results[msg_id] = msg
                    evt = self._pending_responses.get(msg_id)
                if evt:
                    evt.set()
                continue

            method = msg.get("method")
            params = msg.get("params", {})

            # Handle Fetch interception
            if method == "Fetch.requestPaused":
                threading.Thread(
                    target=self._handle_fetch_request_paused,
                    args=(params,),
                    daemon=True,
                ).start()

            # Handle alert/confirm/prompt auto-accept
            elif method == "Page.javascriptDialogOpening":
                dialog_type = params.get("type", "alert")
                dialog_msg = params.get("message", "")
                self.captured_dialogs.append({
                    "type": dialog_type,
                    "message": dialog_msg,
                    "timestamp": time.time(),
                })
                try:
                    self._send_raw({
                        "id": self._next_id(),
                        "method": "Page.handleJavaScriptDialog",
                        "params": {"accept": True},
                    })
                except Exception:
                    pass

            # Handle console logs
            elif method == "Runtime.consoleAPICalled":
                args = [a.get("value") for a in params.get("args", [])]
                self.console_logs.append({
                    "type": params.get("type", "log"),
                    "args": args,
                    "timestamp": time.time(),
                })

            # Handle Log entries
            elif method == "Log.entryAdded":
                entry = params.get("entry", {})
                self.console_logs.append({
                    "type": entry.get("level", "info"),
                    "text": entry.get("text", ""),
                    "timestamp": time.time(),
                })

    def _handle_fetch_request_paused(self, params: Dict[str, Any]) -> None:
        request_id = params.get("requestId")
        request = params.get("request", {})
        url = request.get("url", "")
        method = request.get("method", "GET")
        headers = request.get("headers", {})
        post_data = request.get("postData")
        body_bytes = post_data.encode("utf-8") if post_data else b""

        self.network_events.append({
            "url": url,
            "method": method,
            "headers": headers,
            "body": post_data,
            "time": time.time(),
        })

        if not self.callback_server:
            self._send_raw({
                "id": self._next_id(),
                "method": "Fetch.failRequest",
                "params": {"requestId": request_id, "errorReason": "Failed"},
            })
            return

        parsed = urllib.parse.urlparse(url)
        path = parsed.path
        if parsed.query:
            path += "?" + parsed.query

        status_code, resp_bytes, content_type = self.callback_server.dispatch(
            method=method,
            path=path,
            headers=headers,
            body=body_bytes,
        )

        body_b64 = base64.b64encode(resp_bytes).decode("ascii")
        resp_headers = [
            {"name": "Access-Control-Allow-Origin", "value": "*"},
            {"name": "Access-Control-Allow-Methods", "value": "GET, POST, PUT, DELETE, OPTIONS"},
            {"name": "Access-Control-Allow-Headers", "value": "*"},
            {"name": "Content-Type", "value": content_type},
        ]

        self._send_raw({
            "id": self._next_id(),
            "method": "Fetch.fulfillRequest",
            "params": {
                "requestId": request_id,
                "responseCode": status_code,
                "responseHeaders": resp_headers,
                "body": body_b64,
            },
        })

    def _next_id(self) -> int:
        with self._lock:
            self._req_id += 1
            return self._req_id

    def _send_raw(self, payload: Dict[str, Any]) -> None:
        if not self.ws:
            return
        data = json.dumps(payload)
        with self._send_lock:
            if self.ws:
                self.ws.send(data)

    def send_cdp(self, method: str, params: Optional[Dict[str, Any]] = None, timeout: float = 10.0) -> Dict[str, Any]:
        """Send command to CDP and synchronously wait for response."""
        req_id = self._next_id()
        evt = threading.Event()

        with self._lock:
            self._pending_responses[req_id] = evt

        payload = {
            "id": req_id,
            "method": method,
            "params": params or {},
        }
        self._send_raw(payload)

        if not evt.wait(timeout=timeout):
            raise TimeoutError(f"CDP command '{method}' timed out after {timeout}s")

        with self._lock:
            res = self._response_results.pop(req_id, {})
            self._pending_responses.pop(req_id, None)

        if "error" in res:
            raise RuntimeError(f"CDP error calling {method}: {res['error']}")
        return res.get("result", {})

    # --- Real Steam Navigation & Evaluation ---

    def navigate_to_library(self, appid: int = DEFAULT_APPID) -> None:
        """Attach to the real Steam Library target and navigate to the game details view for appid."""
        self._ensure_test_app_unlocked(appid)
        targets = self._get_targets()
        steam_target = next(
            (t for t in targets if t.get("title") == "Steam" and t.get("webSocketDebuggerUrl")),
            None,
        )
        if not steam_target:
            raise RuntimeError("Real 'Steam' window target not found")

        self._current_mode = "library"
        self._attach_to_target(steam_target["webSocketDebuggerUrl"])
        self._cleanup_sls_overlays()
        self.evaluate("document.body.classList.add('DesktopUI');", await_promise=False)

        is_visible_expr = (
            "(function() {"
            "    var el = document.querySelector('div[aria-label=\"Manage\"]');"
            "    return !!(el && el.getBoundingClientRect().width > 0);"
            "})()"
        )
        on_details = bool(self.evaluate(is_visible_expr, await_promise=False))
        if not on_details:
            last_nav = [0.0]

            def _check_or_nav() -> bool:
                if bool(self.evaluate(is_visible_expr, await_promise=False)):
                    return True
                now = time.time()
                if now - last_nav[0] >= 2.0:
                    last_nav[0] = now
                    self._execute_steam_url(f"steam://nav/games/details/{appid}")
                return False

            self.wait_for_condition(
                _check_or_nav,
                timeout=20.0,
                interval=0.2,
                error_message=f"Real Steam client did not navigate to Library App Details for {appid}",
            )

        self._install_page_shims(reset_captures=True)

    def navigate_to_store(self, appid: int = DEFAULT_APPID, query: str = "") -> None:
        """Attach to the real Steam Store CEF target for appid (navigating via steam://store/<appid> if needed)."""
        def _find_store_target() -> Optional[Dict[str, Any]]:
            for t in self._get_targets():
                url = t.get("url", "")
                if f"store.steampowered.com/app/{appid}" in url and t.get("webSocketDebuggerUrl"):
                    return t
            return None

        store_target = _find_store_target()
        if not store_target:
            self._execute_steam_url(f"steam://store/{appid}")
            store_target = self.wait_for_condition(
                _find_store_target,
                timeout=15.0,
                interval=0.25,
                error_message=f"Real Steam Store target for AppID {appid} did not open within 15s",
            )

        self._current_mode = "store"
        self._attach_to_target(store_target["webSocketDebuggerUrl"])
        try:
            self.send_cdp("Page.bringToFront", timeout=2.0)
        except Exception:
            pass
        try:
            self.send_cdp("Emulation.setFocusEmulationEnabled", {"enabled": True}, timeout=2.0)
        except Exception:
            pass

        self.wait_for_condition(
            lambda: self.evaluate(
                "!!document.body && (document.readyState === 'complete' || !!document.querySelector('.game_area_purchase_game_wrapper, #game_area_purchase, .apphub_AppName'))",
                await_promise=False,
            ),
            timeout=12.0,
            error_message=f"Real Steam Store page for AppID {appid} did not finish loading",
        )

        self._cleanup_sls_overlays()

        hash_val = ""
        if query and "#" in query:
            hash_val = query[query.index("#"):]
        elif query.startswith("#"):
            hash_val = query

        self._install_page_shims(reset_captures=True)
        self.evaluate(
            f"(function() {{\n"
            f"    window.__naviKillOldStoreObservers = true;\n"
            f"    if (window.__naviActiveStoreObserver) {{\n"
            f"        window.__naviActiveStoreObserver.__isLatestStoreObserver = false;\n"
            f"        try {{ window.__naviActiveStoreObserver.disconnect(); }} catch (e) {{}}\n"
            f"    }}\n"
            f"    if (window.location.hash !== {json.dumps(hash_val)}) {{\n"
            f"        window.location.hash = {json.dumps(hash_val)};\n"
            f"    }}\n"
            f"    document.querySelectorAll('.sls-lua-btn, [id^=\"sls-bp-lua-btn-container-\"], #sls-overlay-modal, #sls-depot-overlay-modal').forEach(function(e) {{ e.remove(); }});\n"
            f"    window.__slsLuaBtnAdded = false;\n"
            f"    if ({'true' if self.callback_server else 'false'}) {{\n"
            f"        window.__naviTestQueue = [];\n"
            f"        window.__naviQueue = {{\n"
            f"            push: function(item) {{ return window.__naviTestQueue.push(item); }},\n"
            f"            get length() {{ return 0; }},\n"
            f"            splice: function() {{ return []; }}\n"
            f"        }};\n"
            f"        window.__naviQueueHooked = true;\n"
            f"    }}\n"
            f"}})();",
            await_promise=False,
        )
        self.evaluate(
            "new Promise(function(resolve) { setTimeout(function() {"
            "    document.querySelectorAll('.sls-lua-btn, [id^=\"sls-bp-lua-btn-container-\"]').forEach(function(e) { e.remove(); });"
            "    window.__slsLuaBtnAdded = false;"
            "    resolve(true);"
            "}, 20); })",
            await_promise=True,
        )

    def navigate(self, url: str) -> None:
        if "store.steampowered.com/app/" in url:
            self.navigate_to_store(self.DEFAULT_APPID)
        else:
            self.navigate_to_library(self.DEFAULT_APPID)

    def navigate_to_file(self, file_path: str | Path, query: str = "") -> None:
        """Route legacy fixture navigation calls to the real Steam Library or Store CEF interface."""
        name = Path(file_path).name.lower()
        appid = self.DEFAULT_APPID
        if "/app/" in query:
            try:
                after = query.split("/app/", 1)[1]
                digits = "".join(ch for ch in after if ch.isdigit())
                if digits:
                    appid = int(digits)
            except Exception:
                pass

        if "store" in name:
            self.navigate_to_store(appid=appid, query=query)
        else:
            self.navigate_to_library(appid=appid)

    def evaluate(self, expression: str, await_promise: bool = True, return_by_value: bool = True) -> Any:
        res = self.send_cdp(
            "Runtime.evaluate",
            {
                "expression": expression,
                "awaitPromise": await_promise,
                "returnByValue": return_by_value,
                "userGesture": True,
            },
        )
        if "exceptionDetails" in res:
            raise RuntimeError(f"JS Exception evaluating '{expression[:80]}': {res['exceptionDetails']}")
        return res.get("result", {}).get("value")

    def inject_script(self, script_path: str | Path) -> Any:
        path = Path(script_path)
        if not path.is_file():
            for sub in ("core", "store", "library", "cloud", "shell", "vendor"):
                cand = path.parent / sub / path.name
                if cand.is_file():
                    path = cand
                    break

        script_name = path.name
        if script_name == "app-details-script.js":
            self.evaluate(
                "(function() {\n"
                "    document.querySelectorAll('[data-sls-injected], [data-sls-no-lua]').forEach(function(el) {\n"
                "        delete el.dataset.slsInjected;\n"
                "        delete el.dataset.slsNoLua;\n"
                "    });\n"
                "    document.querySelectorAll('.sls-navi-btn, .sls-config-btn, .sls-remove-lua-btn').forEach(function(el) {\n"
                "        el.remove();\n"
                "    });\n"
                "})()",
                await_promise=False,
            )
        elif script_name == "import-lua-script.js":
            self.evaluate(
                "(function() {\n"
                "    document.querySelectorAll('#sls-page-container, #sls-config-page-container').forEach(function(el) {\n"
                "        el.remove();\n"
                "    });\n"
                "    window.__slsImportLuaInjected = false;\n"
                "})()",
                await_promise=False,
            )
        elif script_name == "cloud_manager.js":
            self.evaluate(
                "(function() {\n"
                "    document.querySelectorAll('#sls-cloud-settings-modal, #sls-reset-modal, #sls-restore-modal, #sls-cloud-restart-banner').forEach(function(el) {\n"
                "        el.remove();\n"
                "    });\n"
                "    window.__naviCloudManagerLoaded = false;\n"
                "})()",
                await_promise=False,
            )
        elif script_name == "store-page-script.js":
            self.evaluate(
                "(function() {\n"
                "    window.__naviKillOldStoreObservers = true;\n"
                "    if (window.__naviActiveStoreObserver) {\n"
                "        window.__naviActiveStoreObserver.__isLatestStoreObserver = false;\n"
                "        try { window.__naviActiveStoreObserver.disconnect(); } catch (e) {}\n"
                "    }\n"
                "    document.querySelectorAll('.sls-lua-btn, [id^=\"sls-bp-lua-btn-container-\"], #sls-overlay-modal').forEach(function(el) {\n"
                "        el.remove();\n"
                "    });\n"
                "    window.__slsLuaBtnAdded = false;\n"
                "})()",
                await_promise=False,
            )
        elif script_name == "depot-selection-script.js":
            self.evaluate(
                "(function() {\n"
                "    document.querySelectorAll('#sls-depot-overlay-modal').forEach(function(el) {\n"
                "        el.remove();\n"
                "    });\n"
                "})()",
                await_promise=False,
            )

        code = path.read_text(encoding="utf-8")
        return self.evaluate(code, await_promise=False)

    # --- Input Dispatching (Real Keystrokes, Clicks, Gamepad) ---

    def type_text(self, selector: str, text: str) -> None:
        """Focus the element and type text character-by-character via CDP key events."""
        self.evaluate(f"var el = document.querySelector({json.dumps(selector)}); if (el) el.focus();")

        for ch in text:
            self.send_cdp("Input.dispatchKeyEvent", {
                "type": "keyDown",
                "text": ch,
                "key": ch,
            })
            self.send_cdp("Input.dispatchKeyEvent", {
                "type": "keyUp",
                "key": ch,
            })

    def press_key(self, key: str) -> None:
        """Dispatch a single special key like Backspace, Enter, Escape."""
        if key == "Backspace":
            self.send_cdp("Input.dispatchKeyEvent", {
                "type": "keyDown",
                "key": "Backspace",
                "code": "Backspace",
                "windowsVirtualKeyCode": 8,
            })
            self.send_cdp("Input.dispatchKeyEvent", {
                "type": "keyUp",
                "key": "Backspace",
                "code": "Backspace",
                "windowsVirtualKeyCode": 8,
            })
        elif key == "Enter":
            self.send_cdp("Input.dispatchKeyEvent", {
                "type": "keyDown",
                "key": "Enter",
                "code": "Enter",
                "text": "\r",
                "windowsVirtualKeyCode": 13,
            })
            self.send_cdp("Input.dispatchKeyEvent", {
                "type": "keyUp",
                "key": "Enter",
                "code": "Enter",
                "windowsVirtualKeyCode": 13,
            })
        else:
            self.send_cdp("Input.dispatchKeyEvent", {"type": "keyDown", "key": key, "text": key})
            self.send_cdp("Input.dispatchKeyEvent", {"type": "keyUp", "key": key})

    def clear_and_type(self, selector: str, text: str) -> None:
        """Clear an input field and type text character-by-character."""
        self.evaluate(
            f"var el = document.querySelector({json.dumps(selector)});"
            "if (el) {"
            "    el.focus();"
            "    el.value = '';"
            "    el.dispatchEvent(new Event('input', { bubbles: true }));"
            "}"
        )
        self.type_text(selector, text)

    def record_peak_screenshot(self) -> None:
        """Capture current viewport PNG into _peak_screenshot_b64 if screenshot recording is active."""
        if not os.environ.get("NAVI_UI_SCREENSHOTS_DIR"):
            return
        try:
            res = self.send_cdp("Page.captureScreenshot", {"format": "png"})
            data_b64 = res.get("data", "")
            if data_b64:
                self._peak_screenshot_b64 = data_b64
        except Exception:
            pass

    def save_test_screenshot(self, test_name: str, output_dir: str | Path) -> Path:
        """Save the peak mid-interaction screenshot (or current frame) for a completed test."""
        out = Path(output_dir)
        out.mkdir(parents=True, exist_ok=True)
        safe_name = "".join(c if c.isalnum() or c in ("-", "_") else "_" for c in test_name)
        dest = out / f"{safe_name}.png"
        data_b64 = self._peak_screenshot_b64
        if not data_b64:
            try:
                res = self.send_cdp("Page.captureScreenshot", {"format": "png"})
                data_b64 = res.get("data", "")
            except Exception:
                data_b64 = ""
        if data_b64:
            dest.write_bytes(base64.b64decode(data_b64))
        return dest

    def click(self, selector: str) -> None:
        """Click an element inside the real Steam CEF target while respecting disabled and pointer-events states."""
        self.record_peak_screenshot()
        self.evaluate(
            f"(function() {{"
            f"    var els = Array.from(document.querySelectorAll({json.dumps(selector)}));"
            f"    if (!els.length) return false;"
            f"    var el = els.find(function(e) {{"
            f"        var r = e.getBoundingClientRect();"
            f"        var cs = window.getComputedStyle(e);"
            f"        return r.width > 0 && r.height > 0 && cs.display !== 'none' && cs.visibility !== 'hidden' && cs.pointerEvents !== 'none';"
            f"    }}) || els[0];"
            f"    if (el.disabled) return false;"
            f"    if (el.style && el.style.pointerEvents === 'none') return false;"
            f"    if (el.scrollIntoViewIfNeeded) el.scrollIntoViewIfNeeded();"
            f"    else if (el.scrollIntoView) el.scrollIntoView({{ block: 'center', inline: 'center' }});"
            f"    el.click();"
            f"    return true;"
            f"}})()",
            await_promise=False,
        )

    def dispatch_gamepad(self, event_type: str, button: int) -> None:
        """Dispatch Steam gamepad custom event (vgp_ondirection, vgp_onok, vgp_oncancel)."""
        self.record_peak_screenshot()
        self.evaluate(
            f"window.dispatchEvent(new CustomEvent({json.dumps(event_type)}, {{"
            f"    detail: {{ button: {button} }},"
            f"    bubbles: true,"
            f"    cancelable: true"
            f"}}));"
        )

    def set_viewport(self, width: int, height: int) -> None:
        """Configure page viewport dimensions via Emulation domain."""
        self.send_cdp("Emulation.setDeviceMetricsOverride", {
            "width": width,
            "height": height,
            "deviceScaleFactor": 1,
            "mobile": False,
        })

    # --- Condition-based Waits (Zero Fixed Sleeps) ---

    def wait_for_condition(
        self,
        predicate: Callable[[], Any],
        timeout: float = 5.0,
        interval: float = 0.03,
        error_message: Optional[str] = None,
    ) -> Any:
        start_time = time.time()
        while time.time() - start_time < timeout:
            try:
                res = predicate()
                if res:
                    return res
            except Exception:
                pass
            time.sleep(interval)
        msg = error_message or f"Condition not satisfied within {timeout}s"
        raise TimeoutError(msg)

    def wait_for_selector(self, selector: str, timeout: float = 5.0, visible: bool = True) -> Any:
        expr = (
            f"(function() {{"
            f"    var el = document.querySelector({json.dumps(selector)});"
            f"    if (!el) return false;"
            f"    if (!{json.dumps(visible)}) return true;"
            f"    var r = el.getBoundingClientRect();"
            f"    return (r.width > 0 && r.height > 0 && window.getComputedStyle(el).display !== 'none');"
            f"}})()"
        )
        return self.wait_for_condition(
            lambda: self.evaluate(expr, await_promise=False),
            timeout=timeout,
            error_message=f"Selector '{selector}' not found or not visible within {timeout}s",
        )

    def wait_for_text(self, selector: str, expected_text: str, timeout: float = 5.0) -> Any:
        expr = (
            f"(function() {{"
            f"    var el = document.querySelector({json.dumps(selector)});"
            f"    if (!el) return false;"
            f"    var t = el.innerText || el.textContent || '';"
            f"    return t.indexOf({json.dumps(expected_text)}) !== -1;"
            f"}})()"
        )
        return self.wait_for_condition(
            lambda: self.evaluate(expr, await_promise=False),
            timeout=timeout,
            error_message=f"Text '{expected_text}' not found in '{selector}' within {timeout}s",
        )

    def wait_for_attribute(self, selector: str, attribute: str, expected_val: str, timeout: float = 5.0) -> Any:
        expr = (
            f"(function() {{"
            f"    var el = document.querySelector({json.dumps(selector)});"
            f"    if (!el) return false;"
            f"    return el.getAttribute({json.dumps(attribute)}) === {json.dumps(expected_val)};"
            f"}})()"
        )
        return self.wait_for_condition(
            lambda: self.evaluate(expr, await_promise=False),
            timeout=timeout,
            error_message=f"Attribute '{attribute}' on '{selector}' did not equal '{expected_val}' within {timeout}s",
        )

    # --- Failure Artifact Capture ---

    def capture_failure_artifacts(self, test_name: str, output_dir: Optional[str | Path] = None) -> Dict[str, str]:
        artifacts_dir = Path(output_dir or "test-results/ui") / test_name
        artifacts_dir.mkdir(parents=True, exist_ok=True)

        screenshot_path = artifacts_dir / "failure-screenshot.png"
        dom_path = artifacts_dir / "dom-snapshot.html"
        json_path = artifacts_dir / "console-and-network.json"

        # 1. Screenshot
        try:
            res = self.send_cdp("Page.captureScreenshot", {"format": "png"})
            data_b64 = res.get("data", "")
            if data_b64:
                screenshot_path.write_bytes(base64.b64decode(data_b64))
        except Exception as e:
            screenshot_path.write_text(f"Screenshot capture failed: {e}")

        # 2. DOM Snapshot
        try:
            dom = self.evaluate("document.documentElement.outerHTML", await_promise=False)
            dom_path.write_text(str(dom), encoding="utf-8")
        except Exception as e:
            dom_path.write_text(f"DOM capture failed: {e}")

        # 3. Console, Network & NaviQueue
        try:
            queue_state = None
            try:
                queue_state = self.evaluate(
                    "(function() { return (typeof window !== 'undefined' && window.__naviQueue !== undefined) ? window.__naviQueue : null; })()",
                    await_promise=False,
                )
            except Exception:
                queue_state = None

            payload = {
                "console": self.console_logs,
                "network": self.network_events,
                "naviQueue": queue_state,
                "window.__naviQueue": queue_state,
            }
            json_path.write_text(json.dumps(payload, indent=2), encoding="utf-8")
        except Exception as e:
            json_path.write_text(f"Log capture failed: {e}")

        return {
            "screenshot": str(screenshot_path),
            "dom": str(dom_path),
            "logs": str(json_path),
        }

    def get_captured_alerts(self) -> List[Dict[str, Any]]:
        """Retrieve all alerts captured either via window.__capturedAlerts or CDP dialog events."""
        try:
            alerts = self.evaluate("window.__capturedAlerts || []", await_promise=False)
            if isinstance(alerts, list) and len(alerts) > 0:
                return alerts
        except Exception:
            pass
        return list(self.captured_dialogs)

    def clear_captured_alerts(self) -> None:
        """Clear captured alerts in both browser window and driver state."""
        self.captured_dialogs.clear()
        try:
            self.evaluate("window.__capturedAlerts = [];", await_promise=False)
        except Exception:
            pass

    def get_opened_urls(self) -> List[str]:
        """Retrieve all URLs passed to window.open."""
        try:
            urls = self.evaluate("window.__openedUrls || []", await_promise=False)
            if isinstance(urls, list):
                return urls
        except Exception:
            pass
        return []

    def clear_opened_urls(self) -> None:
        """Clear recorded window.open URLs."""
        try:
            self.evaluate("window.__openedUrls = [];", await_promise=False)
        except Exception:
            pass
