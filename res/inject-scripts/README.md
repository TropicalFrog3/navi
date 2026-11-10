# Navi CEF Injection Scripts Directory

This directory contains JavaScript files injected into Steam's Chromium Embedded Framework (CEF) and web storefront contexts. Scripts are structured into functional domain categories to optimize semantic indexing, retrieval-augmented generation (RAG), and modular maintenance.

---

## Category Structure & Index

```text
res/inject-scripts/ (mirrored via src/view/assets/)
├── core/                  # Low-level CEF runtime hooks & monkey-patches
│   ├── navi-inject/       # Modular sources for navi_inject.js
│   │   ├── pending-banner.html
│   │   ├── pending-banner.js
│   │   ├── cloud-fallback.js
│   │   ├── disk-space-api.js
│   │   ├── steam-apps-patch.js
│   │   └── main.js
│   └── navi_inject.js     # Bundled self-contained artifact (via tools/bundle-navi-inject.py)
├── store/                 # Steam Store web views (store.steampowered.com)
│   ├── depot-selection/   # Modular sources for depot-selection-script.js
│   │   ├── styles.css
│   │   ├── modal.html
│   │   ├── gamepad.js
│   │   ├── utils.js
│   │   ├── styles.js
│   │   ├── depot-resolver.js
│   │   └── main.js
│   ├── depot-selection-script.js # Bundled self-contained artifact (via tools/bundle-depot-selection.py)
│   ├── store-page/        # Modular sources for store-page-script.js
│   │   ├── styles.css
│   │   ├── provider-modal.html
│   │   ├── remove-modal.html
│   │   ├── config-modal.html
│   │   ├── styles.js
│   │   ├── cdp-bridge.js
│   │   ├── gamepad.js
│   │   ├── provider-config.js
│   │   ├── download-tracker.js
│   │   ├── modals.js
│   │   ├── buttons.js
│   │   ├── ownership-decoration.js
│   │   └── main.js
│   └── store-page-script.js # Bundled self-contained artifact (via tools/bundle-store-page.py)
├── library/               # Steam Client Library UI & game management
│   ├── app-details/       # Modular sources for app-details-script.js
│   │   ├── styles.css
│   │   ├── gamepad.js
│   │   ├── appid.js
│   │   ├── cloud-api.js
│   │   ├── game-manager-modal.html
│   │   ├── game-manager-modal.js
│   │   ├── provider-modal.html
│   │   ├── provider-modal.js
│   │   ├── remove-modal.html
│   │   ├── remove-modal.js
│   │   ├── restore-modal.html
│   │   ├── restore-modal.js
│   │   ├── reset-modal.html
│   │   ├── reset-modal.js
│   │   ├── navi-modal.html
│   │   ├── navi-modal.js
│   │   ├── buttons.js
│   │   └── main.js
│   ├── app-details-script.js  # Bundled self-contained artifact (via tools/bundle-app-details.py)
│   └── auto-collection.js
├── cloud/                 # Cloud saves & remote storage management
│   ├── cloud-manager/     # Modular sources for cloud_manager.js
│   │   ├── styles.css
│   │   ├── state.js
│   │   ├── security.js
│   │   ├── gamepad.js
│   │   ├── pending-banner.js
│   │   ├── api.js
│   │   ├── reset-modal.html
│   │   ├── reset-modal.js
│   │   ├── restore-modal.html
│   │   ├── restore-modal.js
│   │   ├── settings-modal.html
│   │   ├── settings-modal.js
│   │   ├── utils.js
│   │   ├── cloud-files-modal.html
│   │   ├── cloud-files-modal.js
│   │   └── main.js
│   └── cloud_manager.js   # Bundled self-contained artifact (via tools/bundle-cloud-manager.py)
├── shell/                 # Steam Desktop shell navigation & global chrome
│   ├── import-lua/        # Modular source components
│   │   ├── styles.css
│   │   ├── manual-install.html
│   │   ├── utils.js
│   │   ├── styles.js
│   │   ├── cloud-file-viewer.js
│   │   ├── config-helpers.js
│   │   ├── config-sections.js
│   │   ├── config-dialogs.js
│   │   ├── config-controller.js
│   │   ├── manual-install.js
│   │   ├── supernav.js
│   │   └── main.js
│   ├── import-lua-script.js # Bundled self-contained artifact (via tools/bundle-import-lua.py)
│   └── version-top-bar.js
└── vendor/                # Vendored third-party dependencies
    └── jszip.min.js
```

---

## Category Specifications

