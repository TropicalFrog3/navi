#!/usr/bin/env python3
"""
Capture live screenshots of Navi Config page in the running Steam CEF instance via CDP.
"""

import base64
import json
import sys
import time
import urllib.request
import websocket

CDP_JSON_URL = "http://127.0.0.1:8080/json"

def main():
    with urllib.request.urlopen(CDP_JSON_URL, timeout=3) as resp:
        targets = json.loads(resp.read().decode("utf-8"))

    steam_t = next((t for t in targets if t.get("title") == "Steam" and "webSocketDebuggerUrl" in t), None)
    if not steam_t:
        print("Steam target not found!")
        sys.exit(1)

    ws_url = steam_t["webSocketDebuggerUrl"]
    print(f"Connecting to {ws_url}")
    ws = websocket.create_connection(ws_url, timeout=10)

    ws.send(json.dumps({"id": 1, "method": "Page.enable"}))
    ws.send(json.dumps({"id": 2, "method": "Runtime.enable"}))
    time.sleep(0.5)

    # 1. Click #sls-config-top-tab
    click_js = """
    (() => {
        const tab = document.getElementById("sls-config-top-tab");
        if (!tab) return { error: "no tab" };
        tab.click();
        return { ok: true };
    })()
    """
    ws.send(json.dumps({"id": 10, "method": "Runtime.evaluate", "params": {"expression": click_js, "returnByValue": True}}))
    print("Clicked config tab:", ws.recv())

    # Wait 1.5s for render
    time.sleep(1.5)

    # 2. Switch to Cloud Saves tab
    switch_cloud_js = """
    (() => {
        const btn = document.querySelector('[data-section="CloudSaves"]');
        if (btn) btn.click();
        return { ok: Boolean(btn) };
    })()
    """
    ws.send(json.dumps({"id": 20, "method": "Runtime.evaluate", "params": {"expression": switch_cloud_js, "returnByValue": True}}))
    print("Switched to CloudSaves:", ws.recv())
    time.sleep(1.0)

    # 3. Capture screenshot of CloudSaves section
    print("Capturing CloudSaves screenshot...")
    ws.send(json.dumps({"id": 30, "method": "Page.captureScreenshot", "params": {"format": "png"}}))

    while True:
        msg = json.loads(ws.recv())
        if msg.get("id") == 30:
            data = msg.get("result", {}).get("data")
            if data:
                img_bytes = base64.b64decode(data)
                out_path = "/home/nicolas/.gemini/antigravity/brain/9a36c42c-9471-43e4-8894-8f03556b3b87/live_steam_cloud_config.png"
                with open(out_path, "wb") as f:
                    f.write(img_bytes)
                print(f"Saved screenshot to {out_path} ({len(img_bytes)} bytes)")
            break

    ws.close()

if __name__ == "__main__":
    main()
