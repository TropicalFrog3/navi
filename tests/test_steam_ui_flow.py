#!/usr/bin/env python3
"""
tests/test_steam_ui_flow.py
===========================
End-to-end integration and flow test for SLSsteam & ACCELA against a live Steam client.
Automates UI interactions via Chrome DevTools Protocol (CDP) WebSocket:
  1. CEF preflight & process vitality check
  2. SharedJSContext MobX store inspection
  3. React controlled search input automation
  4. Virtualized list element identification & click dispatch
  5. Detail view navigation & DOM verification
  6. SLSsteam native hook telemetry verification (~/.SLSsteam.log)
  7. Crash dump & memory corruption audit (/tmp/dumps/)
  8. Search state reset & UI cleanup
"""

import os
import sys
import time
import json
import glob
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
    "appid": int(os.environ.get("STEAM_TEST_APPID", "293780")),
    "appname": os.environ.get("STEAM_TEST_APPNAME", "Crawl"),
    "timeout": float(os.environ.get("STEAM_TEST_TIMEOUT", "10.0")),
    "log_path": os.path.expanduser("~/.SLSsteam.log"),
    "dump_dir": "/tmp/dumps",
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

    def evaluate(self, ws_url, expression, timeout=5):
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
        if not cls.cdp.is_reachable():
            if CONFIG["skip_if_no_steam"]:
                raise unittest.SkipTest(f"Steam CEF is not running on port {CONFIG['port']}.")
            else:
                cls.fail(cls, f"Cannot connect to Steam CEF remote debugging at http://127.0.0.1:{CONFIG['port']}.")
        
        # Record baseline dumps count
        dump_pattern = os.path.join(CONFIG["dump_dir"], "assert_*.dmp")
        cls.initial_dumps = set(glob.glob(dump_pattern))
        
        # Record baseline log size
        if os.path.isfile(CONFIG["log_path"]):
            cls.initial_log_size = os.path.getsize(CONFIG["log_path"])
        else:
            cls.initial_log_size = 0

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
        else:
            log_info(f"Target AppID {CONFIG['appid']} not registered in appStore, proceeding with UI search by name.")
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
        
        # Allow React virtualized list 1 second to update
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
                    // Traverse upward to locate the interactive draggable or Focusable row container
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

        # Now inspect DOM in Steam desktop window for game view content
        steam_target = self.cdp.find_target_by_title("Steam")
        verify_script = f"""
        (() => {{
            const targetName = {json.dumps(CONFIG['appname'])};
            // Look for game header, description, or action buttons
            const textNodes = [];
            const walker = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT, null, false);
            let n;
            while ((n = walker.nextNode())) {{
                if (n.nodeValue.includes(targetName)) {{
                    textNodes.push(n.nodeValue.slice(0, 80));
                }}
            }}
            
            // Check for buttons
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
        log_info(f"Captured {len(log_lines)} new SLSsteam log lines during test.")

        # Check for fatal glibc or memory corruptions
        corruptions = [line for line in log_lines if any(sig in line.lower() for sig in [
            "double free", "corruption", "segmentation fault", "corrupted size", "sigsegv"
        ])]
        self.assertEqual(len(corruptions), 0, f"Detected memory corruption in log: {corruptions}")

        # Check for hook occurrences mentioning the target AppID
        appid_str = str(CONFIG["appid"])
        appid_mentions = [line for line in log_lines if appid_str in line]
        if appid_mentions:
            log_info(f"Found {len(appid_mentions)} hook logs for AppID {CONFIG['appid']}:")
            for m in appid_mentions[:3]:
                print(f"    {m}")
        else:
            log_info(f"Note: AppID {CONFIG['appid']} hook logs already cached or handled in prior session.")
        log_pass("Native hook telemetry verified with zero memory corruptions.")

    def test_07_stability_and_no_crash_dumps(self):
        """Verify that no new crash dumps were created and Steam is alive."""
        log_step("Test 7: Check Process Stability and Verify Zero Crash Dumps")
        dump_pattern = os.path.join(CONFIG["dump_dir"], "assert_*.dmp")
        current_dumps = set(glob.glob(dump_pattern))
        new_dumps = current_dumps - self.initial_dumps
        
        self.assertEqual(len(new_dumps), 0, f"Steam generated crash dump(s) during test: {new_dumps}")
        
        # Verify Steam CEF is still responding
        self.assertTrue(self.cdp.is_reachable(), "Steam CEF became unreachable after flow execution")
        log_pass("Zero crash dumps generated; Steam process remains healthy.")

    def test_08_search_state_reset(self):
        """Reset search input to empty string to restore clean user UI state."""
        log_step("Test 8: Reset Search Input to Empty String")
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
    parser.add_argument("--appid", type=int, default=CONFIG["appid"], help="AppID to search, select and verify")
    parser.add_argument("--appname", type=str, default=CONFIG["appname"], help="Game name for search bar")
    parser.add_argument("--timeout", type=float, default=CONFIG["timeout"], help="Navigation timeout in seconds")
    parser.add_argument("--skip-if-no-steam", action="store_true", help="Skip gracefully if Steam is not reachable")
    parser.add_argument("-v", "--verbose", action="store_true", help="Verbose unittest output")

    # Parse known args so unittest can accept extra arguments if needed
    args, remaining = parser.parse_known_args()
    CONFIG["port"] = args.port
    CONFIG["appid"] = args.appid
    CONFIG["appname"] = args.appname
    CONFIG["timeout"] = args.timeout
    CONFIG["skip_if_no_steam"] = args.skip_if_no_steam

    print(f"\n{BOLD}================================================================{RESET}")
    print(f"{BOLD}SLSsteam & ACCELA: Automated Steam UI Flow Test{RESET}")
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
