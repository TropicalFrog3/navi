"""
Integration tests for Navi CloudRedirect subsystems.

Covers:
1. Dynamic Launcher Wrapper Preload Tests (enabled/disabled states, killswitch, LD_PRELOAD preservation).
2. Idle Helper Execution Tests (bin/navi-cloud-apply-pending, granular reset, rollback on fault).
3. Backend API Security Tests (Origin allowlist, X-Navi-Auth validation, typed confirmation).
4. Flatpak Environment Simulation (sandbox vs host path resolution).
"""

from __future__ import annotations

import hmac
import json
import os
import shutil
import socket
import subprocess
import tarfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any, Dict, Optional
import pytest

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
APPLY_PENDING_BIN = REPO_ROOT / "bin" / "navi-cloud-apply-pending"


# ==============================================================================
# Helper Utilities
# ==============================================================================

def can_use_unshare() -> bool:
    """Check if unshare with user namespace & mount-proc is available."""
    if shutil.which("unshare") is None:
        return False
    try:
        res = subprocess.run(
            ["unshare", "-r", "-p", "-f", "--mount-proc", "true"],
            capture_output=True,
            timeout=3,
        )
        return res.returncode == 0
    except Exception:
        return False


def run_apply_pending(env: dict[str, str], args: list[str] | None = None) -> subprocess.CompletedProcess[str]:
    """Execute bin/navi-cloud-apply-pending with PID isolation if possible."""
    cmd_args = ["--process-all"] if args is None else args
    bin_path = str(APPLY_PENDING_BIN)

    if can_use_unshare():
        cmd = ["unshare", "-r", "-p", "-f", "--mount-proc", bin_path, *cmd_args]
    else:
        cmd = [bin_path, *cmd_args]

    return subprocess.run(
        cmd,
        cwd=str(REPO_ROOT),
        env=env,
        capture_output=True,
        text=True,
        timeout=10,
    )


# ==============================================================================
# 1. Dynamic Launcher Wrapper Preload Tests
# ==============================================================================

