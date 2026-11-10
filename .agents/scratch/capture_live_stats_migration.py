#!/usr/bin/env python3
"""
Capture live screenshot of Stats Sync and Migration sections in the running Steam client.
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
    ws = websocket.create_connection(ws_url, timeout=10)

    ws.send(json.dumps({"id": 1, "method": "Page.enable"}))
    ws.send(json.dumps({"id": 2, "method": "Runtime.enable"}))
    time.sleep(0.5)

    # Scroll down to Migration & Stats sync
    scroll_js = """
    (() => {
        const bodyEl = document.querySelector('.sls-cfg-body');
        if (bodyEl) {
            bodyEl.scrollTop = 480;
            return { scrolled: true, scrollTop: bodyEl.scrollTop };
        }
        return { scrolled: false };
    })()
    """
    ws.send(json.dumps({"id": 15, "method": "Runtime.evaluate", "params": {"expression": scroll_js, "returnByValue": True}}))
    print("Scrolled:", ws.recv())
    time.sleep(0.5)

    # Capture screenshot
    ws.send(json.dumps({"id": 30, "method": "Page.captureScreenshot", "params": {"format": "png"}}))

    while True:
        msg = json.loads(ws.recv())
        if msg.get("id") == 30:
            data = msg.get("result", {}).get("data")
            if data:
                img_bytes = base64.b64decode(data)
                out_path = "/home/nicolas/.gemini/antigravity/brain/9a36c42c-9471-43e4-8894-8f03556b3b87/live_steam_stats_migration.png"
                with open(out_path, "wb") as f:
                    f.write(img_bytes)
                print(f"Saved screenshot to {out_path} ({len(img_bytes)} bytes)")
            break

    ws.close()

if __name__ == "__main__":
    main()

