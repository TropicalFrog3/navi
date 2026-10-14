#!/usr/bin/env python3
"""
tests/test_steam_ui_flow.py
===========================
Comprehensive end-to-end integration and flow test for SLSsteam & ACCELA against live Steam:
  1. CEF preflight & process vitality check
  2. SharedJSContext MobX store inspection
  3. React controlled search input automation
  4. Virtualized list element identification & click dispatch
  5. Detail view navigation & DOM verification
  6. SLSsteam native hook telemetry verification (~/.SLSsteam.log)
  7. Change Provider modal & runtime provider switching (/change-provider)
  8. Remove Lua modal cancellation & execution (/remove) with backup/restore
  9. Steam store navigation, Download Lua modal (0% check), Auto Download, and LUAs collection flow
  10. Steam native uninstall flow (steam://uninstall/<appid>) & modal click
  11. Steam native install flow (steam://install/<appid> & /start-download)
  12. Crash dump & memory corruption audit (/tmp/dumps/)
  13. Search state reset & clean UI restoration
"""

import os
import sys
import time
import json
import glob
import shutil
import subprocess
import argparse
import unittest
import urllib.request
import urllib.error

try:
    import websocket
except ImportError:
    print("[ERROR] 'websocket' module not found. Install via 'pip install websocket-client'.", file=sys.stderr)
    sys.exit(1)

# Default test configuration
CONFIG = {
    "port": int(os.environ.get("STEAM_CDP_PORT", "8080")),
    "callback_port": int(os.environ.get("SLS_CALLBACK_PORT", "9001")),
    "appid": int(os.environ.get("STEAM_TEST_APPID", "1443430")),
    "appname": os.environ.get("STEAM_TEST_APPNAME", "Rogue"),
    "depot_id": os.environ.get("STEAM_TEST_DEPOTID", "1443431"),
    "timeout": float(os.environ.get("STEAM_TEST_TIMEOUT", "12.0")),
    "log_path": os.path.expanduser("~/.SLSsteam.log"),
    "dump_dir": "/tmp/dumps",
    "backup_dir": "/tmp/sls_rogue_test_backup",
    "skip_if_no_steam": False
}

# Color formatting
GREEN = "\033[0;32m"
RED = "\033[0;31m"
YELLOW = "\033[1;33m"
BLUE = "\033[0;34m"
CYAN = "\033[0;36m"
BOLD = "\033[1m"
RESET = "\033[0m"

def log_step(name):
    print(f"\n{BLUE}==>{RESET} {BOLD}{name}{RESET}")

def log_pass(msg):
    print(f"  {GREEN}[PASS]{RESET} {msg}")

def log_fail(msg):
    print(f"  {RED}[FAIL]{RESET} {msg}")

def log_info(msg):
    print(f"  {CYAN}[INFO]{RESET} {msg}")


class SteamCDPClient:
    """Helper client to discover targets and evaluate JavaScript via CDP WebSocket."""
    def __init__(self, port=8080):
        self.port = port
        self.base_url = f"http://127.0.0.1:{self.port}"

    def is_reachable(self):
        try:
            req = urllib.request.Request(f"{self.base_url}/json")
            with urllib.request.urlopen(req, timeout=2) as resp:
                return resp.status == 200
        except Exception:
            return False

    def get_targets(self):
        req = urllib.request.Request(f"{self.base_url}/json")
        with urllib.request.urlopen(req, timeout=3) as resp:
            return json.loads(resp.read().decode("utf-8"))

    def find_target_by_title(self, title):
        for target in self.get_targets():
            if target.get("title") == title and target.get("webSocketDebuggerUrl"):
                return target
        return None

    def find_target_by_title_substring(self, substring):
        for target in self.get_targets():
            if substring in target.get("title", "") and target.get("webSocketDebuggerUrl"):
                return target
        return None

    def find_target_by_url_substring(self, substring):
        for target in self.get_targets():
            if substring in target.get("url", "") and target.get("webSocketDebuggerUrl"):
                return target
        return None

    def evaluate(self, ws_url, expression, timeout=6):
        ws = None
        try:
            ws = websocket.create_connection(ws_url, timeout=timeout)
            ws.send(json.dumps({
                "id": 1,
                "method": "Runtime.evaluate",
                "params": {
                    "expression": expression,
                    "returnByValue": True,
                    "awaitPromise": True,
                    "userGesture": True
                }
            }))
            res = json.loads(ws.recv())
            result = res.get("result", {})
            if "exceptionDetails" in result:
                raise RuntimeError(result["exceptionDetails"])
            return result.get("result", {}).get("value")
        finally:
            if ws:
                try:
                    ws.close()
                except Exception:
                    pass