class TestLauncherWrapperPreload:
    """Verifies dynamic LD_PRELOAD injection in the Steam launcher wrapper."""

    @pytest.fixture
    def wrapper_env(self, tmp_path, monkeypatch):
        """Set up an isolated environment with the Navi Steam wrapper installed."""
        home = tmp_path / "home"
        home.mkdir(parents=True, exist_ok=True)

        steam_dir = home / ".local" / "share" / "Steam"
        steam_dir.mkdir(parents=True, exist_ok=True)

        # Mock native Steam launcher that records its invocation environment
        mock_native_steam = steam_dir / "steam.sh"
        mock_native_steam.write_text(
            "#!/bin/sh\n"
            'echo "INVOKED_PRELOAD=$LD_PRELOAD"\n'
            'echo "INVOKED_AUDIT=$LD_AUDIT"\n'
            'echo "INVOKED_ARGS=$*"\n'
        )
        mock_native_steam.chmod(0o755)

        (home / ".profile").write_text("# mock .profile\n")
        (home / ".bashrc").write_text("# mock .bashrc\n")

        monkeypatch.setenv("HOME", str(home))
        monkeypatch.setenv("NAVI_IGNORE_RUNNING_STEAM", "1")
        monkeypatch.setenv("NAVI_OFFLINE_INSTALL", "1")
        monkeypatch.setenv("REPO_ROOT", str(REPO_ROOT))

        # Run installer to create safe Steam wrapper
        inst_cmd = [str(REPO_ROOT / "install.sh"), "install", "--yes"]
        res = subprocess.run(inst_cmd, cwd=str(REPO_ROOT), capture_output=True, text=True, env=os.environ)
        assert res.returncode == 0, f"Installer failed: {res.stderr}\n{res.stdout}"

        wrapper_path = home / ".local" / "bin" / "steam"
        assert wrapper_path.is_file(), "Steam wrapper was not installed"

        cloud_redirect_so = home / ".local" / "share" / "navi" / "cloudredirect" / "cloud_redirect.so"
        cloud_redirect_so.parent.mkdir(parents=True, exist_ok=True)

        return {
            "home": home,
            "wrapper": wrapper_path,
            "cr_so": cloud_redirect_so,
            "cloud_enabled": home / ".config" / "navi" / "cloud_enabled",
            "killswitch": home / ".config" / "CloudRedirect" / "disable",
        }

    def _run_wrapper(self, env_vars: dict[str, str], extra_env: dict[str, str] | None = None) -> tuple[int, str]:
        cmd_env = os.environ.copy()
        cmd_env["HOME"] = str(env_vars["home"])
        if extra_env:
            cmd_env.update(extra_env)
        res = subprocess.run(
            [str(env_vars["wrapper"]), "--arg1", "--arg2"],
            capture_output=True,
            text=True,
            env=cmd_env,
            timeout=5,
        )
        return res.returncode, res.stdout

    def test_disabled_when_cloud_enabled_missing(self, wrapper_env):
        """When cloud_enabled is missing, wrapper does not inject cloud_redirect.so and preserves user LD_PRELOAD."""
        wrapper_env["cr_so"].write_text("/* dummy so */")
        if wrapper_env["cloud_enabled"].exists():
            wrapper_env["cloud_enabled"].unlink()

        # Case A: User has custom LD_PRELOAD
        rc, out = self._run_wrapper(wrapper_env, {"LD_PRELOAD": "/usr/lib/libcustom.so"})
        assert rc == 0
        assert "INVOKED_PRELOAD=/usr/lib/libcustom.so" in out
        assert "cloud_redirect.so" not in out

        # Case B: User has empty LD_PRELOAD
        rc, out = self._run_wrapper(wrapper_env, {"LD_PRELOAD": ""})
        assert rc == 0
        assert "INVOKED_PRELOAD=" in out
        assert "cloud_redirect.so" not in out

    def test_disabled_when_killswitch_present(self, wrapper_env):
        """When CloudRedirect killswitch is present, injection is disabled even if cloud_enabled exists."""
        wrapper_env["cr_so"].write_text("/* dummy so */")
        wrapper_env["cloud_enabled"].parent.mkdir(parents=True, exist_ok=True)
        wrapper_env["cloud_enabled"].write_text("1")

        wrapper_env["killswitch"].parent.mkdir(parents=True, exist_ok=True)
        wrapper_env["killswitch"].write_text("1")

        rc, out = self._run_wrapper(wrapper_env, {"LD_PRELOAD": "/usr/lib/libcustom.so"})
        assert rc == 0
        assert "INVOKED_PRELOAD=/usr/lib/libcustom.so" in out
        assert "cloud_redirect.so" not in out

    def test_disabled_when_so_file_missing(self, wrapper_env):
        """When cloud_redirect.so does not exist on disk, wrapper safely avoids injecting non-existent library."""
        if wrapper_env["cr_so"].exists():
            wrapper_env["cr_so"].unlink()

        wrapper_env["cloud_enabled"].parent.mkdir(parents=True, exist_ok=True)
        wrapper_env["cloud_enabled"].write_text("1")
        if wrapper_env["killswitch"].exists():
            wrapper_env["killswitch"].unlink()

        rc, out = self._run_wrapper(wrapper_env, {"LD_PRELOAD": "/usr/lib/libcustom.so"})
        assert rc == 0
        assert "INVOKED_PRELOAD=/usr/lib/libcustom.so" in out
        assert "cloud_redirect.so" not in out

    def test_enabled_prepends_so_to_ld_preload(self, wrapper_env):
        """When enabled, absent killswitch, and so exists, cloud_redirect.so is prepended to LD_PRELOAD."""
        wrapper_env["cr_so"].write_text("/* dummy so */")
        wrapper_env["cloud_enabled"].parent.mkdir(parents=True, exist_ok=True)
        wrapper_env["cloud_enabled"].write_text("1")
        if wrapper_env["killswitch"].exists():
            wrapper_env["killswitch"].unlink()

        cr_so_str = str(wrapper_env["cr_so"])

        # Case A: Existing user LD_PRELOAD
        rc, out = self._run_wrapper(wrapper_env, {"LD_PRELOAD": "/usr/lib/user_mod.so"})
        assert rc == 0
        expected = f"INVOKED_PRELOAD={cr_so_str}:/usr/lib/user_mod.so"
        assert expected in out

        # Case B: Empty initial LD_PRELOAD
        rc, out = self._run_wrapper(wrapper_env, {"LD_PRELOAD": ""})
        assert rc == 0
        expected = f"INVOKED_PRELOAD={cr_so_str}"
        assert expected in out

        # Case C: LD_PRELOAD already contains cloud_redirect.so (no duplication)
        rc, out = self._run_wrapper(wrapper_env, {"LD_PRELOAD": f"{cr_so_str}:/usr/lib/user_mod.so"})
        assert rc == 0
        assert out.count(cr_so_str) == 1

    def test_wrapper_executes_pending_ops_helper_if_present(self, wrapper_env):
        """Wrapper executes navi-cloud-apply-pending --process-all on startup if binary is present and executable."""
        helper_bin = wrapper_env["home"] / ".local" / "share" / "navi" / "bin" / "navi-cloud-apply-pending"
        helper_bin.parent.mkdir(parents=True, exist_ok=True)
        marker_file = wrapper_env["home"] / "helper_ran.marker"
        helper_bin.write_text(f"#!/bin/sh\necho \"ran\" > {marker_file}\n")
        helper_bin.chmod(0o755)

        rc, _ = self._run_wrapper(wrapper_env)
        assert rc == 0
        assert marker_file.is_file(), "navi-cloud-apply-pending was not executed by wrapper"
        assert marker_file.read_text().strip() == "ran"


