"""
Ephemeral-port HTTP server stub emulating Navi port 9001 callback routes.

Provides:
  - Ephemeral port binding (port=0) to prevent test collisions.
  - Complete emulation of Navi routes (/cloud/*, /check, /get-depots, /api-keys, etc.).
  - Request recording (inspect incoming methods, paths, headers, bodies).
  - Fault injection (set_fault to force error status codes and payloads).
  - Deterministic request barriers (hold_endpoint returning RequestBarrier with .wait_until_reached() and .release()).
"""

from __future__ import annotations

import json
import re
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any, Callable, Dict, List, Optional, Tuple
from urllib.parse import parse_qs, urlparse


class RequestBarrier:
    """Deterministic barrier to pause an incoming HTTP request until explicitly released."""

    def __init__(self, path_pattern: str):
        self.path_pattern = path_pattern
        self.reached_event = threading.Event()
        self.release_event = threading.Event()
        self.reached_count = 0
        self.last_request: Optional[Dict[str, Any]] = None

    def mark_reached(self, request_info: Dict[str, Any]) -> None:
        self.reached_count += 1
        self.last_request = request_info
        self.reached_event.set()

    def wait_until_reached(self, timeout: float = 5.0) -> bool:
        if not self.reached_event.wait(timeout=timeout):
            raise TimeoutError(f"RequestBarrier for pattern '{self.path_pattern}' was not reached within {timeout}s")
        return True

    def release(self) -> None:
        self.release_event.set()

    def wait_for_release(self, timeout: float = 10.0) -> bool:
        return self.release_event.wait(timeout=timeout)


