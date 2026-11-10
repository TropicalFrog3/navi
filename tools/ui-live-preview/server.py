#!/usr/bin/env python3
"""
@file  server.py
@brief Live preview HTTP server serving mock Steam UI and scripts


Navi UI Live Preview & Hot Reload Server.

Features:
  1. Standalone Browser Live Preview:
     - Serves interactive Studio Dashboard (index.html) with shell switcher & viewport resizer.
     - Live-reload via Server-Sent Events (SSE) with hot-script evaluation.
     - Integrated Mock API for Navi daemon port 9001 routes.
  2. Live Steam Client CDP Mode (--steam):
     - Connects to Steam CEF remote debugging (port 8080) and hot-evaluates modified scripts.
  3. Zero hard external dependencies (Python 3 standard library).
"""

from __future__ import annotations

import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import re
import socket
import sys
import threading
import time
from typing import Any, Dict, List, Optional, Set, Tuple
from urllib.parse import parse_qs, urlparse

# Local module imports
SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parents[1]

# Ensure tools/ui-live-preview is on sys.path
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from cdp_reloader import SteamCdpReloader
from file_watcher import FileWatcher
from mock_api import NaviMockApi


class SseClient:
    """Represents a connected SSE live-reload subscriber."""

    def __init__(self, wfile: Any) -> None:
        self.wfile = wfile
        self.queue: List[str] = []
        self._lock = threading.Lock()
        self.active = True

    def send_event(self, data: Dict[str, Any], event_type: str = "message") -> bool:
        if not self.active:
            return False
        try:
            payload = json.dumps(data)
            msg = f"event: {event_type}\ndata: {payload}\n\n".encode("utf-8")
            with self._lock:
                self.wfile.write(msg)
                self.wfile.flush()
            return True
        except (BrokenPipeError, ConnectionResetError, OSError):
            self.active = False
            return False

    def send_heartbeat(self) -> bool:
        if not self.active:
            return False
        try:
            with self._lock:
                self.wfile.write(b": heartbeat\n\n")
                self.wfile.flush()
            return True
        except (BrokenPipeError, ConnectionResetError, OSError):
            self.active = False
            return False


