#!/usr/bin/env python3
"""
tests/inspect_store_ui.py
=========================
Interactive CLI and diagnostic harness for inspecting Steam CEF targets,
navigating to the Steam Store, and extracting DOM elements/selectors for
UI unit tests.

Usage examples:
  # Check connection and list targets
  python3 tests/inspect_store_ui.py targets

  # Navigate Steam to Store page for AppID (default 1443430)
  python3 tests/inspect_store_ui.py nav --appid 1443430

  # Search for buttons or elements on the Store page
  python3 tests/inspect_store_ui.py dump --filter "Lua"
  python3 tests/inspect_store_ui.py dump --selector ".sls-lua-btn, #sls-overlay-modal, button"

  # Query specific DOM expression or details
  python3 tests/inspect_store_ui.py eval "document.title"
  python3 tests/inspect_store_ui.py eval "document.querySelector('.sls-lua-btn')?.outerHTML"

  # Click an element by selector or text
  python3 tests/inspect_store_ui.py click --selector ".sls-lua-btn a"
  python3 tests/inspect_store_ui.py click --text "Download Lua"
"""

import os
import sys
import time
import json
import argparse
import urllib.request
import urllib.error

try:
    import websocket
except ImportError:
    print("[ERROR] 'websocket' module not found. Install via 'pip install websocket-client'.", file=sys.stderr)
    sys.exit(1)

DEFAULT_PORT = int(os.environ.get("STEAM_CDP_PORT", "8080"))
DEFAULT_APPID = int(os.environ.get("STEAM_TEST_APPID", "1443430"))

GREEN = "\033[0;32m"
RED = "\033[0;31m"
YELLOW = "\033[1;33m"
BLUE = "\033[0;34m"
CYAN = "\033[0;36m"
BOLD = "\033[1m"
RESET = "\033[0m"


class SteamCEFInspector:
    def __init__(self, port=DEFAULT_PORT):
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

    def find_target(self, query=None):
        """
        Finds a target matching query (by title or URL substring).
        If query is None, prioritizes store.steampowered.com target, then 'Steam'.
        """
        targets = self.get_targets()
        if not targets:
            return None

        if query:
            q_lower = query.lower()
            # Exact match first
            for t in targets:
                if t.get("title", "").lower() == q_lower:
                    return t
            # Substring match
            for t in targets:
                if q_lower in t.get("title", "").lower() or q_lower in t.get("url", "").lower():
                    return t
            return None

        # Prefer active store page
        for t in targets:
            url = t.get("url", "")
            if "store.steampowered.com" in url and t.get("webSocketDebuggerUrl"):
                return t

        # Fallback to main desktop Steam window
        for t in targets:
            if t.get("title") == "Steam" and t.get("webSocketDebuggerUrl"):
                return t

        # Fallback to Big Picture Mode
        for t in targets:
            if t.get("title") == "Steam Big Picture Mode" and t.get("webSocketDebuggerUrl"):
                return t

        # First target with websocket debugger url that is not SharedJSContext
        for t in targets:
            if t.get("title") != "SharedJSContext" and t.get("webSocketDebuggerUrl"):
                return t

        return targets[0] if targets else None

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
                return {
                    "success": False,
                    "error": result["exceptionDetails"].get("text", "Runtime Exception"),
                    "details": result["exceptionDetails"]
                }
            return {
                "success": True,
                "value": result.get("result", {}).get("value")
            }
        except Exception as e:
            return {"success": False, "error": str(e)}
        finally:
            if ws:
                try:
                    ws.close()
                except Exception:
                    pass


def cmd_targets(inspector, args):
    if not inspector.is_reachable():
        print(f"{RED}[FAIL]{RESET} Steam CEF debugging port {inspector.port} is not reachable.")
        print(f"{YELLOW}[HINT]{RESET} Start Steam with -cef-enable-debugging or check QEMU VM.")
        return 1

    targets = inspector.get_targets()
    print(f"\n{BOLD}Discovered {len(targets)} active CEF target(s) on port {inspector.port}:{RESET}\n")
    for idx, t in enumerate(targets):
        title = t.get("title", "<No Title>")
        url = t.get("url", "<No URL>")
        ttype = t.get("type", "page")
        ws = t.get("webSocketDebuggerUrl", "")
        has_ws = f"{GREEN}WS Connected{RESET}" if ws else f"{RED}No WS{RESET}"
        print(f" [{idx + 1}] {BOLD}{title}{RESET} ({CYAN}{ttype}{RESET}) - {has_ws}")
        print(f"     URL: {url}")
        if ws:
            print(f"     WS:  {ws}")
        print()
    return 0


