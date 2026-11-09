#!/usr/bin/env python3
import json, sys, time, urllib.request, websocket, base64

with urllib.request.urlopen("http://127.0.0.1:8080/json") as r:
    targets = json.loads(r.read().decode())
t = next(x for x in targets if x.get("title") == "Steam")
ws = websocket.create_connection(t["webSocketDebuggerUrl"])
ws.send(json.dumps({"id": 1, "method": "Page.enable"}))
ws.send(json.dumps({"id": 2, "method": "Runtime.enable"}))
time.sleep(0.5)

# drain
while True:
    try:
        ws.settimeout(0.3)
        ws.recv()
    except:
        break

js = """
(() => {
    // Find scrollable
    const main = document.querySelector('.sls-cfg-main');
    const container = document.getElementById('sls-config-page-container');
    const shell = document.querySelector('.sls-cfg-shell');
    const body = document.body;
    
    // Test scrolling on each
    if (main) main.scrollTop = 900;
    if (container) container.scrollTop = 900;
    if (shell) shell.scrollTop = 900;
    window.scrollTo(0, 900);
    
    const migrateEl = document.getElementById("sls-cfg-cs-migrate-card");
    if (migrateEl) {
        migrateEl.scrollIntoView();
    }

    return {
        mainScroll: main ? main.scrollTop : null,
        mainHeight: main ? main.scrollHeight : null,
        migrateFound: Boolean(migrateEl)
    };
})()
"""
ws.settimeout(10.0)
ws.send(json.dumps({"id": 100, "method": "Runtime.evaluate", "params": {"expression": js, "returnByValue": True}}))
while True:
    msg = json.loads(ws.recv())
    if msg.get("id") == 100:
        print("Evaluation response:", msg.get("result", {}).get("result", {}).get("value"))
        break

time.sleep(0.5)

ws.send(json.dumps({"id": 200, "method": "Page.captureScreenshot", "params": {"format": "png"}}))
while True:
    msg = json.loads(ws.recv())
    if msg.get("id") == 200:
        data = msg.get("result", {}).get("data")
        with open("/home/nicolas/.gemini/antigravity/brain/9a36c42c-9471-43e4-8894-8f03556b3b87/live_steam_stats_migration.png", "wb") as f:
            f.write(base64.b64decode(data))
        print("Captured new screenshot")
        break
ws.close()