class LivePreviewServer:
    """Core Live Preview Server coordinating HTTP, Mock API, File Watcher, and CDP."""

    def __init__(
        self,
        host: str = "127.0.0.1",
        port: int = 5050,
        mock_port: int = 9001,
        enable_cdp: bool = False,
        cdp_host: str = "127.0.0.1",
        cdp_port: int = 8080,
        default_fixture: str = "library",
        debounce_delay: float = 0.20,
    ) -> None:
        self.host = host
        self.port = port
        self.mock_port = mock_port
        self.enable_cdp = enable_cdp
        self.default_fixture = default_fixture
        self.start_time = time.time()

        self.mock_api = NaviMockApi()
        self.cdp_reloader = SteamCdpReloader(host=cdp_host, port=cdp_port) if enable_cdp else None

        # Watch directories
        self.watch_dirs = [
            REPO_ROOT / "res" / "inject-scripts",
            REPO_ROOT / "tests" / "fixtures" / "ui_shells",
        ]
        self.file_watcher = FileWatcher(
            watch_dirs=self.watch_dirs,
            debounce_delay=debounce_delay,
        )
        self.file_watcher.register_callback(self._on_files_changed)

        self.sse_clients: Set[SseClient] = set()
        self._sse_lock = threading.Lock()

        self.http_server: Optional[ThreadingHTTPServer] = None
        self.mock_daemon_server: Optional[ThreadingHTTPServer] = None
        self._running = False

    def start(self, block: bool = True) -> None:
        """Start all services."""
        self._running = True
        self.file_watcher.start()

        # Start optional dedicated port 9001 daemon if available
        if self.mock_port > 0 and self.mock_port != self.port:
            self._start_dedicated_mock_daemon()

        # Start main preview server
        handler_factory = self._create_handler()
        self.http_server = ThreadingHTTPServer((self.host, self.port), handler_factory)

        print("\n" + "=" * 64)
        print("🚀 NAVI UI STUDIO - LIVE PREVIEW & HOT RELOAD SERVER")
        print("=" * 64)
        print(f"  • Studio Dashboard : http://{self.host}:{self.port}/")
        print(f"  • Library Preview  : http://{self.host}:{self.port}/_preview/shell/library")
        print(f"  • Store Preview    : http://{self.host}:{self.port}/_preview/shell/store")
        print(f"  • Mock Navi API    : http://{self.host}:{self.port}/ (integrated)")
        if self.mock_daemon_server:
            print(f"  • Dedicated Daemon : http://{self.host}:{self.mock_port}/ (active)")
        else:
            print(f"  • Dedicated Daemon : Port {self.mock_port} in use or bypassed (using integrated)")
        if self.enable_cdp:
            cdp_status = "ONLINE" if self.cdp_reloader and self.cdp_reloader.is_available() else "WAITING"
            print(f"  • Steam CEF CDP    : http://{self.cdp_reloader.host}:{self.cdp_reloader.port} ({cdp_status})")
        print("=" * 64)
        print("  Press Ctrl+C to stop.\n")

        if block:
            try:
                self.http_server.serve_forever()
            except KeyboardInterrupt:
                print("\n[Navi Studio] Shutting down...")
            finally:
                self.stop()

    def stop(self) -> None:
        """Stop all running servers and background threads."""
        self._running = False
        self.file_watcher.stop()

        with self._sse_lock:
            for client in list(self.sse_clients):
                client.active = False
            self.sse_clients.clear()

        if self.mock_daemon_server:
            try:
                self.mock_daemon_server.shutdown()
                self.mock_daemon_server.server_close()
            except Exception:
                pass
            self.mock_daemon_server = None

        if self.http_server:
            try:
                self.http_server.shutdown()
                self.http_server.server_close()
            except Exception:
                pass
            self.http_server = None

    def broadcast_reload(self, changed_files: List[Dict[str, Any]]) -> None:
        """Broadcast live-reload event to all connected SSE clients."""
        primary_name = changed_files[0]["name"] if changed_files else "unknown"
        data = {
            "type": "reload",
            "file": primary_name,
            "files": [f["name"] for f in changed_files],
            "timestamp": time.time(),
        }

        with self._sse_lock:
            dead_clients = set()
            for client in self.sse_clients:
                if not client.send_event(data):
                    dead_clients.add(client)
            self.sse_clients -= dead_clients

    def _on_files_changed(self, changes: List[Dict[str, Any]]) -> None:
        file_names = ", ".join(c["name"] for c in changes)
        print(f"[Navi Studio] File change detected: {file_names}")

        # 1. Broadcast to browser preview clients
        self.broadcast_reload(changes)

        # 2. If Steam CDP is enabled, hot-reload scripts into real Steam
        if self.enable_cdp and self.cdp_reloader:
            for item in changes:
                path = Path(item["path"])
                if path.suffix == ".js":
                    res = self.cdp_reloader.reload_script_into_steam(path)
                    print(f"[Navi Studio] Steam CDP hot-reload for {path.name}: {res.get('targets_count', 0)} targets updated.")

    def _start_dedicated_mock_daemon(self) -> None:
        """Attempt to bind dedicated port 9001 for seamless mock API calls."""
        try:
            parent = self

            class DedicatedMockHandler(BaseHTTPRequestHandler):
                def log_message(self, format: str, *args: Any) -> None:
                    pass

                def do_OPTIONS(self) -> None:
                    self.send_response(200)
                    parent._send_cors_headers(self)
                    self.end_headers()

                def _handle(self, method: str) -> None:
                    length = int(self.headers.get("Content-Length", 0))
                    body = self.rfile.read(length) if length > 0 else b""
                    status, resp_bytes, content_type = parent.mock_api.dispatch(
                        method=method,
                        path=self.path,
                        headers=dict(self.headers),
                        body=body,
                    )
                    self.send_response(status)
                    self.send_header("Content-Type", content_type)
                    self.send_header("Content-Length", str(len(resp_bytes)))
                    parent._send_cors_headers(self)
                    self.end_headers()
                    self.wfile.write(resp_bytes)

                def do_GET(self) -> None: self._handle("GET")
                def do_POST(self) -> None: self._handle("POST")
                def do_PUT(self) -> None: self._handle("PUT")
                def do_DELETE(self) -> None: self._handle("DELETE")

            self.mock_daemon_server = ThreadingHTTPServer((self.host, self.mock_port), DedicatedMockHandler)
            t = threading.Thread(target=self.mock_daemon_server.serve_forever, daemon=True, name="DedicatedMock9001")
            t.start()
        except OSError:
            # Port is occupied (e.g. real Navi or another test), continue with integrated mock
            self.mock_daemon_server = None

    def _send_cors_headers(self, handler: BaseHTTPRequestHandler) -> None:
        handler.send_header("Access-Control-Allow-Origin", "*")
        handler.send_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS")
        handler.send_header("Access-Control-Allow-Headers", "*")
        handler.send_header("Access-Control-Max-Age", "86400")

    def _create_handler(self) -> Any:
        parent = self

        class StudioRequestHandler(BaseHTTPRequestHandler):
            def log_message(self, format: str, *args: Any) -> None:
                # Suppress default request logging to keep console clean
                pass

            def do_OPTIONS(self) -> None:
                self.send_response(200)
                parent._send_cors_headers(self)
                self.end_headers()

            def do_GET(self) -> None:
                self._route_request("GET")

            def do_POST(self) -> None:
                self._route_request("POST")

            def do_PUT(self) -> None:
                self._route_request("PUT")

            def do_DELETE(self) -> None:
                self._route_request("DELETE")

            def _route_request(self, method: str) -> None:
                parsed = urlparse(self.path)
                path = parsed.path

                # 1. Dashboard Studio Root
                if path in ["/", "/index.html"]:
                    self._serve_static_file(SCRIPT_DIR / "static" / "index.html", "text/html; charset=utf-8")
                    return

                # 2. Static Assets
                if path.startswith("/static/"):
                    rel = path[len("/static/"):]
                    target = SCRIPT_DIR / "static" / rel
                    self._serve_static_file(target)
                    return

                # 3. Live Reload SSE Stream
                if path == "/_live/events":
                    self._handle_sse_stream()
                    return

                # 4. Status and Diagnostics
                if path == "/_live/status":
                    self._handle_live_status()
                    return

                # 5. API Log endpoint
                if path == "/_live/api-log":
                    query = parse_qs(parsed.query)
                    if "clear" in query:
                        parent.mock_api.clear_logs()
                    logs = parent.mock_api.get_logs()
                    self._send_json(logs)
                    return

                # 6. Manual Reload Trigger
                if path == "/_live/reload":
                    parent.broadcast_reload([{"name": "manual_trigger"}])
                    self._send_json({"success": True, "message": "Reload event emitted"})
                    return

                # 7. Shell Previews
                if path == "/_preview/shell/library":
                    self._serve_library_shell()
                    return

                if path == "/_preview/shell/store":
                    self._serve_store_shell()
                    return

                # 8. Script Serving
                if path.startswith("/_preview/scripts/"):
                    self._serve_inject_script(path[len("/_preview/scripts/"):])
                    return

                # 9. Integrated Mock Navi Daemon API (/cloud/*, /check, /config, etc.)
                length = int(self.headers.get("Content-Length", 0))
                body = self.rfile.read(length) if length > 0 else b""
                status, resp_bytes, content_type = parent.mock_api.dispatch(
                    method=method,
                    path=self.path,
                    headers=dict(self.headers),
                    body=body,
                )
                self.send_response(status)
                self.send_header("Content-Type", content_type)
                self.send_header("Content-Length", str(len(resp_bytes)))
                parent._send_cors_headers(self)
                self.end_headers()
                self.wfile.write(resp_bytes)

            def _handle_sse_stream(self) -> None:
                self.send_response(200)
                self.send_header("Content-Type", "text/event-stream")
                self.send_header("Cache-Control", "no-cache")
                self.send_header("Connection", "keep-alive")
                parent._send_cors_headers(self)
                self.end_headers()

                client = SseClient(self.wfile)
                with parent._sse_lock:
                    parent.sse_clients.add(client)

                client.send_event({
                    "type": "connected",
                    "client_id": id(client),
                    "time": time.time(),
                })

                # Keep thread alive sending periodic heartbeats
                try:
                    while client.active and parent._running:
                        time.sleep(15)
                        client.send_heartbeat()
                except Exception:
                    pass
                finally:
                    with parent._sse_lock:
                        parent.sse_clients.discard(client)

            def _handle_live_status(self) -> None:
                # Scan watched files list
                snapshot = parent.file_watcher.scan_snapshot()
                watched_list = []
                for p, mtime in sorted(snapshot.items(), key=lambda x: str(x[0])):
                    rel = str(p.relative_to(REPO_ROOT)) if p.is_relative_to(REPO_ROOT) else str(p)
                    watched_list.append({
                        "rel_path": rel,
                        "mtime": mtime,
                        "mtime_str": time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(mtime / 1e9)),
                    })

                cdp_info = {
                    "enabled": parent.enable_cdp,
                    "connected": parent.cdp_reloader.is_available() if parent.cdp_reloader else False,
                    "host": parent.cdp_reloader.host if parent.cdp_reloader else "127.0.0.1",
                    "port": parent.cdp_reloader.port if parent.cdp_reloader else 8080,
                    "targets": parent.cdp_reloader.list_targets() if parent.cdp_reloader and parent.cdp_reloader.is_available() else [],
                }

                status_payload = {
                    "uptime_seconds": round(time.time() - parent.start_time, 1),
                    "connected_clients": len(parent.sse_clients),
                    "watched_files": watched_list,
                    "steam_cdp": cdp_info,
                }
                self._send_json(status_payload)

            def _serve_library_shell(self) -> None:
                fixture_path = REPO_ROOT / "tests" / "fixtures" / "ui_shells" / "steam_library_sp.html"
                if not fixture_path.exists():
                    self.send_error(404, f"Fixture not found: {fixture_path}")
                    return

                html = fixture_path.read_text(encoding="utf-8")

                # Inject Live Reload Client and scripts into <head> or <body>
                inject_tags = (
                    '\n    <!-- Navi Live Preview Injections -->\n'
                    '    <script src="/static/live_reload_client.js"></script>\n'
                    '    <script src="/_preview/scripts/vendor/jszip.min.js"></script>\n'
                    '    <script src="/_preview/scripts/shell/version-top-bar.js"></script>\n'
                    '    <script src="/_preview/scripts/cloud/cloud_manager.js"></script>\n'
                    '    <script src="/_preview/scripts/shell/import-lua-script.js"></script>\n'
                    '    <script src="/_preview/scripts/library/app-details-script.js"></script>\n'
                    '    <script src="/_preview/scripts/library/auto-collection.js"></script>\n'
                    '    <script src="/_preview/scripts/core/navi_inject.js"></script>\n'
                )

                if "</head>" in html:
                    html = html.replace("</head>", inject_tags + "</head>")
                else:
                    html = inject_tags + html

                self._send_bytes(html.encode("utf-8"), "text/html; charset=utf-8")

            def _serve_store_shell(self) -> None:
                fixture_path = REPO_ROOT / "tests" / "fixtures" / "ui_shells" / "steam_store_app.html"
                if not fixture_path.exists():
                    self.send_error(404, f"Fixture not found: {fixture_path}")
                    return

                html = fixture_path.read_text(encoding="utf-8")

                # Relax or remove restrictive CSP meta tag so browser preview can connect
                html = re.sub(r'<meta\s+http-equiv=["\']Content-Security-Policy["\'][^>]*>', '', html, flags=re.IGNORECASE)

                inject_tags = (
                    '\n    <!-- Navi Live Preview Injections -->\n'
                    '    <script src="/static/live_reload_client.js"></script>\n'
                    '    <script src="/_preview/scripts/cloud/cloud_manager.js"></script>\n'
                    '    <script src="/_preview/scripts/store/store-page-script.js"></script>\n'
                )

                if "</head>" in html:
                    html = html.replace("</head>", inject_tags + "</head>")
                else:
                    html = inject_tags + html

                self._send_bytes(html.encode("utf-8"), "text/html; charset=utf-8")

            def _serve_inject_script(self, rel_path: str) -> None:
                scripts_root = REPO_ROOT / "res" / "inject-scripts"
                target_file = (scripts_root / rel_path).resolve()

                # Security boundary check
                if not str(target_file).startswith(str(scripts_root)):
                    self.send_error(403, "Access denied")
                    return

                # If path didn't exist directly, check flat symlink or search
                if not target_file.exists():
                    flat_name = Path(rel_path).name
                    candidate = scripts_root / flat_name
                    if candidate.exists():
                        target_file = candidate.resolve()
                    else:
                        self.send_error(404, f"Script not found: {rel_path}")
                        return

                code = target_file.read_text(encoding="utf-8")

                # Perform %APPID% substitution if depot selection script
                if "depot" in target_file.name:
                    code = code.replace("%APPID%", "1443430")

                self._send_bytes(code.encode("utf-8"), "application/javascript; charset=utf-8")

            def _serve_static_file(self, file_path: Path, forced_content_type: Optional[str] = None) -> None:
                resolved = file_path.resolve()
                if not resolved.exists() or not resolved.is_file():
                    self.send_error(404, "File not found")
                    return

                # Content type detection
                ct = forced_content_type
                if not ct:
                    ext = resolved.suffix.lower()
                    ct_map = {
                        ".html": "text/html; charset=utf-8",
                        ".js": "application/javascript; charset=utf-8",
                        ".css": "text/css; charset=utf-8",
                        ".json": "application/json",
                        ".svg": "image/svg+xml",
                        ".png": "image/png",
                    }
                    ct = ct_map.get(ext, "application/octet-stream")

                data = resolved.read_bytes()
                self._send_bytes(data, ct)

            def _send_bytes(self, data: bytes, content_type: str) -> None:
                self.send_response(200)
                self.send_header("Content-Type", content_type)
                self.send_header("Content-Length", str(len(data)))
                parent._send_cors_headers(self)
                self.end_headers()
                self.wfile.write(data)

            def _send_json(self, data: Any) -> None:
                encoded = json.dumps(data, indent=2).encode("utf-8")
                self._send_bytes(encoded, "application/json")

        return StudioRequestHandler