### 1. `core/` (CEF Runtime Hooks & Bootstrapping)
* **Target Context**: Global Steam CEF runtime, `SteamClient` API.
* **`navi-inject/` (Modular Sources)**:
  * Modular component source files for low-level runtime hooks and cloud fallback bootstrap.
  * Developers and agents must edit here rather than modifying the generated bundle:
    * `pending-banner.html`: HTML template for the persistent viewport restart warning banner.
    * `pending-banner.js`: Banner lifecycle controller and restart trigger handler (`updatePendingBanner`).
    * `cloud-fallback.js`: Fallback implementation of `NaviCloudManager` and security helpers (`isOriginCompliant`, `validateTargetUrl`, `secureFetch`, status polling, master toggle, provider auth).
    * `disk-space-api.js`: Remote Steam CDN depot size resolver (`fetchAppSize`), in-memory size cache (`sizeCache`), and unlocked app detection (`needsSizeInjection`).
    * `steam-apps-patch.js`: Intercepts `SteamClient.Apps.RegisterForAppDetails` and patches `appDetailsStore` with real disk space.
    * `main.js`: Primary bootstrap entry point orchestrating cloud setup and SteamClient patching.
* **`navi_inject.js` (Bundled Artifact)**:
  * Bundled standalone script injected into global Steam CEF runtime.
  * Assembled from `core/navi-inject/` via `tools/bundle-navi-inject.py` (or `make bundle-scripts`).
  * Inlines HTML templates so the script is 100% self-contained with no runtime filesystem dependencies.
  * CI and integration gates enforce sync via `python3 tools/bundle-navi-inject.py --check` in `tests/integration/test_bundle_sync.py`.

### 2. `store/` (Steam Storefront Integration)
* **Target Context**: `https://store.steampowered.com/app/*` (isolated web storefront).
* **`store-page/` (Modular Architecture)**:
  * Decomposed into modular components under `store/store-page/` to optimize agent context routing and token consumption:
    * `styles.css`: Stylesheet for loading bar animations, modal dialog cards, scrollbars, and action buttons.
    * `provider-modal.html`: Modal dialog template for Lua download providers and ordering.
    * `remove-modal.html`: Modal confirmation dialog template for uninstalling Lua and game files.
    * `config-modal.html`: Modal dialog template for configuring API credentials (HubcapDB, Ryuu, DepotBox).
    * `styles.js`: Stylesheet injector adding `SLS_STORE_STYLES` into `<style id="sls-bar-style">`.
    * `cdp-bridge.js`: CDP communication queue bridge (`window.naviFetch`, `window.__naviQueue`, `naviRequest`).
    * `gamepad.js`: Steam Gamepad controller modal navigation, focus trapping, and Steam React Fiber focus node registration.
    * `provider-config.js`: Lua provider definitions, key sanitization, API key fetch/store, and download request dispatcher.
    * `download-tracker.js`: Download progress tracking, row loading bar animations, and session state persistence.
    * `modals.js`: Modal dialog controllers for provider selection, removal confirmation, and API credentials.
    * `buttons.js`: Store page action button injection and controller setup for desktop and Big Picture modes.
    * `ownership-decoration.js`: Lua library ownership synchronization and GDynamicStore 'IN LIBRARY' decoration in store pages.
    * `main.js`: Initialization lifecycle, mutation observer setup, and debounced store page runner.
* **`store-page-script.js` (Bundled Artifact)**:
  * Injects "Download Lua" action buttons into store purchase areas and decorates owned items with "IN LIBRARY" labels.
  * Assembled from `store/store-page/` via `tools/bundle-store-page.py` (or `make bundle-scripts`).
  * Inlines HTML templates and CSS styles so the script is 100% self-contained with zero runtime filesystem dependencies.
  * CI and integration gates enforce sync via `python3 tools/bundle-store-page.py --check` in `tests/integration/test_bundle_sync.py`.
* **`depot-selection/` (Modular Architecture)**:
  * Decomposed into modular components under `store/depot-selection/` to optimize agent context routing and token consumption:
    * `styles.css`: Stylesheet for depot selection modal, item checkboxes, badges, and animations.
    * `modal.html`: Modal dialog template containing header, depot list container, and footer action buttons.
    * `gamepad.js`: Steam Gamepad controller modal navigation, focus trapping, and button handling.
    * `utils.js`: Formatting utilities for byte sizes and OS badge generation (`formatBytes`, `getOsInfo`, `buildOsBadge`).
    * `styles.js`: Style injector inserting `SLS_DEPOT_STYLES` into the overlay container.
    * `depot-resolver.js`: Async SteamCMD info resolver for DLC names, sizes, and OS configurations (`resolveSteamCmdMetadata`).
    * `main.js`: Modal lifecycle, depot list fetcher, checkbox state coordinator, and download trigger.
