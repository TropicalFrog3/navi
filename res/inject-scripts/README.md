# Navi CEF Injection Scripts Directory

This directory contains JavaScript files injected into Steam's Chromium Embedded Framework (CEF) and web storefront contexts. Scripts are structured into functional domain categories to optimize semantic indexing, retrieval-augmented generation (RAG), and modular maintenance.

---

## Category Structure & Index

```text
res/inject-scripts/ (mirrored via src/view/assets/)
├── core/                  # Low-level CEF runtime hooks & monkey-patches
│   └── navi_inject.js
├── store/                 # Steam Store web views (store.steampowered.com)
│   ├── store-page-script.js
│   └── depot-selection-script.js
├── library/               # Steam Client Library UI & game management
│   ├── app-details-script.js
│   └── auto-collection.js
├── cloud/                 # Cloud saves & remote storage management
│   └── cloud_manager.js
├── shell/                 # Steam Desktop shell navigation & global chrome
│   ├── import-lua-script.js
│   └── version-top-bar.js
└── vendor/                # Vendored third-party dependencies
    └── jszip.min.js
```

---

## Category Specifications

### 1. `core/` (CEF Runtime Hooks & Bootstrapping)
* **Target Context**: Global Steam CEF runtime, `SteamClient` API.
* **`navi_inject.js`**:
  * Monkey-patches `SteamClient.Apps.RegisterForAppDetails` to intercept disk space queries and supply real disk space bytes for unlocked games that would otherwise show "0 B" in install dialogs.
  * Bootstraps client-level UI components and attaches the pending operations warning banner.

### 2. `store/` (Steam Storefront Integration)
* **Target Context**: `https://store.steampowered.com/app/*` (isolated web storefront).
* **`store-page-script.js`**:
  * Injects "Add to Library" and "Download Lua" action buttons into store purchase areas.
  * Provides DLC checkbox selectors and package pricing displays.
  * Implements the CDP queue bridge (`window.naviFetch`, `window.__naviQueue`) to safely bypass store CSP restrictions (`default-src https:`).
* **`depot-selection-script.js`**:
  * Interactive modal dialog for selecting game depots, DLCs, and manifests when downloading or spoofing a game from the store.

### 3. `library/` (Steam Library & Game Management)
* **Target Context**: `https://steamloopback.host/library/*` (Steam client library view).
* **`app-details-script.js`**:
  * Injected into the library game details page.
  * Hooks the game management gear icon and renders the Game Manager modal with AutoCrack, Online-Fix, DLC management, save snapshot restore, and Lua removal actions.
  * Implements Steam virtual gamepad focus trapping (`vgp_ondirection`, `vgp_onok`, `vgp_oncancel`) for Deck and Big Picture Mode.
* **`auto-collection.js`**:
  * Automatically syncs unlocked games into the "LUAs" collection in `window.collectionStore`.

### 4. `cloud/` (Cloud Saves & Remote Storage Management)
* **Target Context**: Steam Client UI & Cloud Settings modals.
* **`cloud_manager.js`**:
  * Exposes the `window.NaviCloudManager` namespace.
  * Handles OAuth provider sign-in/status (Google Drive, WebDAV, local-only).
  * Polls `/cloud/status` and `/cloud/pending-ops`, maintaining the persistent restart warning banner.
  * Provides backup snapshot creation, restore, and typed-reset guardrails for per-app save directories.

### 5. `shell/` (Steam Desktop Shell & Global Chrome)
* **Target Context**: Steam SuperNav top bar (`https://steamloopback.host/index.html*`).
* **`import-lua-script.js`**:
  * Injects the "Import Lua" top navigation tab into Steam's SuperNav container.
  * Provides drag-and-drop file import, ZIP archive unpacking, Lua manifest parsing, and settings dialogs.
* **`version-top-bar.js`**:
  * Injects the Navi version indicator badge (`Navi %VERSION%`) into Steam's header navigation.

### 6. `vendor/` (Vendored Dependencies)
* **`jszip.min.js`**:
  * Vendored JSZip v3.10.1 library for client-side ZIP archive extraction.
  * Isolated in `vendor/` so RAG embeddings and chunking pipelines can ignore or deprioritize minified third-party code.

---

## Backward Compatibility & Dynamic Resolution

Root-level relative symlinks are preserved in `res/inject-scripts/` pointing to each categorized script. The C++ resource loader in `src/view/cdpinject.cpp` and `src/presenter/removelua.cpp` resolves files by both flat name (`script.js`) and category path (`category/script.js`).

