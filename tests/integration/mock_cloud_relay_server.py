"""
@file  mock_cloud_relay_server.py
@brief Mock HTTP server simulating remote cloud storage endpoints


Mock Cloud Relay Server for E2E Cross-Device Sync Testing.
Emulates Google Drive API v3 (appDataFolder) and WebDAV / S3-compatible endpoints
over real TCP sockets using Python's ThreadingHTTPServer.
"""

from __future__ import annotations

import json
import re
import threading
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any, Dict, List, Optional


class MockCloudRelayRequestHandler(BaseHTTPRequestHandler):
    """HTTP request handler emulating Google Drive v3 and WebDAV APIs."""

    server: MockCloudRelayServer  # type hint for server instance

    def log_message(self, format: str, *args: Any) -> None:
        """Suppress default stdout access logs unless verbose is enabled."""
        if getattr(self.server, "verbose", False):
            super().log_message(format, *args)

    def _read_body(self) -> bytes:
        length_str = self.headers.get("Content-Length")
        if not length_str:
            return b""
        try:
            length = int(length_str)
            return self.rfile.read(length)
        except (ValueError, OSError):
            return b""

    def _send_json(self, status_code: int, data: Dict[str, Any], headers: Optional[Dict[str, str]] = None) -> None:
        body = json.dumps(data).encode("utf-8")
        self.send_response(status_code)
        self.send_header("Content-Type", "application/json; charset=UTF-8")
        self.send_header("Content-Length", str(len(body)))
        if headers:
            for k, v in headers.items():
                self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)

    def _send_bytes(self, status_code: int, body: bytes, headers: Optional[Dict[str, str]] = None) -> None:
        self.send_response(status_code)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(len(body)))
        if headers:
            for k, v in headers.items():
                self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)

    def _check_gdrive_auth(self) -> bool:
        if self.server.simulate_auth_expired:
            return False
        auth = self.headers.get("Authorization", "")
        if not auth.startswith("Bearer "):
            return False
        token = auth[7:].strip()
        if token in ("expired_token", "invalid_token"):
            return False
        return True

    def _check_webdav_auth(self) -> bool:
        if not self.server.webdav_auth_required:
            return True
        if self.server.simulate_auth_expired:
            return False
        auth = self.headers.get("Authorization", "")
        if auth.startswith("Basic ") or auth.startswith("Bearer "):
            token = auth.split(" ", 1)[1].strip()
            if token in ("expired_token", "invalid_token"):
                return False
            return True
        return False

    # ─────────────────────────────────────────────────────────────────────────
    # HTTP Method Handlers
    # ─────────────────────────────────────────────────────────────────────────

    def do_POST(self) -> None:
        url_path = urllib.parse.urlparse(self.path).path
        body = self._read_body()

        # Google Drive RFC 8628 Device Flow: /device/code
        if url_path.endswith("/device/code"):
            resp = {
                "device_code": "dev_code_navi_deck_e2e_12345",
                "user_code": "NAVI-DECK",
                "verification_url": "https://www.google.com/device",
                "verification_uri": "https://www.google.com/device",
                "expires_in": 1800,
                "interval": 1,
            }
            self._send_json(200, resp)
            return

        # Google Drive OAuth Token Endpoint: /oauth2/v4/token or /oauth2/token
        if "/oauth2/" in url_path or url_path.endswith("/token"):
            form_str = body.decode("utf-8", errors="replace")
            params = urllib.parse.parse_qs(form_str)
            grant_type = params.get("grant_type", [""])[0]

            if self.server.simulate_pending_device_auth:
                self._send_json(400, {
                    "error": "authorization_pending",
                    "error_description": "User has not yet verified the device code.",
                })
                return

            access_token = "ya29.mock_valid_access_token_12345"
            refresh_token = "1//mock_valid_refresh_token_67890"

            self._send_json(200, {
                "access_token": access_token,
                "token_type": "Bearer",
                "expires_in": 3600,
                "refresh_token": refresh_token,
            })
            return

        # Google Drive File Metadata Creation: POST /drive/v3/files or /files
        if url_path.endswith("/files"):
            if not self._check_gdrive_auth():
                self._send_json(401, {"error": {"code": 401, "message": "Invalid Credentials"}})
                return

            try:
                meta = json.loads(body.decode("utf-8", errors="replace")) if body else {}
            except Exception:
                meta = {}
            file_name = meta.get("name", "unnamed_file")

            with self.server.lock:
                # Reuse existing file id if same name already exists
                existing_id = None
                for fid, finfo in self.server.gdrive_files.items():
                    if finfo["name"] == file_name:
                        existing_id = fid
                        break

                if existing_id:
                    file_id = existing_id
                    etag = self.server.gdrive_files[file_id]["etag"]
                else:
                    file_id = f"gdrive_{len(self.server.gdrive_files) + 1}_{hash(file_name) & 0xFFFFFF}"
                    etag = '"etag_v1"'
                    self.server.gdrive_files[file_id] = {
                        "id": file_id,
                        "name": file_name,
                        "data": b"",
                        "etag": etag,
                        "version": 1,
                    }

            self._send_json(200, {
                "id": file_id,
                "name": file_name,
                "headRevisionId": etag,
                "version": etag,
            })
            return

        # Google Drive Direct Upload: POST /upload/drive/v3/files
        if "/upload/" in url_path and url_path.endswith("/files"):
            if not self._check_gdrive_auth():
                self._send_json(401, {"error": {"code": 401, "message": "Invalid Credentials"}})
                return

            with self.server.lock:
                if self.server.simulate_quota_exceeded or (
                    self.server.quota_limit > 0 and (self.server.quota_used + len(body)) > self.server.quota_limit
                ):
                    self._send_json(403, {
                        "error": {
                            "errors": [{"reason": "storageQuotaExceeded"}],
                            "code": 403,
                            "message": "The user Drive storage quota has been exceeded.",
                        }
                    })
                    return

                file_id = f"gdrive_upload_{len(self.server.gdrive_files) + 1}"
                etag = '"etag_v1"'
                self.server.gdrive_files[file_id] = {
                    "id": file_id,
                    "name": file_id,
                    "data": body,
                    "etag": etag,
                    "version": 1,
                }
                self.server.quota_used += len(body)

            self._send_json(200, {
                "id": file_id,
                "headRevisionId": etag,
                "version": etag,
            }, headers={"ETag": etag})
            return

        self.send_error(404, "Not Found")

    def do_PATCH(self) -> None:
        parsed = urllib.parse.urlparse(self.path)
        url_path = parsed.path
        body = self._read_body()

        # Google Drive Media Upload: PATCH /upload/drive/v3/files/<id>
        m = re.search(r"/files/([^/?]+)", url_path)
        if m:
            if not self._check_gdrive_auth():
                self._send_json(401, {"error": {"code": 401, "message": "Invalid Credentials"}})
                return

            file_id = m.group(1)
            with self.server.lock:
                if self.server.simulate_quota_exceeded or (
                    self.server.quota_limit > 0 and (self.server.quota_used + len(body)) > self.server.quota_limit
                ):
                    self._send_json(403, {
                        "error": {
                            "errors": [{"reason": "storageQuotaExceeded"}],
                            "code": 403,
                            "message": "The user Drive storage quota has been exceeded.",
                        }
                    })
                    return

                file_info = self.server.gdrive_files.get(file_id)
                if not file_info:
                    file_info = {
                        "id": file_id,
                        "name": file_id,
                        "data": b"",
                        "etag": '"etag_v0"',
                        "version": 0,
                    }
                    self.server.gdrive_files[file_id] = file_info

                # Conditional ETag check
                if_match = self.headers.get("If-Match")
                if if_match and if_match != file_info["etag"]:
                    self.send_error(412, "Precondition Failed")
                    return

                old_len = len(file_info["data"])
                file_info["data"] = body
                file_info["version"] += 1
                new_etag = f'"etag_v{file_info["version"]}"'
                file_info["etag"] = new_etag
                self.server.quota_used += (len(body) - old_len)

            self._send_json(200, {
                "id": file_id,
                "headRevisionId": new_etag,
                "version": new_etag,
            }, headers={"ETag": new_etag})
            return

        self.send_error(404, "Not Found")

    def do_GET(self) -> None:
        parsed = urllib.parse.urlparse(self.path)
        url_path = parsed.path
        query = urllib.parse.parse_qs(parsed.query)

        # ── WebDAV GET: /webdav/<path> ──
        if url_path.startswith("/webdav/"):
            if not self._check_webdav_auth():
                self.send_error(401, "Unauthorized")
                return

            with self.server.lock:
                item = self.server.webdav_files.get(url_path)
                if not item:
                    self.send_error(404, "Not Found")
                    return
                data = item["data"]
                etag = item["etag"]

            self._send_bytes(200, data, headers={"ETag": etag})
            return

        # ── Google Drive GET ──
        if not self._check_gdrive_auth():
            self._send_json(401, {"error": {"code": 401, "message": "Invalid Credentials"}})
            return

        # /drive/v3/about?fields=storageQuota
        if url_path.endswith("/about"):
            with self.server.lock:
                usage = self.server.quota_used
                limit = self.server.quota_limit
            self._send_json(200, {
                "storageQuota": {
                    "limit": str(limit),
                    "usage": str(usage),
                }
            })
            return

        # /drive/v3/files/<id>?alt=media
        m_media = re.search(r"/files/([^/?]+)", url_path)
        if m_media and "alt" in query and query["alt"] == ["media"]:
            file_id = m_media.group(1)
            with self.server.lock:
                file_info = self.server.gdrive_files.get(file_id)
                if not file_info:
                    self.send_error(404, "Not Found")
                    return
                data = file_info["data"]
                etag = file_info["etag"]
            self._send_bytes(200, data, headers={"ETag": etag})
            return

        # /drive/v3/files?spaces=appDataFolder&q=...
        if url_path.endswith("/files"):
            q_list = query.get("q", [])
            q_str = q_list[0] if q_list else ""

            target_name: Optional[str] = None
            m_name = re.search(r"name\s*=\s*'([^']+)'", q_str)
            if m_name:
                target_name = m_name.group(1)

            with self.server.lock:
                files_out = []
                for fid, finfo in self.server.gdrive_files.items():
                    if target_name is None or finfo["name"] == target_name:
                        files_out.append({
                            "id": fid,
                            "name": finfo["name"],
                            "headRevisionId": finfo["etag"],
                            "version": finfo["etag"],
                        })

            self._send_json(200, {"files": files_out})
            return

        self.send_error(404, "Not Found")

    def do_PROPFIND(self) -> None:
        """WebDAV PROPFIND quota query returning RFC 4331 quota elements."""
        if not self._check_webdav_auth():
            self.send_error(401, "Unauthorized")
            return

        with self.server.lock:
            used = self.server.quota_used
            limit = self.server.quota_limit
            avail = max(0, limit - used) if limit > 0 else 107374182400

        xml = (
            '<?xml version="1.0" encoding="utf-8" ?>\n'
            '<D:multistatus xmlns:D="DAV:">\n'
            '  <D:response>\n'
            f'    <D:href>{self.path}</D:href>\n'
            '    <D:propstat>\n'
            '      <D:prop>\n'
            f'        <D:quota-available-bytes>{avail}</D:quota-available-bytes>\n'
            f'        <D:quota-used-bytes>{used}</D:quota-used-bytes>\n'
            '      </D:prop>\n'
            '      <D:status>HTTP/1.1 200 OK</D:status>\n'
            '    </D:propstat>\n'
            '  </D:response>\n'
            '</D:multistatus>\n'
        ).encode("utf-8")

        self.send_response(207, "Multi-Status")
        self.send_header("Content-Type", "application/xml; charset=utf-8")
        self.send_header("Content-Length", str(len(xml)))
        self.end_headers()
        self.wfile.write(xml)

    def do_MKCOL(self) -> None:
        """WebDAV MKCOL directory creation."""
        if not self._check_webdav_auth():
            self.send_error(401, "Unauthorized")
            return

        self.send_response(201, "Created")
        self.send_header("Content-Length", "0")
        self.end_headers()

    def do_PUT(self) -> None:
        """WebDAV PUT storing encrypted blobs / manifests with conditional ETag check."""
        if not self._check_webdav_auth():
            self.send_error(401, "Unauthorized")
            return

        url_path = urllib.parse.urlparse(self.path).path
        body = self._read_body()

        with self.server.lock:
            # 507 Insufficient Storage quota check
            if self.server.simulate_quota_exceeded or (
                self.server.quota_limit > 0 and (self.server.quota_used + len(body)) > self.server.quota_limit
            ):
                self.send_error(507, "Insufficient Storage")
                return

            existing = self.server.webdav_files.get(url_path)

            # 412 Precondition Failed check on If-Match mismatch
            if_match = self.headers.get("If-Match")
            if if_match and existing and if_match != existing["etag"]:
                self.send_error(412, "Precondition Failed")
                return

            old_len = len(existing["data"]) if existing else 0
            ver = (existing["version"] + 1) if existing else 1
            new_etag = f'"etag_dav_v{ver}"'

            self.server.webdav_files[url_path] = {
                "path": url_path,
                "data": body,
                "etag": new_etag,
                "version": ver,
            }
            self.server.quota_used += (len(body) - old_len)

        self.send_response(201, "Created")
        self.send_header("ETag", new_etag)
        self.send_header("Content-Length", "0")
        self.end_headers()


