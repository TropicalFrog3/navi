# Feature Spec: Move "Import Lua Files" Page into Navi Config Sidebar

- **Status:** Spec v1
- **Category:** Core Feature
- **Owning Layer:** CEF Shell Inject (`res/inject-scripts/shell/import-lua/`) & UI Integration Tests (`tests/integration/ui/`)
- **Source Wishlist Item:** `plans/01-backlog/dev-plan.md` — `- move the import lua files page into a section in navi config` (`put a import lua file title on the side where are the title section (cloud saves, provider keys, unlocking, account, etc...)`; `when click on the title on the side it open a same model design like the navi config, don't recopy the same design as the currently using one for import lua files.`)

---

## 1. Problem Statement & Architecture Overview

Currently, Navi injects **two** separate custom tabs into Steam's top SuperNav bar via [`res/inject-scripts/shell/import-lua/supernav.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/supernav.js#L42-L65):
- `#sls-top-tab` (**Import Lua files**) -> mounts `#sls-page-container` (`manual-install.js:5-29`) using [`manual-install.html`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/manual-install.html) with an older standalone page design (bright gradient background `radial-gradient(...)`, `rgba(255,255,255,0.02)` outer card, and custom cyan header).
- `#sls-config-top-tab` (**Navi Config**) -> mounts `#sls-config-page-container` (`config-controller.js:20-60`) using the redesigned sidebar + card design (`#14171d` background, `#191c24` sidebar, `#1c202a` `makeCard` panels, `#171a22` `sls-cfg-item` rows, `.sls-cfg-btn` / `.sls-cfg-btn-primary` buttons).

Moving **Import Lua files** inside the Navi Config sidebar unifies both Navi workflows into a single **Navi Config** SuperNav tab, eliminates the legacy standalone page look, and ensures the dropzone and staged file list adopt the `#1c202a` / `#171a22` Navi Config card design tokens.

---

## 2. Target Files & Symbols