def parse_args(args: Optional[List[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Navi UI Live Preview & Hot Reload Dev Server",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument("--port", type=int, default=5050, help="Live preview server port")
    parser.add_argument("--host", type=str, default="127.0.0.1", help="Server bind host")
    parser.add_argument("--mock-port", type=int, default=9001, help="Dedicated Navi daemon mock port")
    parser.add_argument("--steam", "--cdp", action="store_true", dest="enable_cdp", help="Enable Steam Client CDP hot-reloading")
    parser.add_argument("--cdp-host", type=str, default="127.0.0.1", help="Steam remote debugging host")
    parser.add_argument("--cdp-port", type=int, default=8080, help="Steam remote debugging port")
    parser.add_argument("--fixture", choices=["library", "store"], default="library", help="Default shell fixture")
    parser.add_argument("--no-browser", action="store_true", help="Do not open web browser automatically")
    parser.add_argument("--debounce", type=float, default=0.20, help="File watcher debounce delay (seconds)")
    return parser.parse_args(args)


def main() -> None:
    opts = parse_args()

    server = LivePreviewServer(
        host=opts.host,
        port=opts.port,
        mock_port=opts.mock_port,
        enable_cdp=opts.enable_cdp,
        cdp_host=opts.cdp_host,
        cdp_port=opts.cdp_port,
        default_fixture=opts.fixture,
        debounce_delay=opts.debounce,
    )

    if not opts.no_browser:
        def _open() -> None:
            time.sleep(0.5)
            try:
                import webbrowser
                webbrowser.open(f"http://{opts.host}:{opts.port}/")
            except Exception:
                pass
        threading.Thread(target=_open, daemon=True).start()

    server.start(block=True)


if __name__ == "__main__":
    main()

