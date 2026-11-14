Outcome: shipped — Added ensureLibraryViewForOverlay/detachSteamHistoryListener in utils.js, wired showConfigPage/hideConfigPage and showSlsPage/hideSlsPage to navigate to /library/home when opened from non-library tabs, enabled capture-phase SuperNav tab click dismissal, and added UI regression test — 2026-11-14

# Bug Spec: Fix Navi Config Rendering Underneath Non-Library SuperNav Tabs

- **Status:** Shipped (v1)
- **Category:** UI & UX Bug
- **Owning Layer:** CEF Shell Inject (`res/inject-scripts/shell/import-lua/`)
- **Source Wishlist Item:** `plans/01-backlog/dev-plan.md` — `- when clicking navi config tab when currently in any other tabs except library it show the navi config tabs under the tab we click from.`

---

## 1. Problem Statement & Root Cause Analysis

When the user is currently viewing **STORE**, **COMMUNITY**, **PROFILE**, or any top-bar tab other than **LIBRARY**, clicking **Navi Config** (`#sls-config-top-tab`) renders `#sls-config-page-container` (`config-controller.js:28`), **but the Store/Community page stays on top**, hiding Navi Config underneath it.

Inspection of Steam Desktop CEF architecture and [`res/inject-scripts/shell/import-lua/config-controller.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/config-controller.js#L20-L41), [`manual-install.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/manual-install.js#L5-L29), and [`utils.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/utils.js#L95-L120) reveals the exact mechanism:

1. In Steam's `SharedJSContext` (`SP`) window, **LIBRARY** renders inside the React DOM tree (`SP`), whereas **STORE**, **COMMUNITY**, and user **PROFILE** tabs mount a native OS/CEF `BrowserView` webview window layered **on top of** the entire `SP` HTML viewport. No CSS `z-index` (not even `z-index: 999999`) inside `SP` can paint above a separate native CEF `BrowserView`!
2. [`res/inject-scripts/shell/import-lua/utils.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/utils.js#L95-L120) already defines `getSteamHistory()` (which walks the React Fiber tree on the SuperNav container to obtain React Router `history`) and `var historyUnlisten = null;`, specifically designed to navigate Steam's router to `/library/home` (which immediately hides/unmounts the native `BrowserView` overlay) and auto-hide the Navi overlay if React Router subsequently navigates elsewhere.
3. Neither `showConfigPage()` (`config-controller.js:20-41`) nor `showSlsPage()` (`manual-install.js:5-29`) calls `getSteamHistory()`, leaving the native Store/Community `BrowserView` active above `#sls-config-page-container`.

---

## 2. Target Files & Symbols

| File | Symbol / Selector | Change Summary |
|---|---|---|
| [`res/inject-scripts/shell/import-lua/utils.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/utils.js#L95-L120) | `getSteamHistory()`, `navigateToLibraryForOverlay(onLeaveCallback)` | Extract a shared helper that checks `history.location.pathname`, pushes `/library/home` under a short navigation guard if not already under `/library`, and attaches/cleans up `historyUnlisten` |
| [`res/inject-scripts/shell/import-lua/config-controller.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/config-controller.js#L5-L41) | `showConfigPage()`, `hideConfigPage()` | Call the shared `navigateToLibraryForOverlay(hideConfigPage)` on open; clean up `historyUnlisten` inside `hideConfigPage()` |
| [`res/inject-scripts/shell/import-lua/manual-install.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/manual-install.js#L5-L40) | `showSlsPage()`, `hideSlsPage()` | Also invoke `navigateToLibraryForOverlay(hideSlsPage)` (or delegate to `showConfigPage` once unified in the Import Lua sidebar spec) and clear `historyUnlisten` on hide |
| [`res/inject-scripts/shell/import-lua/supernav.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/supernav.js#L73-L88) | `initSlsTabAndPage()` | Ensure native tab clicks (even clicking `LIBRARY` again while already at `/library/home`) reliably call `hideConfigPage()` / `hideSlsPage()` (verify capture phase / React Router overlap) |
| [`res/inject-scripts/shell/import-lua-script.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua-script.js) | Bundled artifact | Regenerated via `./install.sh bundle-scripts lua` |

---

## 3. Detailed Implementation Steps

### Step 3.1 — Add Shared Overlay Router Helper in `utils.js`
Near `getSteamHistory()` in [`res/inject-scripts/shell/import-lua/utils.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/utils.js#L95-L120), add a single shared helper (never duplicated between `config-controller.js` and `manual-install.js`):
```js
function detachSteamHistoryListener() {
    if (typeof historyUnlisten === 'function') {
        try { historyUnlisten(); } catch (e) {}
        historyUnlisten = null;
    }
}

function ensureLibraryViewForOverlay(onNavigateAway) {
    detachSteamHistoryListener();
    var history = getSteamHistory();
    if (!history) return;

    var curPath = (history.location && history.location.pathname) ? history.location.pathname : '';
    if (curPath.indexOf('/library') !== 0) {
        window.__slsNavigating = true;
        try {
            history.push('/library/home');
        } finally {
            setTimeout(function () { window.__slsNavigating = false; }, 150);
        }
    }

    if (typeof history.listen === 'function') {
        historyUnlisten = history.listen(function () {
            if (window.__slsNavigating) return;
            onNavigateAway();
        });
    }
}
```

### Step 3.2 — Wire `showConfigPage()` / `hideConfigPage()` (and `showSlsPage()` / `hideSlsPage()`)
1. In [`config-controller.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/config-controller.js#L5-L41):
   - In `showConfigPage()`, call `ensureLibraryViewForOverlay(hideConfigPage);` before showing `#sls-config-page-container`.
   - In `hideConfigPage()`, call `detachSteamHistoryListener();`.
2. In [`manual-install.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/manual-install.js#L5-L40):
   - Mirror the call in `showSlsPage()` (`ensureLibraryViewForOverlay(hideSlsPage)`) and `hideSlsPage()` (`detachSteamHistoryListener()`) unless superseded by `import-lua-config-section-spec-v1.md`.
3. In [`supernav.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/supernav.js#L75-L87):
   - Use capture phase (`true`) on `container.addEventListener('click', ..., true)` so clicking `LIBRARY` / `STORE` / `COMMUNITY` always runs `hideConfigPage()` / `hideSlsPage()` even if Steam's React handler stops propagation in bubble phase or stays on `/library/home`.

### Step 3.3 — Bundle Scripts
Run `./install.sh bundle-scripts lua`.

---

## 4. Domain Invariants & Rules

- [`.agents/rules/steam_cef_ui_patterns.md`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/.agents/rules/steam_cef_ui_patterns.md#L52-L55): **Modal Recursion Safety** — the `window.__slsNavigating` flag ensures our own `history.push('/library/home')` never re-triggers `history.listen` -> `hideConfigPage()` synchronously, and `hideSlsPage()` / `hideConfigPage()` never call each other recursively (`hideSlsPage` does not call `hideConfigPage`).
- [`.agents/rules/test-regression.md`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/.agents/rules/test-regression.md): Add UI regression test mocking a React Fiber `history` object at `/store` and verifying `showConfigPage()` pushes `/library/home` and leaves `#sls-config-page-container` visible.

---

## 5. Verification Plan

1. **Build & Bundle Sync**:
   - `./install.sh bundle-scripts lua`
   - `./install.sh build`
2. **Regression Test (run only when explicitly requested by user)**:
   - Add a test in `tests/integration/ui/test_uj05_config_editor_validation.py` that attaches a mock React Fiber `history` object (`.__reactFiberTest` / `__reactFiber` with `memoizedProps.history = { location: { pathname: '/store' }, push, listen }`) onto the SuperNav container tab in `steam_library_sp.html`, clicks `#sls-config-top-tab`, asserts `push('/library/home')` was called, `#sls-config-page-container` is visible, and simulating a subsequent `history.listen` callback for `/store` hides `#sls-config-page-container`.
   - Execute via `./install.sh test-ui`.
   - On live Steam client (`./install.sh test-flow` / `deck-ctl.sh` when requested): switch to `STORE`, click `Navi Config`, confirm Navi Config renders cleanly above everything without Store `BrowserView` obstructing it.

---

## 6. Risks & Edge Cases

- **React Fiber key names across Steam updates**: `getSteamHistory()` (`utils.js:103`) already handles both `__reactFiber` and `__reactInternalInstance`. If Steam moves `history` off `memoizedProps` or renames the property, `ensureLibraryViewForOverlay` degrades gracefully (`if (!history) return;`) without crashing the config UI.
- **Navigating directly to `/library/home` while already on a sub-route like `/library/app/440`**: Checking `curPath.indexOf('/library') !== 0` avoids unnecessarily kicking a user away from their open game details route inside `/library/app/440` when opening & closing Navi Config! Meanwhile, clicking `LIBRARY` or pressing Back in Steam triggers either capture-phase `click` or `history.listen`, closing the config overlay cleanly.

