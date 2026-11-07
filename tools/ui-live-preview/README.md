# Navi UI Live Preview & Hot Reload Studio

The **Navi UI Live Preview & Hot Reload Studio** enables instantaneous, zero-install live previewing and hot reloading of Navi's injected CEF user interface scripts and Steam client shells.

It runs with **zero external dependencies** (standard Python 3 library) and supports both standalone browser previewing and direct live-injection into a running Steam client via the Chrome DevTools Protocol (CDP).

---

## Key Features

1. **Standalone Browser Studio (`index.html`)**:
   - **Shell Switcher**: Toggle seamlessly between:
     - **Steam Client Library Shell** (`tests/fixtures/ui_shells/steam_library_sp.html` with `import-lua-script.js`, `app-details-script.js`, `version-top-bar.js`, etc.)
     - **Steam Store Shell** (`tests/fixtures/ui_shells/steam_store_app.html` with `store-page-script.js` and `depot-selection-script.js`)
   - **Multi-Device Viewport Presets**:
     - 🖥️ **Desktop Fluid**: Responsive 100% viewport.
     - 🎮 **Steam Deck**: Pixel-accurate 1280 × 800 layout.
     - 📺 **Big Picture / Full HD**: 1920 × 1080 10-foot UI.
   - **Quick Action Modal Launchers**:
     - ⚙️ **Config Editor**: Opens Navi's configuration editor modal (`window.__naviShowConfigPage()`).
     - ☁️ **Cloud Save Manager**: Opens the Cloud Saves Management dialog (`window.NaviCloudManager.openSettingsModal()`).
     - 📦 **Depot Selector**: Triggers the depot selection and manifest install modal (`depot-selection-script.js`).
     - 📄 **LUA Import**: Opens the manual LUA and ACF installer page (`window.__naviShowImportLuaPage()`).
     - 🔄 **Force Reload**: Re-executes scripts or refreshes the preview.

2. **Server-Sent Events (SSE) Live Reload**:
   - High-performance, debounced file watcher monitoring `res/inject-scripts/` and fixtures.
   - When any `.js` file is edited and saved, updates are broadcast instantly over SSE (`/_live/events`).
   - The preview client hot-evaluates updated code in place, avoiding lost application state.

3. **Integrated Mock Navi Daemon API**:
   - Emulates Navi port 9001 endpoints:
     - `/cloud/*` (state, OAuth sign-in/out, app statuses, file explorer, backups)
     - `/config` & `/config/full` (schema validation and live updates)
     - `/check?id=<appid>` (LUA status, Online-Fix, and AutoCrack detection)
     - `/get-depots`, `/start-download`, `/cancel-download` (depot manifest queries)
     - `/api-keys` & `/set-api-keys` (Hubcap, Ryuu, Dropbox keys)
     - `/install-fix`, `/remove-fix`, `/install-crack`, `/remove-crack`
     - `/lua_apps`, `/list`, `/manual-install`, `/restart`, `/clear-cache`
   - Real-time API inspection log table in the Studio HUD.
   - Drains `window.__naviQueue` automatically for Steam Store pages without requiring CDP.

4. **Live Steam Client CDP Hot Reload (`--steam` / `--cdp`)**:
   - For physical Steam Decks or desktop Linux when Steam is launched with `steam -cef-enable-debugging`.
   - Discovers targets on port 8080 (`http://127.0.0.1:8080/json`).
   - Hot-re-evaluates modified scripts directly into active Steam windows (`Steam`, `Steam Big Picture Mode`, Store tabs) via a pure Python RFC 6455 WebSocket client or `websocket-client`.

---

## Quick Start

### 1. Launch Browser Studio

```bash
cd tools/ui-live-preview
./start.sh
```

Or invoke directly with python:
```bash
python3 tools/ui-live-preview/server.py --port 5050
```

Open your browser at:
👉 **`http://127.0.0.1:5050/`**

### 2. Live Steam Client CDP Mode

To hot reload scripts into an active Steam client:

1. Launch Steam with CEF debugging enabled:
   ```bash
   steam -cef-enable-debugging &
   ```
2. Start the live preview server with `--steam`:
   ```bash
   ./start.sh --steam
   ```
3. Whenever you edit any script in `res/inject-scripts/`, the server automatically discovers the matching Steam window target and hot-evaluates the code in real time!

---

## CLI Options

| Flag | Default | Description |
|---|---|---|
| `--port` | `5050` | Port for the Live Preview HTTP server & Studio dashboard |
| `--host` | `127.0.0.1` | Network interface to bind |
| `--mock-port` | `9001` | Port for standalone Navi daemon mock (falls back to integrated) |
| `--steam`, `--cdp` | `False` | Enable live Steam Client CDP hot reloading |
| `--cdp-host` | `127.0.0.1` | Steam CEF remote debugging host |
| `--cdp-port` | `8080` | Steam CEF remote debugging port |
| `--fixture` | `library` | Default fixture shell (`library` or `store`) |
| `--no-browser` | `False` | Prevent automatically opening default web browser |
| `--debounce` | `0.20` | Debounce delay in seconds for file watcher notifications |

---

## Architecture

```
tools/ui-live-preview/
├── server.py              # Main HTTP dev server, SSE broadcaster, and router
├── mock_api.py            # Clean mock implementation of Navi port 9001 endpoints
├── file_watcher.py        # Mtime debounce file watcher for res/inject-scripts/
├── cdp_reloader.py        # Steam CEF CDP target discovery and RFC 6455 reloader
├── static/
│   ├── index.html         # Studio dashboard with viewport framing & live HUD
│   └── live_reload_client.js # Injected client: SSE listener, SteamClient mock, bridge
├── test_preview_server.py # Test suite for mock endpoints, watcher, and server
├── start.sh               # Executable launcher script
└── README.md              # Documentation
```

---

## Running Self-Tests

The tool includes a dedicated test suite verifying the mock endpoints, file watcher debounce, CDP frame builder, and HTTP routing:

```bash
pytest tools/ui-live-preview/test_preview_server.py
```
or
```bash
python3 tools/ui-live-preview/test_preview_server.py
```

