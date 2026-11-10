"""
@file  mock_api.py
@brief Mock REST API server simulating engine responses for live preview


Navi Port 9001 Callback Mock API Handler.

Provides zero-dependency emulation of Navi's 32-bit daemon REST endpoints:
  - /cloud/* (Cloud save state, provider auth, app sync/backups, file explorer)
  - /config and /config/full (Get and set live configuration)
  - /check (LUA / Online-Fix / AutoCrack existence check)
  - /get-depots, /start-download, /cancel-download (Depot selection)
  - /api-keys, /set-api-keys (Provider API credentials)
  - /install-fix, /remove-fix, /install-crack, /remove-crack
  - /lua_apps, /list, /manual-install
  - /restart, /log, /clear-cache
"""

from __future__ import annotations

import json
import re
import threading
import time
from typing import Any, Dict, List, Optional, Tuple
from urllib.parse import parse_qs, urlparse


class NaviMockApi:
    """Thread-safe mock handler for Navi port 9001 endpoints."""

    def __init__(self) -> None:
        self._lock = threading.Lock()
        self.request_log: List[Dict[str, Any]] = []
        self._max_logs = 100

        # State initialization
        self.cloud_enabled: bool = True
        self.cloud_provider: str = "local-only"
        self.cloud_authenticated: bool = True
        self.cloud_user: str = "deck"
        self.authenticated_providers = {"local-only", "gdrive", "onedrive", "r2", "s3"}

        self.api_keys: Dict[str, str] = {
            "sls-hubcap-key": "demo_hubcap_preview_key",
            "sls-ryuu-key": "demo_ryuu_preview_key",
            "sls-dpbx-key": "",
        }

        self.app_fix_installed: Dict[int, bool] = {1443430: False}
        self.app_crack_installed: Dict[int, bool] = {1443430: False}
        self.app_cloud_redirected: Dict[int, bool] = {1443430: True, 107410: True}
        self.app_backups: Dict[int, List[str]] = {
            1443430: ["/storage/1443430/backups/backup_1443430_auto_1.tar.gz"],
            107410: ["/storage/107410/backups/backup_107410_auto_1.tar.gz"],
        }
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
            "RyuuKey": "demo_ryuu_preview_key",
            "DepotBoxKey": "",
            "HubcapKey": "demo_hubcap_preview_key",
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
                "AutoBackupBeforeReset": True,
                "BackupRetentionCount": 5,
                "ManagedApps": [1443430],
            },
        }

    def reset_state(self) -> None:
        """Reset state back to initial values."""
        with self._lock:
            self.__init__()

    def log_request(self, req_info: Dict[str, Any]) -> None:
        with self._lock:
            self.request_log.append(req_info)
            if len(self.request_log) > self._max_logs:
                self.request_log.pop(0)

    def get_logs(self, limit: int = 50) -> List[Dict[str, Any]]:
        with self._lock:
            return list(self.request_log[-limit:])

    def clear_logs(self) -> None:
        with self._lock:
            self.request_log.clear()

    def dispatch(
        self,
        method: str,
        path: str,
        headers: Optional[Dict[str, str]] = None,
        body: bytes = b"",
    ) -> Tuple[int, bytes, str]:
        """
        Dispatch request to corresponding handler.
        Returns (status_code, body_bytes, content_type).
        """
        headers = headers or {}
        if method.upper() == "OPTIONS":
            return 200, b"", "text/plain"

        parsed = urlparse(path)
        clean_path = parsed.path
        query = parse_qs(parsed.query)

        t_start = time.time()
        status_code, body_bytes, content_type = self._route(method.upper(), clean_path, query, body)
        duration_ms = round((time.time() - t_start) * 1000, 2)

        self.log_request({
            "timestamp": time.time(),
            "time_str": time.strftime("%H:%M:%S"),
            "method": method.upper(),
            "path": path,
            "clean_path": clean_path,
            "status": status_code,
            "duration_ms": duration_ms,
            "body_snippet": body[:200].decode("utf-8", errors="replace") if body else "",
        })

        return status_code, body_bytes, content_type

    def _route(
        self,
        method: str,
        clean_path: str,
        query: Dict[str, List[str]],
        body: bytes,
    ) -> Tuple[int, bytes, str]:
        def json_resp(data: Any, status: int = 200) -> Tuple[int, bytes, str]:
            return status, json.dumps(data, indent=2).encode("utf-8"), "application/json"

        # --- Cloud Save Routes ---
        if clean_path == "/cloud/status":
            return json_resp({
                "enabled": self.cloud_enabled,
                "provider": self.cloud_provider,
                "authenticated": self.cloud_authenticated,
                "user": self.cloud_user,
            })

        if clean_path == "/cloud/toggle":
            with self._lock:
                self.cloud_enabled = not self.cloud_enabled
            return json_resp({"success": True, "enabled": self.cloud_enabled})

        if clean_path == "/cloud/provider/status":
            prov = query.get("provider", [self.cloud_provider])[0]
            if prov == "local-only":
                is_auth = True
            else:
                is_auth = self.cloud_authenticated and (prov in self.authenticated_providers)
            return json_resp({
                "provider": prov,
                "authenticated": is_auth,
                "email": f"{self.cloud_user}@preview.local",
                "accountEmail": f"{self.cloud_user}@preview.local" if is_auth else "",
                "status": "connected" if is_auth else "unauthenticated",
            })

        if clean_path == "/cloud/provider/signin":
            try:
                data = json.loads(body.decode("utf-8")) if body else {}
                with self._lock:
                    if data.get("provider"):
                        self.cloud_provider = data["provider"]
                    self.cloud_authenticated = True
            except Exception:
                pass
            return json_resp({
                "success": True,
                "authenticated": True,
                "authUrl": "https://accounts.google.com/o/oauth2/v2/auth?client_id=navi_preview",
            })

        if clean_path == "/cloud/provider/signout":
            with self._lock:
                self.cloud_authenticated = False
            return json_resp({"success": True, "authenticated": False})

        if clean_path == "/cloud/pending-ops":
            return json_resp({"pending": []})

        if clean_path == "/cloud/apps":
            return json_resp({
                "success": True,
                "apps": [
                    {"appId": 1443430, "gameTitle": "Rogue", "fileCount": 5, "totalSizeBytes": 29285888, "redirected": True},
                    {"appId": 107410, "gameTitle": "Arma 3", "fileCount": 4, "totalSizeBytes": 8221844, "redirected": True},
                    {"appId": 730, "gameTitle": "Counter-Strike 2", "fileCount": 3, "totalSizeBytes": 1542000, "redirected": False},
                ],
            })

        m_app_files = re.match(r"^/cloud/apps/(\d+)/files(/download)?$", clean_path)
        if m_app_files:
            appid = int(m_app_files.group(1))
            is_download = bool(m_app_files.group(2))
            if is_download:
                return 200, b"MOCK_CLOUD_FILE_BINARY_CONTENT", "application/octet-stream"
            return json_resp({
                "success": True,
                "appId": appid,
                "gameTitle": "Rogue" if appid == 1443430 else f"App {appid}",
                "files": [
                    {"folder": "remote", "fileName": "savedata_slot0.bin", "sizeBytes": 3984580, "timestamp": 1730582400},
                    {"folder": "remote", "fileName": "savedata_slot1.bin", "sizeBytes": 4218880, "timestamp": 1730578800},
                    {"folder": f"userdata/{appid}", "fileName": "remotecache.vdf", "sizeBytes": 12288, "timestamp": 1730582400},
                    {"folder": f"userdata/{appid}", "fileName": "profile.cfg", "sizeBytes": 4096, "timestamp": 1730500000},
                    {"folder": "stplug-in", "fileName": f"{appid}.lua", "sizeBytes": 18432, "timestamp": 1730582000},
                    {"folder": "depotcache", "fileName": f"{appid + 1}_892183912.manifest", "sizeBytes": 25270680, "timestamp": 1730581000},
                ],
            })

        m_app_status = re.match(r"^/cloud/apps/(\d+)/status$", clean_path)
        if m_app_status:
            appid = int(m_app_status.group(1))
            redirected = self.app_cloud_redirected.get(appid, True)
            backups = self.app_backups.get(
                appid,
                [f"/storage/{appid}/backups/backup_{appid}_auto_1.tar.gz"],
            )
            return json_resp({
                "appid": appid,
                "enabled": redirected,
                "redirected": redirected,
                "backups": backups,
            })

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
            with self._lock:
                self.app_cloud_redirected[appid] = target_redirect
            return json_resp({"success": True, "enabled": target_redirect, "redirected": target_redirect})

        m_app_backup = re.match(r"^/cloud/apps/(\d+)/backup$", clean_path)
        if m_app_backup:
            appid = int(m_app_backup.group(1))
            b_time = int(time.time())
            b_file = f"/storage/{appid}/backups/backup_{appid}_{b_time}.tar.gz"
            with self._lock:
                if appid not in self.app_backups:
                    self.app_backups[appid] = [f"/storage/{appid}/backups/backup_{appid}_auto_1.tar.gz"]
                self.app_backups[appid].append(b_file)
            return json_resp({"success": True, "backupId": f"backup_{b_time}", "backupFile": b_file})

        m_app_restore = re.match(r"^/cloud/apps/(\d+)/restore$", clean_path)
        if m_app_restore:
            return json_resp({"success": True, "message": "Save data restored successfully"}, status=202)

        m_app_reset = re.match(r"^/cloud/apps/(\d+)/reset$", clean_path)
        if m_app_reset:
            return json_resp({"success": True, "message": "App save reset enqueued"})

        m_app_sync = re.match(r"^/cloud/apps/(\d+)/sync$", clean_path)
        if m_app_sync:
            return json_resp({"success": True, "message": "App synced with cloud"})

        # --- Configuration Routes ---
        if clean_path in ["/config", "/config/full"]:
            if method == "POST":
                try:
                    data = json.loads(body.decode("utf-8")) if body else {}
                except Exception as e:
                    return json_resp({"success": False, "message": f"Malformed JSON: {e}"}, status=400)

                # Clamping BackupRetentionCount [1, 100]
                if "CloudSaves" in data and isinstance(data["CloudSaves"], dict):
                    cs = data["CloudSaves"]
                    if "BackupRetentionCount" in cs:
                        try:
                            ret = int(cs["BackupRetentionCount"])
                            cs["BackupRetentionCount"] = max(1, min(100, ret))
                        except (ValueError, TypeError):
                            cs["BackupRetentionCount"] = 5

                with self._lock:
                    for k, v in data.items():
                        if isinstance(v, dict) and isinstance(self.config_data.get(k), dict):
                            self.config_data[k].update(v)
                        else:
                            self.config_data[k] = v

                return json_resp({"success": True, "message": "Configuration saved and reloaded"})
            else:
                return json_resp(self.config_data)

        # --- Game Fixes and Crack Routes ---
        if clean_path in ["/install-fix", "/remove-fix"]:
            aid = int(query.get("id", ["1443430"])[0]) if query.get("id", ["1443430"])[0].isdigit() else 1443430
            val = (clean_path == "/install-fix")
            with self._lock:
                self.app_fix_installed[aid] = val
            return json_resp({"success": True, "onlineFixInstalled": val})

        if clean_path in ["/install-crack", "/remove-crack"]:
            aid = int(query.get("id", ["1443430"])[0]) if query.get("id", ["1443430"])[0].isdigit() else 1443430
            val = (clean_path == "/install-crack")
            with self._lock:
                self.app_crack_installed[aid] = val
            return json_resp({"success": True, "autoCrackInstalled": val})

        if clean_path == "/crack-status":
            aid = int(query.get("id", ["1443430"])[0]) if query.get("id", ["1443430"])[0].isdigit() else 1443430
            return json_resp({"success": True, "autoCrackInstalled": self.app_crack_installed.get(aid, False)})

        if clean_path == "/change-provider":
            return json_resp({"success": True, "message": "Provider switched successfully"})

        if clean_path == "/remove":
            aid = int(query.get("id", ["1443430"])[0]) if query.get("id", ["1443430"])[0].isdigit() else 1443430
            with self._lock:
                if aid in self.lua_app_ids:
                    self.lua_app_ids.remove(aid)
            return json_resp({"success": True, "message": f"App {aid} removed successfully"})

        if clean_path == "/lua_apps":
            return json_resp({"appids": self.lua_app_ids})

        if clean_path == "/list":
            return json_resp(self.lua_app_ids)

        # --- LUA Check / Metadata ---
        if clean_path == "/check":
            aid_str = query.get("id", ["1443430"])[0]
            aid = int(aid_str) if aid_str.isdigit() else 1443430
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

        # --- Depot Selection & Downloads ---
        if clean_path == "/get-depots":
            aid_str = query.get("id", ["1443430"])[0]
            aid = int(aid_str) if aid_str.isdigit() else 1443430
            return json_resp({
                "success": True,
                "appid": aid,
                "depots": [
                    {
                        "id": aid + 1,
                        "name": f"Main Game Content Depot ({aid + 1})",
                        "size": 1428576000,
                        "manifest": "7364819283748291",
                    },
                    {
                        "id": aid + 2,
                        "name": f"High Resolution Textures & Audio ({aid + 2})",
                        "size": 524288000,
                        "manifest": "8273648192837482",
                    },
                    {
                        "id": aid + 3,
                        "name": f"Deluxe DLC Soundtrack & Artbook ({aid + 3})",
                        "size": 104857600,
                        "manifest": "9182736451293847",
                    },
                ],
            })

        if clean_path in ["/start-download", "/cancel-download", "/download"]:
            return json_resp({"success": True, "action": clean_path.strip("/")})

        # --- API Keys ---
        if clean_path == "/api-keys":
            return json_resp(self.api_keys)

        if clean_path == "/set-api-keys":
            with self._lock:
                if method == "POST" and body:
                    try:
                        data = json.loads(body.decode("utf-8"))
                        self.api_keys.update(data)
                    except Exception:
                        pass
                else:
                    for k in ["ryuu", "dpbx", "hubcap"]:
                        val = query.get(k, [""])[0]
                        if val:
                            self.api_keys[f"sls-{k}-key"] = val
            return json_resp({"success": True})

        # --- Cache & Diagnostics ---
        if clean_path in ["/clear-cache", "/clear-all-cache"]:
            return json_resp({"success": True, "message": "Cache invalidated"})

        if clean_path == "/manual-install":
            return json_resp({"success": True, "message": "Files installed successfully!"})

        if clean_path == "/restart":
            return json_resp({"success": True, "restarted": True, "message": "Steam client restart triggered"})

        if clean_path == "/log":
            return json_resp({"success": True})

        # Default catch-all
        return json_resp({"success": True, "path": clean_path, "method": method})

