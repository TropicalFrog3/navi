---
name: cef-cdp-injection
description: Autonomously inspect targets and inject/evaluate JavaScript into Steam CEF or Chromium applications via Chrome DevTools Protocol (CDP) WebSocket without user intervention.
---

# CEF CDP Injection Skill

## Purpose
When debugging or testing CEF/Chromium-embedded applications running with `--remote-debugging-port` (such as Steam running with `-cef-enable-debugging` on port 8080), execute JavaScript directly and inspect DOM/contexts autonomously via Python without asking the user to open DevTools.

## Step 1: Check Active Targets
Query `http://127.0.0.1:<port>/json` using `urllib.request` or `curl -s http://127.0.0.1:8080/json`.

Common Steam CEF window titles:
- `"Steam Big Picture Mode"`: The active gamepad/BPM UI.
- `"Steam"`: The desktop client window.
- `"SharedJSContext"`: Headless shared runtime hosting global MobX stores (`window.appStore.m_mapApps`, `window.appStore.allApps`). Do not inject visual DOM here, but use it for querying client library state or deleting removed entries (`window.appStore.m_mapApps.delete(appId)`).
- `"MainMenu_uid2"` / `"QuickAccess_uid2"`: Flyout popups (often 0-height or background).

## Step 2: Inject / Evaluate via Python
Create and run a scratch Python script using `urllib.request` and `websocket`:

```python
import json
import urllib.request
import websocket

def inject(target_title="Steam Big Picture Mode", script=""):
    req = urllib.request.Request("http://127.0.0.1:8080/json")
    with urllib.request.urlopen(req) as resp:
        pages = json.loads(resp.read().decode())

    target_ws = None
    for p in pages:
        if p.get("title") == target_title:
            target_ws = p.get("webSocketDebuggerUrl")
            break

    if not target_ws:
        # Fallback to first non-empty, non-SharedJSContext page
        for p in pages:
            title = p.get("title", "")
            if title and title != "SharedJSContext" and "Menu" not in title:
                target_ws = p.get("webSocketDebuggerUrl")
                break

    if not target_ws:
        print("ERROR: Target window not found")
        return

    # Wrap in try-catch to surface runtime errors
    wrapped_script = f"""
    (() => {{
        try {{
            {script}
            return {{ success: true }};
        }} catch (err) {{
            return {{ error: err.toString(), stack: err.stack }};
        }}
    }})()
    """

    ws = websocket.create_connection(target_ws, timeout=5)
    ws.send(json.dumps({
        "id": 1,
        "method": "Runtime.evaluate",
        "params": {
            "expression": wrapped_script,
            "returnByValue": True,
            "awaitPromise": True,
            "userGesture": True
        }
    }))
    res = json.loads(ws.recv())
    ws.close()
    # Value extraction:
    # Evaluate result is wrapped: res.get("result", {}).get("result", {}).get("value")
    print("CDP Result:", res.get("result", {}).get("result", {}).get("value"))

if __name__ == "__main__":
    inject(
        target_title="Steam Big Picture Mode",
        script="console.log('Injected from agent');"
    )
```

## Step 3: Steam Protocol Navigation & Dialog Automation

When testing Steam UI flows (like install/uninstall dialogues):

```python
import json, urllib.request, websocket, time

def evaluate(ws_url, expr):
    ws = websocket.create_connection(ws_url, timeout=3)
    ws.send(json.dumps({
        "id": 1,
        "method": "Runtime.evaluate",
        "params": {"expression": expr, "returnByValue": True, "userGesture": True}
    }))
    res = json.loads(ws.recv())
    ws.close()
    return res.get("result", {}).get("result", {}).get("value")

def trigger_url(protocol_url):
    req = urllib.request.Request("http://127.0.0.1:8080/json")
    with urllib.request.urlopen(req) as resp:
        pages = json.loads(resp.read().decode())
    for p in pages:
        if p.get("title") == "Steam":
            evaluate(p.get("webSocketDebuggerUrl"), f"window.location.href = '{protocol_url}';")
            break

def click_button_by_text(btn_text):
    req = urllib.request.Request("http://127.0.0.1:8080/json")
    with urllib.request.urlopen(req) as resp:
        pages = json.loads(resp.read().decode())
    script = f"""
    (() => {{
        for (let b of document.querySelectorAll('button')) {{
            if (b.textContent.trim() === '{btn_text}') {{
                b.click();
                return true;
            }}
        }}
        return false;
    }})()
    """
    for p in pages:
        ws_url = p.get("webSocketDebuggerUrl")
        if ws_url and evaluate(ws_url, script):
            return True
    return False
```

### Clean State Requirement for Install Flows
Before running an install flow test, ensure previous installations are cleared:
1. Trigger `steam://uninstall/<appid>` and confirm using `click_button_by_text("Uninstall")`.
2. Verify `~/.local/share/Steam/steamapps/appmanifest_<appid>.acf` has been removed.
## Step 4: React Controlled Inputs & Virtualized List Automation

### A. Setting React Controlled Inputs (e.g. Search Bar)
Setting `input.value = "..."` directly is ignored by React because synthetic event tracking overrides DOM mutations. Use the native prototype descriptor setter:
```javascript
const input = document.querySelector('input[placeholder="Search by Name"], input.DialogInput');
input.focus();
const nativeSetter = Object.getOwnPropertyDescriptor(window.HTMLInputElement.prototype, 'value').set;
nativeSetter.call(input, 'Crawl');
input.dispatchEvent(new Event('input', { bubbles: true }));
input.dispatchEvent(new Event('change', { bubbles: true }));
```

### B. Clicking Virtualized List Elements (e.g. Library Game Entries)
Steam uses `ReactVirtualized` grids where list items are recycled.
1. Use a `TreeWalker` to find the exact text node matching the title.
2. Traverse ancestors to locate the `.Focusable` or `[draggable="true"]` container.
3. Dispatch full pointer/mouse event sequences:
```javascript
targetEl.scrollIntoView({ block: 'nearest' });
targetEl.focus();
const evt = (type) => new MouseEvent(type, { view: window, bubbles: true, cancelable: true, buttons: 1 });
targetEl.dispatchEvent(evt('mousedown'));
targetEl.dispatchEvent(evt('mouseup'));
targetEl.dispatchEvent(evt('click'));
if (typeof targetEl.click === 'function') targetEl.click();
```

### C. Identifying Target Windows
- **`title="Steam"`:** The primary desktop client window (contains Library, Search results, top navigation).
- **`title="SharedJSContext"`:** Headless background store (MobX `window.appStore.m_mapApps`).
- **`title="data:text/html,...tracking:...:/library/app/<appid>"`:** Confirms successful navigation to game details page.

## Guidelines
- Always wrap injected test scripts in a `try...catch` block to capture syntax or runtime exceptions.
- Use `returnByValue: True` to serialize objects back into JSON so the output can be inspected directly in Python.
- For async scripts (e.g. using `fetch()` or `await`), wrap in `(async () => { ... })()` and include `"awaitPromise": True` in `Runtime.evaluate` params.
- Do not use `Page.enable` unless event streaming is required, as high volume events can flood the socket.
- In Steam Big Picture Mode, hash changes (`window.location.hash`) do not trigger Steam's internal React router; use UI tile interaction or `steam://nav/...` navigation.