class MockCloudRelayServer(ThreadingHTTPServer):
    """
    In-memory loopback HTTP server managing both Google Drive v3 and WebDAV
    relay endpoints for automated end-to-end testing.
    """

    def __init__(self, port: int = 0, verbose: bool = False) -> None:
        super().__init__(("127.0.0.1", port), MockCloudRelayRequestHandler)
        self.requested_port = port
        self.verbose = verbose
        self.port: int = self.server_address[1]
        self.lock = threading.Lock()

        # In-memory storage tables
        self.gdrive_files: Dict[str, Dict[str, Any]] = {}
        self.webdav_files: Dict[str, Dict[str, Any]] = {}

        # Quota configuration
        self.quota_limit: int = 500 * 1024 * 1024  # 500 MB default
        self.quota_used: int = 0

        # Simulation flags
        self.simulate_auth_expired: bool = False
        self.simulate_quota_exceeded: bool = False
        self.simulate_pending_device_auth: bool = False
        self.webdav_auth_required: bool = True

        self.thread: Optional[threading.Thread] = None

    def start(self) -> str:
        """Starts the server thread and returns base URL."""
        self.thread = threading.Thread(target=self.serve_forever, daemon=True)
        self.thread.start()
        return self.server_url()

    def stop(self) -> None:
        """Shuts down and frees the server socket."""
        self.shutdown()
        self.server_close()
        if self.thread and self.thread.is_alive():
            self.thread.join(timeout=2.0)
            self.thread = None

    def server_url(self) -> str:
        """Returns the base HTTP URL (e.g. http://127.0.0.1:41234)."""
        return f"http://127.0.0.1:{self.port}"

    def get_all_stored_blobs(self) -> List[bytes]:
        """
        Returns all raw stored byte payloads across Google Drive and WebDAV,
        allowing assertion of NAVIENC1 headers and zero plaintext leakage.
        """
        blobs: List[bytes] = []
        with self.lock:
            for item in self.gdrive_files.values():
                if item["data"]:
                    blobs.append(item["data"])
            for item in self.webdav_files.values():
                if item["data"]:
                    blobs.append(item["data"])
        return blobs

    def get_all_stored_blobs_dict(self) -> Dict[str, bytes]:
        """Returns a path -> bytes mapping of all stored objects."""
        out: Dict[str, bytes] = {}
        with self.lock:
            for fid, finfo in self.gdrive_files.items():
                if finfo["data"]:
                    out[f"gdrive://{finfo['name']}"] = finfo["data"]
            for path, pinfo in self.webdav_files.items():
                if pinfo["data"]:
                    out[f"webdav://{path}"] = pinfo["data"]
        return out

    def set_quota_limit(self, limit_bytes: int) -> None:
        with self.lock:
            self.quota_limit = limit_bytes

    def set_quota_used(self, used_bytes: int) -> None:
        with self.lock:
            self.quota_used = used_bytes

    def set_auth_expired(self, expired: bool) -> None:
        with self.lock:
            self.simulate_auth_expired = expired

    def set_quota_exceeded(self, exceeded: bool) -> None:
        with self.lock:
            self.simulate_quota_exceeded = exceeded

    def clear_storage(self) -> None:
        with self.lock:
            self.gdrive_files.clear()
            self.webdav_files.clear()
            self.quota_used = 0

    def __enter__(self) -> MockCloudRelayServer:
        self.start()
        return self

    def __exit__(self, exc_type: Any, exc_val: Any, exc_tb: Any) -> None:
        self.stop()