* **`depot-selection-script.js` (Bundled Artifact)**:
  * Interactive modal dialog for selecting game depots, DLCs, and manifests when downloading or spoofing a game from the store.
  * Assembled from `store/depot-selection/` via `tools/bundle-depot-selection.py` (or `make bundle-scripts`).
  * Inlines HTML templates and CSS styles so the script is 100% self-contained with zero runtime filesystem dependencies.
  * CI and integration gates enforce sync via `python3 tools/bundle-depot-selection.py --check` in `tests/integration/test_bundle_sync.py`.

### 3. `library/` (Steam Library & Game Management)
* **Target Context**: `https://steamloopback.host/library/*` (Steam client library view).
* **`app-details/` (Modular Sources)**:
  * Modular component source files for library app actions and modals.
  * Developers and agents must edit here rather than modifying the generated bundle:
    * `styles.css`: CSS styles (modal cards, overlays, `.sls-btn-action`, `.sls-btn-close`, restore items, hover/active states).
    * `gamepad.js`: Steam React Fiber focus node registration (`registerSteamFocusNode`) and virtual gamepad modal focus navigation/trapping (`trapSteamGamepadModal`).
    * `appid.js`: Steam DOM & React Fiber AppID extraction logic (`extractAppId`).
    * `cloud-api.js`: Cloud save operations helper functions (`fetchAppCloudStatus`, `toggleAppCloudRedirect`, `backupAppCloudSave`).
    * `game-manager-modal.html` & `game-manager-modal.js`: SLS Game Manager modal template and Online-Fix / AutoCrack logic (`openSlsConfig`).
    * `provider-modal.html` & `provider-modal.js`: Provider selection modal template, list rendering, download tracking, and status polling (`openChangeProviderModal`, `luaProviders`).
    * `remove-modal.html` & `remove-modal.js`: Remove Lua confirmation modal template and removal execution (`openRemoveConfirmModal`).
    * `restore-modal.html` & `restore-modal.js`: Restore Cloud Save modal template, backup snapshots listing, and restore queuing (`openRestoreModal`).
    * `reset-modal.html` & `reset-modal.js`: Reset Cloud Save modal template, typing validation (`RESET`), and reset queuing (`openResetModal`).
    * `navi-modal.html` & `navi-modal.js`: Steam Deck / Big Picture unified Navi menu template and actions (`openNaviModal`).
    * `buttons.js`: Steam-native button builder (`makeSteamButton`), Big Picture unified button injection, and Desktop action row injection (`createRemoveButton`).
    * `main.js`: Core bootstrapping, observer, debouncing, `__slsRefreshButtons`, `checkAndCreateButton`, and `addRemoveLuaButton`.
* **`app-details-script.js` (Bundled Artifact)**:
  * Injected into the library game details page.
  * Assembled from `library/app-details/` via `tools/bundle-app-details.py` (or `make bundle-scripts`).
  * Inlines HTML templates and CSS styles so the script is 100% self-contained with no runtime filesystem dependencies.
  * CI and integration gates enforce sync via `python3 tools/bundle-app-details.py --check` in `tests/integration/test_bundle_sync.py`.
* **`auto-collection.js`**:
  * Automatically syncs unlocked games into the "LUAs" collection in `window.collectionStore`.

### 4. `cloud/` (Cloud Saves & Remote Storage Management)
* **Target Context**: Steam Client UI & Cloud Settings modals.
* **`cloud-manager/` (Modular Sources)**:
  * Modular component source files for CloudRedirect settings, file browsing, and modals.
  * Developers and agents must edit here rather than modifying the generated bundle:
    * `styles.css`: CSS styles (Steam Cloud file table, responsive layout, table headers/cells).
    * `state.js`: Core runtime state object (`state`), backend origins, and polling intervals.
    * `security.js`: CEF origin compliance checks, loopback address validation, and `secureFetch` transport wrapper.
    * `gamepad.js`: Steam Deck / Big Picture gamepad modal navigation and focus trapping (`trapSteamGamepadModal`).
    * `pending-banner.js`: Persistent viewport restart warning banner (`updatePendingBanner`) for pending operations.
    * `api.js`: Client-side REST API wrapper for engine CloudRedirect endpoints (`/cloud/*`, `/restart`).
    * `reset-modal.html` & `reset-modal.js`: Typed RESET confirmation modal template and execution logic (`resetAppSave`).
    * `restore-modal.html` & `restore-modal.js`: Restore Cloud Save modal template, backup snapshots listing, and restore queuing (`openRestoreModal`).
    * `settings-modal.html` & `settings-modal.js`: Global Cloud Saves Settings modal template, master toggle, provider authentication, and status polling (`openSettingsModal`).
    * `utils.js`: Data formatting (`formatBytes`, `formatSteamCloudDate`) and Steam AppID game title resolution cache (`resolveGameTitle`).
    * `cloud-files-modal.html` & `cloud-files-modal.js`: Steam-styled "View Steam Cloud" file explorer modal template, file listing, download handler, and table styles injection (`openCloudFilesModal`).
    * `main.js`: Core polling tick (`poll`, `startPolling`, `stopPolling`), `window.NaviCloudManager` public API namespace exports, and `DOMContentLoaded` bootstrap.