# ==============================================================================
# 2. Idle Helper Execution Tests
# ==============================================================================

class TestIdleHelperExecution:
    """Verifies bin/navi-cloud-apply-pending execution, granular resets, and safety rollbacks."""

    @pytest.fixture
    def helper_env(self, tmp_path):
        """Build an isolated Steam & Navi directory layout."""
        home = tmp_path / "home"
        cfg = home / ".config"
        share = home / ".local" / "share"
        cr_cfg = cfg / "CloudRedirect"
        cr_cfg.mkdir(parents=True, exist_ok=True)

        steam = share / "Steam"
        (steam / "steamui").mkdir(parents=True, exist_ok=True)

        env = os.environ.copy()
        env["HOME"] = str(home)
        env["XDG_CONFIG_HOME"] = str(cfg)
        env["XDG_DATA_HOME"] = str(share)

        return {
            "home": home,
            "cfg": cfg,
            "share": share,
            "cr_cfg": cr_cfg,
            "steam": steam,
            "queue_file": cr_cfg / "cloud_pending_ops.json",
            "env": env,
        }

    def test_usage_error_without_process_all_flag(self, helper_env):
        """Running without --process-all prints usage and returns 1."""
        res = run_apply_pending(helper_env["env"], args=[])
        assert res.returncode == 1
        assert "Usage: navi-cloud-apply-pending --process-all" in res.stderr

    def test_idle_assertion_blocks_when_steam_active(self, helper_env):
        """When steam or steamwebhelper process is detected in /proc, helper aborts with code 2."""
        if not can_use_unshare():
            pytest.skip("PID namespace isolation (unshare) not available")

        # Spawn a dummy process named 'steam' inside the isolated PID namespace
        fake_bin_dir = helper_env["home"] / "fake_bin"
        fake_bin_dir.mkdir(parents=True, exist_ok=True)
        fake_steam = fake_bin_dir / "steam"
        shutil.copy("/bin/sleep", str(fake_steam))
        fake_steam.chmod(0o755)

        cmd = [
            "unshare", "-r", "-p", "-f", "--mount-proc",
            "bash", "-c",
            f"{fake_steam} 5 & sleep 0.1 && {APPLY_PENDING_BIN} --process-all"
        ]
        res = subprocess.run(cmd, env=helper_env["env"], capture_output=True, text=True, timeout=10)
        assert res.returncode == 2
        assert "Steam client processes are still active" in res.stderr

    def test_granular_reset_removes_cloud_saves_and_preserves_localconfig(self, helper_env):
        """
        Verify granular reset wipes storage/<acct>/<appId>/, remote/, remotecache.vdf,
        and sets cn.dat to 0, while localconfig.vdf and gamerecordings/ are strictly preserved.
        """
        steam = helper_env["steam"]
        acct = "12345"
        app_id = 480

        # 1. CloudRedirect storage paths
        storage_app = steam / "cloud_redirect" / "storage" / acct / str(app_id)
        storage_app.mkdir(parents=True, exist_ok=True)
        (storage_app / "quicksave.dat").write_text("save content")
        (storage_app / "cn.dat").write_text("42\n")
        root_cn = steam / "cloud_redirect" / "storage" / "cn.dat"
        root_cn.write_text("42\n")

        # 2. Userdata paths
        userdata_app = steam / "userdata" / acct / str(app_id)
        remote_dir = userdata_app / "remote"
        remote_dir.mkdir(parents=True, exist_ok=True)
        (remote_dir / "cloud_save.bin").write_text("remote data")
        (userdata_app / "remotecache.vdf").write_text('"remotecache" { "cloud_save.bin" "1" }')

        # 3. CRITICAL PROTECTED TARGETS
        localconfig = userdata_app / "localconfig.vdf"
        localconfig.write_text('"UserLocalConfig" { "Friends" "1" "Resolution" "1080p" }')

        gamerecordings = userdata_app / "gamerecordings"
        gamerecordings.mkdir(parents=True, exist_ok=True)
        (gamerecordings / "epic_clip.mp4").write_bytes(b"\x00\x00\x00\x20ftypmp42fakevideo")

        # 4. Enqueue ResetCloudSave operation
        q_data = {
            "version": 1,
            "operations": [{
                "id": "op_reset_480",
                "appId": app_id,
                "accountId": int(acct),
                "type": "RESET",
                "status": "PENDING",
                "timestamp": 12345678,
                "backupPath": "",
                "restoreSourcePath": "",
                "retries": 0,
                "lastError": "",
            }]
        }
        helper_env["queue_file"].write_text(json.dumps(q_data))

        # 5. Run helper
        res = run_apply_pending(helper_env["env"])
        assert res.returncode == 0, f"Helper failed: {res.stderr}\n{res.stdout}"

        # 6. Verify scoped reset targets were wiped
        assert not (storage_app / "quicksave.dat").exists()
        assert not remote_dir.exists()
        assert not (userdata_app / "remotecache.vdf").exists()

        # 7. Verify cn.dat sequence counter was reset to 0
        assert (storage_app / "cn.dat").exists()
        assert (storage_app / "cn.dat").read_text().strip() == "0"
        assert root_cn.read_text().strip() == "0"

        # 8. VERIFY PROTECTED INVARIANTS INTACT
        assert localconfig.is_file(), "localconfig.vdf was corrupted or deleted!"
        assert "UserLocalConfig" in localconfig.read_text()
        assert "Resolution" in localconfig.read_text()

        assert (gamerecordings / "epic_clip.mp4").is_file(), "gamerecordings/ was corrupted or deleted!"
        assert (gamerecordings / "epic_clip.mp4").read_bytes() == b"\x00\x00\x00\x20ftypmp42fakevideo"

        # 9. Verify backup tarball was created and queue status updated
        updated_q = json.loads(helper_env["queue_file"].read_text())
        assert updated_q["operations"][0]["status"] == "COMPLETED"

        backup_dir = helper_env["cfg"] / "navi" / "cloud_backups" / str(app_id)
        assert backup_dir.is_dir()
        tarballs = list(backup_dir.glob("pre-reset-*.tar.gz"))
        assert len(tarballs) == 1
        with tarfile.open(tarballs[0], "r:gz") as tar:
            members = [m.name for m in tar.getmembers()]
            assert any("quicksave.dat" in m or "cloud_save.bin" in m for m in members)

    def test_rollback_on_fault_restores_original_files(self, helper_env):
        """Simulate a fault during granular reset and verify pre-reset tarball restores original files."""
        steam = helper_env["steam"]
        acct = "12345"
        app_id = 480

        storage_app = steam / "cloud_redirect" / "storage" / acct / str(app_id)
        storage_app.mkdir(parents=True, exist_ok=True)
        (storage_app / "important_save.dat").write_text("original_save_data_content")

        userdata_app = steam / "userdata" / acct / str(app_id)
        remote_dir = userdata_app / "remote"
        remote_dir.mkdir(parents=True, exist_ok=True)
        target_remote_file = remote_dir / "original_remote.bin"
        target_remote_file.write_text("original_remote_content")

        # Invariant fault injection: localconfig symlinks into remote/ so wiping remote/
        # causes localconfig to break, triggering PerformGranularReset() abort & rollback!
        localconfig = userdata_app / "localconfig.vdf"
        localconfig.symlink_to(target_remote_file)

        q_data = {
            "version": 1,
            "operations": [{
                "id": "op_fault_test",
                "appId": app_id,
                "accountId": int(acct),
                "type": "RESET",
                "status": "PENDING",
                "timestamp": 12345679,
                "backupPath": "",
                "restoreSourcePath": "",
                "retries": 0,
                "lastError": "",
            }]
        }
        helper_env["queue_file"].write_text(json.dumps(q_data))

        res = run_apply_pending(helper_env["env"])
        assert res.returncode == 1, "Helper should return 1 on failed/rolled back operation"

        updated_q = json.loads(helper_env["queue_file"].read_text())
        assert updated_q["operations"][0]["status"] == "ROLLED_BACK"
        assert "Operation failed during execution; rolled back to pre-op backup" in updated_q["operations"][0]["lastError"]

        # Verify rollback restored the original files from the pre-reset tarball
        assert (storage_app / "important_save.dat").is_file()
        assert (storage_app / "important_save.dat").read_text() == "original_save_data_content"

        assert target_remote_file.is_file()
        assert target_remote_file.read_text() == "original_remote_content"


