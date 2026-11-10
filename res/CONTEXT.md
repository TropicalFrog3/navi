# CONTEXT.md — `res/` Runtime Resources & CEF Injection Workspace

## Workspace Role & Workflow

This room owns everything the installed client loads at runtime: `res/inject-scripts/`
(CEF JS by category — `core/`, `store/`, `library/`, `cloud/`, `shell/`, `vendor/`),
config template + declarative migration rules, `cloud_redirect.so` + `cloud_redirect_cli`,
version file, branding (`res/SLSlogos/`). Work sequence: (1) read
`res/inject-scripts/README.md` before touching any script — it is the category map and
per-file spec; (2) edit inside the existing category folders; (3) confirm the loader
search paths in `src/view/cdpinject.cpp:850-875` still cover your file; (4) run the UI
gates (`./install.sh test-ui`, `./install.sh test-flow`); (5) release packaging copies this folder
verbatim (`scripts/lib/release.sh`) — never rename without updating that flow.
`res/SLSlogos/` is a git submodule: never modify or commit into it.

| Task / Stage | Read These Files | Skip These Files | Skills / MCPs / Commands Needed |
|---|---|---|---|
| CEF inject-script change | `res/inject-scripts/README.md`, target script inside its category dir (for library app details: edit modular `res/inject-scripts/library/app-details/` and run `./install.sh bundle-scripts`), `src/view/cdpinject.cpp` (search dirs) | `src/core/`, `src/model/`, `pkg/`, `bin/` | `./install.sh bundle-scripts`; `./install.sh test-ui` / `./install.sh test-flow`; MCP `vibe-annotations` + Skill `vibe-implement-annotations` for annotation work; CDP live check via Skill `cef-cdp-injection` |
| Config template change | `res/config.yaml`, `res/config.diff.yaml`, `res/cache.diff.yaml`, `src/model/migrations/`, `tests/unit/test_config.cpp` | `res/inject-scripts/` | `./install.sh test`; follow the `$(OldKey)$(NewKey):` bridge syntax (actions: rename, rename_and_convert, remove, convert) |
| `cloud_redirect.so` replacement | `.gitignore` exception line `!res/cloud_redirect.so`, `docs/references/cloud-redirect-architecture.md` (§1 Components), `src/core/cloud_oauth.*` | `bin/`, `pkg/` | `./install.sh check-cloudredirect` — engine version (v2.6.5) and docs must stay in lockstep |
| Version / release bump | `res/version` (`Version: beta-v1.2.0` + Changelog — feeds Makefile `DATE`), `res/updates.yaml` (SafeModeHashes per client build) | `bin/`, `obj/` | Skill `release-workflow` owns `./install.sh release` — orchestrator-only |
| Launcher assets / updater | `pkg/navi/navi.desktop`, `pkg/navi/navi-native.desktop`, `scripts/update-all.sh` | `res/SLSlogos/` (git submodule — read-only), `res/vendor`-style blobs | — |

## File Manifest

<!-- MANIFEST:BEGIN -->
| File | Purpose |
|---|---|
| `inject-scripts/cloud/cloud_manager.js` | Client-side CloudRedirect UI manager for Steam CEF settings |
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
| `inject-scripts/shell/import-lua-script.js` | UI modal enabling drag-and-drop or manual import of Lua manifests |
| `inject-scripts/shell/version-top-bar.js` | Renders the Navi version banner in Steam client navigation header |
| `inject-scripts/store/depot-selection-script.js` | UI modal for selecting specific depots to download on store pages |
| `inject-scripts/store/store-page-script.js` | Injects unlock and depot download buttons into Steam store pages |
<!-- MANIFEST:END -->
