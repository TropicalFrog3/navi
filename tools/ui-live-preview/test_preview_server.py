"""
Test Suite for Navi UI Live Preview Server, Mock API, and File Watcher.

Can be run via:
    pytest tools/ui-live-preview/test_preview_server.py
or
    python3 tools/ui-live-preview/test_preview_server.py
"""

from __future__ import annotations

import json
import os
from pathlib import Path
import socket
import sys
import tempfile
import threading
import time
import unittest
from urllib.error import HTTPError
import urllib.request

# Ensure tools/ui-live-preview is importable
TEST_DIR = Path(__file__).resolve().parent
REPO_ROOT = TEST_DIR.parents[1]

if str(TEST_DIR) not in sys.path:
    sys.path.insert(0, str(TEST_DIR))

from cdp_reloader import SteamCdpReloader
from file_watcher import FileWatcher
from mock_api import NaviMockApi
from server import LivePreviewServer


class TestNaviMockApi(unittest.TestCase):
    """Unit tests for Navi port 9001 Mock API implementation."""

    def setUp(self) -> None:
        self.api = NaviMockApi()

    def test_cloud_status_and_toggle(self) -> None:
        status, body, ct = self.api.dispatch("GET", "/cloud/status")
        self.assertEqual(status, 200)
        data = json.loads(body.decode("utf-8"))
        self.assertTrue(data["enabled"])
        self.assertEqual(data["user"], "deck")

        # Toggle master cloud switch
        status, body, _ = self.api.dispatch("POST", "/cloud/toggle")
        self.assertEqual(status, 200)
        data = json.loads(body.decode("utf-8"))
        self.assertFalse(data["enabled"])

    def test_cloud_provider_auth(self) -> None:
        # Check initial local-only status
        status, body, _ = self.api.dispatch("GET", "/cloud/provider/status?provider=local-only")
        self.assertEqual(status, 200)
        data = json.loads(body.decode("utf-8"))
        self.assertTrue(data["authenticated"])
        self.assertEqual(data["status"], "connected")

        # Sign-in to gdrive
        signin_payload = json.dumps({"provider": "gdrive"}).encode("utf-8")
        status, body, _ = self.api.dispatch("POST", "/cloud/provider/signin", body=signin_payload)
        self.assertEqual(status, 200)
        data = json.loads(body.decode("utf-8"))
        self.assertTrue(data["authenticated"])
        self.assertIn("accounts.google.com", data["authUrl"])

        # Sign-out
        status, body, _ = self.api.dispatch("POST", "/cloud/provider/signout")
        self.assertEqual(status, 200)
        data = json.loads(body.decode("utf-8"))
        self.assertFalse(data["authenticated"])

    def test_cloud_apps_and_files(self) -> None:
        # List apps
        status, body, _ = self.api.dispatch("GET", "/cloud/apps")
        self.assertEqual(status, 200)
        data = json.loads(body.decode("utf-8"))
        self.assertTrue(data["success"])
        self.assertGreaterEqual(len(data["apps"]), 2)

        # App status
        status, body, _ = self.api.dispatch("GET", "/cloud/apps/1443430/status")
        self.assertEqual(status, 200)
        data = json.loads(body.decode("utf-8"))
        self.assertEqual(data["appid"], 1443430)
        self.assertTrue(data["redirected"])

        # App files
        status, body, _ = self.api.dispatch("GET", "/cloud/apps/1443430/files")
        self.assertEqual(status, 200)
        data = json.loads(body.decode("utf-8"))
        self.assertEqual(data["appId"], 1443430)
        self.assertGreater(len(data["files"]), 0)

        # App backup creation
        status, body, _ = self.api.dispatch("POST", "/cloud/apps/1443430/backup")
        self.assertEqual(status, 200)
        data = json.loads(body.decode("utf-8"))
        self.assertTrue(data["success"])
        self.assertIn("backupId", data)

        # App restore
        status, body, _ = self.api.dispatch("POST", "/cloud/apps/1443430/restore")
        self.assertEqual(status, 202)

        # App reset
        status, body, _ = self.api.dispatch("POST", "/cloud/apps/1443430/reset")
        self.assertEqual(status, 200)

    def test_config_endpoints_and_retention_clamping(self) -> None:
        # GET config
        status, body, _ = self.api.dispatch("GET", "/config/full")
        self.assertEqual(status, 200)
        data = json.loads(body.decode("utf-8"))
        self.assertEqual(data["FakeName"], "Gordon Freeman")

        # POST config with clamped retention count
        update_payload = json.dumps({
            "FakeName": "Eli Vance",
            "CloudSaves": {"BackupRetentionCount": 500},
        }).encode("utf-8")
        status, body, _ = self.api.dispatch("POST", "/config/full", body=update_payload)
        self.assertEqual(status, 200)

        # Verify updated values
        _, body, _ = self.api.dispatch("GET", "/config/full")
        cfg = json.loads(body.decode("utf-8"))
        self.assertEqual(cfg["FakeName"], "Eli Vance")
        self.assertEqual(cfg["CloudSaves"]["BackupRetentionCount"], 100)  # Clamped to 100

    def test_check_and_depots(self) -> None:
        # Check app existence
        status, body, _ = self.api.dispatch("GET", "/check?id=1443430")
        self.assertEqual(status, 200)
        data = json.loads(body.decode("utf-8"))
        self.assertTrue(data["exists"])
        self.assertEqual(data["appid"], 1443430)

        # Get depots
        status, body, _ = self.api.dispatch("GET", "/get-depots?id=1443430")
        self.assertEqual(status, 200)
        data = json.loads(body.decode("utf-8"))
        self.assertTrue(data["success"])
        self.assertEqual(data["appid"], 1443430)
        self.assertGreater(len(data["depots"]), 0)

        # Download start/cancel
        status, _, _ = self.api.dispatch("GET", "/start-download?id=1443430&depots=1443431")
        self.assertEqual(status, 200)
        status, _, _ = self.api.dispatch("GET", "/cancel-download?id=1443430")
        self.assertEqual(status, 200)

    def test_fixes_and_cracks(self) -> None:
        # Install fix
        status, body, _ = self.api.dispatch("GET", "/install-fix?id=1443430")
        self.assertEqual(status, 200)
        data = json.loads(body.decode("utf-8"))
        self.assertTrue(data["onlineFixInstalled"])

        # Remove fix
        status, body, _ = self.api.dispatch("GET", "/remove-fix?id=1443430")
        self.assertEqual(status, 200)
        data = json.loads(body.decode("utf-8"))
        self.assertFalse(data["onlineFixInstalled"])

        # Install crack
        status, body, _ = self.api.dispatch("GET", "/install-crack?id=1443430")
        self.assertEqual(status, 200)
        data = json.loads(body.decode("utf-8"))
        self.assertTrue(data["autoCrackInstalled"])

        # Check crack status
        status, body, _ = self.api.dispatch("GET", "/crack-status?id=1443430")
        self.assertEqual(status, 200)
        data = json.loads(body.decode("utf-8"))
        self.assertTrue(data["autoCrackInstalled"])

    def test_api_keys(self) -> None:
        status, body, _ = self.api.dispatch("GET", "/api-keys")
        self.assertEqual(status, 200)
        data = json.loads(body.decode("utf-8"))
        self.assertIn("sls-hubcap-key", data)

        # Set API key
        status, body, _ = self.api.dispatch("GET", "/set-api-keys?hubcap=new_secret_key")
        self.assertEqual(status, 200)
        _, body, _ = self.api.dispatch("GET", "/api-keys")
        data = json.loads(body.decode("utf-8"))
        self.assertEqual(data["sls-hubcap-key"], "new_secret_key")

    def test_request_logging_and_clear(self) -> None:
        self.api.clear_logs()
        self.api.dispatch("GET", "/cloud/status")
        self.api.dispatch("GET", "/check?id=1443430")

        logs = self.api.get_logs()
        self.assertEqual(len(logs), 2)
        self.assertEqual(logs[0]["clean_path"], "/cloud/status")
        self.assertEqual(logs[1]["clean_path"], "/check")

        self.api.clear_logs()
        self.assertEqual(len(self.api.get_logs()), 0)