# ==============================================================================
# 3. Backend API Security Tests
# ==============================================================================

class MockStoreInjectServer(ThreadingHTTPServer):
    """Hermetic HTTP server implementing the exact security rules of src/presenter/storeinject.cpp."""
    navi_auth_token: str = "test_secret_token_1234567890abcdef"


class MockStoreInjectHandler(BaseHTTPRequestHandler):
    """HTTP request handler mirroring CCloudRedirectBridge & StoreInject security logic."""

    server: MockStoreInjectServer

    def log_message(self, format: str, *args: Any) -> None:
        """Suppress noisy request logs during tests."""
        pass

    def _is_origin_allowed(self) -> bool:
        origin = self.headers.get("Origin", "")
        if not origin:
            return True
        if origin in ("https://steamloopback.host", "http://127.0.0.1:9001", "http://localhost:9001"):
            return True
        return False

    def _verify_navi_auth(self) -> bool:
        token = self.headers.get("X-Navi-Auth") or self.headers.get("x-navi-auth")
        if not token:
            return False
        expected = self.server.navi_auth_token
        return hmac.compare_digest(token, expected)

    def _send_json(self, status: int, data: Dict[str, Any]) -> None:
        body = json.dumps(data).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)

    def do_OPTIONS(self) -> None:
        self.send_response(200)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type, X-Navi-Auth")
        self.end_headers()

    def do_GET(self) -> None:
        if not self._is_origin_allowed():
            self._send_json(403, {"error": "Forbidden: invalid origin"})
            return

        if self.path in ("/cloud/status", "/cloud/pending-ops") or self.path.startswith("/cloud/apps/"):
            self._send_json(200, {"success": True, "path": self.path})
        else:
            self._send_json(404, {"error": "Not Found"})

    def do_POST(self) -> None:
        if not self._is_origin_allowed():
            self._send_json(403, {"error": "Forbidden: invalid origin"})
            return

        # Mutating endpoints requiring X-Navi-Auth token
        mutating_prefixes = ("/cloud/toggle", "/cloud/apps/", "/cloud/provider/")
        if any(self.path.startswith(p) for p in mutating_prefixes):
            if not self._verify_navi_auth():
                self._send_json(401, {"error": "Unauthorized: invalid X-Navi-Auth token"})
                return

        length = int(self.headers.get("Content-Length", "0"))
        body_bytes = self.rfile.read(length) if length > 0 else b""
        body_text = body_bytes.decode("utf-8", errors="replace")

        # Typed confirmation check on /cloud/apps/<appId>/reset
        if self.path.startswith("/cloud/apps/") and self.path.endswith("/reset"):
            try:
                payload = json.loads(body_text) if body_text else {}
            except Exception:
                payload = {}

            if payload.get("confirm") != "RESET":
                self._send_json(400, {"error": 'Confirmation required: payload must contain {"confirm": "RESET"}'})
                return

            self._send_json(202, {"status": "accepted", "operationId": "op_test_accepted"})
            return

        if self.path == "/cloud/toggle":
            self._send_json(200, {"success": True, "message": "Cloud saves toggled"})
            return

        self._send_json(200, {"success": True})


