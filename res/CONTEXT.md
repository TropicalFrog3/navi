# CONTEXT.md — `res/` Runtime Resources & CEF Injection Workspace

## Workspace Role & Workflow

This room owns everything the installed client loads at runtime: `res/inject-scripts/`
(CEF JS by category — `core/`, `store/`, `library/`, `cloud/`, `shell/`, `vendor/`),
config template + declarative migration rules, `cloud_redirect.so` + `cloud_redirect_cli`,
version file, branding (`res/SLSlogos/`). Work sequence: (1) read
`res/inject-scripts/README.md` before touching any script — it is the category map and
per-file spec; (2) edit inside the existing category folders; (3) bundle your specific
component via `./install.sh bundle-scripts <target>` (`library`, `cloud`, `core`, `lua`, `depots`, `store`);
(4) verify changes with path-scoped checks (`git status -s -- res/inject-scripts/<category>/`
and `git diff -- res/inject-scripts/<category>/`) — never run bare repo-wide git status on shared trees;
(5) run targeted bundle sync (`pytest tests/integration/test_bundle_sync.py`); (6) run UI
gates (`./install.sh test-ui`, `./install.sh test-flow`) only for structural flow changes.
`res/SLSlogos/` is a git submodule: never modify or commit into it.

| Task / Stage | Read These Files | Skip These Files | Skills / MCPs / Commands Needed |
|---|---|---|---|
| Library UI & Navi Menu | `res/inject-scripts/library/app-details/navi-modal.html` or target file in `library/app-details/` | `src/`, `tests/`, other `inject-scripts/` | `./install.sh bundle-scripts library`; `pytest tests/integration/test_bundle_sync.py`; scoped: `git status -s -- res/inject-scripts/library/` |
| Cloud Manager UI | `res/inject-scripts/cloud/cloud-manager/` target files | `src/`, `tests/`, other `inject-scripts/` | `./install.sh bundle-scripts cloud`; `pytest tests/integration/test_bundle_sync.py`; scoped: `git status -s -- res/inject-scripts/cloud/` |
| Core Runtime Injection | `res/inject-scripts/core/navi-inject/` target files | `src/`, `tests/`, other `inject-scripts/` | `./install.sh bundle-scripts core`; `pytest tests/integration/test_bundle_sync.py`; scoped: `git status -s -- res/inject-scripts/core/` |
| Store & Shell Injectors | `res/inject-scripts/{store,shell}/` target files | `src/`, `tests/`, other `inject-scripts/` | `./install.sh bundle-scripts store` (or `lua`/`depots`); scoped: `git status -s -- res/inject-scripts/` |
| Config template change | `res/config.yaml`, `res/config.diff.yaml`, `res/cache.diff.yaml`, `src/model/migrations/`, `tests/unit/test_config.cpp` | `res/inject-scripts/` | `./install.sh test`; follow the `$(OldKey)$(NewKey):` bridge syntax (actions: rename, rename_and_convert, remove, convert) |
| `cloud_redirect.so` replacement | `.gitignore` exception line `!res/cloud_redirect.so`, `docs/references/cloud-redirect-architecture.md` (§1 Components), `src/core/cloud_oauth.*` | `bin/`, `pkg/` | `./install.sh check-cloudredirect` — engine version (v2.6.5) and docs must stay in lockstep |
| Version / release bump | `res/version` (`Version: beta-v1.2.0` + Changelog — feeds Makefile `DATE`), `res/updates.yaml` (SafeModeHashes per client build) | `bin/`, `obj/` | Skill `release-workflow` owns `./install.sh release` — orchestrator-only |
| Launcher assets / updater | `pkg/navi/navi.desktop`, `pkg/navi/navi-native.desktop`, `scripts/update-all.sh` | `res/SLSlogos/` (git submodule — read-only), `res/vendor`-style blobs | — |

## File Manifest