def cmd_nav(inspector, args):
    if not inspector.is_reachable():
        print(f"{RED}[FAIL]{RESET} Steam CEF debugging port {inspector.port} is not reachable.")
        return 1

    steam_target = inspector.find_target("Steam")
    if not steam_target:
        print(f"{RED}[FAIL]{RESET} Main 'Steam' target window not found.")
        return 1

    appid = args.appid
    target_url = f"https://store.steampowered.com/app/{appid}"
    protocol_url = f"steam://store/{appid}"

    print(f"{BLUE}==>{RESET} Navigating to Store for AppID {BOLD}{appid}{RESET}...")
    
    # Try navigating via window.location.href or protocol
    nav_script = f"""
    (() => {{
        window.location.href = '{protocol_url}';
        return {{ method: 'protocol', url: '{protocol_url}' }};
    }})()
    """
    res = inspector.evaluate(steam_target["webSocketDebuggerUrl"], nav_script)
    print(f"{GREEN}[PASS]{RESET} Dispatched store navigation: {res}")

    # Wait for store page target to emerge
    print(f"{CYAN}[INFO]{RESET} Polling for Store target (up to 10s)...")
    store_target = None
    for _ in range(20):
        time.sleep(0.5)
        store_target = inspector.find_target("store.steampowered.com")
        if store_target:
            break

    if store_target:
        print(f"{GREEN}[PASS]{RESET} Found Store target: {BOLD}{store_target.get('title')}{RESET}")
        print(f"     URL: {store_target.get('url')}")
    else:
        print(f"{YELLOW}[WARN]{RESET} Direct store.steampowered.com target not yet visible in target list.")
        print(f"       Current active targets: {[t.get('title') for t in inspector.get_targets()]}")
    return 0