* **`cloud_manager.js` (Bundled Artifact)**:
  * Injected into the Steam CEF runtime for cloud save settings and modals.
  * Assembled from `cloud/cloud-manager/` via `tools/bundle-cloud-manager.py` (or `make bundle-scripts`).
  * Inlines HTML templates and CSS styles so the script is 100% self-contained with no runtime filesystem dependencies.
  * CI and integration gates enforce sync via `python3 tools/bundle-cloud-manager.py --check` in `tests/integration/test_bundle_sync.py`.

### 5. `shell/` (Steam Desktop Shell & Global Chrome)
* **Target Context**: Steam SuperNav top bar (`https://steamloopback.host/index.html*`).
* **`import-lua/` (Modular Architecture)**:
  * Decomposed into modular components under `shell/import-lua/` to optimize agent context routing and token consumption:
    * `styles.css`: Stylesheet for custom Steam top-bar tabs, Navi config shell, cards, and manual install modal.
    * `manual-install.html`: Modal markup for manual Lua script and manifest depot cache installation.
    * `utils.js`: Core utilities, Steam class definitions, JSZip loader, and SuperNav helpers.
    * `styles.js`: Style injector inserting `SLS_SHELL_STYLES` with runtime Steam class substitutions into `document.head`.
    * `cloud-file-viewer.js`: Collapsible folder accordion and preview module (`CloudFileViewerModule`) for game cloud saves.
    * `config-helpers.js`: HTML generation helper components for Navi configuration sections and cards.
    * `config-sections.js`: HTML section templates (S1–S8, sidebar, header) and layout assembler (`buildConfigSectionsHtml`).
    * `config-dialogs.js`: Modal confirmation dialogs (`slsConfirmDialog`) and status alert banners (`showStatusBanner`).
    * `config-controller.js`: Navi configuration page controller, settings editor, saves table, and save pipeline (`renderConfigPage`, `showConfigPage`, `hideConfigPage`).
    * `manual-install.js`: Manual Lua/manifest installation page controller, dropzone, and file processor (`showSlsPage`, `initManualInstallPage`).
    * `supernav.js`: SuperNav top-bar tab injector, history sync, and DOM mutation observer (`createSuperNavTab`, `initSlsTabAndPage`).
    * `main.js`: Main entry point, double-injection guard, window exports, and initialization.
* **`import-lua-script.js` (Bundled Artifact)**:
  * Injects the "Import Lua" and "Navi Config" top navigation tabs into Steam's SuperNav container.
  * Provides drag-and-drop file import, ZIP archive unpacking, Lua manifest parsing, and full Navi settings UI.
  * Assembled from `shell/import-lua/` via `tools/bundle-import-lua.py` (or `make bundle-scripts`).
  * Inlines HTML templates and CSS styles so the script is 100% self-contained with zero runtime filesystem dependencies.
  * CI and integration gates enforce sync via `python3 tools/bundle-import-lua.py --check` in `tests/integration/test_bundle_sync.py`.
* **`version-top-bar.js`**:
  * Injects the Navi version indicator badge (`Navi %VERSION%`) into Steam's header navigation.

### 6. `vendor/` (Vendored Dependencies)
* **`jszip.min.js`**:
  * Vendored JSZip v3.10.1 library for client-side ZIP archive extraction.
  * Isolated in `vendor/` so RAG embeddings and chunking pipelines can ignore or deprioritize minified third-party code.

---

## Backward Compatibility & Dynamic Resolution

Root-level relative symlinks are preserved in `res/inject-scripts/` pointing to each categorized script. The C++ resource loader in `src/view/cdpinject.cpp` and `src/presenter/removelua.cpp` resolves files by both flat name (`script.js`) and category path (`category/script.js`).