class TestFileWatcher(unittest.TestCase):
    """Unit tests for FileWatcher with debounce."""

    def test_file_watcher_detects_changes_and_debounce(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp_path = Path(tmpdir)
            test_file = tmp_path / "test-script.js"
            test_file.write_text("console.log('v1');", encoding="utf-8")

            events_received: list[list[dict]] = []
            event_flag = threading.Event()

            def on_change(changes: list[dict]) -> None:
                events_received.append(changes)
                event_flag.set()

            watcher = FileWatcher(
                watch_dirs=[tmp_path],
                poll_interval=0.05,
                debounce_delay=0.10,
            )
            watcher.register_callback(on_change)
            watcher.start()

            try:
                # Baseline scan done. Now modify file twice in rapid succession.
                time.sleep(0.08)
                test_file.write_text("console.log('v2');", encoding="utf-8")
                time.sleep(0.02)
                test_file.write_text("console.log('v3');", encoding="utf-8")

                # Wait for debounced event to fire
                fired = event_flag.wait(timeout=2.0)
                self.assertTrue(fired, "Watcher callback was not triggered within timeout.")
                self.assertGreaterEqual(len(events_received), 1)

                # Check event metadata
                last_batch = events_received[0]
                self.assertTrue(any(e["name"] == "test-script.js" for e in last_batch))
            finally:
                watcher.stop()


class TestSteamCdpReloader(unittest.TestCase):
    """Unit tests for Steam CDP target discovery and routing logic."""

    def test_target_filtering(self) -> None:
        reloader = SteamCdpReloader(host="127.0.0.1", port=8080)
        # Mock target list
        mock_targets = [
            {"id": "1", "title": "Steam", "type": "page", "url": "https://steamloopback.host/index.html", "webSocketDebuggerUrl": "ws://127.0.0.1:8080/1"},
            {"id": "2", "title": "Buy Rogue on Steam", "type": "page", "url": "https://store.steampowered.com/app/1443430", "webSocketDebuggerUrl": "ws://127.0.0.1:8080/2"},
            {"id": "3", "title": "DevTools", "type": "other", "url": "devtools://devtools", "webSocketDebuggerUrl": "ws://127.0.0.1:8080/3"},
        ]
        reloader.targets = mock_targets
        reloader.list_targets = lambda: mock_targets  # type: ignore

        # Store script should target the Store page
        store_targets = reloader.find_relevant_targets("store-page-script.js")
        self.assertEqual(len(store_targets), 1)
        self.assertIn("Buy Rogue", store_targets[0]["title"])

        # Library script should target the Steam client window
        lib_targets = reloader.find_relevant_targets("app-details-script.js")
        self.assertEqual(len(lib_targets), 1)
        self.assertEqual(lib_targets[0]["title"], "Steam")


class TestLivePreviewServerIntegration(unittest.TestCase):
    """Integration tests verifying HTTP endpoints, shell injections, and script serving."""

    server: LivePreviewServer
    server_thread: threading.Thread
    port: int

    @classmethod
    def setUpClass(cls) -> None:
        # Find a free ephemeral port
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.bind(("127.0.0.1", 0))
            cls.port = s.getsockname()[1]

        cls.server = LivePreviewServer(
            host="127.0.0.1",
            port=cls.port,
            mock_port=0,  # disable dedicated mock port in integration test to avoid collision
            enable_cdp=False,
            debounce_delay=0.1,
        )
        cls.server_thread = threading.Thread(target=cls.server.start, kwargs={"block": True}, daemon=True)
        cls.server_thread.start()

        # Wait until server is accepting connections
        start_t = time.time()
        while time.time() - start_t < 3.0:
            try:
                with socket.create_connection(("127.0.0.1", cls.port), timeout=0.2):
                    break
            except OSError:
                time.sleep(0.05)

    @classmethod
    def tearDownClass(cls) -> None:
        cls.server.stop()

    def _get(self, path: str) -> tuple[int, bytes, str]:
        url = f"http://127.0.0.1:{self.port}{path}"
        req = urllib.request.Request(url, headers={"User-Agent": "Navi-Preview-Test"})
        with urllib.request.urlopen(req, timeout=3.0) as resp:
            return resp.status, resp.read(), resp.headers.get("Content-Type", "")

    def test_dashboard_index_serves_html(self) -> None:
        status, body, ct = self._get("/")
        self.assertEqual(status, 200)
        self.assertIn("text/html", ct)
        content = body.decode("utf-8")
        self.assertIn("NAVI UI STUDIO", content)
        self.assertIn("Library Shell", content)
        self.assertIn("Store Shell", content)

    def test_library_shell_serves_injected_fixtures(self) -> None:
        status, body, ct = self._get("/_preview/shell/library")
        self.assertEqual(status, 200)
        self.assertIn("text/html", ct)
        content = body.decode("utf-8")
        self.assertIn("live_reload_client.js", content)
        self.assertIn("import-lua-script.js", content)
        self.assertIn("app-details-script.js", content)
        self.assertIn("navi_inject.js", content)

    def test_store_shell_serves_and_relaxes_csp(self) -> None:
        status, body, ct = self._get("/_preview/shell/store")
        self.assertEqual(status, 200)
        self.assertIn("text/html", ct)
        content = body.decode("utf-8")
        self.assertIn("live_reload_client.js", content)
        self.assertIn("store-page-script.js", content)
        # Ensure restrictive CSP meta tag was stripped/relaxed
        self.assertNotIn("connect-src https:;", content)

    def test_inject_script_serving_and_appid_substitution(self) -> None:
        status, body, ct = self._get("/_preview/scripts/store/depot-selection-script.js")
        self.assertEqual(status, 200)
        self.assertIn("application/javascript", ct)
        content = body.decode("utf-8")
        # Ensure %APPID% was substituted with 1443430
        self.assertNotIn("%APPID%", content)
        self.assertIn("1443430", content)

    def test_live_status_endpoint(self) -> None:
        status, body, ct = self._get("/_live/status")
        self.assertEqual(status, 200)
        data = json.loads(body.decode("utf-8"))
        self.assertIn("uptime_seconds", data)
        self.assertIn("watched_files", data)
        self.assertGreater(len(data["watched_files"]), 0)

    def test_integrated_mock_api_routing(self) -> None:
        # Check /check?id=1443430 routed through preview dev server port
        status, body, ct = self._get("/check?id=1443430")
        self.assertEqual(status, 200)
        data = json.loads(body.decode("utf-8"))
        self.assertTrue(data["exists"])
        self.assertEqual(data["appid"], 1443430)

        # Check /cloud/status
        status, body, ct = self._get("/cloud/status")
        self.assertEqual(status, 200)
        data = json.loads(body.decode("utf-8"))
        self.assertIn("enabled", data)

        # Check API log recorded this call
        status, body, _ = self._get("/_live/api-log")
        self.assertEqual(status, 200)
        logs = json.loads(body.decode("utf-8"))
        self.assertGreater(len(logs), 0)


if __name__ == "__main__":
    unittest.main()
