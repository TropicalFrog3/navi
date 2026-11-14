# Bug Spec: Hide White Controller Focus Border in Game Details Navi Menu on Desktop Mode

- **Status:** Spec v1
- **Category:** UI & UX Bug
- **Owning Layer:** CEF Library App-Details Inject (`res/inject-scripts/library/app-details/`)
- **Source Wishlist Item:** `plans/01-backlog/dev-plan.md` — `- (in navi menu from game detail) when not in steam big picture or steam deck mode the white border appear, it is not supposed to show the white border when the user isn't using the controller to navigate`

---

## 1. Problem Statement & Root Cause Analysis

When opening the **Navi Menu** modal from a game's Library Details page on Desktop Steam (mouse & keyboard, `.DesktopUI` present, not in Big Picture Mode or Steam Deck Gamepad UI), the first interactive option inside the modal immediately renders with a thick white border (`3px solid #fff`).

Inspection of [`res/inject-scripts/library/app-details/gamepad.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/library/app-details/gamepad.js#L39-L108) shows why:
1. Inside `trapSteamGamepadModal(overlay, buttons)` (`lines 39–78`), `updateFocus()` unconditionally applies `buttons[i].style.outline = '3px solid #fff'; buttons[i].style.outlineOffset = '2px';` on `activeIdx` (`line 65`).
2. `setTimeout(updateFocus, 100);` (`line 78`) runs unconditionally every time the modal mounts, setting `activeIdx = firstFocusable()` (`0`) and drawing the white outline even when the user is in Desktop Mode and opened the modal with a mouse click!
3. There is no input-modality tracking (`usingController` / Desktop check + pointer/mouse listener to hide the controller ring if mouse clicks happen).

---

## 2. Target Files & Symbols

| File | Symbol / Selector | Change Summary |
|---|---|---|
| [`res/inject-scripts/library/app-details/gamepad.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/library/app-details/gamepad.js#L39-L124) | `trapSteamGamepadModal(overlay, buttons)`, `updateFocus()`, `gamepadCatcher()`, `cleanup()` | Gate white `3px solid #fff` outline on controller modality (`!isDesktopMode()` initially; turn `true` on `vgp_ondirection`/`vgp_onok`, turn `false` and clear outlines on `mousedown`/`pointerdown`) |
| [`res/inject-scripts/library/app-details-script.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/library/app-details-script.js) | Bundled artifact | Regenerated via `./install.sh bundle-scripts library` |
| [`tests/integration/ui/test_uj08_a11y_gamepad_responsive.py`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/tests/integration/ui/test_uj08_a11y_gamepad_responsive.py) (or [`test_uj10_navi_menu_drilldown.py`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/tests/integration/ui/test_uj10_navi_menu_drilldown.py)) | Regression check | Assert Desktop mode (`body.DesktopUI`) hides `3px solid` white outline on mount and shows it only after `vgp_ondirection`, while Big Picture/Deck mode shows it on mount |

---

## 3. Detailed Implementation Steps

### Step 3.1 — Add Modality & Desktop Mode Detection in `library/app-details/gamepad.js`
Inside [`res/inject-scripts/library/app-details/gamepad.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/library/app-details/gamepad.js#L39-L124):

1. Use the canonical Desktop vs. Deck/Big Picture detection pattern from [`.agents/rules/steam_cef_ui_patterns.md`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/.agents/rules/steam_cef_ui_patterns.md#L16-L26):
   ```js
   function isGamepadUiActive() {
       var hasDesktopUi = !!((document.body && document.body.classList.contains('DesktopUI')) || document.querySelector('.DesktopUI'));
       var isBpmTitle = /Steam Big Picture Mode/i.test(document.title || '');
       var isBpmWindow = (window.name || '').indexOf('BPM') !== -1;
       return !hasDesktopUi || isBpmTitle || isBpmWindow;
   }
   ```
   Initialize `var showFocusRing = isGamepadUiActive();`.

2. Update `updateFocus()` (`lines 60–73`):
   - Only apply `buttons[i].style.outline = '3px solid #fff'; buttons[i].style.outlineOffset = '2px';` when `i === activeIdx && showFocusRing`!
   - When `!showFocusRing`, set `buttons[i].style.outline = 'none';` for all buttons and avoid forced initial scroll jumps (`scrollIntoView*`).
   - Only invoke initial `setTimeout(updateFocus, 100);` if `showFocusRing` is `true` (or let `updateFocus()` run as a no-op outline clear when `showFocusRing` is `false`).
   - Also in `overlay.__slsResetFocus` (`lines 74–77`), re-evaluate or preserve `showFocusRing` without flashing a white outline in Desktop mode unless controller input is active.

3. Track universal input modality transitions (`.agents/rules/steam_controller_mapping.md` § Universal Input Modality Tracking):
   - In `gamepadCatcher(e)` (`line 85`): set `showFocusRing = true;` before calling `updateFocus();` so if a user picks up a controller even while on Desktop Steam, D-pad/stick navigation immediately shows the white focus ring!
   - Register `onPointerInput()` for `'mousedown'` / `'pointerdown'` on `overlay`: set `showFocusRing = false;` and clear `outline = 'none'` on `buttons`. Remove both listeners inside `cleanup()` (`lines 80–84`).

### Step 3.2 — Bundle Scripts
Run `./install.sh bundle-scripts library`.

---

## 4. Domain Invariants & Rules

- [`.agents/rules/steam_cef_ui_patterns.md`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/.agents/rules/steam_cef_ui_patterns.md#L16-L26): Follow the exact `.DesktopUI` / `BPM` / `"Steam Big Picture Mode"` detection rule.
- [`.agents/rules/steam_controller_mapping.md`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/.agents/rules/steam_controller_mapping.md#L33-L39): Track `vgp_ondirection`, `mousedown`, and `pointerdown` to switch focus-ring visibility cleanly between controller and mouse users without breaking Big Picture / Steam Deck navigation.
- [`.agents/rules/test-regression.md`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/.agents/rules/test-regression.md): Add UI regression tests covering both Desktop and Deck/Big Picture modes.

---

## 5. Verification Plan

1. **Build & Bundle Sync**:
   - `./install.sh bundle-scripts library`
   - `./install.sh build`
2. **Regression Test (run only when explicitly requested by user)**:
   - Update `tests/integration/ui/test_uj10_navi_menu_drilldown.py` (or `test_uj08_a11y_gamepad_responsive.py`) to test `app-details-script.js` on a Desktop fixture (`class="DesktopUI"`):
     - Open Navi Menu -> wait 150ms -> assert none of the modal buttons have `3px` / `rgb(255, 255, 255)` inline `outline`.
     - Dispatch `new CustomEvent('vgp_ondirection', { detail: { button: 10 } })` -> assert the active button now displays `3px solid` white outline.
     - Dispatch `mousedown` -> assert the white outline is cleared.
   - Execute via `./install.sh test-ui`.

---

## 6. Risks & Edge Cases

- **Keyboard `Tab` accessibility**: Notice `mousedown` only clears inline `outline` (`buttons[i].style.outline = 'none'`); CSS `:focus-visible` remains free to provide keyboard focus indicators when tabbing without triggering the Steam controller white ring on open.
- **Desktop user with connected controller**: Setting `showFocusRing = true` inside `gamepadCatcher` ensures Desktop users who decide to navigate the modal with a plugged-in Xbox/DualSense controller still get full visual focus feedback on first D-pad press.