<!-- MANIFEST:BEGIN -->
| File | Purpose |
|---|---|
| `inject-scripts/cloud/cloud-manager/api.js` | Client-side REST API wrapper for engine CloudRedirect endpoints |
| `inject-scripts/cloud/cloud-manager/cloud-files-modal.js` | Steam-styled per-game cloud files explorer modal controller |
| `inject-scripts/cloud/cloud-manager/gamepad.js` | Steam Gamepad controller modal navigation and focus trapping handler |
| `inject-scripts/cloud/cloud-manager/main.js` | Lifecycle initialization, polling coordinator, and public NaviCloudManager API export |
| `inject-scripts/cloud/cloud-manager/pending-banner.js` | Persistent restart warning banner for pending cloud save operations |
| `inject-scripts/cloud/cloud-manager/reset-modal.js` | Confirmation modal controller for resetting cloud save directories |
| `inject-scripts/cloud/cloud-manager/restore-modal.js` | Modal controller for browsing and restoring cloud save backups |
| `inject-scripts/cloud/cloud-manager/security.js` | CEF origin verification, loopback URL validation, and secureFetch transport wrapper |
| `inject-scripts/cloud/cloud-manager/settings-modal.js` | Global cloud save settings and provider authentication modal controller |
| `inject-scripts/cloud/cloud-manager/state.js` | Core runtime state and polling intervals for Cloud Saves manager |
| `inject-scripts/cloud/cloud-manager/utils.js` | Data formatting helpers and Steam game title resolution cache |
| `inject-scripts/cloud/cloud_manager.js` | Client-side CloudRedirect UI manager for Steam CEF settings |
| `inject-scripts/core/navi-inject/cloud-fallback.js` | Standalone fallback implementation of NaviCloudManager when cloud_manager.js is not loaded |
| `inject-scripts/core/navi-inject/disk-space-api.js` | App install size resolver, depot manifest cache, and unlocked app detection |
| `inject-scripts/core/navi-inject/main.js` | Orchestrates cloud bootstrap and SteamClient disk size patching |
| `inject-scripts/core/navi-inject/pending-banner.js` | Viewport warning banner controller for pending cloud save restart operations |
| `inject-scripts/core/navi-inject/steam-apps-patch.js` | Intercepts SteamClient.Apps.RegisterForAppDetails to provide accurate disk requirements |
| `inject-scripts/core/navi_inject.js` | Primary CEF JavaScript injection bootstrap and event coordinator |
| `inject-scripts/library/app-details/appid.js` | AppId resolution and extraction from current library DOM state |
| `inject-scripts/library/app-details/buttons.js` | DOM button injectors for Navi and CloudRedirect library actions |
| `inject-scripts/library/app-details/cloud-api.js` | Client-side REST API wrapper for engine CloudRedirect endpoints |
| `inject-scripts/library/app-details/game-manager-modal.js` | Modal UI for configuring game-specific depot and save settings |
| `inject-scripts/library/app-details/gamepad.js` | Steam Deck gamepad controller navigation handler for library modals |
| `inject-scripts/library/app-details/main.js` | Modular entry point for library app details page enhancements |
| `inject-scripts/library/app-details/navi-modal.js` | Base modal dialog container and backdrop management for CEF |
| `inject-scripts/library/app-details/provider-modal.js` | Modal UI for configuring CloudRedirect cloud storage providers |
| `inject-scripts/library/app-details/remove-modal.js` | Confirmation modal for removing Lua plugins and resetting saves |
| `inject-scripts/library/app-details/reset-modal.js` | Confirmation modal for resetting local save games from cloud backups |
| `inject-scripts/library/app-details/restore-modal.js` | Modal UI for browsing and restoring historical cloud save snapshots |
| `inject-scripts/library/app-details-script.js` | Bundled standalone script for Steam library game details page |
| `inject-scripts/library/auto-collection.js` | Automatically organizes unlocked games into Steam library collections |
| `inject-scripts/shell/import-lua/cloud-file-viewer.js` | Collapsible folder accordion and preview module for game cloud saves |
| `inject-scripts/shell/import-lua/config-controller.js` | Navi configuration page controller, settings editor, saves table, and save pipeline |
| `inject-scripts/shell/import-lua/config-dialogs.js` | Modal confirmation dialogs and status alert banners for configuration UI |
| `inject-scripts/shell/import-lua/config-helpers.js` | HTML generation helper components for Navi configuration sections and cards |
| `inject-scripts/shell/import-lua/config-sections.js` | HTML section templates and layout assembler for Navi configuration UI |
| `inject-scripts/shell/import-lua/main.js` | Main entry point, double-injection guard, window exports, and initialization |
| `inject-scripts/shell/import-lua/manual-install.js` | Manual Lua/manifest installation page controller, dropzone, and file processor |
| `inject-scripts/shell/import-lua/styles.js` | Injects custom CSS rules for Navi top-bar tabs, config shell, and manual install modal |
| `inject-scripts/shell/import-lua/supernav.js` | SuperNav top-bar tab injector, history sync, and DOM mutation observer |
| `inject-scripts/shell/import-lua/utils.js` | Core utilities, Steam class definitions, JSZip loader, and SuperNav helpers |
| `inject-scripts/shell/import-lua-script.js` | UI modal enabling drag-and-drop or manual import of Lua manifests |
| `inject-scripts/shell/version-top-bar.js` | Renders the Navi version banner in Steam client navigation header |
| `inject-scripts/store/depot-selection/depot-resolver.js` | Async SteamCMD info resolver for DLC names, sizes, and OS configurations |
| `inject-scripts/store/depot-selection/gamepad.js` | Steam Gamepad controller modal navigation, focus trapping, and button handling |
| `inject-scripts/store/depot-selection/main.js` | Modal lifecycle, depot list fetcher, checkbox state coordinator, and download trigger |
| `inject-scripts/store/depot-selection/styles.js` | Injects stylesheet for depot selection dialog into overlay container |
| `inject-scripts/store/depot-selection/utils.js` | Formatting utilities for byte sizes and OS badge generation |
| `inject-scripts/store/depot-selection-script.js` | UI modal for selecting specific depots to download on store pages |
| `inject-scripts/store/store-page/buttons.js` | Store page action button injection and controller setup for desktop and Big Picture modes |
| `inject-scripts/store/store-page/cdp-bridge.js` | CDP communication bridge and network request helpers for store pages |
| `inject-scripts/store/store-page/download-tracker.js` | Progress tracking, row loading bar animations, and download session state management |
| `inject-scripts/store/store-page/gamepad.js` | Steam Deck and controller navigation focus trapping for store page modals and buttons |
| `inject-scripts/store/store-page/main.js` | Initialization lifecycle, mutation observer setup, and debounced store page runner |
| `inject-scripts/store/store-page/modals.js` | Modal controllers for Lua provider configuration, removal confirmation, and API credentials |
| `inject-scripts/store/store-page/ownership-decoration.js` | Lua library ownership synchronization and GDynamicStore 'IN LIBRARY' decoration in store pages |
| `inject-scripts/store/store-page/provider-config.js` | Lua provider settings, API key synchronization, and download request dispatcher |
| `inject-scripts/store/store-page/styles.js` | Dynamic stylesheet injection for store page elements and modals |
| `inject-scripts/store/store-page-script.js` | Injects unlock and depot download buttons into Steam store pages |
<!-- MANIFEST:END -->