def cmd_dump(inspector, args):
    if not inspector.is_reachable():
        print(f"{RED}[FAIL]{RESET} Steam CEF debugging port {inspector.port} is not reachable.")
        return 1

    target = inspector.find_target(args.target)
    if not target:
        print(f"{RED}[FAIL]{RESET} Target matching '{args.target}' not found.")
        return 1

    print(f"{BLUE}==>{RESET} Inspecting DOM in target: {BOLD}{target.get('title')}{RESET} ({target.get('url')})")

    filter_text = args.filter or ""
    css_selector = args.selector or "button, a, input, [role='button'], .btn_addtocart, .sls-lua-btn, #sls-overlay-modal, [data-sls-provider]"

    script = f"""
    (() => {{
        const filterText = {json.dumps(filter_text)}.toLowerCase();
        const selector = {json.dumps(css_selector)};
        const elements = Array.from(document.querySelectorAll(selector));

        const results = elements.map((el, i) => {{
            const rect = el.getBoundingClientRect();
            const text = (el.innerText || el.textContent || el.value || '').trim().replace(/\\s+/g, ' ');
            const isVisible = rect.width > 0 && rect.height > 0 && window.getComputedStyle(el).display !== 'none';
            return {{
                index: i,
                tag: el.tagName.toLowerCase(),
                id: el.id || null,
                className: el.className || null,
                text: text.slice(0, 100),
                type: el.type || null,
                visible: isVisible,
                rect: {{
                    x: Math.round(rect.x),
                    y: Math.round(rect.y),
                    width: Math.round(rect.width),
                    height: Math.round(rect.height)
                }},
                attributes: Array.from(el.attributes).filter(a => a.name.startsWith('data-') || a.name === 'role' || a.name === 'href').map(a => `${{a.name}}="${{a.value}}"`)
            }};
        }}).filter(item => {{
            if (!filterText) return true;
            return item.text.toLowerCase().includes(filterText) ||
                   (item.id && item.id.toLowerCase().includes(filterText)) ||
                   (item.className && item.className.toLowerCase().includes(filterText)) ||
                   item.attributes.some(a => a.toLowerCase().includes(filterText));
        }});

        return {{
            totalFound: elements.length,
            matchingCount: results.length,
            title: document.title,
            url: window.location.href,
            hasSlsLuaBtn: !!document.querySelector('.sls-lua-btn'),
            hasSlsModal: !!document.getElementById('sls-overlay-modal'),
            elements: results
        }};
    }})()
    """

    res = inspector.evaluate(target["webSocketDebuggerUrl"], script)
    if not res.get("success"):
        print(f"{RED}[FAIL]{RESET} Evaluation error: {res.get('error')}")
        return 1

    data = res.get("value", {})
    print(f"Target Document Title: {BOLD}{data.get('title')}{RESET}")
    print(f"Target URL:            {data.get('url')}")
    print(f"SLS Lua Button mounted: {GREEN if data.get('hasSlsLuaBtn') else RED}{data.get('hasSlsLuaBtn')}{RESET}")
    print(f"SLS Modal mounted:      {GREEN if data.get('hasSlsModal') else RED}{data.get('hasSlsModal')}{RESET}")
    print(f"Matching Elements:     {BOLD}{data.get('matchingCount')}{RESET} (out of {data.get('totalFound')} scanned)\n")

    for el in data.get("elements", []):
        ident = f"<{el['tag']}"
        if el['id']:
            ident += f" id='{el['id']}'"
        if el['className']:
            ident += f" class='{el['className']}'"
        ident += ">"

        vis_badge = f"{GREEN}[VIS]{RESET}" if el['visible'] else f"{YELLOW}[HID]{RESET}"
        text_badge = f"\"{el['text']}\"" if el['text'] else "<no text>"
        attrs = " ".join(el['attributes'])
        if attrs:
            attrs = f" ({attrs})"

        print(f"  {vis_badge} {BOLD}{ident}{RESET}{attrs}")
        print(f"        Text: {text_badge}")
        print(f"        Box:  {el['rect']}")
    print()
    return 0


def cmd_eval(inspector, args):
    if not inspector.is_reachable():
        print(f"{RED}[FAIL]{RESET} Steam CEF debugging port {inspector.port} is not reachable.")
        return 1

    target = inspector.find_target(args.target)
    if not target:
        print(f"{RED}[FAIL]{RESET} Target matching '{args.target}' not found.")
        return 1

    expr = args.expression
    print(f"{BLUE}==>{RESET} Evaluating expression in '{BOLD}{target.get('title')}{RESET}':")
    print(f"     {CYAN}{expr}{RESET}\n")

    res = inspector.evaluate(target["webSocketDebuggerUrl"], expr)
    if res.get("success"):
        val = res.get("value")
        if isinstance(val, (dict, list)):
            print(json.dumps(val, indent=2))
        else:
            print(f"Result: {BOLD}{val}{RESET}")
        return 0
    else:
        print(f"{RED}[FAIL]{RESET} Error: {res.get('error')}")
        return 1