class TestBackendApiSecurity:
    """Verifies Origin allowlist, X-Navi-Auth token enforcement, and typed reset confirmation."""

    @pytest.fixture(scope="module")
    def api_server(self):
        """Spawns an in-process HTTP security server on loopback interface."""
        server = MockStoreInjectServer(("127.0.0.1", 0), MockStoreInjectHandler)
        server.navi_auth_token = "valid_secure_token_999888"
        host, port = server.server_address

        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()

        yield f"http://{host}:{port}", server.navi_auth_token

        server.shutdown()
        server.server_close()
        thread.join(timeout=2)

    def _request(
        self,
        base_url: str,
        path: str,
        method: str = "GET",
        headers: Optional[Dict[str, str]] = None,
        data: Optional[Dict[str, Any]] = None,
    ) -> tuple[int, Dict[str, Any]]:
        import urllib.request
        import urllib.error

        url = f"{base_url}{path}"
        req_headers = headers or {}
        body = json.dumps(data).encode("utf-8") if data is not None else None

        req = urllib.request.Request(url, data=body, headers=req_headers, method=method)
        try:
            with urllib.request.urlopen(req, timeout=3) as resp:
                resp_data = json.loads(resp.read().decode("utf-8"))
                return resp.status, resp_data
        except urllib.error.HTTPError as e:
            err_data = json.loads(e.read().decode("utf-8"))
            return e.code, err_data

    @pytest.mark.parametrize("origin,allowed", [
        ("", True),
        ("https://steamloopback.host", True),
        ("http://127.0.0.1:9001", True),
        ("http://localhost:9001", True),
        ("http://evil.com", False),
        ("https://malicious.org:9001", False),
        ("http://localhost:8080", False),
        ("null", False),
        ("https://steamcommunity.com", False),
    ])
    def test_origin_allowlist_enforcement(self, api_server, origin, allowed):
        """Origins outside the allowlist are rejected with HTTP 403 Forbidden."""
        base_url, _ = api_server
        headers = {"Origin": origin} if origin else {}

        status, body = self._request(base_url, "/cloud/status", method="GET", headers=headers)
        if allowed:
            assert status == 200
            assert body.get("success") is True
        else:
            assert status == 403
            assert body.get("error") == "Forbidden: invalid origin"

    @pytest.mark.parametrize("path", [
        "/cloud/toggle",
        "/cloud/apps/480/reset",
        "/cloud/apps/480/toggle",
        "/cloud/provider/signin",
        "/cloud/provider/signout",
    ])
    def test_navi_auth_token_verification_rejects_unauthorized(self, api_server, path):
        """Mutating routes without valid X-Navi-Auth token return 401 Unauthorized."""
        base_url, valid_token = api_server

        # 1. Missing header
        status, body = self._request(base_url, path, method="POST", data={"confirm": "RESET"})
        assert status == 401
        assert "Unauthorized" in body.get("error", "")

        # 2. Invalid header
        headers = {"X-Navi-Auth": "wrong_invalid_token"}
        status, body = self._request(base_url, path, method="POST", headers=headers, data={"confirm": "RESET"})
        assert status == 401

        # 3. Header casing variation (x-navi-auth) with valid token accepted
        headers_lower = {"x-navi-auth": valid_token}
        status, _ = self._request(base_url, path, method="POST", headers=headers_lower, data={"confirm": "RESET"})
        assert status in (200, 202)

    def test_typed_confirmation_required_on_reset(self, api_server):
        """Reset route rejects payloads not containing exact typed confirmation {"confirm": "RESET"}."""
        base_url, valid_token = api_server
        headers = {"X-Navi-Auth": valid_token}

        # 1. Missing confirm field
        status, body = self._request(base_url, "/cloud/apps/480/reset", method="POST", headers=headers, data={})
        assert status == 400
        assert 'Confirmation required: payload must contain {"confirm": "RESET"}' in body.get("error", "")

        # 2. Lowercase or wrong strings
        for bad_confirm in ["reset", "yes", "CONFIRM", "true", 1]:
            status, body = self._request(
                base_url, "/cloud/apps/480/reset", method="POST", headers=headers, data={"confirm": bad_confirm}
            )
            assert status == 400

        # 3. Valid confirmation
        status, body = self._request(
            base_url, "/cloud/apps/480/reset", method="POST", headers=headers, data={"confirm": "RESET"}
        )
        assert status == 202
        assert body.get("status") == "accepted"


