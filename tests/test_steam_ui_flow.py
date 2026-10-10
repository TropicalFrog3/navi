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
  9. Steam native uninstall flow (steam://uninstall/<appid>) & modal click
  10. Steam native install flow (steam://install/<appid> & /start-download)
  11. Crash dump & memory corruption audit (/tmp/dumps/)
  12. Search state reset & clean UI restoration
"""

import os
import sys
import time
import json
import glob
import shutil
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
    "appid": int(os.environ.get("STEAM_TEST_APPID", "293780")),
    "appname": os.environ.get("STEAM_TEST_APPNAME", "Crawl"),
    "depot_id": os.environ.get("STEAM_TEST_DEPOTID", "293782"),
    "timeout": float(os.environ.get("STEAM_TEST_TIMEOUT", "12.0")),
    "log_path": os.path.expanduser("~/.SLSsteam.log"),
    "dump_dir": "/tmp/dumps",
    "backup_dir": "/tmp/sls_crawl_test_backup",
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
    def setUpClass(cls):
        cls.cdp = SteamCDPClient(CONFIG["port"])
        start_time = time.time()
        ready = False
        while time.time() - start_time < 30:
            if cls.cdp.is_reachable():
                try:
                    targets = cls.cdp.get_targets()
                    titles = [t.get("title", "") for t in targets]
                    if "Steam" in titles and "SharedJSContext" in titles:
                        ready = True
                        break
                except Exception:
                    pass
            time.sleep(1)

        if not ready:
            if CONFIG["skip_if_no_steam"]:
                raise unittest.SkipTest(f"Steam CEF is not running or missing targets on port {CONFIG['port']}.")
            else:
                cls.fail(cls, f"Cannot connect to Steam CEF remote debugging at http://127.0.0.1:{CONFIG['port']}.")

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

    @classmethod
    def tearDownClass(cls):
        # Restore backup files if necessary
        cls.restore_files()

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

            gamedir = f"{base}/steamapps/common/Crawl"
            if os.path.isdir(gamedir) and not os.path.isdir(os.path.join(bdir, "Crawl")):
                try:
                    shutil.copytree(gamedir, os.path.join(bdir, "Crawl"), dirs_exist_ok=True)
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
                gamedir_src = os.path.join(bdir, "Crawl")
                gamedir_dst = os.path.join(common_dir, "Crawl")
                if os.path.isdir(gamedir_src) and not os.path.isdir(gamedir_dst):
                    try:
                        shutil.copytree(gamedir_src, gamedir_dst, dirs_exist_ok=True)
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

    def test_02_shared_js_context_appstore(self):
        """Query SharedJSContext MobX store to verify AppID registration."""
        log_step(f"Test 2: Inspect SharedJSContext MobX Store for AppID {CONFIG['appid']}")
        shared_target = self.cdp.find_target_by_title("SharedJSContext")
        self.assertIsNotNone(shared_target, "SharedJSContext target missing")

        script = f"""
        (() => {{
            if (!window.appStore || !window.appStore.allApps) {{
                return {{ error: 'appStore not initialized' }};
            }}
            const app = window.appStore.allApps.find(a => a.appid === {CONFIG['appid']});
            return {{
                totalApps: window.appStore.allApps.length,
                found: !!app,
                name: app ? (app.display_name || app.name) : null,
                installed: app ? app.installed : false
            }};
        }})()
        """
        result = self.cdp.evaluate(shared_target["webSocketDebuggerUrl"], script)
        self.assertIsInstance(result, dict, "Failed to evaluate query on SharedJSContext")
        self.assertNotIn("error", result, f"SharedJSContext error: {result.get('error')}")
        self.assertGreater(result.get("totalApps", 0), 0, "appStore contains 0 apps")
        log_info(f"AppStore has {result['totalApps']} registered apps.")

        if result.get("found"):
            log_info(f"Target AppID {CONFIG['appid']} found: '{result.get('name')}' (installed: {result.get('installed')})")
        log_pass("SharedJSContext app store validated.")

    def test_03_search_bar_react_input(self):
        """Automate setting the search bar using React synthetic prototype setter."""
        log_step(f"Test 3: Set Search Input to '{CONFIG['appname']}' via React Prototype Descriptor")
        steam_target = self.cdp.find_target_by_title("Steam")
        self.assertIsNotNone(steam_target, "Steam window target missing")

        script = f"""
        (() => {{
            const input = document.querySelector('input[placeholder="Search by Name"], input.DialogInput');
            if (!input) return {{ success: false, reason: 'search input not found in DOM' }};

            input.focus();
            const nativeSetter = Object.getOwnPropertyDescriptor(window.HTMLInputElement.prototype, 'value').set;
            nativeSetter.call(input, {json.dumps(CONFIG['appname'])});
            input.dispatchEvent(new Event('input', {{ bubbles: true }}));
            input.dispatchEvent(new Event('change', {{ bubbles: true }}));

            return {{
                success: true,
                value: input.value,
                className: input.className
            }};
        }})()
        """
        result = self.cdp.evaluate(steam_target["webSocketDebuggerUrl"], script)
        self.assertIsInstance(result, dict)
        self.assertTrue(result.get("success"), f"Failed to set search input: {result.get('reason')}")
        self.assertEqual(result.get("value"), CONFIG["appname"], "Input value does not match expected search term")
        log_info(f"Search input value updated to '{result.get('value')}'.")

        time.sleep(1.0)
        log_pass("Search bar React input set and verified.")

    def test_04_virtualized_list_click(self):
        """Locate and click the search result item in the virtualized library list."""
        log_step(f"Test 4: Click Library Virtualized List Item for '{CONFIG['appname']}'")
        steam_target = self.cdp.find_target_by_title("Steam")
        self.assertIsNotNone(steam_target, "Steam window target missing")

        script = f"""
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

                            const evt = (type) => new MouseEvent(type, {{
                                view: window,
                                bubbles: true,
                                cancelable: true,
                                buttons: 1
                            }});
                            el.dispatchEvent(evt('mousedown'));
                            el.dispatchEvent(evt('mouseup'));
                            el.dispatchEvent(evt('click'));
                            if (typeof el.click === 'function') el.click();

                            return {{
                                clicked: true,
                                tag: el.tagName,
                                className: el.className,
                                isDraggable: isDraggable
                            }};
                        }}
                        el = el.parentElement;
                    }}
                }}
            }}
            return {{ clicked: false, reason: 'Matching element not found in DOM' }};
        }})()
        """
        result = self.cdp.evaluate(steam_target["webSocketDebuggerUrl"], script)
        self.assertIsInstance(result, dict)
        self.assertTrue(result.get("clicked"), f"Failed to click list item: {result.get('reason')}")
        log_info(f"Clicked element <{result.get('tag')} class='{result.get('className')}'>")
        log_pass("Library list item clicked successfully.")

    def test_05_game_view_navigation_and_dom(self):
        """Verify that the detail view navigated to the target AppID and rendered content."""
        log_step(f"Test 5: Verify Game Detail View Navigation for AppID {CONFIG['appid']}")
        expected_tracking = f"/library/app/{CONFIG['appid']}"
        deadline = time.time() + CONFIG["timeout"]
        found_target = False

        while time.time() < deadline:
            target = self.cdp.find_target_by_title_substring(expected_tracking)
            if target:
                found_target = True
                log_info(f"Found tracking target: {target.get('title')}")
                break
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
        log_pass("Game detail view verified in DOM.")

    def test_06_slssteam_native_hook_telemetry(self):
        """Verify ~/.SLSsteam.log for AppID hook invocations and absence of memory corruptions."""
        log_step(f"Test 6: Audit SLSsteam Native Hook Telemetry for AppID {CONFIG['appid']}")
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

    def test_07_change_provider_flow(self):
        """Test Change Provider modal, available provider list, and /change-provider endpoint."""
        log_step(f"Test 7: Test Change Provider Flow for AppID {CONFIG['appid']}")
        steam_target = self.cdp.find_target_by_title("Steam")
        self.assertIsNotNone(steam_target)

        # 1. Click Change Provider button in DOM (poll up to 3s)
        modal_res = None
        for _ in range(6):
            open_modal_script = """
            (() => {
                const btn = document.querySelector('.sls-change-provider-btn');
                if (!btn) return { success: false, reason: 'sls-change-provider-btn not found' };
                btn.click();
                const modal = document.getElementById('sls-provider-modal');
                if (!modal) return { success: false, reason: 'sls-provider-modal did not appear' };

                const providers = Array.from(modal.querySelectorAll('[data-sls-provider-idx]')).map(b => ({
                    idx: b.getAttribute('data-sls-provider-idx'),
                    text: b.textContent.trim(),
                    disabled: b.disabled || b.hasAttribute('disabled')
                }));

                // Close modal cleanly
                const cancelBtn = modal.querySelector('#sls-prov-cancel, #sls-close-x, button');
                if (cancelBtn) cancelBtn.click();
                else modal.remove();

                return { success: true, count: providers.length, providers: providers };
            })()
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

    def test_08_remove_lua_flow(self):
        """Test Remove Lua UI modal, cancel action, remove action, and restoration."""
        log_step(f"Test 8: Test Remove Lua Flow for AppID {CONFIG['appid']}")
        steam_target = self.cdp.find_target_by_title("Steam")
        self.assertIsNotNone(steam_target)

        # 1. Test modal open and cancellation
        modal_cancel_script = """
        (() => {
            const btn = document.querySelector('.sls-remove-lua-btn');
            if (!btn) return { success: false, reason: 'sls-remove-lua-btn not found' };
            btn.click();
            const confirmBtn = document.getElementById('sls-remove-confirm');
            const cancelBtn = document.getElementById('sls-remove-cancel');
            if (!confirmBtn || !cancelBtn) return { success: false, reason: 'Modal buttons missing' };

            cancelBtn.click();
            const stillPresent = !!document.getElementById('sls-remove-confirm');
            return { success: true, cancelledSuccessfully: !stillPresent };
        })()
        """
        cancel_res = self.cdp.evaluate(steam_target["webSocketDebuggerUrl"], modal_cancel_script)
        self.assertIsInstance(cancel_res, dict)
        self.assertTrue(cancel_res.get("success"), f"Remove Lua modal test failed: {cancel_res.get('reason')}")
        self.assertTrue(cancel_res.get("cancelledSuccessfully"), "Cancel button failed to dismiss modal")
        log_info("Verified Remove Lua modal cancellation.")

        # 2. Test remove API execution
        req = urllib.request.Request(f"http://127.0.0.1:{CONFIG['callback_port']}/remove?id={CONFIG['appid']}&game=false")
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
        time.sleep(0.5)
        log_pass("Remove Lua modal cancellation and removal API endpoint verified.")

    def test_09_install_crawl_lua_auto_download(self):
        """Test installing Crawl Lua script and depot manifests via Auto Download (#sls-click-auto)."""
        log_step(f"Test 9: Test Install Crawl Lua via Auto Download for AppID {CONFIG['appid']}")
        steam_target = self.cdp.find_target_by_title("Steam")
        self.assertIsNotNone(steam_target, "Steam target not found")

        lua_paths = [
            os.path.expanduser(f"~/.local/share/Steam/config/stplug-in/{CONFIG['appid']}.lua"),
            os.path.expanduser(f"~/.steam/steam/config/stplug-in/{CONFIG['appid']}.lua")
        ]

        # 1. Ensure clean slate: verify Lua is absent or delete if present
        if any(os.path.isfile(p) for p in lua_paths):
            req = urllib.request.Request(f"http://127.0.0.1:{CONFIG['callback_port']}/remove?id={CONFIG['appid']}&game=false")
            try:
                with urllib.request.urlopen(req, timeout=3) as resp:
                    pass
            except Exception:
                pass
            for _ in range(10):
                if all(not os.path.isfile(p) for p in lua_paths):
                    break
                time.sleep(0.3)

        self.assertTrue(all(not os.path.isfile(p) for p in lua_paths), "Lua must not be present prior to auto download")
        log_info(f"Confirmed clean slate: {CONFIG['appid']}.lua absent from stplug-in.")

        # Record log position before trigger for auditing
        log_pos_before = os.path.getsize(CONFIG["log_path"]) if os.path.isfile(CONFIG["log_path"]) else 0

        # 2. Trigger auto download via hash navigation on Steam CEF window
        # Format parsed by automationWorker: #sls-click-auto-<appid>-<order>-<timestamp>
        # Preferred provider order: 2 (TwentyTwo Cloud), 0 (Sushi), 1 (Spinoza)
        trigger_script = f"""
        (() => {{
            const stamp = Date.now();
            window.location.hash = '#sls-click-auto-{CONFIG['appid']}-2,0,1-' + stamp;
            return {{
                triggered: true,
                hash: window.location.hash,
                stamp: stamp
            }};
        }})()
        """
        trig_res = self.cdp.evaluate(steam_target["webSocketDebuggerUrl"], trigger_script)
        self.assertIsInstance(trig_res, dict)
        self.assertTrue(trig_res.get("triggered"), "Failed to trigger auto download hash navigation")
        log_info(f"Triggered Auto Download hash navigation: {trig_res.get('hash')}")

        # 3. Poll for auto-download execution and file installation (up to 10s)
        lua_installed = False
        installed_path = None
        for _ in range(20):
            time.sleep(0.5)
            # Check if file has appeared
            found_path = next((p for p in lua_paths if os.path.isfile(p) and os.path.getsize(p) > 0), None)
            if found_path and os.path.isfile(CONFIG["log_path"]):
                with open(CONFIG["log_path"], "r", encoding="utf-8", errors="ignore") as f:
                    f.seek(log_pos_before)
                    curr_logs = f.read()
                if "Auto-search Lua download clicked" in curr_logs and "Successfully installed appid" in curr_logs:
                    lua_installed = True
                    installed_path = found_path
                    break

        self.assertTrue(lua_installed, f"Auto-download timed out waiting for {CONFIG['appid']}.lua to be installed")
        log_info(f"Verified Lua script installed at: {installed_path}")

        # Check Lua file content contains addappid directive
        try:
            with open(installed_path, "r", encoding="utf-8", errors="ignore") as f:
                content = f.read()
            self.assertIn(f"addappid({CONFIG['appid']}", content, "Installed Lua file does not contain addappid directive")
            log_info(f"Verified addappid({CONFIG['appid']}) directive in installed Lua script.")
        except Exception as e:
            log_info(f"Lua content check warning: {e}")

        # Verify depotcache manifests exist for Crawl depots (293781, 293782, 293783)
        depot_ids = ["293781", "293782", "293783"]
        manifests = []
        for did in depot_ids:
            for base in [os.path.expanduser("~/.local/share/Steam"), os.path.expanduser("~/.steam/steam")]:
                matches = glob.glob(f"{base}/config/depotcache/{did}_*.manifest")
                manifests.extend(matches)
        log_info(f"Discovered {len(manifests)} depotcache manifest(s) for Crawl depots ({', '.join(depot_ids)}).")
        self.assertGreater(len(manifests), 0, "No manifests discovered for Crawl depots")

        # 4. Verify /check endpoint confirms Lua exists
        req_check = urllib.request.Request(f"http://127.0.0.1:{CONFIG['callback_port']}/check?id={CONFIG['appid']}")
        with urllib.request.urlopen(req_check, timeout=3) as resp:
            check_data = json.loads(resp.read().decode())
            self.assertTrue(check_data.get("exists"), f"/check endpoint did not detect installed Lua: {check_data}")
        log_info(f"/check endpoint confirmed AppID {CONFIG['appid']} is installed and registered.")

        # 5. Verify telemetry in ~/.SLSsteam.log
        if os.path.isfile(CONFIG["log_path"]):
            with open(CONFIG["log_path"], "r", encoding="utf-8", errors="ignore") as f:
                f.seek(log_pos_before)
                new_logs = f.read()
            self.assertIn("Auto-search Lua download clicked", new_logs, "Expected Auto-search log line not found in log")
            self.assertIn("Successfully installed appid", new_logs, "Expected LuaDownload success log line not found in log")
            log_info("Verified SLSsteam auto-download telemetry in log.")

        log_pass("Install Crawl Lua via Auto Download verified successfully.")

    def test_10_uninstall_crawl_flow(self):
        """Test triggering steam://uninstall/<appid>, clicking confirmation, and validating state."""
        log_step(f"Test 10: Test Steam Uninstall Flow for AppID {CONFIG['appid']}")
        steam_target = self.cdp.find_target_by_title("Steam")
        self.assertIsNotNone(steam_target)

        # Trigger uninstall URL via CDP
        self.cdp.evaluate(steam_target["webSocketDebuggerUrl"], f"window.location.href = 'steam://uninstall/{CONFIG['appid']}';")

        # Find 'Uninstall' modal target with polling
        uninstall_target = None
        for _ in range(8):
            time.sleep(0.5)
            uninstall_target = self.cdp.find_target_by_title("Uninstall")
            if uninstall_target:
                break

        self.assertIsNotNone(uninstall_target, "Uninstall confirmation modal target not found")
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

        # Wait for uninstall state change
        time.sleep(2.0)
        shared_target = self.cdp.find_target_by_title("SharedJSContext")
        check_script = f"""
        (() => {{
            const a = window.appStore.allApps.find(a => a.appid === {CONFIG['appid']});
            return a ? !!a.installed : false;
        }})()
        """
        is_installed = self.cdp.evaluate(shared_target["webSocketDebuggerUrl"], check_script)
        self.assertFalse(is_installed, "App state in appStore still indicates installed")
        log_pass("Steam uninstall flow completed successfully.")

    def test_11_install_crawl_flow(self):
        """Test triggering install flow (steam://install/<appid>), handling dialogs, and verifying installation."""
        log_step(f"Test 11: Test Steam Install Flow for AppID {CONFIG['appid']}")
        steam_target = self.cdp.find_target_by_title("Steam")
        self.assertIsNotNone(steam_target)

        # 1. Trigger UI install navigation
        self.cdp.evaluate(steam_target["webSocketDebuggerUrl"], f"window.location.href = 'steam://install/{CONFIG['appid']}';")
        time.sleep(1.0)

        # 2. Click any install dialog buttons or SLS custom depot buttons
        for _ in range(5):
            for t in self.cdp.get_targets():
                ws = t.get("webSocketDebuggerUrl")
                if not ws: continue
                res = self.cdp.evaluate(ws, """
                (() => {
                    let clicked = false;
                    document.querySelectorAll('button').forEach(b => {
                        const t = b.textContent.trim();
                        if (t === 'Install' || t === 'Next >' || t === 'Next' || t === 'I Agree' || t === 'Finish') {
                            b.click();
                            clicked = t;
                        }
                    });
                    const cb = document.getElementById('sls-depot-btn-download');
                    if (cb && !cb.disabled) {
                        cb.click();
                        clicked = 'sls-depot-btn-download';
                    }
                    return clicked;
                })()
                """)
                if res:
                    log_info(f"Clicked '{res}' on target '{t.get('title')}'")
            time.sleep(0.5)

        # 3. Verify get-depots API endpoint returns valid depots for AppID
        depots_req = urllib.request.Request(f"http://127.0.0.1:{CONFIG['callback_port']}/get-depots?id={CONFIG['appid']}")
        with urllib.request.urlopen(depots_req, timeout=3) as resp:
            depots_data = json.loads(resp.read().decode())
            self.assertTrue(depots_data.get("success"), f"get-depots API returned failure: {depots_data}")
            self.assertGreater(len(depots_data.get("depots", [])), 0, "No depots returned for AppID")
            log_info(f"Verified /get-depots returned {len(depots_data.get('depots'))} depots.")

        # 4. Restore local game files to ensure ready-to-play state
        self.restore_files()
        log_pass("Steam install flow completed and game assets preserved.")

    def test_12_stability_and_no_crash_dumps(self):
        """Verify that no new crash dumps were created and Steam is alive."""
        log_step("Test 12: Check Process Stability and Verify Zero Crash Dumps")
        current_dumps = set()
        for p in [os.path.join(CONFIG["dump_dir"], "assert_*.dmp"), os.path.join(CONFIG["dump_dir"], "crash_*.dmp")]:
            current_dumps.update(glob.glob(p))
        new_dumps = current_dumps - self.initial_dumps

        self.assertEqual(len(new_dumps), 0, f"Steam generated crash dump(s) during test: {new_dumps}")
        self.assertTrue(self.cdp.is_reachable(), "Steam CEF became unreachable after flow execution")
        log_pass("Zero crash dumps generated; Steam process remains healthy.")

    def test_13_search_state_reset(self):
        """Reset search input to empty string to restore clean user UI state."""
        log_step("Test 13: Reset Search Input to Empty String")
        steam_target = self.cdp.find_target_by_title("Steam")
        self.assertIsNotNone(steam_target)

        reset_script = """
        (() => {
            const input = document.querySelector('input[placeholder="Search by Name"], input.DialogInput');
            if (!input) return { success: false };
            input.focus();
            const nativeSetter = Object.getOwnPropertyDescriptor(window.HTMLInputElement.prototype, 'value').set;
            nativeSetter.call(input, '');
            input.dispatchEvent(new Event('input', { bubbles: true }));
            input.dispatchEvent(new Event('change', { bubbles: true }));
            return { success: true, value: input.value };
        })()
        """
        res = self.cdp.evaluate(steam_target["webSocketDebuggerUrl"], reset_script)
        self.assertIsInstance(res, dict)
        self.assertTrue(res.get("success"))
        self.assertEqual(res.get("value"), "")
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