def cmd_click(inspector, args):
    if not inspector.is_reachable():
        print(f"{RED}[FAIL]{RESET} Steam CEF debugging port {inspector.port} is not reachable.")
        return 1

    target = inspector.find_target(args.target)
    if not target:
        print(f"{RED}[FAIL]{RESET} Target matching '{args.target}' not found.")
        return 1

    selector = args.selector
    text = args.text

    print(f"{BLUE}==>{RESET} Attempting click in '{BOLD}{target.get('title')}{RESET}'...")
    if selector:
        print(f"     Selector: {CYAN}{selector}{RESET}")
    if text:
        print(f"     Text:     {CYAN}{text}{RESET}")

    click_script = f"""
    (() => {{
        const selector = {json.dumps(selector)};
        const textTarget = {json.dumps(text)};

        let targetEl = null;

        if (selector) {{
            targetEl = document.querySelector(selector);
        }}

        if (!targetEl && textTarget) {{
            const walker = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT, null, false);
            let n;
            while ((n = walker.nextNode())) {{
                if (n.nodeValue.trim().toLowerCase() === textTarget.toLowerCase() ||
                    n.nodeValue.trim().toLowerCase().includes(textTarget.toLowerCase())) {{
                    let el = n.parentElement;
                    while (el && el !== document.body) {{
                        if (el.tagName === 'BUTTON' || el.tagName === 'A' || el.classList.contains('Focusable') || el.onclick) {{
                            targetEl = el;
                            break;
                        }}
                        el = el.parentElement;
                    }}
                    if (targetEl) break;
                    targetEl = n.parentElement;
                    break;
                }}
            }}
        }}

        if (!targetEl) {{
            return {{ clicked: false, reason: 'Element matching selector/text not found in DOM' }};
        }}

        targetEl.scrollIntoView({{ block: 'nearest', inline: 'nearest' }});
        if (typeof targetEl.focus === 'function') targetEl.focus();

        const evt = (type) => new MouseEvent(type, {{
            view: window,
            bubbles: true,
            cancelable: true,
            buttons: 1
        }});

        targetEl.dispatchEvent(evt('mousedown'));
        targetEl.dispatchEvent(evt('mouseup'));
        targetEl.dispatchEvent(evt('click'));
        if (typeof targetEl.click === 'function') {{
            targetEl.click();
        }}

        return {{
            clicked: true,
            tag: targetEl.tagName,
            id: targetEl.id,
            className: targetEl.className,
            text: (targetEl.innerText || targetEl.textContent || '').trim().slice(0, 80)
        }};
    }})()
    """

    res = inspector.evaluate(target["webSocketDebuggerUrl"], click_script)
    if not res.get("success"):
        print(f"{RED}[FAIL]{RESET} Click evaluation failed: {res.get('error')}")
        return 1

    val = res.get("value", {})
    if val.get("clicked"):
        print(f"{GREEN}[PASS]{RESET} Click successfully dispatched to <{val.get('tag')} id='{val.get('id')}' class='{val.get('className')}'>")
        print(f"       Text: \"{val.get('text')}\"")
        return 0
    else:
        print(f"{RED}[FAIL]{RESET} Could not click: {val.get('reason')}")
        return 1


def main():
    parser = argparse.ArgumentParser(description="Steam CEF Store UI Inspection Tool")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help="CEF port (default 8080)")

    subparsers = parser.add_subparsers(dest="command", help="Inspection command")

    # targets
    sub = subparsers.add_parser("targets", help="List active CEF targets")

    # nav
    sub_nav = subparsers.add_parser("nav", help="Navigate to Steam Store page for AppID")
    sub_nav.add_argument("--appid", type=int, default=DEFAULT_APPID, help="AppID to open")

    # dump
    sub_dump = subparsers.add_parser("dump", help="Dump DOM elements/buttons in target")
    sub_dump.add_argument("--target", type=str, default=None, help="Target window title/url query")
    sub_dump.add_argument("--filter", type=str, default=None, help="Filter elements by text or class")
    sub_dump.add_argument("--selector", type=str, default=None, help="CSS selector to query")

    # eval
    sub_eval = subparsers.add_parser("eval", help="Evaluate JavaScript expression in target")
    sub_eval.add_argument("expression", type=str, help="JavaScript expression to run")
    sub_eval.add_argument("--target", type=str, default=None, help="Target window title/url query")

    # click
    sub_click = subparsers.add_parser("click", help="Simulate click on element")
    sub_click.add_argument("--target", type=str, default=None, help="Target window title/url query")
    sub_click.add_argument("--selector", type=str, default=None, help="CSS selector")
    sub_click.add_argument("--text", type=str, default=None, help="Button/element text")

    args = parser.parse_args()
    if not args.command:
        parser.print_help()
        sys.exit(0)

    inspector = SteamCEFInspector(port=args.port)

    cmds = {
        "targets": cmd_targets,
        "nav": cmd_nav,
        "dump": cmd_dump,
        "eval": cmd_eval,
        "click": cmd_click,
    }

    code = cmds[args.command](inspector, args)
    sys.exit(code or 0)


if __name__ == "__main__":
    main()