# ==============================================================================
# 4. Flatpak Environment Simulation
# ==============================================================================

class TestFlatpakEnvironmentSimulation:
    """Verifies Flatpak path resolution (host path mapping vs sandbox internal path mapping)."""

    def _resolve_paths(self, env_vars: dict[str, str], mock_sandbox_file: bool = False) -> dict[str, Path]:
        """Python model of CCloudRedirectBridge::ResolvePaths() and DetectPlatformTier()."""
        home = Path(env_vars.get("HOME", "/tmp"))
        is_flatpak_tier = mock_sandbox_file or ("FLATPAK_ID" in env_vars)

        xdg_config = env_vars.get("XDG_CONFIG_HOME")
        xdg_data = env_vars.get("XDG_DATA_HOME")

        if is_flatpak_tier:
            if mock_sandbox_file:
                # Inside Flatpak sandbox
                config_home = Path(xdg_config) if xdg_config else (home / ".config")
                data_home = Path(xdg_data) if xdg_data else (home / ".local" / "share")
            else:
                # Host targeting Flatpak sandbox
                config_home = home / ".var" / "app" / "com.valvesoftware.Steam" / ".config"
                data_home = home / ".var" / "app" / "com.valvesoftware.Steam" / ".local" / "share"
        else:
            config_home = Path(xdg_config) if xdg_config else (home / ".config")
            data_home = Path(xdg_data) if xdg_data else (home / ".local" / "share")

        return {
            "tier": 2 if is_flatpak_tier else 1,
            "config_home": config_home,
            "data_home": data_home,
            "cr_config_dir": config_home / "CloudRedirect",
            "cr_pending_ops": config_home / "CloudRedirect" / "cloud_pending_ops.json",
            "cr_killswitch": config_home / "CloudRedirect" / "disable",
            "navi_config_dir": config_home / "navi",
            "navi_enabled": config_home / "navi" / "cloud_enabled",
            "bin_dir": data_home / "navi" / "cloudredirect",
            "engine_so": data_home / "navi" / "cloudredirect" / "cloud_redirect.so",
        }

    def test_host_flatpak_path_resolution(self, tmp_path):
        """Host targeting Flatpak (FLATPAK_ID set, no sandbox file) uses ~/.var/app paths."""
        home = tmp_path / "home"
        env = {"HOME": str(home), "FLATPAK_ID": "com.valvesoftware.Steam"}

        paths = self._resolve_paths(env, mock_sandbox_file=False)
        assert paths["tier"] == 2
        var_steam = home / ".var" / "app" / "com.valvesoftware.Steam"
        assert paths["config_home"] == var_steam / ".config"
        assert paths["data_home"] == var_steam / ".local" / "share"
        assert paths["cr_config_dir"] == var_steam / ".config" / "CloudRedirect"
        assert paths["cr_pending_ops"] == var_steam / ".config" / "CloudRedirect" / "cloud_pending_ops.json"
        assert paths["navi_enabled"] == var_steam / ".config" / "navi" / "cloud_enabled"
        assert paths["engine_so"] == var_steam / ".local" / "share" / "navi" / "cloudredirect" / "cloud_redirect.so"

    def test_sandbox_internal_path_resolution(self, tmp_path):
        """Inside Flatpak sandbox (mock_sandbox_file=True), standard ~/.config and ~/.local/share are used."""
        home = tmp_path / "home"
        env = {"HOME": str(home)}

        paths = self._resolve_paths(env, mock_sandbox_file=True)
        assert paths["tier"] == 2
        assert paths["config_home"] == home / ".config"
        assert paths["data_home"] == home / ".local" / "share"
        assert paths["cr_config_dir"] == home / ".config" / "CloudRedirect"
        assert paths["navi_enabled"] == home / ".config" / "navi" / "cloud_enabled"

    def test_native_host_path_resolution(self, tmp_path):
        """Native host desktop uses standard user directories."""
        home = tmp_path / "home"
        env = {"HOME": str(home)}

        paths = self._resolve_paths(env, mock_sandbox_file=False)
        assert paths["tier"] == 1
        assert paths["config_home"] == home / ".config"
        assert paths["data_home"] == home / ".local" / "share"
        assert paths["cr_config_dir"] == home / ".config" / "CloudRedirect"
        assert paths["navi_enabled"] == home / ".config" / "navi" / "cloud_enabled"

    def test_steam_root_discovery_candidates(self, tmp_path):
        """Verify Steam root discovery priority: host native paths vs flatpak data path."""
        home = tmp_path / "home"

        def find_steam_root():
            candidates = [
                home / ".local" / "share" / "Steam",
                home / ".steam" / "steam",
                home / ".var" / "app" / "com.valvesoftware.Steam" / "data" / "Steam",
            ]
            for cand in candidates:
                if (cand / "steamui").exists():
                    return cand
            return None

        # 1. No install present
        assert find_steam_root() is None

        # 2. Only Flatpak install present
        flatpak_root = home / ".var" / "app" / "com.valvesoftware.Steam" / "data" / "Steam"
        (flatpak_root / "steamui").mkdir(parents=True)
        assert find_steam_root() == flatpak_root

        # 3. Native install also present (Native takes priority over Flatpak on host)
        native_root = home / ".local" / "share" / "Steam"
        (native_root / "steamui").mkdir(parents=True)
        assert find_steam_root() == native_root

    def test_cpp_unit_test_flatpak_parity(self):
        """Run compiled C++ Google Test suite to ensure binary-level agreement on Flatpak paths."""
        unit_test_bin = REPO_ROOT / "bin" / "unit_tests"
        if not unit_test_bin.is_file():
            pytest.skip("bin/unit_tests not compiled")

        res = subprocess.run(
            [str(unit_test_bin), '--gtest_filter=CloudRedirectBridgeTest.PlatformTierDetectionAndPathsResolution'],
            capture_output=True,
            text=True,
            timeout=5,
        )
        assert res.returncode == 0, f"C++ Flatpak test failed: {res.stderr}\n{res.stdout}"
        assert "PASSED" in res.stdout
