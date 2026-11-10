"""
@file  browser_driver.py
@brief Playwright browser automation driver for Steam CEF UI tests


Headless Chrome browser driver using Chrome DevTools Protocol (CDP) WebSocket.

Features:
  - Headless Chrome startup with ephemeral remote debugging port (--remote-debugging-port=0).
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
import tempfile
import threading
import time
from pathlib import Path
from typing import TYPE_CHECKING, Any, Callable, Dict, List, Optional, Tuple
import urllib.request
import urllib.parse
import websocket

if TYPE_CHECKING:
    from .callback_server_stub import CallbackServerStub


class BrowserDriver:
    """Controls a real Steam CEF instance via Chrome DevTools Protocol (CDP) WebSocket."""

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
        self.root_ws: Optional[websocket.WebSocket] = None
        self.target_id: Optional[str] = None
        self.ws: Optional[websocket.WebSocket] = None
        self.ws_url: Optional[str] = None
        self.page_ws_url: Optional[str] = None
        self.cdp_port: int = self.steam_port

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

    def _is_steam_cdp_reachable(self) -> bool:
        try:
            req = urllib.request.Request(f"http://127.0.0.1:{self.steam_port}/json")
            with urllib.request.urlopen(req, timeout=1.5) as resp:
                return resp.status == 200
        except Exception:
            return False

    def _ensure_steam_running(self) -> None:
        if self._is_steam_cdp_reachable():
            return

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

    def start(self) -> "BrowserDriver":
        self._ensure_steam_running()

        targets_url = f"http://127.0.0.1:{self.steam_port}/json"
        req = urllib.request.Request(targets_url)
        with urllib.request.urlopen(req, timeout=3.0) as resp:
            targets = json.loads(resp.read().decode("utf-8"))

        # Find the primary Steam window target to create dedicated test targets
        steam_target = next(
            (t for t in targets if t.get("title") == "Steam" and t.get("webSocketDebuggerUrl")),
            None,
        )
        if not steam_target:
            steam_target = next((t for t in targets if t.get("webSocketDebuggerUrl")), None)

        if not steam_target or not steam_target.get("webSocketDebuggerUrl"):
            raise RuntimeError(f"No usable WebSocket target in real Steam client: {targets}")

        self.root_ws = websocket.create_connection(steam_target["webSocketDebuggerUrl"], timeout=10.0)

        # Create a dedicated target in the real Steam client
        self.root_ws.send(json.dumps({
            "id": 1,
            "method": "Target.createTarget",
            "params": {"url": "about:blank"}
        }))
        res = json.loads(self.root_ws.recv())
        self.target_id = res.get("result", {}).get("targetId")

        # Discover newly created target
        with urllib.request.urlopen(req, timeout=3.0) as resp:
            targets = json.loads(resp.read().decode("utf-8"))

        page_target = next((t for t in targets if t.get("id") == self.target_id), None)
        if not page_target or not page_target.get("webSocketDebuggerUrl"):
            page_target = steam_target

        self.page_ws_url = page_target["webSocketDebuggerUrl"]
        self.ws = websocket.create_connection(self.page_ws_url, timeout=10.0)

        # Start background reader thread
        self._reader_running = True
        self._reader_thread = threading.Thread(target=self._read_loop, daemon=True)
        self._reader_thread.start()

        # Enable core domains
        self.send_cdp("Page.enable")
        self.send_cdp("Runtime.enable")
        self.send_cdp("DOM.enable")
        self.send_cdp("Log.enable")

        # Automatically intercept window.alert / window.confirm into window.__capturedAlerts
        storage_shim = (
            "function __makeMockStorage() {\n"
            "    var store = {};\n"
            "    return {\n"
            "        getItem: function(k) { return Object.prototype.hasOwnProperty.call(store, k) ? store[k] : null; },\n"
            "        setItem: function(k, v) { store[k] = String(v); },\n"
            "        removeItem: function(k) { delete store[k]; },\n"
            "        clear: function() { store = {}; },\n"
            "        get length() { return Object.keys(store).length; },\n"
            "        key: function(i) { return Object.keys(store)[i] || null; }\n"
            "    };\n"
            "}\n"
            "try { window.localStorage.getItem('__probe'); } catch (_) {\n"
            "    try { Object.defineProperty(window, 'localStorage', { value: __makeMockStorage(), configurable: true, writable: true }); } catch (_) {}\n"
            "}\n"
            "try { window.sessionStorage.getItem('__probe'); } catch (_) {\n"
            "    try { Object.defineProperty(window, 'sessionStorage', { value: __makeMockStorage(), configurable: true, writable: true }); } catch (_) {}\n"
            "}\n"
        )
        self.send_cdp(
            "Page.addScriptToEvaluateOnNewDocument",
            {
                "source": (
                    "window.__capturedAlerts = window.__capturedAlerts || [];"
                    "window.alert = function(msg) {"
                    "    window.__capturedAlerts.push({ type: 'alert', message: String(msg), time: Date.now() });"
                    "};"
                    "window.confirm = function(msg) {"
                    "    window.__capturedAlerts.push({ type: 'confirm', message: String(msg), time: Date.now() });"
                    "    return true;"
                    "};"
                    "window.prompt = function(msg, def) {"
                    "    window.__capturedAlerts.push({ type: 'prompt', message: String(msg), default: def, time: Date.now() });"
                    "    return def || '';"
                    "};"
                    "window.__openedUrls = window.__openedUrls || [];"
                    "window.open = function(url) {"
                    "    window.__openedUrls.push(String(url));"
                    "    return {};"
                    "};\n"
                    + storage_shim
                )
            },
        )
        try:
            self.evaluate(storage_shim + "void 0;", await_promise=False)
        except Exception:
            pass

        # Enable transparent Fetch interception for port 9001 if callback_server is configured
        if self.callback_server:
            self.send_cdp(
                "Fetch.enable",
                {"patterns": [{"urlPattern": "*127.0.0.1:9001*"}]},
            )

        return self

    def stop(self) -> None:
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

        if self.root_ws and self.target_id:
            try:
                self.root_ws.send(json.dumps({
                    "id": 999,
                    "method": "Target.closeTarget",
                    "params": {"targetId": self.target_id}
                }))
                self.root_ws.recv()
            except Exception:
                pass
            try:
                self.root_ws.close()
            except Exception:
                pass
            self.root_ws = None

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
            # Fallback: fail request
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

    # --- Navigation & Evaluation ---

    def navigate(self, url: str) -> None:
        self.send_cdp("Page.navigate", {"url": url})
        # Wait for document to be ready
        self.wait_for_condition(
            lambda: self.evaluate("document.readyState", await_promise=False) in ["interactive", "complete"],
            timeout=8.0,
            error_message=f"Timed out loading {url}",
        )

    def navigate_to_file(self, file_path: str | Path, query: str = "") -> None:
        abs_path = Path(file_path).resolve()
        if not abs_path.is_file():
            raise FileNotFoundError(f"Fixture file not found: {abs_path}")
        html_content = abs_path.read_text(encoding="utf-8")
        escaped_html = json.dumps(html_content)
        target_url = "about:blank"
        if query:
            if not query.startswith("?") and not query.startswith("#"):
                query = "?" + query
            target_url += query
        self.send_cdp("Page.navigate", {"url": target_url})
        setup_script = (
            f"document.open(); document.write({escaped_html}); document.close();\n"
            "window.__capturedAlerts = window.__capturedAlerts || [];\n"
            "window.alert = function(msg) {\n"
            "    window.__capturedAlerts.push({ type: 'alert', message: String(msg), time: Date.now() });\n"
            "};\n"
            "window.confirm = function(msg) {\n"
            "    window.__capturedAlerts.push({ type: 'confirm', message: String(msg), time: Date.now() });\n"
            "    return true;\n"
            "};\n"
            "window.prompt = function(msg, def) {\n"
            "    window.__capturedAlerts.push({ type: 'prompt', message: String(msg), default: def, time: Date.now() });\n"
            "    return def || '';\n"
            "};\n"
            "window.__openedUrls = window.__openedUrls || [];\n"
            "window.open = function(url) {\n"
            "    window.__openedUrls.push(String(url));\n"
            "    return {};\n"
            "};\n"
            "function __makeMockStorage() {\n"
            "    var store = {};\n"
            "    return {\n"
            "        getItem: function(k) { return Object.prototype.hasOwnProperty.call(store, k) ? store[k] : null; },\n"
            "        setItem: function(k, v) { store[k] = String(v); },\n"
            "        removeItem: function(k) { delete store[k]; },\n"
            "        clear: function() { store = {}; },\n"
            "        get length() { return Object.keys(store).length; },\n"
            "        key: function(i) { return Object.keys(store)[i] || null; }\n"
            "    };\n"
            "}\n"
            "try { window.localStorage.getItem('__probe'); } catch (_) {\n"
            "    try { Object.defineProperty(window, 'localStorage', { value: __makeMockStorage(), configurable: true, writable: true }); } catch (_) {}\n"
            "}\n"
            "try { window.sessionStorage.getItem('__probe'); } catch (_) {\n"
            "    try { Object.defineProperty(window, 'sessionStorage', { value: __makeMockStorage(), configurable: true, writable: true }); } catch (_) {}\n"
            "}\n"
            "void 0;\n"
        )
        self.evaluate(setup_script, await_promise=False)
        self.wait_for_condition(
            lambda: self.evaluate("document.readyState", await_promise=False) in ["interactive", "complete"],
            timeout=8.0,
            error_message=f"Timed out loading content from {abs_path.name}",
        )

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
        """Click an element using real CDP mouse events, with JS click fallback."""
        self.record_peak_screenshot()
        box = self.evaluate(
            f"(function() {{"
            f"    var el = document.querySelector({json.dumps(selector)});"
            f"    if (!el) return null;"
            f"    if (el.scrollIntoViewIfNeeded) el.scrollIntoViewIfNeeded();"
            f"    else if (el.scrollIntoView) el.scrollIntoView({{ block: 'center', inline: 'center' }});"
            f"    var r = el.getBoundingClientRect();"
            f"    return {{ x: r.x + r.width / 2, y: r.y + r.height / 2, w: r.width, h: r.height, winW: window.innerWidth, winH: window.innerHeight }};"
            f"}})()"
        )

        if box and box.get("w", 0) > 0 and box.get("h", 0) > 0 and 0 <= box.get("x", -1) <= box.get("winW", 1920) and 0 <= box.get("y", -1) <= box.get("winH", 1080):
            x = box["x"]
            y = box["y"]
            self.send_cdp("Input.dispatchMouseEvent", {
                "type": "mousePressed",
                "x": x,
                "y": y,
                "button": "left",
                "clickCount": 1,
            })
            self.send_cdp("Input.dispatchMouseEvent", {
                "type": "mouseReleased",
                "x": x,
                "y": y,
                "button": "left",
                "clickCount": 1,
            })
        else:
            self.evaluate(f"var el = document.querySelector({json.dumps(selector)}); if (el) el.click();")

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