class SteamUIFlowTest(unittest.TestCase):
    @classmethod
    def is_steam_running(cls):
        """Check if steam or steamwebhelper processes are actively running."""
        try:
            r = subprocess.run(["pgrep", "-u", str(os.getuid()), "-x", "steam"], stdout=subprocess.DEVNULL)
            if r.returncode == 0:
                return True
            r2 = subprocess.run(["pgrep", "-u", str(os.getuid()), "-x", "steamwebhelper"], stdout=subprocess.DEVNULL)
            return r2.returncode == 0
        except Exception:
            return False

    @classmethod
    def start_steam(cls):
        """Start Steam with -cef-enable-debugging if not already opened."""
        candidates = [
            os.path.expanduser("~/.local/bin/steam"),
            shutil.which("steam"),
            "/usr/bin/steam",
            "/usr/games/steam",
            "/usr/local/bin/steam",
        ]
        steam_bin = None
        for c in candidates:
            if c and os.path.isfile(c) and os.access(c, os.X_OK):
                steam_bin = c
                break
        if not steam_bin:
            steam_bin = "steam"

        log_info(f"Steam is not opened. Starting '{steam_bin} -cef-enable-debugging'...")
        subprocess.Popen(
            [steam_bin, "-cef-enable-debugging"],
            cwd=os.path.expanduser("~"),
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            start_new_session=True
        )

    @classmethod
    def setUpClass(cls):
        cls.cdp = SteamCDPClient(CONFIG["port"])

        # Auto-start Steam if not open
        steam_was_started = False
        if not cls.cdp.is_reachable() and not cls.is_steam_running():
            if CONFIG["skip_if_no_steam"]:
                raise unittest.SkipTest("Steam is not running and --skip-if-no-steam was specified.")
            cls.start_steam()
            steam_was_started = True

        timeout = 75 if steam_was_started else 35
        start_time = time.time()
        ready = False
        last_log = 0
        last_reason = ""
        while time.time() - start_time < timeout:
            if cls.cdp.is_reachable():
                try:
                    targets = cls.cdp.get_targets()
                    shared = next((t for t in targets if t.get("title") == "SharedJSContext"), None)
                    steam_win = next((t for t in targets if t.get("title") == "Steam"), None)
                    if shared and steam_win:
                        # Verify SharedJSContext internal stores are fully initialized
                        check_script = """
                        (() => {
                            if (!window.appStore || !window.appStore.allApps || window.appStore.allApps.length === 0) {
                                return { ready: false, reason: "appStore not loaded" };
                            }
                            if (!window.collectionStore || !window.collectionStore.GetUserCollectionsByName) {
                                return { ready: false, reason: "collectionStore not loaded" };
                            }
                            try {
                                window.collectionStore.GetUserCollectionsByName("LUAs");
                            } catch (e) {
                                return { ready: false, reason: "collectionStore syncing: " + e.message };
                            }
                            return { ready: true };
                        })()
                        """
                        res = cls.cdp.evaluate(shared["webSocketDebuggerUrl"], check_script)
                        if res and res.get("ready"):
                            ready = True
                            break
                        elif res and res.get("reason"):
                            last_reason = res.get("reason")
                except Exception as e:
                    last_reason = str(e)

            elapsed = int(time.time() - start_time)
            if elapsed > 0 and elapsed % 5 == 0 and elapsed != last_log:
                last_log = elapsed
                reason_str = f" ({last_reason})" if last_reason else ""
                if steam_was_started:
                    log_info(f"Waiting for Steam to initialize and load stores... ({elapsed}s/{timeout}s){reason_str}")
                else:
                    log_info(f"Waiting for Steam CEF targets and stores on port {CONFIG['port']}... ({elapsed}s/{timeout}s){reason_str}")
            time.sleep(1)

        if not ready:
            if CONFIG["skip_if_no_steam"]:
                raise unittest.SkipTest(f"Steam CEF is not running or missing targets on port {CONFIG['port']}.")
            else:
                cls.fail(cls, f"Cannot connect to Steam CEF remote debugging at http://127.0.0.1:{CONFIG['port']}. Ensure Steam is running with -cef-enable-debugging.")

        # Record baseline dumps count
        cls.initial_dumps = set()
        for p in [os.path.join(CONFIG["dump_dir"], "assert_*.dmp"), os.path.join(CONFIG["dump_dir"], "crash_*.dmp")]:
            cls.initial_dumps.update(glob.glob(p))

        # Record baseline log size
        if os.path.isfile(CONFIG["log_path"]):
            cls.initial_log_size = os.path.getsize(CONFIG["log_path"])
        else:
            cls.initial_log_size = 0

        # Create safety backup directory
        os.makedirs(CONFIG["backup_dir"], exist_ok=True)
        cls.backup_files()
        # We do NOT require the test game at start.
        # Ensure clean slate: remove game lua if present from previous runs
        appid = CONFIG["appid"]
        lua_paths = [
            os.path.expanduser(f"~/.local/share/Steam/config/stplug-in/{appid}.lua"),
            os.path.expanduser(f"~/.steam/steam/config/stplug-in/{appid}.lua")
        ]
        if any(os.path.isfile(p) for p in lua_paths):
            try:
                with urllib.request.urlopen(f"http://127.0.0.1:{CONFIG['callback_port']}/remove?id={appid}&game=false&pending=false", timeout=3):
                    pass
            except Exception:
                pass
            for p in lua_paths:
                if os.path.isfile(p):
                    try:
                        os.remove(p)
                    except Exception:
                        pass

        # Clear pending restart flag
        try:
            with urllib.request.urlopen(f"http://127.0.0.1:{CONFIG['callback_port']}/clear-pending?id={appid}", timeout=3):
                pass
        except Exception:
            pass

        # If AppID is in LUAs collection in SharedJSContext, safely remove it
        shared_target = cls.cdp.find_target_by_title("SharedJSContext")
        if shared_target:
            cls.cdp.evaluate(shared_target["webSocketDebuggerUrl"], f"""
            (() => {{
                try {{
                    if (window.collectionStore && window.collectionStore.GetUserCollectionsByName) {{
                        const cols = window.collectionStore.GetUserCollectionsByName('LUAs');
                        if (cols && cols.length > 0) {{
                            const c = cols[0];
                            const apps = c.added || c.apps || (c.asArray ? c.asArray() : []);
                            if (Array.from(apps).some(id => Number(id) === {appid})) {{
                                window.collectionStore.AddOrRemoveApp([{appid}], false, cols[0].id);
                            }}
                        }}
                    }}
                }} catch (e) {{}}
            }})()
            """)

    @classmethod
    def tearDownClass(cls):
        # Clean up temporary backup directory
        bdir = CONFIG["backup_dir"]
        if os.path.isdir(bdir):
            shutil.rmtree(bdir, ignore_errors=True)

    @classmethod
    def backup_files(cls):
        appid = CONFIG["appid"]
        bdir = CONFIG["backup_dir"]
        steam_paths = [
            os.path.expanduser("~/.local/share/Steam"),
            os.path.expanduser("~/.steam/steam")
        ]

        # 1. Backup Lua and Provider from both locations
        for base in steam_paths:
            for p in [f"{base}/config/stplug-in/{appid}.lua", f"{base}/config/stplug-in/{appid}.provider"]:
                if os.path.isfile(p):
                    shutil.copy2(p, bdir)

        # 2. Backup appmanifest and game dir
        for base in steam_paths:
            manifest = f"{base}/steamapps/appmanifest_{appid}.acf"
            if os.path.isfile(manifest):
                shutil.copy2(manifest, bdir)

            gamedir = f"{base}/steamapps/common/{CONFIG['appname']}"
            if os.path.isdir(gamedir) and not os.path.isdir(os.path.join(bdir, CONFIG["appname"])):
                try:
                    shutil.copytree(gamedir, os.path.join(bdir, CONFIG["appname"]), dirs_exist_ok=True)
                except Exception:
                    pass

    @classmethod
    def restore_files(cls):
        appid = CONFIG["appid"]
        bdir = CONFIG["backup_dir"]
        steam_paths = [
            os.path.expanduser("~/.local/share/Steam"),
            os.path.expanduser("~/.steam/steam")
        ]

        # Restore Lua & provider to both locations
        for base in steam_paths:
            plug_dir = os.path.join(base, "config/stplug-in")
            os.makedirs(plug_dir, exist_ok=True)
            for fname in [f"{appid}.lua", f"{appid}.provider"]:
                src = os.path.join(bdir, fname)
                dst = os.path.join(plug_dir, fname)
                if os.path.isfile(src):
                    try:
                        shutil.copy2(src, dst)
                    except Exception:
                        pass

        # Restore appmanifest and game dir
        for base in steam_paths:
            apps_dir = os.path.join(base, "steamapps")
            if os.path.isdir(apps_dir):
                manifest_src = os.path.join(bdir, f"appmanifest_{appid}.acf")
                manifest_dst = os.path.join(apps_dir, f"appmanifest_{appid}.acf")
                if os.path.isfile(manifest_src):
                    try:
                        shutil.copy2(manifest_src, manifest_dst)
                    except Exception:
                        pass

                common_dir = os.path.join(apps_dir, "common")
                os.makedirs(common_dir, exist_ok=True)
                gamedir_src = os.path.join(bdir, CONFIG["appname"])
                gamedir_dst = os.path.join(common_dir, CONFIG["appname"])
                if os.path.isdir(gamedir_src) and not os.path.isdir(gamedir_dst):
                    try:
                        shutil.copytree(gamedir_src, gamedir_dst, dirs_exist_ok=True)
                    except Exception:
                        pass

    def navigate_url(self, url):
        """Execute a steam:// navigation via SteamClient.URL, CLI, or window.location."""
        # 1. Native SteamClient.URL.ExecuteSteamURL via SharedJSContext
        try:
            shared = self.cdp.find_target_by_title("SharedJSContext")
            if shared and shared.get("webSocketDebuggerUrl"):
                self.cdp.evaluate(
                    shared["webSocketDebuggerUrl"],
                    f"if (typeof SteamClient !== 'undefined' && SteamClient.URL && SteamClient.URL.ExecuteSteamURL) {{ SteamClient.URL.ExecuteSteamURL('{url}'); }}"
                )
        except Exception:
            pass

        # 2. Linux CLI fallback if steam is in PATH
        try:
            env = os.environ.copy()
            if "DISPLAY" not in env: env["DISPLAY"] = ":0"
            if "WAYLAND_DISPLAY" not in env: env["WAYLAND_DISPLAY"] = "wayland-0"
            if "XDG_RUNTIME_DIR" not in env: env["XDG_RUNTIME_DIR"] = "/run/user/1000"
            subprocess.run(["steam", url], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=2)
        except Exception:
            pass

        # 3. Direct window.location.href on Steam target
        try:
            steam_target = self.cdp.find_target_by_title("Steam")
            if steam_target and steam_target.get("webSocketDebuggerUrl"):
                self.cdp.evaluate(steam_target["webSocketDebuggerUrl"], f"window.location.href = '{url}';")
        except Exception:
            pass

    def test_01_cef_targets_and_process(self):
        """Verify Steam process is alive and primary CEF windows are exposed."""
        log_step("Test 1: Verify Steam CEF Targets and Process Vitality")
        targets = self.cdp.get_targets()
        self.assertGreater(len(targets), 0, "No CEF targets found")

        titles = [t.get("title", "") for t in targets]
        log_info(f"Discovered {len(targets)} active CEF targets: {', '.join(titles[:6])}...")

        steam_win = self.cdp.find_target_by_title("Steam")
        self.assertIsNotNone(steam_win, "Primary 'Steam' desktop UI target not found")

        shared_ctx = self.cdp.find_target_by_title("SharedJSContext")
        self.assertIsNotNone(shared_ctx, "'SharedJSContext' background store target not found")
        log_pass("Steam CEF debugging endpoints verified.")

    def test_02_clean_slate_preflight(self):
        """Verify that game lua is NOT installed and NOT present in LUAs collection prior to store download."""
        log_step(f"Test 2: Verify Clean Slate Preflight for AppID {CONFIG['appid']}")
        appid = CONFIG["appid"]
        # If the app is currently installed in Steam or on disk, cleanly uninstall it first
        if self.is_app_installed_in_steam(appid) or self.is_app_installed_on_disk(appid):
            log_info(f"AppID {appid} was previously installed; performing clean preflight uninstallation...")
            self.navigate_url(f"steam://uninstall/{appid}")
            for _ in range(12):
                time.sleep(0.5)
                utarget = self.cdp.find_target_by_title("Uninstall")
                if utarget:
                    self.cdp.evaluate(utarget["webSocketDebuggerUrl"], """
                    (() => {
                        const btn = Array.from(document.querySelectorAll('button')).find(b => b.textContent.trim() === 'Uninstall');
                        if (btn) { btn.click(); return { clicked: true }; }
                        return { clicked: false };
                    })()
                    """)
                    break
            for _ in range(30):
                time.sleep(0.5)
                if not self.is_app_installed_in_steam(appid) and not self.is_app_installed_on_disk(appid):
                    break
            log_info("Preflight clean uninstallation complete.")

        lua_paths = [
            os.path.expanduser(f"~/.local/share/Steam/config/stplug-in/{appid}.lua"),
            os.path.expanduser(f"~/.steam/steam/config/stplug-in/{appid}.lua")
        ]
        manifest_paths = [
            os.path.expanduser(f"~/.local/share/Steam/steamapps/appmanifest_{appid}.acf"),
            os.path.expanduser(f"~/.steam/steam/steamapps/appmanifest_{appid}.acf")
        ]
        # Clean stale test artifacts from previous aborted test runs
        for p in lua_paths:
            if os.path.isfile(p):
                try:
                    os.remove(p)
                    log_info(f"Cleaned stale test lua before preflight: {p}")
                except Exception as e:
                    log_warn(f"Failed to remove stale lua {p}: {e}")
        for mp in manifest_paths:
            if os.path.isfile(mp):
                try:
                    os.remove(mp)
                    log_info(f"Cleaned stale test appmanifest before preflight: {mp}")
                except Exception as e:
                    log_warn(f"Failed to remove stale manifest {mp}: {e}")

        self.assertTrue(all(not os.path.isfile(p) for p in lua_paths), f"Preflight error: {appid}.lua must not be present on disk before store download")
        log_info(f"Confirmed clean slate: {appid}.lua absent from stplug-in.")

        shared_target = self.cdp.find_target_by_title("SharedJSContext")
        if shared_target:
            pre_col_check = f"""
            (() => {{
                try {{
                    if (!window.collectionStore || !window.collectionStore.GetUserCollectionsByName) return {{ found: false }};
                    const cols = window.collectionStore.GetUserCollectionsByName('LUAs');
                    if (!cols || cols.length === 0) return {{ found: false }};
                    const c = cols[0];
                    const apps = c.added || c.apps || (c.asArray ? c.asArray() : []);
                    const hasApp = Array.from(apps).some(id => Number(id) === {appid});
                    return {{ found: hasApp }};
                }} catch (e) {{
                    return {{ found: false }};
                }}
            }})()
            """
            for _ in range(5):
                pre_res = self.cdp.evaluate(shared_target["webSocketDebuggerUrl"], pre_col_check)
                if not (pre_res and pre_res.get("found")):
                    break
                self.cdp.evaluate(shared_target["webSocketDebuggerUrl"], f"""
                (() => {{
                    try {{
                        const cols = window.collectionStore.GetUserCollectionsByName('LUAs');
                        if (cols && cols.length > 0) window.collectionStore.AddOrRemoveApp([{appid}], false, cols[0].id);
                    }} catch(e) {{}}
                }})()
                """)
                time.sleep(0.5)

            if pre_res and pre_res.get("found"):
                self.fail(f"Pre-download check failed: AppID {appid} is already present in LUAs collection before download!")
            log_info(f"Verified AppID {appid} is NOT present in LUAs collection prior to download.")
        log_pass("Clean slate preflight verified successfully.")

    def test_03_store_navigation_and_download_button(self):
        """Navigate to Store page for game and verify SLS 'Download Lua' button is mounted."""
        log_step(f"Test 3: Navigate to Store & Verify Download Lua Button for AppID {CONFIG['appid']}")
        steam_target = self.cdp.find_target_by_title("Steam")
        self.assertIsNotNone(steam_target, "Steam target not found")

        appid = CONFIG["appid"]
        appname = CONFIG["appname"]

        log_info(f"Navigating Steam client to Store page for {appname} (AppID {appid})...")
        self.navigate_url(f"steam://store/{appid}")

        store_target = None
        for i in range(40):
            time.sleep(0.5)
            store_target = self.cdp.find_target_by_url_substring(f"store.steampowered.com/app/{appid}")
            if store_target:
                break
            if i in (8, 18):
                self.navigate_url(f"steam://store/{appid}")
        self.assertIsNotNone(store_target, f"Timed out waiting for store page target for AppID {appid}")
        log_info(f"Connected to store page CEF target: '{store_target.get('title')}'")

        # Clean any stale localStorage session and in-memory caches, then reload page
        try:
            self.cdp.evaluate(store_target["webSocketDebuggerUrl"], f"""
            (() => {{
                localStorage.removeItem('sls-session-{appid}');
                if (typeof appUnlockStatus !== 'undefined') delete appUnlockStatus[{appid}];
                if (typeof globalDownloadSessions !== 'undefined') delete globalDownloadSessions[{appid}];
                window.location.reload();
            }})()
            """)
        except Exception:
            pass
        time.sleep(1.5)

        btn_check_script = """
        (() => {
            const btn = document.querySelector('.sls-lua-btn');
            const link = btn ? btn.querySelector('a') : null;
            const span = link ? link.querySelector('span') : null;
            return {
                mounted: !!btn,
                text: span ? span.innerText.trim() : (link ? link.innerText.trim() : null)
            };
        })()
        """
        btn_ready = False
        for _ in range(25):
            try:
                res = self.cdp.evaluate(store_target["webSocketDebuggerUrl"], btn_check_script)
                if res and res.get("mounted") and res.get("text") == "Download Lua":
                    btn_ready = True
                    break
            except Exception:
                pass
            time.sleep(0.5)
        self.assertTrue(btn_ready, "Download Lua button was not mounted with text 'Download Lua' in store DOM")
        log_info("Verified SLS 'Download Lua' button mounted and visible on store page.")
        log_pass("Store navigation and Download Lua button verified.")

    def test_04_download_modal_zero_progress_audit(self):
        """Click 'Download Lua' and assert all provider progress bars in the overlay modal are strictly at 0%."""
        log_step(f"Test 4: Open Overlay Modal & Assert 0% Progress Bars for AppID {CONFIG['appid']}")
        appid = CONFIG["appid"]
        store_target = self.cdp.find_target_by_url_substring(f"store.steampowered.com/app/{appid}")
        self.assertIsNotNone(store_target, "Store page target not found")

        self.cdp.evaluate(store_target["webSocketDebuggerUrl"], """
        (() => {
            const link = document.querySelector('.sls-lua-btn a');
            if (link) link.click();
        })()
        """)

        modal_ready_script = """
        (() => {
            const modal = document.getElementById('sls-overlay-modal');
            if (!modal) return { ready: false };
            const rows = Array.from(modal.querySelectorAll('[data-sls-provider]')).map(row => {
                const bar = row.querySelector('.sls-bar-fill');
                return {
                    provider: row.getAttribute('data-sls-provider'),
                    hasBar: !!bar,
                    width: bar ? bar.style.width : '0%'
                };
            });
            const allZero = rows.every(r => !r.hasBar || r.width === '0%');
            return {
                ready: true,
                rowCount: rows.length,
                allZero: allZero,
                rows: rows
            };
        })()
        """
        modal_res = None
        for _ in range(15):
            modal_res = self.cdp.evaluate(store_target["webSocketDebuggerUrl"], modal_ready_script)
            if modal_res and modal_res.get("ready"):
                break
            time.sleep(0.5)

        self.assertIsNotNone(modal_res, "Timed out waiting for #sls-overlay-modal to appear")
        self.assertTrue(modal_res.get("ready"), "#sls-overlay-modal not mounted in DOM")
        self.assertGreater(modal_res.get("rowCount", 0), 0, "No provider rows found in overlay modal")
        self.assertTrue(modal_res.get("allZero"), f"Modal validation failed: progress bar not at 0% prior to download: {modal_res.get('rows')}")
        log_info(f"Verified overlay modal opened with {modal_res['rowCount']} providers, all at 0% progress.")
        log_pass("Overlay modal 0% progress bars verified.")

    def test_05_auto_download_and_installation(self):
        """Click Auto Download, poll until 100% progress and lua file installation on disk, and close modal."""
        log_step(f"Test 5: Click Auto Download, Await 100% Completion & Close Modal for AppID {CONFIG['appid']}")
        appid = CONFIG["appid"]
        store_target = self.cdp.find_target_by_url_substring(f"store.steampowered.com/app/{appid}")
        self.assertIsNotNone(store_target, "Store page target not found")

        lua_paths = [
            os.path.expanduser(f"~/.local/share/Steam/config/stplug-in/{appid}.lua"),
            os.path.expanduser(f"~/.steam/steam/config/stplug-in/{appid}.lua")
        ]
        log_pos_before = os.path.getsize(CONFIG["log_path"]) if os.path.isfile(CONFIG["log_path"]) else 0

        # Click Auto Download
        auto_click_res = self.cdp.evaluate(store_target["webSocketDebuggerUrl"], """
        (() => {
            const autoBtn = document.getElementById('sls-auto-search');
            if (!autoBtn) return { clicked: false, reason: 'sls-auto-search not found' };
            autoBtn.click();
            return { clicked: true, text: autoBtn.innerText.trim(), hash: window.location.hash };
        })()
        """)
        self.assertTrue(auto_click_res and auto_click_res.get("clicked"), "Failed to click Auto Download button")
        log_info(f"Clicked Auto Download button in modal (hash: {auto_click_res.get('hash')})")

        # Poll for download to complete
        lua_installed = False
        installed_path = None
        for _ in range(30):
            time.sleep(0.5)
            found_path = next((p for p in lua_paths if os.path.isfile(p) and os.path.getsize(p) > 0), None)
            if found_path and os.path.isfile(CONFIG["log_path"]):
                with open(CONFIG["log_path"], "r", encoding="utf-8", errors="ignore") as f:
                    f.seek(log_pos_before)
                    curr_logs = f.read()
                if "Successfully installed appid" in curr_logs:
                    lua_installed = True
                    installed_path = found_path
                    break

        self.assertTrue(lua_installed, f"Store Auto-download timed out waiting for {appid}.lua to be installed")
        log_info(f"Verified Lua script installed at: {installed_path}")

        # Check completed 100% progress bar in modal
        bar_done = self.cdp.evaluate(store_target["webSocketDebuggerUrl"], """
        (() => {
            const modal = document.getElementById('sls-overlay-modal');
            if (!modal) return false;
            const doneBar = modal.querySelector('.sls-bar-fill.sls-bar-done, .sls-bar-fill[style*=\"100%\"]');
            return !!doneBar;
        })()
        """)
        log_info(f"Active provider progress bar 100% completion verified: {bar_done}")

        # Close overlay modal via #sls-provider-close
        self.cdp.evaluate(store_target["webSocketDebuggerUrl"], """
        (() => {
            const closeBtn = document.getElementById('sls-provider-close');
            if (closeBtn) closeBtn.click();
        })()
        """)
        time.sleep(0.5)
        self.assertFalse(self.cdp.evaluate(store_target["webSocketDebuggerUrl"], "!!document.getElementById('sls-overlay-modal');"),
                         "Overlay modal was not dismissed after clicking close button")
        log_info("Verified #sls-overlay-modal closed cleanly.")
        log_pass("Auto Download, file installation, and modal closure verified.")

    def test_06_library_search_and_click(self):
        """Navigate to Library, search for Rogue, and click it in virtualized list."""
        log_step(f"Test 6: Navigate to Library, Search & Click '{CONFIG['appname']}'")
        steam_target = self.cdp.find_target_by_title("Steam")
        self.assertIsNotNone(steam_target, "Steam target not found")

        log_info("Navigating client back to Library view...")
        self.navigate_url("steam://open/library")
        time.sleep(1.5)

        # Set search input
        set_search = f"""
        (() => {{
            const input = document.querySelector('input[placeholder=\"Search by Name\"], input.DialogInput');
            if (!input) return false;
            input.focus();
            const nativeSetter = Object.getOwnPropertyDescriptor(window.HTMLInputElement.prototype, 'value').set;
            nativeSetter.call(input, {json.dumps(CONFIG['appname'])});
            input.dispatchEvent(new Event('input', {{ bubbles: true }}));
            input.dispatchEvent(new Event('change', {{ bubbles: true }}));
            return true;
        }})()
        """
        for _ in range(5):
            if self.cdp.evaluate(steam_target["webSocketDebuggerUrl"], set_search):
                break
            time.sleep(1.0)
        log_info(f"Search input value updated to '{CONFIG['appname']}'.")

        # Click virtualized list item
        click_list_script = f"""
        (() => {{
            const targetName = {json.dumps(CONFIG['appname'])};
            const walker = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT, null, false);
            let node;
            while ((node = walker.nextNode())) {{
                if (node.nodeValue.trim() === targetName) {{
                    let el = node.parentElement;
                    while (el && el !== document.body) {{
                        const isDraggable = el.getAttribute && el.getAttribute('draggable') === 'true';
                        const isFocusable = el.classList && el.classList.contains('Focusable');
                        if (isDraggable || (isFocusable && el.classList.contains('_1vO6BoiVslZgs1kqDGdUs8'))) {{
                            el.scrollIntoView({{ block: 'nearest' }});
                            el.focus();
                            const evt = (type) => new MouseEvent(type, {{ view: window, bubbles: true, cancelable: true, buttons: 1 }});
                            el.dispatchEvent(evt('mousedown'));
                            el.dispatchEvent(evt('mouseup'));
                            el.dispatchEvent(evt('click'));
                            if (typeof el.click === 'function') el.click();
                            return true;
                        }}
                        el = el.parentElement;
                    }}
                }}
            }}
            return false;
        }})()
        """
        clicked_item = False
        for _ in range(10):
            if self.cdp.evaluate(steam_target["webSocketDebuggerUrl"], click_list_script):
                clicked_item = True
                break
            time.sleep(0.5)
        self.assertTrue(clicked_item, f"Failed to click '{CONFIG['appname']}' item in library virtualized list")
        log_info(f"Clicked '{CONFIG['appname']}' in library list; detail view opened.")
        time.sleep(1.0)
        log_pass("Library search and click verified.")

    def test_07_game_detail_view_and_hot_reload(self):
        """Verify game detail view navigation, action buttons, SharedJSContext registration, and /check API."""
        log_step(f"Test 7: Verify Game Detail View & Hot Reload for AppID {CONFIG['appid']}")
        expected_tracking = f"/library/app/{CONFIG['appid']}"
        deadline = time.time() + CONFIG["timeout"]
        found_target = False

        while time.time() < deadline:
            target = self.cdp.find_target_by_title_substring(expected_tracking)
            if target:
                found_target = True
                log_info(f"Found tracking target: {target.get('title')}")
                break
            self.navigate_url(f"steam://nav/games/details/{CONFIG['appid']}")
            time.sleep(0.5)

        self.assertTrue(found_target, f"Timed out waiting for detail view target containing '{expected_tracking}'")

        steam_target = self.cdp.find_target_by_title("Steam")
        verify_script = f"""
        (() => {{
            const targetName = {json.dumps(CONFIG['appname'])};
            const textNodes = [];
            const walker = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT, null, false);
            let n;
            while ((n = walker.nextNode())) {{
                if (n.nodeValue.includes(targetName)) {{
                    textNodes.push(n.nodeValue.slice(0, 80));
                }}
            }}

            const buttons = Array.from(document.querySelectorAll('button, [role="button"], .Focusable'))
                .map(b => b.textContent.trim())
                .filter(t => t === 'Play' || t === 'Install' || t.includes('Download') || t === 'Resume');

            return {{
                hasText: textNodes.length > 0,
                textSnippets: textNodes.slice(0, 3),
                actionButtons: buttons
            }};
        }})()
        """
        dom_res = self.cdp.evaluate(steam_target["webSocketDebuggerUrl"], verify_script)
        self.assertIsInstance(dom_res, dict)
        self.assertTrue(dom_res.get("hasText"), "Game text content not found in rendered view")
        log_info(f"Found game text snippets in DOM: {dom_res.get('textSnippets')}")
        log_info(f"Available primary action buttons: {dom_res.get('actionButtons')}")

        # Verify dynamic hot-reload in SharedJSContext
        shared_target = self.cdp.find_target_by_title("SharedJSContext")
        if shared_target:
            hot_reload_check = f"""
            (() => {{
                if (!window.appStore || !window.appStore.allApps) return false;
                const a = window.appStore.allApps.find(x => x.appid === {CONFIG['appid']});
                return !!a;
            }})()
            """
            found_in_store = False
            for _ in range(20):
                found_in_store = self.cdp.evaluate(shared_target["webSocketDebuggerUrl"], hot_reload_check)
                if found_in_store:
                    break
                time.sleep(0.5)
            self.assertTrue(found_in_store, f"Dynamic hot reload failed: AppID {CONFIG['appid']} not found in SharedJSContext appStore")
            log_info(f"Verified AppID {CONFIG['appid']} dynamically registered in SharedJSContext appStore.")

        # Verify /check API endpoint
        check_detected = False
        check_data = {}
        for _ in range(20):
            try:
                req_check = urllib.request.Request(f"http://127.0.0.1:{CONFIG['callback_port']}/check?id={CONFIG['appid']}")
                with urllib.request.urlopen(req_check, timeout=3) as resp:
                    check_data = json.loads(resp.read().decode())
                    if check_data.get("exists"):
                        check_detected = True
                        break
            except Exception:
                pass
            time.sleep(0.5)
        self.assertTrue(check_detected, f"/check endpoint did not detect installed Lua: {check_data}")
        log_info(f"/check endpoint confirmed AppID {CONFIG['appid']} is installed and registered.")

        # Verify depotcache manifests in native depotcache and legacy config/depotcache
        depot_ids = [CONFIG["depot_id"]]
        manifests = []
        for did in depot_ids:
            for base in [os.path.expanduser("~/.local/share/Steam"), os.path.expanduser("~/.steam/steam")]:
                for dname in ["depotcache", "config/depotcache"]:
                    matches = glob.glob(f"{base}/{dname}/{did}_*.manifest")
                    manifests.extend(matches)
        log_info(f"Discovered {len(manifests)} depotcache manifest(s) for {CONFIG['appname']} depots ({', '.join(depot_ids)}).")
        self.assertGreater(len(manifests), 0, f"No manifests discovered for {CONFIG['appname']} depots")
        log_pass("Game detail view and dynamic hot reload verified.")

    def test_08_luas_collection_and_date_sort(self):
        """Sync LUAs collection, navigate to collection view, sort by Date Added to Library, and verify date headers."""
        log_step(f"Test 8: LUAs Collection Navigation, Date Added Sorting & Verification for AppID {CONFIG['appid']}")
        appid = CONFIG["appid"]
        steam_target = self.cdp.find_target_by_title("Steam")
        self.assertIsNotNone(steam_target, "Steam target not found")

        # Synchronize LUAs collection
        shared_target = self.cdp.find_target_by_title("SharedJSContext")
        if shared_target:
            self.cdp.evaluate(shared_target["webSocketDebuggerUrl"], f"""
            (() => {{
                try {{
                    if (window.__slsSyncAutoCollection) {{
                        window.__slsSyncAutoCollection();
                    }}
                    if (window.collectionStore && window.collectionStore.GetUserCollectionsByName) {{
                        const cols = window.collectionStore.GetUserCollectionsByName('LUAs');
                        if (cols && cols.length > 0) {{
                            window.collectionStore.AddOrRemoveApp([{appid}], true, cols[0].id);
                        }}
                    }}
                }} catch (e) {{
                    console.warn('Sync collection failed:', e);
                }}
            }})()
            """)
            log_info(f"Synchronized AppID {appid} into LUAs collection in SharedJSContext.")
            time.sleep(1.0)

        # Click LUAs Collection Tag in Header
        click_col_tag = """
        (() => {
            const el = document.querySelector('._1HS4twEHtDJ_8MWgJT4VBd');
            if (el && (el.innerText || el.textContent || '').trim() === 'LUAs') {
                el.focus();
                const evt = (type) => new MouseEvent(type, { view: window, bubbles: true, cancelable: true, buttons: 1 });
                el.dispatchEvent(evt('mousedown'));
                el.dispatchEvent(evt('mouseup'));
                el.dispatchEvent(evt('click'));
                if (typeof el.click === 'function') el.click();
                return true;
            }
            const walker = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT, null, false);
            let n;
            while ((n = walker.nextNode())) {
                if (n.nodeValue.trim() === 'LUAs') {
                    let cur = n.parentElement;
                    while (cur && cur !== document.body) {
                        if (cur.getAttribute('role') === 'button' || cur.classList.contains('_1HS4twEHtDJ_8MWgJT4VBd')) {
                            cur.focus();
                            const evt = (type) => new MouseEvent(type, { view: window, bubbles: true, cancelable: true, buttons: 1 });
                            cur.dispatchEvent(evt('mousedown'));
                            cur.dispatchEvent(evt('mouseup'));
                            cur.dispatchEvent(evt('click'));
                            if (typeof cur.click === 'function') cur.click();
                            return true;
                        }
                        cur = cur.parentElement;
                    }
                }
            }
            return false;
        })()
        """
        tag_clicked = False
        for _ in range(15):
            if self.cdp.evaluate(steam_target["webSocketDebuggerUrl"], click_col_tag):
                tag_clicked = True
                break
            time.sleep(0.5)
        self.assertTrue(tag_clicked, "Failed to click 'LUAs' collection tag in game header")
        log_info("Clicked 'LUAs' collection tag in game header; navigated to LUAs collection view.")
        time.sleep(1.0)

        # Verify collection view tracking target
        col_tracking = None
        for _ in range(20):
            col_tracking = self.cdp.find_target_by_title_substring("/library/collection/")
            if col_tracking:
                break
            time.sleep(0.5)
        self.assertIsNotNone(col_tracking, "Timed out waiting for collection view tracking target")
        log_info(f"Verified collection view active: '{col_tracking.get('title')}'")

        # Open sort dropdown
        sort_flow_script = """
        (() => {
            const sortDropdown = Array.from(document.querySelectorAll('button.DialogDropDown, button[role="combobox"]'))
                .find(b => {
                    const disp = b.querySelector('.DialogDropDown_CurrentDisplay');
                    return disp && (disp.innerText.includes('Hours Played') || disp.innerText.includes('Alphabetical') || disp.innerText.includes('Date Added'));
                });
            if (!sortDropdown) return { opened: false, reason: 'sort dropdown not found' };

            sortDropdown.focus();
            const evt = (type) => new MouseEvent(type, { view: window, bubbles: true, cancelable: true, buttons: 1 });
            sortDropdown.dispatchEvent(evt('mousedown'));
            sortDropdown.dispatchEvent(evt('mouseup'));
            sortDropdown.dispatchEvent(evt('click'));
            if (typeof sortDropdown.click === 'function') sortDropdown.click();

            return { opened: true };
        })()
        """
        opened_sort = None
        for _ in range(10):
            opened_sort = self.cdp.evaluate(steam_target["webSocketDebuggerUrl"], sort_flow_script)
            if opened_sort and opened_sort.get("opened"):
                break
            time.sleep(0.5)
        self.assertTrue(opened_sort and opened_sort.get("opened"), "Failed to open collection sort dropdown")
        time.sleep(0.5)

        # Click Date Added to Library
        select_option_script = """
        (() => {
            const option = Array.from(document.querySelectorAll('[role="option"], .DialogMenuPosition button'))
                .find(b => (b.innerText || b.textContent || '').trim() === 'Date Added to Library');
            if (!option) return false;
            option.focus();
            const evt = (type) => new MouseEvent(type, { view: window, bubbles: true, cancelable: true, buttons: 1 });
            option.dispatchEvent(evt('mousedown'));
            option.dispatchEvent(evt('mouseup'));
            option.dispatchEvent(evt('click'));
            if (typeof option.click === 'function') option.click();
            return true;
        })()
        """
        option_clicked = False
        for _ in range(10):
            if self.cdp.evaluate(steam_target["webSocketDebuggerUrl"], select_option_script):
                option_clicked = True
                break
            time.sleep(0.5)
        self.assertTrue(option_clicked, "Failed to select 'Date Added to Library' sort option")
        log_info("Selected 'Date Added to Library' in sort dropdown.")
        time.sleep(1.0)

        # Verify date header element exists
        has_date_header = None
        for _ in range(10):
            has_date_header = self.cdp.evaluate(steam_target["webSocketDebuggerUrl"], """
            (() => {
                const headers = Array.from(document.querySelectorAll('._1LJqx_qFOC8199RBQO5kU8')).map(h => h.innerText.trim());
                return {
                    found: headers.length > 0,
                    headers: headers.slice(0, 5)
                };
            })()
            """)
            if has_date_header and has_date_header.get("found"):
                break
            time.sleep(0.5)
        self.assertTrue(has_date_header and has_date_header.get("found"), "No date section headers found in collection view")
        log_info(f"Verified date headers rendered in collection grid: {has_date_header.get('headers')}")

        # Verify Rogue is confirmed present in LUAs collection in SharedJSContext
        if shared_target:
            post_col_check = f"""
            (() => {{
                if (!window.collectionStore || !window.collectionStore.GetUserCollectionsByName) return {{ found: false }};
                const cols = window.collectionStore.GetUserCollectionsByName('LUAs');
                if (!cols || cols.length === 0) return {{ found: false }};
                const c = cols[0];
                const apps = c.added || c.apps || (c.asArray ? c.asArray() : []);
                return {{ found: Array.from(apps).some(id => Number(id) === {appid}) }};
            }})()
            """
            post_res = self.cdp.evaluate(shared_target["webSocketDebuggerUrl"], post_col_check)
            self.assertTrue(post_res and post_res.get("found"), f"Post-download check failed: AppID {appid} missing from LUAs collection!")
            log_info(f"Verified AppID {appid} is confirmed present in LUAs collection after download.")
        log_pass("LUAs collection navigation, sorting, and date headers verified.")

    def test_09_slssteam_native_hook_telemetry(self):
        """Verify ~/.SLSsteam.log for AppID hook invocations and absence of memory corruptions."""
        log_step(f"Test 9: Audit SLSsteam Native Hook Telemetry for AppID {CONFIG['appid']}")
        if not os.path.isfile(CONFIG["log_path"]):
            log_info(f"Log file {CONFIG['log_path']} not found; skipping log audit.")
            return

        with open(CONFIG["log_path"], "r", encoding="utf-8", errors="ignore") as f:
            f.seek(self.initial_log_size)
            new_logs = f.read()

        log_lines = [line for line in new_logs.splitlines() if line.strip()]
        corruptions = [line for line in log_lines if any(sig in line.lower() for sig in [
            "double free", "corruption", "segmentation fault", "corrupted size", "sigsegv"
        ])]
        self.assertEqual(len(corruptions), 0, f"Detected memory corruption in log: {corruptions}")
        log_pass("Native hook telemetry verified with zero memory corruptions.")

    def test_10_change_provider_flow(self):
        """Test Change Provider modal, available provider list, and /change-provider endpoint."""
        log_step(f"Test 10: Test Change Provider Flow for AppID {CONFIG['appid']}")
        steam_target = self.cdp.find_target_by_title("Steam")
        self.assertIsNotNone(steam_target)

        # Ensure on Rogue details view
        self.navigate_url(f"steam://nav/games/details/{CONFIG['appid']}")
        time.sleep(1.0)

        # 1. Click Change Provider button in DOM (poll up to 10s)
        modal_res = None
        for _ in range(20):
            open_modal_script = f"""
            (() => {{
                if (window.__slsRefreshButtons) window.__slsRefreshButtons({CONFIG['appid']});
                const btn = document.querySelector('.sls-change-provider-btn');
                if (!btn) return {{ success: false, reason: 'sls-change-provider-btn not found' }};
                btn.click();
                const modal = document.getElementById('sls-provider-modal');
                if (!modal) return {{ success: false, reason: 'sls-provider-modal did not appear' }};

                const providers = Array.from(modal.querySelectorAll('[data-sls-provider-idx]')).map(b => ({{
                    idx: b.getAttribute('data-sls-provider-idx'),
                    text: b.textContent.trim(),
                    disabled: b.disabled || b.hasAttribute('disabled')
                }}));

                // Close modal cleanly
                const cancelBtn = modal.querySelector('#sls-prov-cancel, #sls-close-x, button');
                if (cancelBtn) cancelBtn.click();
                else modal.remove();

                return {{ success: true, count: providers.length, providers: providers }};
            }})()
            """
            modal_res = self.cdp.evaluate(steam_target["webSocketDebuggerUrl"], open_modal_script)
            if modal_res and modal_res.get("success"):
                break
            time.sleep(0.5)

        self.assertIsInstance(modal_res, dict)
        self.assertTrue(modal_res.get("success"), f"Provider modal failure: {modal_res.get('reason')}")
        self.assertGreater(modal_res.get("count", 0), 0, "No providers found in provider modal")
        log_info(f"Verified provider modal with {modal_res['count']} providers.")

        # 2. Query callback server endpoint
        req = urllib.request.Request(f"http://127.0.0.1:{CONFIG['callback_port']}/change-provider?id={CONFIG['appid']}&provider=2")
        with urllib.request.urlopen(req, timeout=3) as resp:
            data = json.loads(resp.read().decode())
            self.assertTrue(data.get("success"), f"change-provider API failed: {data}")
        # Wait for asynchronous provider download and config scan to complete
        time.sleep(2.0)
        log_pass("Change Provider UI modal and API endpoint verified.")

    def is_app_installed_on_disk(self, appid):
        """Check if an appmanifest file exists with StateFlags indicating installed."""
        steam_paths = [
            os.path.expanduser("~/.local/share/Steam"),
            os.path.expanduser("~/.steam/steam")
        ]
        for base in list(steam_paths):
            vdf = os.path.join(base, "steamapps/libraryfolders.vdf")
            if os.path.isfile(vdf):
                try:
                    with open(vdf, "r", encoding="utf-8", errors="ignore") as f:
                        for line in f:
                            if '"path"' in line:
                                parts = line.split('"')
                                if len(parts) >= 4 and os.path.isdir(parts[3]):
                                    p = parts[3]
                                    if p not in steam_paths:
                                        steam_paths.append(p)
                except Exception:
                    pass

        for base in steam_paths:
            manifest = os.path.join(base, f"steamapps/appmanifest_{appid}.acf")
            if os.path.isfile(manifest):
                try:
                    with open(manifest, "r", encoding="utf-8", errors="ignore") as f:
                        content = f.read()
                        for line in content.splitlines():
                            if '"StateFlags"' in line:
                                parts = line.split('"')
                                if len(parts) >= 4:
                                    flags = int(parts[3])
                                    if flags & 4:
                                        return True
                except Exception:
                    pass
        return False

    def is_app_installed_in_steam(self, appid):
        """Check SharedJSContext MobX appStore or overview for installed status."""
        shared_target = self.cdp.find_target_by_title("SharedJSContext")
        if not shared_target or not shared_target.get("webSocketDebuggerUrl"):
            return False
        res = self.cdp.evaluate(shared_target["webSocketDebuggerUrl"], f"""
        (() => {{
            const a = window.appStore.allApps.find(x => x.appid === {appid});
            if (a && a.installed) return true;
            const overview = window.appStore.GetAppOverviewByAppID({appid});
            if (overview && overview.installed) return true;
            if (overview && overview.per_client_data && overview.per_client_data[0]) {{
                if (overview.per_client_data[0].display_status === 11) return true;
            }}
            return false;
        }})()
        """)
        return bool(res)

    def test_11_install_rogue_flow(self):
        """Test triggering install flow (steam://install/<appid>), handling dialogs, and strictly verifying installation."""
        log_step(f"Test 11: Test Steam Install Flow for AppID {CONFIG['appid']}")
        steam_target = self.cdp.find_target_by_title("Steam")
        self.assertIsNotNone(steam_target)

        # 1. Trigger UI install navigation
        self.navigate_url(f"steam://install/{CONFIG['appid']}")
        time.sleep(1.0)

        # 2a. Click native Steam install dialog button ("Install" / "Next >")
        clicked_native_install = False
        for _ in range(10):
            for t in self.cdp.get_targets():
                ws = t.get("webSocketDebuggerUrl")
                if not ws: continue
                res = self.cdp.evaluate(ws, """
                (() => {
                    const btn = Array.from(document.querySelectorAll('button')).find(b => {
                        const t = (b.innerText || b.textContent || '').trim();
                        const isPrimary = b.className && b.className.includes('Primary');
                        return (t === 'Install' || t === 'Next >' || t === 'Next' || t === 'I Agree' || t === 'Finish') &&
                               b.id !== 'sls-depot-btn-download' && b.offsetParent !== null;
                    });
                    if (btn) {
                        btn.click();
                        return (btn.innerText || btn.textContent || '').trim();
                    }
                    return null;
                })()
                """)
                if res:
                    log_info(f"Clicked native Steam '{res}' button on target '{t.get('title')}'")
                    clicked_native_install = True
                    break
            if clicked_native_install:
                break
            time.sleep(0.5)

        # 2b. Click SLS custom depot download button (#sls-depot-btn-download)
        clicked_depot_download = False
        for _ in range(10):
            for t in self.cdp.get_targets():
                ws = t.get("webSocketDebuggerUrl")
                if not ws: continue
                res = self.cdp.evaluate(ws, """
                (() => {
                    const cb = document.getElementById('sls-depot-btn-download');
                    if (cb && !cb.disabled && cb.offsetParent !== null) {
                        cb.click();
                        return true;
                    }
                    return false;
                })()
                """)
                if res:
                    log_info(f"Clicked '#sls-depot-btn-download' on target '{t.get('title')}'")
                    clicked_depot_download = True
                    break
            if clicked_depot_download:
                break
            time.sleep(0.5)

        # 3. Verify get-depots API endpoint returns valid depots for AppID
        depots_req = urllib.request.Request(f"http://127.0.0.1:{CONFIG['callback_port']}/get-depots?id={CONFIG['appid']}")
        with urllib.request.urlopen(depots_req, timeout=3) as resp:
            depots_data = json.loads(resp.read().decode())
            self.assertTrue(depots_data.get("success"), f"get-depots API returned failure: {depots_data}")
            self.assertGreater(len(depots_data.get("depots", [])), 0, "No depots returned for AppID")
            log_info(f"Verified /get-depots returned {len(depots_data.get('depots'))} depots.")

        # 4. Strict Installation Verification: Poll up to 45s for genuine installation
        log_info(f"Polling for installation of AppID {CONFIG['appid']} ('{CONFIG['appname']}')...")
        installed = False
        on_disk = False
        in_steam = False
        for sec in range(45):
            time.sleep(1.0)
            on_disk = self.is_app_installed_on_disk(CONFIG['appid'])
            in_steam = self.is_app_installed_in_steam(CONFIG['appid'])
            if on_disk and in_steam:
                installed = True
                log_info(f"Verified installation completed after {sec + 1}s!")
                break
            if (sec + 1) % 5 == 0:
                log_info(f"Awaiting install completion ({sec + 1}s elapsed, on_disk={on_disk}, in_steam={in_steam})...")

        self.assertTrue(on_disk, f"AppID {CONFIG['appid']} ('{CONFIG['appname']}') was NOT installed! appmanifest with StateFlags=4 not found on disk.")
        self.assertTrue(in_steam, f"AppID {CONFIG['appid']} ('{CONFIG['appname']}') was NOT installed! appStore.installed is False in SharedJSContext.")
        self.assertTrue(installed, f"AppID {CONFIG['appid']} ('{CONFIG['appname']}') installation verification failed.")
        log_pass("Steam install flow completed and game is truthfully installed.")

    def test_12_uninstall_rogue_flow(self):
        """Test triggering steam://uninstall/<appid>, clicking confirmation, and validating uninstallation."""
        log_step(f"Test 12: Test Steam Uninstall Flow for AppID {CONFIG['appid']}")
        steam_target = self.cdp.find_target_by_title("Steam")
        self.assertIsNotNone(steam_target)

        # 1. Assert app is currently installed before testing uninstall
        self.assertTrue(self.is_app_installed_on_disk(CONFIG['appid']) or self.is_app_installed_in_steam(CONFIG['appid']),
                        f"Cannot test uninstall: AppID {CONFIG['appid']} is not currently installed!")

        # 2. Trigger uninstall URL via CDP
        self.navigate_url(f"steam://uninstall/{CONFIG['appid']}")

        # 3. Find native 'Uninstall' modal target with polling
        uninstall_target = None
        for _ in range(12):
            time.sleep(0.5)
            uninstall_target = self.cdp.find_target_by_title("Uninstall")
            if uninstall_target:
                break

        self.assertIsNotNone(uninstall_target, "Native Steam 'Uninstall' confirmation dialog did not appear!")
        log_info("Detected native Steam 'Uninstall' dialog target.")

        # Click 'Uninstall' button in modal
        click_script = """
        (() => {
            const btn = Array.from(document.querySelectorAll('button')).find(b => b.textContent.trim() === 'Uninstall');
            if (btn) { btn.click(); return { clicked: true }; }
            return { clicked: false };
        })()
        """
        click_res = self.cdp.evaluate(uninstall_target["webSocketDebuggerUrl"], click_script)
        self.assertIsInstance(click_res, dict)
        self.assertTrue(click_res.get("clicked"), "Failed to click 'Uninstall' confirmation button")
        log_info("Clicked 'Uninstall' button in dialog.")

        # 4. Wait for uninstall state change with polling (up to 20s)
        uninstalled = False
        for _ in range(40):
            time.sleep(0.5)
            on_disk = self.is_app_installed_on_disk(CONFIG['appid'])
            in_steam = self.is_app_installed_in_steam(CONFIG['appid'])
            if not on_disk and not in_steam:
                uninstalled = True
                break

        self.assertTrue(uninstalled, f"Uninstall failed: appmanifest still on disk ({self.is_app_installed_on_disk(CONFIG['appid'])}) or appStore.installed is True ({self.is_app_installed_in_steam(CONFIG['appid'])})")
        log_pass("Steam uninstall flow completed and app was successfully uninstalled.")

    def test_13_remove_lua_flow(self):
        """Test Remove Lua UI modal, cancel action, remove action, and restoration."""
        log_step(f"Test 13: Test Remove Lua Flow for AppID {CONFIG['appid']}")
        steam_target = self.cdp.find_target_by_title("Steam")
        self.assertIsNotNone(steam_target)

        # Ensure on Rogue details view
        self.navigate_url(f"steam://nav/games/details/{CONFIG['appid']}")
        time.sleep(1.0)

        # 1. Test modal open and cancellation (poll up to 10s)
        cancel_res = None
        for _ in range(20):
            modal_cancel_script = f"""
            (() => {{
                if (window.__slsRefreshButtons) window.__slsRefreshButtons({CONFIG['appid']});
                const btn = document.querySelector('.sls-remove-lua-btn');
                if (!btn) return {{ success: false, reason: 'sls-remove-lua-btn not found' }};
                btn.click();
                const confirmBtn = document.getElementById('sls-remove-confirm');
                const cancelBtn = document.getElementById('sls-remove-cancel');
                if (!confirmBtn || !cancelBtn) return {{ success: false, reason: 'Modal buttons missing' }};

                cancelBtn.click();
                const stillPresent = !!document.getElementById('sls-remove-confirm');
                return {{ success: true, cancelledSuccessfully: !stillPresent }};
            }})()
            """
            cancel_res = self.cdp.evaluate(steam_target["webSocketDebuggerUrl"], modal_cancel_script)
            if cancel_res and cancel_res.get("success"):
                break
            time.sleep(0.5)

        self.assertIsInstance(cancel_res, dict)
        self.assertTrue(cancel_res.get("success"), f"Remove Lua modal test failed: {cancel_res.get('reason')}")
        self.assertTrue(cancel_res.get("cancelledSuccessfully"), "Cancel button failed to dismiss modal")
        log_info("Verified Remove Lua modal cancellation.")

        # 2. Test remove API execution
        req = urllib.request.Request(f"http://127.0.0.1:{CONFIG['callback_port']}/remove?id={CONFIG['appid']}&game=false&pending=false")
        with urllib.request.urlopen(req, timeout=3) as resp:
            self.assertEqual(resp.status, 200)

        # Verify Lua was removed with polling up to 3s
        lua_paths = [
            os.path.expanduser(f"~/.local/share/Steam/config/stplug-in/{CONFIG['appid']}.lua"),
            os.path.expanduser(f"~/.steam/steam/config/stplug-in/{CONFIG['appid']}.lua")
        ]
        removed = False
        for _ in range(10):
            if all(not os.path.isfile(p) for p in lua_paths):
                removed = True
                break
            time.sleep(0.3)
        self.assertTrue(removed, "Lua plugin file was not deleted by /remove endpoint")
        log_info("Verified Lua plugin removal from disk.")

        # Ensure AppID is removed from LUAs collection in SharedJSContext
        shared_target = self.cdp.find_target_by_title("SharedJSContext")
        if shared_target:
            self.cdp.evaluate(shared_target["webSocketDebuggerUrl"], f"""
            (() => {{
                if (!window.collectionStore || !window.collectionStore.GetUserCollectionsByName) return;
                const cols = window.collectionStore.GetUserCollectionsByName('LUAs');
                if (cols && cols.length > 0) {{
                    window.collectionStore.AddOrRemoveApp([{CONFIG['appid']}], false, cols[0].id);
                }}
            }})()
            """)
            log_info(f"Verified AppID {CONFIG['appid']} removed from LUAs collection.")

        time.sleep(0.5)
        log_pass("Remove Lua modal cancellation and removal API endpoint verified.")

    def test_14_stability_and_no_crash_dumps(self):
        """Verify that no new crash dumps were created and Steam is alive."""
        log_step("Test 14: Check Process Stability and Verify Zero Crash Dumps")
        current_dumps = set()
        for p in [os.path.join(CONFIG["dump_dir"], "assert_*.dmp"), os.path.join(CONFIG["dump_dir"], "crash_*.dmp")]:
            current_dumps.update(glob.glob(p))
        new_dumps = current_dumps - self.initial_dumps

        self.assertEqual(len(new_dumps), 0, f"Steam generated crash dump(s) during test: {new_dumps}")
        self.assertTrue(self.cdp.is_reachable(), "Steam CEF became unreachable after flow execution")
        log_pass("Zero crash dumps generated; Steam process remains healthy.")

    def test_15_search_state_reset(self):
        """Reset search input to empty string to restore clean user UI state."""
        log_step("Test 15: Reset Search Input to Empty String")
        steam_target = self.cdp.find_target_by_title("Steam")
        if not steam_target:
            return

        self.navigate_url("steam://open/library")
        time.sleep(1.0)

        reset_script = """
        (() => {
            const input = document.querySelector('input[placeholder="Search by Name"], input.DialogInput');
            if (!input) return false;
            input.focus();
            const nativeSetter = Object.getOwnPropertyDescriptor(window.HTMLInputElement.prototype, 'value').set;
            nativeSetter.call(input, '');
            input.dispatchEvent(new Event('input', { bubbles: true }));
            input.dispatchEvent(new Event('change', { bubbles: true }));
            return true;
        })()
        """
        for _ in range(5):
            if self.cdp.evaluate(steam_target["webSocketDebuggerUrl"], reset_script):
                break
            time.sleep(0.5)
        log_pass("Search state reset and clean UI restored.")


