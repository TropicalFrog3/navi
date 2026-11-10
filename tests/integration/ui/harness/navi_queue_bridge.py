"""
@file  navi_queue_bridge.py
@brief Bridge facilitating communication with Navi operation queue


Bridge between store-page window.__naviQueue and the callback_server_stub.

Polls and drains window.__naviQueue in store page targets, dispatches requests
against CallbackServerStub, and resolves via window.__naviResolve(id, status, body).
"""

from __future__ import annotations

import json
import threading
import time
from typing import TYPE_CHECKING, Any, Optional
from urllib.parse import unquote
import urllib.request
import urllib.error

if TYPE_CHECKING:
    from .browser_driver import BrowserDriver
    from .callback_server_stub import CallbackServerStub


class NaviQueueBridge:
    """
    Bridge that emulates Navi's CDP polling loop (storeinject.cpp lines 701-744)
    while isolating test queue items from any background C++ StoreInject polling.
    """

    INSTALL_HOOK_EXPR = (
        "(function(){"
        "if (window.__naviQueueHooked) return true;"
        "var existing = Array.isArray(window.__naviQueue) ? window.__naviQueue.splice(0) : [];"
        "window.__naviTestQueue = (window.__naviTestQueue || []).concat(existing);"
        "window.__naviQueue = {"
        "    push: function(item) { return window.__naviTestQueue.push(item); },"
        "    get length() { return 0; },"
        "    splice: function() { return []; }"
        "};"
        "window.__naviQueueHooked = true;"
        "return true;"
        "})()"
    )

    RESTORE_HOOK_EXPR = (
        "(function(){"
        "window.__naviQueueHooked = false;"
        "window.__naviTestQueue = [];"
        "window.__naviQueue = [];"
        "return true;"
        "})()"
    )

    DRAIN_EXPR = (
        "(function(){"
        "var q = window.__naviQueueHooked ? window.__naviTestQueue : window.__naviQueue;"
        "if (!q || !q.length) return '';"
        "return encodeURIComponent(q.splice(0).join('\\n'));"
        "})()"
    )

    def __init__(self, driver: "BrowserDriver", callback_server: "CallbackServerStub"):
        self.driver = driver
        self.callback_server = callback_server
        self._running = False
        self._thread: Optional[threading.Thread] = None
        self._lock = threading.Lock()
        self.handled_count = 0

    def install_hook(self) -> None:
        """Install __naviQueue interception hook on the current Store target."""
        try:
            self.driver.evaluate(self.INSTALL_HOOK_EXPR, await_promise=False)
        except Exception:
            pass

    def poll_once(self) -> int:
        """
        Drain window.__naviQueue once and resolve pending requests.
        Returns count of resolved requests.
        """
        try:
            self.install_hook()
            encoded = self.driver.evaluate(self.DRAIN_EXPR, await_promise=False)
        except Exception:
            return 0

        if not encoded:
            return 0

        raw_queue = unquote(str(encoded))
        lines = [line.strip() for line in raw_queue.split("\n") if line.strip()]
        count = 0

        for line in lines:
            parts = line.split(" ", 1)
            if len(parts) != 2:
                continue
            req_id, path = parts[0], parts[1]

            # Route to stub
            status, body_bytes, _ = self.callback_server.dispatch(
                method="GET",
                path=path,
                headers={"X-Navi-Auth": "test_auth"},
                body=b"",
            )
            body_str = body_bytes.decode("utf-8", errors="replace")

            # Resolve in browser via window.__naviResolve
            resolve_js = f"window.__naviResolve && window.__naviResolve({json.dumps(req_id)}, {status}, {json.dumps(body_str)});"
            try:
                self.driver.evaluate(resolve_js, await_promise=False)
                count += 1
            except Exception:
                pass

        with self._lock:
            self.handled_count += count
        return count

    def start(self, interval: float = 0.03) -> "NaviQueueBridge":
        """Start background polling thread."""
        if self._running:
            return self
        self.install_hook()
        self._running = True

        def _loop() -> None:
            while self._running:
                try:
                    self.poll_once()
                except Exception:
                    pass
                time.sleep(interval)

        self._thread = threading.Thread(target=_loop, daemon=True)
        self._thread.start()
        return self

    def stop(self) -> None:
        """Stop background polling thread."""
        self._running = False
        if self._thread:
            self._thread.join(timeout=2.0)
            self._thread = None
        try:
            self.driver.evaluate(self.RESTORE_HOOK_EXPR, await_promise=False)
        except Exception:
            pass

    def __enter__(self) -> "NaviQueueBridge":
        return self.start()

    def __exit__(self, exc_type: Any, exc_val: Any, exc_tb: Any) -> None:
        self.stop()
