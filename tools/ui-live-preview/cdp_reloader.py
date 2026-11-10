"""
@file  cdp_reloader.py
@brief WebSocket CDP client reloading CEF scripts on source changes


Steam CEF Chrome DevTools Protocol (CDP) Hot Reloader.

Connects to Steam running with -cef-enable-debugging (default port 8080)
and hot-evaluates or reloads modified scripts into active Steam CEF targets.

Zero hard external dependencies: includes a pure Python standard-library
RFC 6455 WebSocket client with transparent fallback to websocket-client if available.
"""

from __future__ import annotations

import base64
import json
import os
from pathlib import Path
import re
import socket
import struct
import threading
import time
from typing import Any, Dict, List, Optional, Tuple
from urllib.error import URLError
from urllib.parse import urlparse
import urllib.request


def _send_cdp_pure_python(ws_url: str, message_dict: Dict[str, Any], timeout: float = 3.0) -> Optional[Dict[str, Any]]:
    """
    Pure Python RFC 6455 client to send a single CDP JSON message and read response.
    """
    parsed = urlparse(ws_url)
    host = parsed.hostname or "127.0.0.1"
    port = parsed.port or 8080
    path = parsed.path or "/"

    s = socket.create_connection((host, port), timeout=timeout)
    try:
        # Perform WebSocket Handshake
        key = base64.b64encode(os.urandom(16)).decode("ascii")
        handshake = (
            f"GET {path} HTTP/1.1\r\n"
            f"Host: {host}:{port}\r\n"
            f"Upgrade: websocket\r\n"
            f"Connection: Upgrade\r\n"
            f"Sec-WebSocket-Key: {key}\r\n"
            f"Sec-WebSocket-Version: 13\r\n\r\n"
        )
        s.sendall(handshake.encode("ascii"))

        # Read handshake response
        resp = b""
        while b"\r\n\r\n" not in resp:
            chunk = s.recv(4096)
            if not chunk:
                break
            resp += chunk

        if not resp.startswith(b"HTTP/1.1 101"):
            return None

        # Build RFC 6455 Text Frame
        payload = json.dumps(message_dict).encode("utf-8")
        length = len(payload)

        header = bytearray([0x81])  # FIN + Text Opcode
        if length < 126:
            header.append(0x80 | length)  # Mask bit set
        elif length < 65536:
            header.append(0x80 | 126)
            header.extend(struct.pack("!H", length))
        else:
            header.append(0x80 | 127)
            header.extend(struct.pack("!Q", length))

        mask = os.urandom(4)
        header.extend(mask)

        # Mask payload
        masked_payload = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
        s.sendall(header + masked_payload)

        # Read response frame with timeout
        s.settimeout(timeout)
        b1_b2 = s.recv(2)
        if len(b1_b2) < 2:
            return None
        b1, b2 = b1_b2[0], b1_b2[1]
        payload_len = b2 & 0x7F
        if payload_len == 126:
            ext = s.recv(2)
            payload_len = struct.unpack("!H", ext)[0]
        elif payload_len == 127:
            ext = s.recv(8)
            payload_len = struct.unpack("!Q", ext)[0]

        is_masked = bool(b2 & 0x80)
        res_mask = s.recv(4) if is_masked else b""

        received = b""
        while len(received) < payload_len:
            chunk = s.recv(min(4096, payload_len - len(received)))
            if not chunk:
                break
            received += chunk

        if is_masked:
            received = bytes(b ^ res_mask[i % 4] for i, b in enumerate(received))

        return json.loads(received.decode("utf-8", errors="replace"))
    finally:
        try:
            s.close()
        except Exception:
            pass


def send_cdp_command(ws_url: str, method: str, params: Optional[Dict[str, Any]] = None) -> Optional[Dict[str, Any]]:
    """
    Send a CDP command via WebSocket. Tries websocket-client if installed,
    falls back to pure Python RFC 6455 implementation.
    """
    msg = {
        "id": int(time.time() * 1000) % 1000000,
        "method": method,
        "params": params or {},
    }

    # Attempt websocket-client if available
    try:
        import websocket  # type: ignore
        ws = websocket.create_connection(ws_url, timeout=3.0)
        try:
            ws.send(json.dumps(msg))
            resp = ws.recv()
            return json.loads(resp)
        finally:
            ws.close()
    except (ImportError, Exception):
        # Fall back to pure standard library implementation
        return _send_cdp_pure_python(ws_url, msg)


