# Bug Spec: Hide White Controller Focus Border in Store Details Modals on Desktop Mode

- **Status:** Spec v1
- **Category:** UI & UX Bug
- **Owning Layer:** CEF Store Page & Depot Selection Inject (`res/inject-scripts/store/store-page/`, `res/inject-scripts/store/depot-selection/`)
- **Source Wishlist Item:** `plans/01-backlog/dev-plan.md` — `- (in store details download lua modal) when not in steam big picture or steam deck mode the white border appear, it is not supposed to show the white border when the user isn't using the controller to navigate`

---

## 1. Problem Statement & Root Cause Analysis

On Steam Store pages in Desktop Mode (clicking **Download Lua files**, provider configuration modal, or the **Depot Selection** modal), the first interactive button/element inside the opened modal immediately renders with a `3px solid #fff` controller focus border — and clicking any option with a mouse also leaves a `3px solid #fff` controller outline on it.

Inspection of [`res/inject-scripts/store/store-page/gamepad.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/store/store-page/gamepad.js#L5-L169) and [`res/inject-scripts/store/depot-selection/gamepad.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/store/depot-selection/gamepad.js#L14-L126) reveals why:

1. **`store-page/gamepad.js`**:
   - `setTimeout(updateFocus, 100);` (`line 55`) runs unconditionally on modal mount (`activeR = 0; activeC = 0`), setting `btn.style.outline = '3px solid #fff'` (`line 17`).
   - `onOverlayFocusIn` (`lines 57–69`) listens to `'focusin'` on `overlay` and calls `updateFocus()` on every focus change — including normal Desktop mouse clicks (`<button>` / `<input>` click focuses the element, firing `focusin`, which draws `3px solid #fff`)!
   - Even `overlay.__slsUpdateGamepadGrid` (`lines 78–94`) calls `updateFocus()`, drawing the white outline in Desktop Mode.
2. **`depot-selection/gamepad.js`**:
   - Has the identical issue on `lines 36–37`: calls `updateFocus(); setTimeout(updateFocus, 50);` unconditionally on mount, painting `3px solid #fff` (`line 26`) even in Desktop Mode.
3. **Store iframe detection subtlety**: Store pages run inside `https://store.steampowered.com/...` frames (not the `SP` root DOM where `.DesktopUI` lives on `body`), so detecting Desktop vs. Big Picture/Steam Deck mode in `store/` scripts must inspect Big Picture/Gamepad signals (`window.name.indexOf('BPM')`, `document.title` matching `Steam Big Picture Mode`, `navigator.userAgent` containing `Valve Steam Tenfoot`, `document.body.classList.contains('GamepadUI')`, and safely checking `window.top.document` if same-origin / falling back to non-Tenfoot Desktop default).

---

## 2. Target Files & Symbols

| File | Symbol / Selector | Change Summary |
|---|---|---|
| [`res/inject-scripts/store/store-page/gamepad.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/store/store-page/gamepad.js#L5-L169) | `trapSteamGamepadModal(overlay, buttons)`, `updateFocus()`, `onOverlayFocusIn()`, `gamepadCatcher()`, `cleanup()` | Add Desktop vs. Big Picture/Deck detection (`isStoreGamepadMode()`), gate `3px solid #fff` on `showFocusRing`, turn `showFocusRing = true` on `vgp_ondirection`, and clear it on `mousedown`/`pointerdown` |
| [`res/inject-scripts/store/depot-selection/gamepad.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/store/depot-selection/gamepad.js#L14-L126) | `trapSteamGamepadModal(...)`, `updateFocus()`, `gamepadCatcher()`, `cleanup()` | Apply the same modality gate and pointer listener so the Store Depot Selection modal behaves identically in Desktop Mode |
| [`res/inject-scripts/store/store-page-script.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/store/store-page-script.js) | Bundled artifact | Regenerated via `./install.sh bundle-scripts store` |
| [`res/inject-scripts/store/depot-selection-script.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/store/depot-selection-script.js) | Bundled artifact | Regenerated via `./install.sh bundle-scripts depots` |

---

## 3. Detailed Implementation Steps

### Step 3.1 — Add Store Modality Detection & Pointer Guard in `store-page/gamepad.js`
In [`res/inject-scripts/store/store-page/gamepad.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/store/store-page/gamepad.js#L5-L96):

1. Detect whether the Store view is running inside Steam Big Picture / Steam Deck Gamepad mode:
   ```js
   function isStoreGamepadMode() {
       var isBpmWindow = (window.name || '').indexOf('BPM') !== -1;
       var isBpmTitle = /Steam Big Picture Mode/i.test(document.title || '');
       var isTenfootUa = /Valve Steam Tenfoot/i.test(navigator.userAgent || '');
       var hasGamepadClass = !!(document.body && (document.body.classList.contains('GamepadUI') || document.body.classList.contains('gamepad_mode')));
       var hasDesktopClass = !!((document.body && document.body.classList.contains('DesktopUI')) || document.querySelector('.DesktopUI'));
       if (hasDesktopClass && !isBpmWindow && !isBpmTitle && !isTenfootUa) return false;
       return isBpmWindow || isBpmTitle || isTenfootUa || hasGamepadClass;
   }
   ```
   Initialize `var showFocusRing = isStoreGamepadMode();`.

2. Update `updateFocus()` (`lines 11–54`):
   - Only draw `btn.style.outline = '3px solid #fff'; btn.style.outlineOffset = '2px';` when `r === activeR && c === activeC && showFocusRing`!
   - When `!showFocusRing`, set `btn.style.outline = 'none';` and skip initial scroll-into-view jitter (`if (showFocusRing) { ... }`).

3. Track input modality transitions cleanly:
   - `function onOverlayPointerDown() { showFocusRing = false; updateFocus(); }` attached to `'mousedown'` and `'pointerdown'` (capture phase) on `overlay`, so mouse clicks do NOT cause `onOverlayFocusIn` to draw a white controller border!
   - `gamepadCatcher(e)` (`line 98`): set `showFocusRing = true;` before `updateFocus()` so pressing D-pad/stick immediately enables the white focus ring even in Desktop Mode.
   - `cleanup()` (`lines 71–76`): remove `mousedown` and `pointerdown` listeners cleanly.

### Step 3.2 — Apply Matching Fix to `depot-selection/gamepad.js`
Apply the same `isStoreGamepadMode()` + `showFocusRing` guard, `mousedown`/`pointerdown` listener (clearing `showFocusRing = false`), `gamepadCatcher` (`showFocusRing = true`), and listener removal in `cleanup()` inside [`res/inject-scripts/store/depot-selection/gamepad.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/store/depot-selection/gamepad.js#L14-L126).

### Step 3.3 — Bundle Store & Depot Scripts
Run:
- `./install.sh bundle-scripts store`
- `./install.sh bundle-scripts depots`

---

## 4. Domain Invariants & Rules

- [`.agents/rules/steam_cef_ui_patterns.md`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/.agents/rules/steam_cef_ui_patterns.md#L16-L26) & [`.agents/rules/steam_controller_mapping.md`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/.agents/rules/steam_controller_mapping.md#L33-L39): Keep full controller navigation (`vgp_ondirection`, `vgp_onok`, `vgp_oncancel`) working in Big Picture / Steam Deck (`BPM` / `Valve Steam Tenfoot`), while preventing false controller outlines in Desktop Chrome/CEF mouse sessions.
- [`.agents/rules/test-regression.md`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/.agents/rules/test-regression.md): Add UI regression tests for both Desktop and Gamepad modal states in `test_uj04_store_bridge_depots.py` / `test_uj08_a11y_gamepad_responsive.py`.

---

## 5. Verification Plan

1. **Build & Bundle Sync**:
   - `./install.sh bundle-scripts store`
   - `./install.sh bundle-scripts depots`
   - `./install.sh build`
2. **Regression Test (run only when explicitly requested by user)**:
   - Extend `tests/integration/ui/test_uj08_a11y_gamepad_responsive.py` (or `test_uj04_store_bridge_depots.py`) to verify:
     - Store Download Lua modal opened in Desktop fixture (`window.name === ''`, no `BPM` / `Tenfoot`) has `outline: none` on mount and after mouse `click` / `mousedown`.
     - Dispatching `vgp_ondirection` activates the `3px solid` white outline on the active button, and subsequent `mousedown` clears it.
     - Same check for Depot Selection modal.
   - Execute via `./install.sh test-ui`.

---

## 6. Risks & Edge Cases

- **Store iframe `window.top` cross-origin restriction**: Store pages load from `https://store.steampowered.com` while Steam's parent container may be a different origin (`https://steamloopback.host`). Using `window.name`, `document.title`, `navigator.userAgent` (`Valve Steam Tenfoot` in Steam Deck Gaming Mode), and `document.body.classList` avoids touching `window.top.document` (which would throw `SecurityError` in strict cross-origin frames).
- **Dynamic grid updates (`__slsUpdateGamepadGrid`)**: Respect the current `showFocusRing` state instead of forcing an outline in Desktop Mode when async provider/depot rows finish loading.

