import urllib.request
import json
import asyncio
import websockets
import threading
import time

class SteamCDPInjector:
    def __init__(self, app_id, title="Spoofed Game"):
        self.app_id = app_id
        self.title = title
        self.percentage = 0
        self.running = False
        self.ws_url = None
        self.thread = None
        self.loop = None
        self.cancelled = False
        self.paused = False

    def _get_target_ws(self):
        try:
            req = urllib.request.urlopen('http://127.0.0.1:8080/json/list', timeout=2)
            targets = json.loads(req.read().decode('utf-8'))
            # We target the main Steam window to inject the fake DOM
            steam_win = next((t for t in targets if t.get('title') == 'Steam'), None)
            if steam_win:
                return steam_win['webSocketDebuggerUrl']
        except Exception as e:
            print("Error finding Steam CDP target:", e)
        return None

    def start(self):
        self.ws_url = self._get_target_ws()
        if not self.ws_url:
            print("Steam CDP Injector: Steam not found.")
            return

        self.running = True
        self.thread = threading.Thread(target=self._run_loop, daemon=True)
        self.thread.start()

    def _run_loop(self):
        self.loop = asyncio.new_event_loop()
        asyncio.set_event_loop(self.loop)
        self.loop.run_until_complete(self._inject_progress())

    async def _inject_progress(self):
        try:
            async with websockets.connect(self.ws_url, ping_timeout=None) as ws:
                # 1. Trigger downloads page to ensure user sees it
                await ws.send(json.dumps({'id': 1, 'method': 'Runtime.evaluate', 'params': {
                    'expression': 'window.location.href = \"steam://open/downloads\";',
                    'returnByValue': True
                }}))
                
                await asyncio.sleep(2) # Wait for page to load
                
                # 2. Inject fake DOM
                js_inject = f"""
                (function() {{
                    let container = document.querySelector('.downloads_Page_13e90') || 
                                    document.querySelector('[class*=downloads_Page]') || 
                                    document.body;
                    
                    let fakeRowId = 'sls_fake_download_{self.app_id}';
                    let existing = document.getElementById(fakeRowId);
                    if (existing) return;
                    
                    let fakeRow = document.createElement('div');
                    fakeRow.id = fakeRowId;
                    fakeRow.style.padding = '16px';
                    fakeRow.style.margin = '8px';
                    fakeRow.style.backgroundColor = '#1e2329';
                    fakeRow.style.borderRadius = '4px';
                    fakeRow.style.borderLeft = '3px solid #00a4ff';
                    fakeRow.style.color = '#fff';
                    fakeRow.style.fontFamily = 'Motiva Sans, sans-serif';
                    fakeRow.style.display = 'flex';
                    fakeRow.style.flexDirection = 'column';
                    fakeRow.style.zIndex = '9999';
                    fakeRow.style.position = 'relative';
                    
                    fakeRow.innerHTML = \`
                        <div style="display: flex; justify-content: space-between; margin-bottom: 8px;">
                            <span style="font-weight: 500; font-size: 14px; text-transform: uppercase;">{self.title}</span>
                            <div style="display: flex; gap: 12px; align-items: center;">
                                <span id="sls_status_{self.app_id}" style="color: #66c0f4; font-size: 13px;">Downloading... 0%</span>
                                <button onclick="window.sls_action_{self.app_id} = window.sls_action_{self.app_id} === 'pause' ? 'resume' : 'pause'; this.innerText = window.sls_action_{self.app_id} === 'pause' ? 'Resume' : 'Pause';" style="background: transparent; border: 1px solid #66c0f4; color: #66c0f4; cursor: pointer; padding: 2px 8px; border-radius: 2px;">Pause</button>
                                <button onclick="window.sls_action_{self.app_id} = 'cancel'" style="background: transparent; border: 1px solid #ff4c4c; color: #ff4c4c; cursor: pointer; padding: 2px 8px; border-radius: 2px;">Cancel</button>
                            </div>
                        </div>
                        <div style="width: 100%; height: 6px; background-color: #171a21; border-radius: 3px; overflow: hidden;">
                            <div id="sls_bar_{self.app_id}" style="width: 0%; height: 100%; background-color: #66c0f4; transition: width 0.3s;"></div>
                        </div>
                    \`;
                    
                    if (container !== document.body && container.firstChild) {{
                        container.insertBefore(fakeRow, container.firstChild);
                    }} else {{
                        document.body.appendChild(fakeRow);
                    }}
                }})();
                """
                await ws.send(json.dumps({'id': 2, 'method': 'Runtime.evaluate', 'params': {'expression': js_inject}}))
                
                # 3. Update loop
                last_pct = -1
                window_action = None

                async def recv_loop():
                    nonlocal window_action
                    try:
                        while self.running:
                            msg = await ws.recv()
                            data = json.loads(msg)
                            if data.get('id') == 5:
                                val = data.get('result', {}).get('result', {}).get('value')
                                if val:
                                    window_action = val
                    except Exception:
                        pass
                
                asyncio.create_task(recv_loop())

                while self.running:
                    # check action
                    await ws.send(json.dumps({'id': 5, 'method': 'Runtime.evaluate', 'params': {'expression': f'window.sls_action_{self.app_id}', 'returnByValue': True}}))
                    
                    if window_action == 'cancel':
                        self.cancelled = True
                        break
                    elif window_action == 'pause':
                        self.paused = True
                    elif window_action == 'resume':
                        self.paused = False

                    if self.percentage != last_pct:
                        last_pct = self.percentage
                        js_update = f"""
                        (function() {{
                            let bar = document.getElementById('sls_bar_{self.app_id}');
                            let status = document.getElementById('sls_status_{self.app_id}');
                            if (bar) bar.style.width = '{self.percentage}%';
                            if (status) status.innerText = 'Downloading... {self.percentage}%';
                            
                            // Hide native steam row
                            let nativeImg = document.querySelector('img[src*="{self.app_id}"]');
                            if (nativeImg) {{
                                let row = nativeImg.closest('[class*="downloaditem_"]') || nativeImg.closest('[class*="Item"]');
                                if (row) row.style.display = 'none';
                            }}
                        }})();
                        """
                        await ws.send(json.dumps({'id': 3, 'method': 'Runtime.evaluate', 'params': {'expression': js_update}}))
                    await asyncio.sleep(0.5)
                    
                # 4. Finished
                js_finish = f"""
                (function() {{
                    let status = document.getElementById('sls_status_{self.app_id}');
                    let bar = document.getElementById('sls_bar_{self.app_id}');
                    if (status) status.innerText = 'Completed';
                    if (bar) bar.style.backgroundColor = '#5c7e10';
                    setTimeout(() => {{
                        let row = document.getElementById('sls_fake_download_{self.app_id}');
                        if (row) row.remove();
                    }}, 5000);
                }})();
                """
                await ws.send(json.dumps({'id': 4, 'method': 'Runtime.evaluate', 'params': {'expression': js_finish}}))
                
        except Exception as e:
            print("Steam CDP Injector Error:", e)

    def update(self, percentage):
        self.percentage = percentage

    def stop(self):
        self.running = False
        if self.thread:
            self.thread.join(timeout=2)