| File | Symbol / Selector | Change Summary |
|---|---|---|
| [`res/inject-scripts/shell/import-lua/config-sections.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/config-sections.js#L219-L505) | `buildConfigSectionsHtml(cfg)`, sidebar `.sls-cfg-tablist`, `secImportHtml` | Add `Import Lua files` `.sls-cfg-pill[data-target="sls-cfg-sec-import"]` in the sidebar and build `secImportHtml = makeSection('sls-cfg-sec-import', ...)` using `makeCard(...)` + Navi Config dropzone/staged-files design |
| [`res/inject-scripts/shell/import-lua/config-controller.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/config-controller.js#L20-L153) | `showConfigPage(initialTabId)`, `renderConfigPage(container, cfg, sync, initialTabId)`, `TAB_METAS`, `switchTab()` | Register `'sls-cfg-sec-import'` in `TAB_METAS`, support opening directly to an initial tab (`initialTabId`), and initialize `initManualInstallPage()` once inside `#sls-config-page-container` after rendering |
| [`res/inject-scripts/shell/import-lua/manual-install.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/manual-install.js#L5-L457) | `showSlsPage()`, `hideSlsPage()`, `initManualInstallPage()`, `updateFileList()`, `#sls-manual-file-list` | Route `showSlsPage()` / `hideSlsPage()` to `showConfigPage('sls-cfg-sec-import')` / `hideConfigPage()`; restyle staged `<details>` items, drag-leave defaults (`#171a22` / `rgba(255,255,255,0.12)`), and buttons to match Navi Config `.sls-cfg-item` + `.sls-cfg-btn` chrome; remove `#sls-page-container` references during scroll reflow |
| [`res/inject-scripts/shell/import-lua/manual-install.html`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/manual-install.html) | Legacy template (verify bundle-import-lua.py usage) | Deprecate/empty standalone gradient wrapper or update [`scripts/bundle-import-lua.py`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/scripts/bundle-import-lua.py) so `MANUAL_INSTALL_HTML` is no longer injected as an obsolete full-page gradient |
| [`res/inject-scripts/shell/import-lua/supernav.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/supernav.js#L42-L88) | `addSlsTab()`, `initSlsTabAndPage()` | Remove separate `#sls-top-tab` (`Import Lua files`) top-bar tab injection so only `#sls-config-top-tab` (`Navi Config`) appears in Steam's SuperNav |
| [`res/inject-scripts/shell/import-lua/utils.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/utils.js#L81-L93) | `setActiveTabStyle(activeTab)` | Simplify active-tab toggle for `#sls-config-top-tab` (null-safe if `#sls-top-tab` is absent) |
| [`res/inject-scripts/shell/import-lua-script.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua-script.js) | Bundled artifact | Regenerated via `./install.sh bundle-scripts lua` |
| [`tests/integration/ui/pages/import_config_page.py`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/tests/integration/ui/pages/import_config_page.py#L28-L80) | `ImportConfigPage` | Update `open()` / `is_open()` / `container_selector` so opening Import Lua opens `#sls-config-page-container` and selects `.sls-cfg-pill[data-target="sls-cfg-sec-import"]` (preserving all `#sls-manual-*` child selectors) |
| [`tests/integration/ui/test_uj03_manual_upload_dragdrop.py`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/tests/integration/ui/test_uj03_manual_upload_dragdrop.py) & [`test_uj05_config_editor_validation.py`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/tests/integration/ui/test_uj05_config_editor_validation.py#L200-L210) | Tab switching & drag-drop specs | Update fixture expectations for `#sls-config-top-tab` + `[data-target="sls-cfg-sec-import"]` (replacing `#sls-top-tab` <-> `#sls-config-top-tab` test in `test_uj05:200-210`) |

---

## 3. Detailed Implementation Steps

### Step 3.1 — Add Sidebar Pill & Redesigned Section in `config-sections.js`
1. In [`res/inject-scripts/shell/import-lua/config-sections.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/config-sections.js#L434-L477), add a sidebar `.sls-cfg-pill` item (`data-target="sls-cfg-sec-import"`) alongside the other navigation items (e.g., right after `Library` or `Unlocking`) with an upload/import SVG icon and label `<span>Import Lua files</span>`.
2. Build `secImportHtml = makeSection('sls-cfg-sec-import', 'Import Lua files', ..., ..., false)` using Navi Config `makeCard` styling (NOT the old gradient wrapper):
   - **Card 1 (`Add game files`, subtitle `'Drop .lua, .manifest, or .zip files anywhere below, or browse your computer.'`)**:
     - Inner `#sls-manual-dropzone` with Navi Config styling (`min-height: 168px; border: 1.5px dashed rgba(255, 255, 255, 0.14); border-radius: 6px; background: #171a22; padding: 28px 20px; ...`).
     - Action buttons `#sls-manual-select-files` (`Select files`) and `#sls-manual-select-folder` (`Select folder`) using `class="sls-cfg-btn"`.
   - **Card 2 (`Queued files`, subtitle `'Review detected apps, toggle individual depots, then click Import.'`)**:
     - `#sls-manual-file-list` (styled cleanly on `#171a22` rows)
     - `#sls-manual-status` (`role="status" aria-live="polite"`)
     - `#sls-manual-submit` (`class="sls-cfg-btn sls-cfg-btn-primary"`, initially `disabled`, keeping existing enable/disable behavior cleanly)
3. Append `secImportHtml` into `dialogContentHtml` (`config-sections.js:492-501`).

### Step 3.2 — Wire Tab Routing & Lifecycle in `config-controller.js`
1. Add `'sls-cfg-sec-import'` to `TAB_METAS` (`config-controller.js:89-118`):
   ```js
   'sls-cfg-sec-import': {
       title: 'Import Lua files',
       desc: 'Manually add .lua scripts, depot manifests, and zip bundles into your Steam library.'
   }
   ```
2. Update `showConfigPage(initialTabId)` (`config-controller.js:20`) and `renderConfigPage(container, cfg, sync, initialTabId)` (`config-controller.js:63`):
   - Note: if `#sls-config-page-container` is already mounted and `initialTabId` is passed, allow switching directly to `initialTabId` (`switchTab(initialTabId)`) without losing unsaved edits in other tabs; otherwise fetch `/config/full` and call `renderConfigPage(..., initialTabId)`.
   - Inside `renderConfigPage`, initialize `activeTabId = (initialTabId && TAB_METAS[initialTabId]) ? initialTabId : 'sls-cfg-sec-cloudsaves';` and call `switchTab(activeTabId);`.
   - Call `initManualInstallPage()` once immediately after `container.innerHTML = ...` renders so `#sls-manual-dropzone` and file inputs bind cleanly inside `#sls-config-page-container`.
3. Clean up hidden DOM `<input type="file">` elements created by `initManualInstallPage()` on re-render (assign unique IDs or remove prior hidden file inputs on re-init so repeated `renderConfigPage` calls never leak detached `<input>` elements on `document.body`).

### Step 3.3 — Restyle & Delegate `manual-install.js`, `supernav.js`, and `manual-install.html`
1. Inspect [`scripts/bundle-import-lua.py`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/scripts/bundle-import-lua.py) during implementation to check how `manual-install.html` is embedded; either retire `MANUAL_INSTALL_HTML` cleanly or keep it unused/aligned with `bundle-import-lua.py` + `tests/integration/test_bundle_sync.py`.
2. In [`manual-install.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/manual-install.js):
   - Update `showSlsPage()` to delegate to `showConfigPage('sls-cfg-sec-import')` (so `window.__naviShowImportLuaPage()` still opens directly to the Import Lua section!).
   - Restyle `updateFileList()` `<details>` items (`line 533`) from `rgba(255,255,255,0.03)` to `#171a22` (`border: 1px solid rgba(255, 255, 255, 0.06)`), content panels from `rgba(0,0,0,0.1)` to `#14171d`, and buttons to `.sls-cfg-btn` chrome.
   - Update `dropzone.ondragleave` / `dropzone.ondrop` (`lines 221–230`) to restore `borderColor = 'rgba(255, 255, 255, 0.14)'` and `background = '#171a22'`.
   - Update `item.ontoggle` reflow hack (`lines 548–554`) to reference `#sls-config-page-container .sls-cfg-dialog-body` instead of removed `#sls-page-container`.
3. In [`supernav.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/supernav.js#L42-L65):
   - Remove the separate `#sls-top-tab` (`Import Lua files`) creation so only `#sls-config-top-tab` (`Navi Config`) appears in Steam's top SuperNav bar.

### Step 3.4 — Update Page Objects & UI Journey Tests
- Check `CallbackServerStub` in [`tests/integration/ui/harness/callback_server_stub.py`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/tests/integration/ui/harness/callback_server_stub.py) — ensure `/config/full` responds cleanly when `test_uj03_manual_upload_dragdrop.py` opens Import Lua via `showConfigPage('sls-cfg-sec-import')`.
- Update `ImportConfigPage` in [`tests/integration/ui/pages/import_config_page.py`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/tests/integration/ui/pages/import_config_page.py#L28-L78):
  - `container_selector = "#sls-config-page-container"`, `is_open()` checks that `#sls-config-page-container` AND `#sls-cfg-sec-import` are both visible, and `open()` calls `window.__naviShowImportLuaPage()` or opens `#sls-config-top-tab` + clicks `.sls-cfg-pill[data-target="sls-cfg-sec-import"]`.
- Update [`tests/integration/ui/test_uj05_config_editor_validation.py`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/tests/integration/ui/test_uj05_config_editor_validation.py#L200-L210) section 3 (`tab switching`) to verify switching between `sls-cfg-sec-cloudsaves` and `sls-cfg-sec-import` sidebar pills preserves dirty/clean state inside `#sls-config-page-container` and confirms `#sls-top-tab` is no longer injected into the top bar.

### Step 3.5 — Bundle Scripts
Run `./install.sh bundle-scripts lua`.

---

## 4. Domain Invariants & Rules

- [`res/inject-scripts/shell/import-lua/config-controller.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/config-controller.js): `Import Lua files` performs file uploads via `/manual-install`, NOT via `Save changes` (`/config/full`); ensure `serializeCurrentForm()` does NOT include `#sls-manual-*` elements so staging files never marks config settings dirty (`window.__naviConfigDirty` must remain unaffected!).
- [`.agents/rules/steam_cef_ui_patterns.md`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/.agents/rules/steam_cef_ui_patterns.md#L69): Keep all `#sls-manual-*` IDs (`#sls-manual-dropzone`, `#sls-manual-file-list`, `#sls-manual-select-files`, `#sls-manual-select-folder`, `#sls-manual-status`, `#sls-manual-submit`) and `window.__naviShowImportLuaPage` export intact.
- [`res/CONTEXT.md`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/CONTEXT.md): Re-bundle via `./install.sh bundle-scripts lua` and keep `tests/integration/test_bundle_sync.py` green.

---

## 5. Verification Plan

1. **Build & Bundle Sync**:
   - `./install.sh bundle-scripts lua`
   - `./install.sh build`
2. **Test Gates (run only when explicitly requested by user)**:
   - Verify bundle sync (`./install.sh test-gate07` / `pytest tests/integration/test_bundle_sync.py` via `./install.sh`).
   - Run UI journey suites `./install.sh test-ui` covering `test_uj03_manual_upload_dragdrop.py`, `test_uj05_config_editor_validation.py`, and `test_uj08_a11y_gamepad_responsive.py` (confirm dropzone highlights on dragover, resets on dragleave, stages `.lua`/`.manifest`/`.zip`, submits to `/manual-install`, and does not trigger config dirty state).

---

## 6. Risks & Edge Cases

- **Double-binding hidden `<input type="file">` on re-render**: `Discard changes` (`config-controller.js:2268`) calls `renderConfigPage(...)` again! If `initManualInstallPage()` appends `<input type="file">` to `document.body` without removing any previous `#sls-manual-file-input` / `#sls-manual-folder-input`, repeated discards leak DOM inputs. Always reuse or replace fixed-ID elements (`sls-manual-file-input`, `sls-manual-folder-input`).
- **Offline/backend-down fallback on Import Lua**: Currently `showConfigPage()` fails the whole page if `GET /config/full` rejects. Verify whether `/manual-install` also requires the backend (it does, since both talk to `127.0.0.1:9001`), and keep the `#sls-cfg-retry-btn` error screen clean.
- **Settings search (`#sls-cfg-search`)**: Make sure `secImportHtml` items have appropriate `data-search="import lua manifest zip bundle install game"` attributes and handle empty/search-hidden states gracefully without breaking `Import Lua` interaction.