class CallbackServerStub:
    """
    HTTP stub server emulating Navi's port 9001 daemon API.
    """

    def __init__(self, host: str = "127.0.0.1", port: int = 0):
        self.host = host
        self.port = port
        self.server: Optional[ThreadingHTTPServer] = None
        self.server_thread: Optional[threading.Thread] = None

        self._lock = threading.Lock()
        self.requests: List[Dict[str, Any]] = []
        self.faults: Dict[str, Dict[str, Any]] = {}
        self.barriers: Dict[str, RequestBarrier] = {}
        self.custom_responses: Dict[str, Tuple[int, bytes, str]] = {}

        # Default state
        self.cloud_enabled = True
        self.cloud_provider = "local-only"
        self.cloud_authenticated = True
        self.cloud_user = "deck"
        self.authenticated_providers = {"local-only", "gdrive", "onedrive", "r2", "s3"}
        self.api_keys = {
            "sls-hubcap-key": "stub_hubcap_key",
            "sls-ryuu-key": "",
            "sls-dpbx-key": "",
        }
        self.app_fix_installed: Dict[int, bool] = {}
        self.app_crack_installed: Dict[int, bool] = {}
        self.app_cloud_redirected: Dict[int, bool] = {}
        self.app_backups: Dict[int, List[str]] = {}
        self.lua_app_ids: List[int] = [440, 730, 1443430]
        self.config_data: Dict[str, Any] = {
            "DisableFamilyShareLock": False,
            "UseWhitelist": False,
            "SafeMode": True,
            "WarnHashMissmatch": True,
            "NotifyInit": True,
            "API": False,
            "DisableCloud": False,
            "DisableUpdates": False,
            "DumpClientInterfaces": False,
            "ExtendedLogging": False,
            "MaxSchemaTries": 10,
            "FakeWalletBalance": 0,
            "LogLevels": 255,
            "FakeName": "Gordon Freeman",
            "FakeEmail": "gordon@blackmesa.gov",
            "RyuuKey": "stub_ryuu_key",
            "DepotBoxKey": "stub_dpbx_key",
            "HubcapKey": "stub_hubcap_key",
            "IdleStatus": {"AppId": 0, "Title": ""},
            "UnownedStatus": {"AppId": 0, "Title": ""},
            "AppIds": [440, 730],
            "AdditionalApps": [1443430],
            "FakeOffline": [],
            "DepotBlacklist": [],
            "FakeAppIds": {"480": "440"},
            "ManifestIds": {},
            "AppTokens": {},
            "CDKeys": {},
            "GameTitles": {},
            "SubscriptionTimestamps": {},
            "SteamIdOverride": {},
            "DlcDataYaml": "",
            "DenuvoGamesYaml": "",
            "CloudSaves": {
                "Enabled": True,
                "Provider": "local-only",
                "NotificationsEnabled": True,
                "AutoBackupBeforeReset": True,
                "BackupRetentionCount": 5,
                "StatsSync": {
                    "Enabled": False,
                    "SyncAchievements": True,
                    "SyncPlaytime": True,
                },
                "ManagedApps": [1443430],
            },
        }

    @property
    def base_url(self) -> str:
        if not self.server:
            raise RuntimeError("Server is not running")
        return f"http://{self.host}:{self.server.server_port}"

    @property
    def server_port(self) -> int:
        if not self.server:
            raise RuntimeError("Server is not running")
        return self.server.server_port

    def start(self) -> "CallbackServerStub":
        parent = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, format: str, *args: Any) -> None:
                # Suppress normal stderr logging to keep pytest output clean
                pass

            def do_OPTIONS(self) -> None:
                self.send_response(200)
                self._send_cors_headers()
                self.end_headers()

            def do_GET(self) -> None:
                self._handle("GET")

            def do_POST(self) -> None:
                self._handle("POST")

            def do_PUT(self) -> None:
                self._handle("PUT")

            def do_DELETE(self) -> None:
                self._handle("DELETE")

            def _send_cors_headers(self) -> None:
                self.send_header("Access-Control-Allow-Origin", "*")
                self.send_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS")
                self.send_header("Access-Control-Allow-Headers", "*")

            def _handle(self, method: str) -> None:
                content_len = int(self.headers.get("Content-Length", 0))
                body = self.rfile.read(content_len) if content_len > 0 else b""

                status_code, resp_bytes, content_type = parent.dispatch(
                    method=method,
                    path=self.path,
                    headers=dict(self.headers),
                    body=body,
                )

                self.send_response(status_code)
                self.send_header("Content-Type", content_type)
                self.send_header("Content-Length", str(len(resp_bytes)))
                self._send_cors_headers()
                self.end_headers()
                self.wfile.write(resp_bytes)

        self.server = ThreadingHTTPServer((self.host, self.port), Handler)
        self.server_thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.server_thread.start()
        return self

    def stop(self) -> None:
        with self._lock:
            for barrier in self.barriers.values():
                barrier.release()
        if self.server:
            self.server.shutdown()
            self.server.server_close()
            self.server = None
        if self.server_thread:
            self.server_thread.join(timeout=2.0)
            self.server_thread = None

    def __enter__(self) -> "CallbackServerStub":
        return self.start()

    def __exit__(self, exc_type: Any, exc_val: Any, exc_tb: Any) -> None:
        self.stop()

    # --- Barrier, Fault Injection, and Custom Response API ---

    def hold_endpoint(self, path_pattern: str) -> RequestBarrier:
        """Create and register a barrier that pauses incoming requests matching path_pattern."""
        barrier = RequestBarrier(path_pattern)
        with self._lock:
            self.barriers[path_pattern] = barrier
        return barrier

    def set_fault(self, path: str, status_code: int = 500, body: str = '{"error":"injected fault"}', count: int = 1) -> None:
        """Configure a fault (error code/body) for matching path."""
        with self._lock:
            self.faults[path] = {
                "status_code": status_code,
                "body": body.encode("utf-8") if isinstance(body, str) else body,
                "count": count,
            }

    def set_response(self, path: str, body: Any, status_code: int = 200, content_type: str = "application/json") -> None:
        """Override response for specific path."""
        if not isinstance(body, bytes):
            if isinstance(body, (dict, list)):
                body_bytes = json.dumps(body).encode("utf-8")
            else:
                body_bytes = str(body).encode("utf-8")
        else:
            body_bytes = body
        with self._lock:
            self.custom_responses[path] = (status_code, body_bytes, content_type)

    def get_requests(self, path_substring: Optional[str] = None, method: Optional[str] = None) -> List[Dict[str, Any]]:
        with self._lock:
            reqs = list(self.requests)
            if path_substring:
                reqs = [r for r in reqs if path_substring in r["path"]]
            if method:
                reqs = [r for r in reqs if r["method"].upper() == method.upper()]
            return reqs

    def last_request(self, path_substring: Optional[str] = None, method: Optional[str] = None) -> Optional[Dict[str, Any]]:
        reqs = self.get_requests(path_substring, method=method)
        return reqs[-1] if reqs else None

    def clear_requests(self) -> None:
        with self._lock:
            self.requests.clear()

    # --- Request Dispatching Logic ---

    def dispatch(
        self,
        method: str,
        path: str,
        headers: Dict[str, str],
        body: bytes,
    ) -> Tuple[int, bytes, str]:
        """
        Process request, apply barriers/faults, and route to emulated Navi handlers.
        Returns (status_code, body_bytes, content_type).
        """
        if method == "OPTIONS":
            return 200, b"", "text/plain"

        parsed = urlparse(path)
        clean_path = parsed.path
        query = parse_qs(parsed.query)

        req_info = {
            "method": method,
            "path": path,
            "clean_path": clean_path,
            "query": query,
            "headers": headers,
            "body": body,
            "time": time.time(),
        }

        with self._lock:
            self.requests.append(req_info)

            # Check barriers (for non-OPTIONS requests)
            active_barrier: Optional[RequestBarrier] = None
            for pattern, barrier in list(self.barriers.items()):
                if pattern in path or re.search(pattern, path):
                    active_barrier = barrier
                    break

            # Check faults
            fault = self.faults.get(clean_path) or self.faults.get(path)
            if fault and fault["count"] > 0:
                fault["count"] -= 1
                return fault["status_code"], fault["body"], "application/json"

            # Check custom responses
            if path in self.custom_responses:
                return self.custom_responses[path]
            if clean_path in self.custom_responses:
                return self.custom_responses[clean_path]

        # Handle barrier outside the lock to prevent server deadlocks
        if active_barrier:
            active_barrier.mark_reached(req_info)
            # Wait for test to explicitly release
            active_barrier.wait_for_release(timeout=10.0)

        # Route to default Navi endpoint handlers
        return self._route_default(method, clean_path, query, body)

    def _route_default(
        self, method: str, clean_path: str, query: Dict[str, List[str]], body: bytes
    ) -> Tuple[int, bytes, str]:
        def json_resp(data: Any, status: int = 200) -> Tuple[int, bytes, str]:
            return status, json.dumps(data).encode("utf-8"), "application/json"

        # /cloud/status
        if clean_path == "/cloud/status":
            return json_resp({
                "enabled": self.cloud_enabled,
                "provider": self.cloud_provider,
                "authenticated": self.cloud_authenticated,
                "user": self.cloud_user,
            })

        # /cloud/toggle
        if clean_path == "/cloud/toggle":
            self.cloud_enabled = not self.cloud_enabled
            return json_resp({"success": True, "enabled": self.cloud_enabled})

        # /cloud/provider/status
        if clean_path == "/cloud/provider/status":
            prov = query.get("provider", [self.cloud_provider])[0]
            if prov == "local-only":
                is_auth = True
            else:
                is_auth = self.cloud_authenticated and (prov in self.authenticated_providers)
            return json_resp({
                "provider": prov,
                "authenticated": is_auth,
                "email": "deck@local",
                "accountEmail": "deck@local" if is_auth else "",
                "status": "connected" if is_auth else "unauthenticated",
            })

        # /cloud/provider/signin
        if clean_path == "/cloud/provider/signin":
            try:
                data = json.loads(body.decode("utf-8")) if body else {}
                if data.get("provider"):
                    self.cloud_provider = data["provider"]
            except Exception:
                pass
            self.cloud_authenticated = True
            return json_resp({
                "success": True,
                "authenticated": True,
                "authUrl": "https://accounts.google.com/o/oauth2/v2/auth?client_id=navi",
            })

        # /cloud/provider/signout
        if clean_path == "/cloud/provider/signout":
            self.cloud_authenticated = False
            return json_resp({"success": True, "authenticated": False})

        # /cloud/pending-ops
        if clean_path == "/cloud/pending-ops":
            return json_resp({"pending": []})

        # /cloud/provider/credentials
        if clean_path == "/cloud/provider/credentials":
            prov = query.get("provider", ["r2"])[0]
            return json_resp({
                "success": True,
                "authenticated": True,
                "provider": prov,
                "configured": True,
                "accountId": "stub_r2_acc",
                "accessKeyId": "AKIASTUBKEY",
                "secretAccessKey": "********",
                "hasSecretKey": True,
                "bucket": "stub-bucket",
                "region": "auto" if prov == "r2" else "us-east-1",
                "endpoint": "https://stub_r2_acc.r2.cloudflarestorage.com",
                "keyPrefix": "saves/",
            })

        # /cloud/migrate/scan
        if clean_path == "/cloud/migrate/scan":
            prov = query.get("provider", ["local-only"])[0]
            return json_resp({
                "success": True,
                "provider": prov,
                "totalApps": 2,
                "appCount": 2,
                "totalFiles": 6,
                "fileCount": 6,
                "totalBytes": 8232084,
                "totalSizeBytes": 8232084,
                "apps": [
                    {"appId": 107410, "gameTitle": "Arma 3", "fileCount": 4, "totalBytes": 8221844, "totalSizeBytes": 8221844},
                    {"appId": 1443430, "gameTitle": "Test Game", "fileCount": 2, "totalBytes": 10240, "totalSizeBytes": 10240},
                ],
            })

        # /cloud/migrate
        if clean_path == "/cloud/migrate":
            return json_resp({
                "success": True,
                "migrated": 6,
                "skipped": 0,
                "failed": 0,
                "total": 6,
                "totalBytes": 8232084,
            })

        # /cloud/orphans
        if clean_path == "/cloud/orphans":
            prov = query.get("provider", ["local-only"])[0]
            return json_resp({
                "success": True,
                "provider": prov,
                "orphans": [
                    {
                        "appId": 480,
                        "gameTitle": "Spacewar",
                        "fileCount": 2,
                        "totalSizeBytes": 4096,
                        "inRemoteCloud": False,
                        "inLocalMirror": True,
                        "inLocalStorage": True,
                        "inGoldberg": False,
                        "inGoldbergPrefix": False,
                    }
                ],
            })

        # /cloud/orphans/clean
        if clean_path == "/cloud/orphans/clean":
            return json_resp({
                "success": True,
                "appId": 480,
                "deleted": 2,
                "blobsDeleted": 2,
                "kept": 0,
                "failed": 0,
                "localDeleted": False,
                "localFilesDeleted": False,
            })

        # /cloud/backups
        if clean_path == "/cloud/backups":
            return json_resp({
                "success": True,
                "backups": [
                    {
                        "appId": 107410,
                        "gameTitle": "Arma 3",
                        "fileName": "backup_107410_auto_1.tar.gz",
                        "fullPath": "/storage/107410/backups/backup_107410_auto_1.tar.gz",
                        "sizeBytes": 524288,
                        "timestamp": 1730582400,
                    }
                ],
            })

        # /cloud/backups/delete
        if clean_path == "/cloud/backups/delete":
            return json_resp({"success": True, "message": "Backup deleted"})

        # /config and /config/full
        if clean_path in ["/config", "/config/full"]:
            if method == "POST":
                try:
                    data = json.loads(body.decode("utf-8")) if body else {}
                except Exception as e:
                    return json_resp({"success": False, "message": f"Malformed JSON: {e}"}, status=400)

                # DlcDataYaml syntax check
                dlc_yaml = data.get("DlcDataYaml")
                if dlc_yaml and isinstance(dlc_yaml, str) and dlc_yaml.strip() not in ("{}", "null", ""):
                    import yaml
                    try:
                        yaml.safe_load(dlc_yaml)
                    except Exception as e:
                        return json_resp({"success": False, "message": f"Failed to parse DlcDataYaml: {e}"}, status=400)

                # DenuvoGamesYaml syntax check
                denuvo_yaml = data.get("DenuvoGamesYaml")
                if denuvo_yaml and isinstance(denuvo_yaml, str) and denuvo_yaml.strip() not in ("{}", "null", ""):
                    import yaml
                    try:
                        yaml.safe_load(denuvo_yaml)
                    except Exception as e:
                        return json_resp({"success": False, "message": f"Failed to parse DenuvoGamesYaml: {e}"}, status=400)

                # Clamping BackupRetentionCount [1, 100]
                if "CloudSaves" in data and isinstance(data["CloudSaves"], dict):
                    cs = data["CloudSaves"]
                    if "BackupRetentionCount" in cs:
                        try:
                            ret = int(cs["BackupRetentionCount"])
                            if ret < 1:
                                ret = 1
                            elif ret > 100:
                                ret = 100
                            cs["BackupRetentionCount"] = ret
                        except (ValueError, TypeError):
                            cs["BackupRetentionCount"] = 5

                # Update stub config data
                for k, v in data.items():
                    if isinstance(v, dict) and isinstance(self.config_data.get(k), dict):
                        self.config_data[k].update(v)
                    else:
                        self.config_data[k] = v

                return json_resp({"success": True, "message": "Configuration saved and reloaded"})
            else:
                return json_resp(self.config_data)

        # /install-fix and /remove-fix
        if clean_path in ["/install-fix", "/remove-fix"]:
            aid_str = query.get("id", ["1443430"])[0]
            try:
                aid = int(aid_str)
            except ValueError:
                aid = 1443430
            self.app_fix_installed[aid] = (clean_path == "/install-fix")
            return json_resp({"success": True, "onlineFixInstalled": self.app_fix_installed[aid]})

        # /install-crack and /remove-crack
        if clean_path in ["/install-crack", "/remove-crack"]:
            aid_str = query.get("id", ["1443430"])[0]
            try:
                aid = int(aid_str)
            except ValueError:
                aid = 1443430
            self.app_crack_installed[aid] = (clean_path == "/install-crack")
            return json_resp({"success": True, "autoCrackInstalled": self.app_crack_installed[aid]})

        # /remove (Remove Lua and Game files)
        if clean_path == "/remove":
            aid_str = query.get("id", ["1443430"])[0]
            try:
                aid = int(aid_str)
            except ValueError:
                aid = 1443430
            if aid in self.lua_app_ids:
                self.lua_app_ids.remove(aid)
            return json_resp({"success": True, "message": f"App {aid} removed successfully"})

        # /lua_apps and /list
        if clean_path == "/lua_apps":
            return json_resp({"appids": self.lua_app_ids})
        if clean_path == "/list":
            return json_resp(self.lua_app_ids)

        # /cloud/apps/<id>/files
        m_app_files = re.match(r"^/cloud/apps/(\d+)/files$", clean_path)
        if m_app_files:
            appid = int(m_app_files.group(1))
            return json_resp({
                "success": True,
                "appId": appid,
                "gameTitle": "Arma 3" if appid == 107410 else f"App {appid}",
                "files": [
                    {"folder": "remote", "fileName": "savedata_slot0.bin", "sizeBytes": 3984580, "timestamp": 1730582400},
                    {"folder": "remote", "fileName": "savedata_slot1.bin", "sizeBytes": 4218880, "timestamp": 1730578800},
                    {"folder": "userdata/107410", "fileName": "remotecache.vdf", "sizeBytes": 12288, "timestamp": 1730582400},
                    {"folder": "userdata/107410", "fileName": "profile.cfg", "sizeBytes": 4096, "timestamp": 1730500000},
                    {"folder": "stplug-in", "fileName": "107410.lua", "sizeBytes": 18432, "timestamp": 1730582000},
                    {"folder": "depotcache", "fileName": "107411_892183912.manifest", "sizeBytes": 25270680, "timestamp": 1730581000},
                    {"folder": "steamapps", "fileName": "appmanifest_107410.acf", "sizeBytes": 4096, "timestamp": 1730580000},
                ],
            })

        # /cloud/apps
        if clean_path == "/cloud/apps":
            return json_resp({
                "success": True,
                "apps": [
                    {"appId": 107410, "gameTitle": "Arma 3", "fileCount": 4, "totalSizeBytes": 8221844, "redirected": True},
                    {"appId": 1443430, "gameTitle": "Test Game", "fileCount": 2, "totalSizeBytes": 10240, "redirected": True},
                ],
            })

        # /cloud/apps/<id>/status
        m_app_status = re.match(r"^/cloud/apps/(\d+)/status$", clean_path)
        if m_app_status:
            appid = int(m_app_status.group(1))
            app_cloud_state = self.app_cloud_redirected.get(appid, True)
            backups = self.app_backups.get(
                appid,
                [f"/storage/{appid}/backups/backup_{appid}_auto_1.tar.gz"],
            )
            return json_resp({
                "appid": appid,
                "enabled": app_cloud_state,
                "redirected": app_cloud_state,
                "backups": backups,
            })

        # /cloud/apps/<id>/toggle
        m_app_toggle = re.match(r"^/cloud/apps/(\d+)/toggle$", clean_path)
        if m_app_toggle:
            appid = int(m_app_toggle.group(1))
            target_redirect = not self.app_cloud_redirected.get(appid, True)
            if body:
                try:
                    b_data = json.loads(body.decode("utf-8"))
                    if "redirect" in b_data:
                        target_redirect = bool(b_data["redirect"])
                except Exception:
                    pass
            self.app_cloud_redirected[appid] = target_redirect
            return json_resp({"success": True, "enabled": target_redirect, "redirected": target_redirect})

        # /cloud/apps/<id>/backup
        m_app_backup = re.match(r"^/cloud/apps/(\d+)/backup$", clean_path)
        if m_app_backup:
            appid = int(m_app_backup.group(1))
            b_time = int(time.time())
            b_file = f"/storage/{appid}/backups/backup_{appid}_{b_time}.tar.gz"
            if appid not in self.app_backups:
                self.app_backups[appid] = [f"/storage/{appid}/backups/backup_{appid}_auto_1.tar.gz"]
            self.app_backups[appid].append(b_file)
            return json_resp({"success": True, "backupId": f"backup_{b_time}", "backupFile": b_file})

        # /cloud/apps/<id>/restore
        m_app_restore = re.match(r"^/cloud/apps/(\d+)/restore$", clean_path)
        if m_app_restore:
            return json_resp({"success": True, "message": "Save data restored successfully"}, status=202)

        # /cloud/apps/<id>/reset
        m_app_reset = re.match(r"^/cloud/apps/(\d+)/reset$", clean_path)
        if m_app_reset:
            return json_resp({"success": True, "message": "App save reset enqueued"})

        # /check (query: id=<id>)
        if clean_path == "/check":
            aid_str = query.get("id", ["1443430"])[0]
            try:
                aid = int(aid_str)
            except ValueError:
                aid = 1443430
            fix_inst = self.app_fix_installed.get(aid, False)
            crack_inst = self.app_crack_installed.get(aid, False)
            return json_resp({
                "exists": True,
                "appid": aid,
                "provider": "HubcapDB",
                "installed": True,
                "onlineFixInstalled": fix_inst,
                "autoCrackInstalled": crack_inst,
            })

        # /get-depots
        if clean_path == "/get-depots":
            return json_resp({
                "success": True,
                "depots": [
                    {
                        "id": 1443431,
                        "name": "Rogue Content Depot",
                        "size": 1048576,
                        "manifest": "7364819283748291",
                    }
                ]
            })

        # /api-keys
        if clean_path == "/api-keys":
            return json_resp(self.api_keys)

        # /set-api-keys
        if clean_path == "/set-api-keys":
            try:
                data = json.loads(body.decode("utf-8")) if body else {}
                self.api_keys.update(data)
            except Exception:
                pass
            return json_resp({"success": True})

        # /manual-install
        if clean_path == "/manual-install":
            return json_resp({"success": True, "message": "Files installed successfully!"})

        # /start-download, /cancel-download, /download
        if clean_path in ["/start-download", "/cancel-download", "/download"]:
            return json_resp({"success": True})

        # /log
        if clean_path == "/log":
            return json_resp({"success": True})

        # Default 200 JSON fallback for any unhandled path
        return json_resp({"success": True, "path": clean_path})