class SteamCdpReloader:
    """
    Discovers Steam CEF targets on port 8080 and handles hot-reloading.
    """

    def __init__(self, host: str = "127.0.0.1", port: int = 8080) -> None:
        self.host = host
        self.port = port
        self.last_connected_time: float = 0.0
        self.targets: List[Dict[str, Any]] = []
        self._lock = threading.Lock()

    @property
    def discovery_url(self) -> str:
        return f"http://{self.host}:{self.port}/json"

    def is_available(self) -> bool:
        """Check whether Steam CEF remote debugging is reachable."""
        try:
            req = urllib.request.Request(self.discovery_url, headers={"User-Agent": "Navi-Preview"})
            with urllib.request.urlopen(req, timeout=1.0) as resp:
                return resp.status == 200
        except (URLError, OSError, TimeoutError):
            return False

    def list_targets(self) -> List[Dict[str, Any]]:
        """Fetch active CDP targets from Steam CEF."""
        try:
            req = urllib.request.Request(self.discovery_url, headers={"User-Agent": "Navi-Preview"})
            with urllib.request.urlopen(req, timeout=1.5) as resp:
                data = json.loads(resp.read().decode("utf-8"))
                with self._lock:
                    self.targets = data
                    self.last_connected_time = time.time()
                return data
        except Exception:
            return []

    def find_relevant_targets(self, script_name: str) -> List[Dict[str, Any]]:
        """
        Filter Steam targets matching the script type:
        - Store scripts -> Steam Store windows
        - Library / Shell scripts -> Steam Client / Library windows
        """
        all_targets = self.list_targets()
        relevant: List[Dict[str, Any]] = []

        is_store = "store" in script_name or "depot" in script_name
        is_library = "library" in script_name or "app-details" in script_name or "lua" in script_name or "cloud" in script_name or "top-bar" in script_name

        for t in all_targets:
            title = t.get("title", "").lower()
            url = t.get("url", "").lower()
            ws_url = t.get("webSocketDebuggerUrl")

            if not ws_url:
                continue

            # Skip background extensions or devtools
            if "devtools://" in url or "chrome-extension://" in url:
                continue

            is_store_target = "store.steampowered.com" in url or (title.startswith("buy ") and "on steam" in title)
            is_library_target = ("steamloopback.host" in url or title == "steam" or "steam big picture" in title or "library" in title) and not is_store_target

            if is_store:
                if is_store_target:
                    relevant.append(t)
            elif is_library:
                if is_library_target:
                    relevant.append(t)
            else:
                relevant.append(t)

        # Fallback: if no specific match, match all page targets
        if not relevant:
            relevant = [t for t in all_targets if t.get("type") == "page" and t.get("webSocketDebuggerUrl")]

        return relevant

    def reload_script_into_steam(self, script_path: Path | str) -> Dict[str, Any]:
        """
        Hot-evaluate the updated script in matching Steam targets.
        """
        path = Path(script_path)
        if not path.exists():
            return {"success": False, "error": f"Script not found: {path}"}

        code = path.read_text(encoding="utf-8")
        targets = self.find_relevant_targets(path.name)

        if not targets:
            return {
                "success": False,
                "targets_count": 0,
                "message": f"No active Steam targets found on {self.discovery_url}",
            }

        results = []
        for target in targets:
            ws_url = target.get("webSocketDebuggerUrl")
            title = target.get("title", "Unknown")
            try:
                # Wrap script in self-executing block with error protection
                eval_code = f"try {{ {code}\n console.log('[Navi Hot-Reload] Re-evaluated {path.name}'); }} catch(e) {{ console.error('[Navi Hot-Reload Error]', e); }}"
                res = send_cdp_command(
                    ws_url,
                    "Runtime.evaluate",
                    {"expression": eval_code, "userGesture": True, "awaitPromise": False},
                )
                results.append({"target": title, "success": True, "res": res})
            except Exception as e:
                results.append({"target": title, "success": False, "error": str(e)})

        return {
            "success": any(r["success"] for r in results),
            "script": path.name,
            "targets_count": len(targets),
            "results": results,
        }

    def reload_target_page(self, target_title_substr: str = "") -> Dict[str, Any]:
        """Hard reload the matching Steam CEF page."""
        all_targets = self.list_targets()
        count = 0
        for t in all_targets:
            if not target_title_substr or target_title_substr.lower() in t.get("title", "").lower():
                ws_url = t.get("webSocketDebuggerUrl")
                if ws_url:
                    send_cdp_command(ws_url, "Page.reload", {"ignoreCache": True})
                    count += 1
        return {"success": count > 0, "reloaded_targets": count}