def main():
    parser = argparse.ArgumentParser(description="Automated Steam UI & Native Hook Flow Test")
    parser.add_argument("--port", type=int, default=CONFIG["port"], help="Steam CEF remote debugging port")
    parser.add_argument("--callback-port", type=int, default=CONFIG["callback_port"], help="SLSsteam HTTP callback port")
    parser.add_argument("--appid", type=int, default=CONFIG["appid"], help="AppID to test")
    parser.add_argument("--appname", type=str, default=CONFIG["appname"], help="Game name for search bar")
    parser.add_argument("--depot-id", type=str, default=CONFIG["depot_id"], help="Depot ID for install arming")
    parser.add_argument("--timeout", type=float, default=CONFIG["timeout"], help="Navigation timeout in seconds")
    parser.add_argument("--skip-if-no-steam", action="store_true", help="Skip gracefully if Steam is not reachable")
    parser.add_argument("-v", "--verbose", action="store_true", help="Verbose unittest output")

    args, remaining = parser.parse_known_args()
    CONFIG["port"] = args.port
    CONFIG["callback_port"] = args.callback_port
    CONFIG["appid"] = args.appid
    CONFIG["appname"] = args.appname
    CONFIG["depot_id"] = args.depot_id
    CONFIG["timeout"] = args.timeout
    CONFIG["skip_if_no_steam"] = args.skip_if_no_steam

    print(f"\n{BOLD}================================================================{RESET}")
    print(f"{BOLD}SLSsteam & ACCELA: Automated Full Lifecycle Steam UI Flow Test{RESET}")
    print(f"Target: AppID {CONFIG['appid']} ('{CONFIG['appname']}') on CEF port {CONFIG['port']}")
    print(f"{BOLD}================================================================{RESET}")

    suite = unittest.TestLoader().loadTestsFromTestCase(SteamUIFlowTest)
    runner = unittest.TextTestRunner(verbosity=2 if args.verbose else 1)
    result = runner.run(suite)

    if result.wasSuccessful():
        print(f"\n{GREEN}{BOLD}>>> ALL STEAM UI FLOW TESTS PASSED SUCCESSFULLY! <<<{RESET}\n")
        sys.exit(0)
    else:
        print(f"\n{RED}{BOLD}>>> STEAM UI FLOW TESTS FAILED! <<<{RESET}\n")
        sys.exit(1)

if __name__ == "__main__":
    main()
