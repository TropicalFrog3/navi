Outcome: shipped — Reset top margin/padding on Migrate cloud saves card item and source/destination labels, and added UI regression assertion — 2026-11-13

# Bug Spec: Remove Top Spacer in "Migrate Cloud Saves" Source/Destination Section

- **Status:** Shipped (v1)
- **Category:** UI & UX Bug
- **Owning Layer:** CEF Shell Inject (`res/inject-scripts/shell/import-lua/`)
- **Source Wishlist Item:** `plans/01-backlog/dev-plan.md` — `- (in navi config tab) migrate cloud saves: remove the spacer on top of the from source & to destination section`

---

## 1. Problem Statement & Root Cause Analysis

Inside **Navi Config → Cloud saves → Migrate cloud saves**, there is a noticeably large vertical spacer above the `From (source)` and `To (destination)` dropdown row.

Inspection of [`res/inject-scripts/shell/import-lua/config-sections.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/config-sections.js#L146-L165), [`res/inject-scripts/shell/import-lua/config-helpers.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/config-helpers.js#L5-L12), and [`res/inject-scripts/shell/import-lua/styles.css`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/styles.css#L26-L27) shows four stacked spacing sources combining above `From (source)`:

1. `makeCard('Migrate cloud saves', 'Copy all redirected cloud save blobs...', ...)` (`config-helpers.js:9`) renders the subtitle `<div style="... margin-bottom: 12px;">`.
2. Immediately below that subtitle sits `<div class="sls-cfg-item" ...>` (`config-sections.js:147`), and `.sls-cfg-item` (`styles.css:27`) enforces `margin-top: 8px !important; padding: 14px 18px !important;`.
3. `makeCard` applies Steam's native `.DialogControlsSection` class (`config-helpers.js:7`). Inside Steam's SharedJSContext stylesheet, `.DialogControlsSection label` and `.DialogControlsSection select` inherit Steam's dialog label top margins/line-height rules unless explicitly zeroed, adding ~12–18px above `<label for="sls-cfg-cs-migrate-src">` and `<label for="sls-cfg-cs-migrate-dst">`.
4. The first `<div class="sls-cfg-item">` already sits directly below a card subtitle that has `margin-bottom: 12px`, so `12px + 8px (!important) + 14px (!important) + native label margin` accumulates into a ~38px–45px empty band above the `From (source)` / `To (destination)` labels.

---

## 2. Target Files & Symbols

| File | Symbol / Selector | Change Summary |
|---|---|---|
| [`res/inject-scripts/shell/import-lua/config-sections.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/config-sections.js#L146-L165) | `migrateCard`, `#sls-cfg-cs-migrate-src`, `#sls-cfg-cs-migrate-dst` | Reset top spacing on the migrate card's `.sls-cfg-item` and add explicit `margin: 0; padding: 0; line-height: 16px;` resets on the two `<label>` elements |
| [`res/inject-scripts/shell/import-lua/styles.css`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/styles.css#L26-L28) | `.sls-cfg-card > .sls-cfg-item:first-child`, `.sls-cfg-migrate-item`, `.sls-cfg-card label` | Neutralize redundant top margin/Steam `.DialogControlsSection label` spacing cleanly |
| [`res/inject-scripts/shell/import-lua-script.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua-script.js) | Bundled artifact | Regenerated via `./install.sh bundle-scripts lua` |
| [`tests/integration/ui/test_uj05_config_editor_validation.py`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/tests/integration/ui/test_uj05_config_editor_validation.py#L262-L268) | `test_uj05_cloudredirect_ux_parity_controls` (or dedicated test) | Add bounding-box vertical gap check between `Migrate cloud saves` card subtitle and `From (source)` label |

---

## 3. Detailed Implementation Steps

### Step 3.1 — Remove Extra Top Spacer in `config-sections.js`
In [`res/inject-scripts/shell/import-lua/config-sections.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/config-sections.js#L146-L165):

**Before (`lines 146–157`):**
```js
var migrateCard = makeCard('Migrate cloud saves', 'Copy all redirected cloud save blobs and metadata from one storage provider to another.',
    '<div class="sls-cfg-item" data-search="migrate cloud saves copy provider source destination" style="flex-direction: column; align-items: stretch; gap: 12px;">' +
        '<div style="display: flex; align-items: flex-end; justify-content: space-between; gap: 12px; flex-wrap: wrap;">' +
            '<div style="display: flex; align-items: flex-end; gap: 12px; flex-wrap: wrap; flex: 1 1 420px;">' +
                '<div style="display: flex; flex-direction: column; gap: 4px; flex: 1 1 180px;">' +
                    '<label for="sls-cfg-cs-migrate-src" style="font-size: 12px; font-weight: 500; color: #cbd5e1;">From (source)</label>' +
                    '<select id="sls-cfg-cs-migrate-src" class="sls-cfg-input" style="width: 100%;">' + migrateSrcOptions + '</select>' +
                '</div>' +
                '<div style="display: flex; flex-direction: column; gap: 4px; flex: 1 1 180px;">' +
                    '<label for="sls-cfg-cs-migrate-dst" style="font-size: 12px; font-weight: 500; color: #cbd5e1;">To (destination)</label>' +
                    '<select id="sls-cfg-cs-migrate-dst" class="sls-cfg-input" style="width: 100%;">' + migrateDstOptions + '</select>' +
                '</div>' +
            '</div>' +
```

**After:**
- Add class `sls-cfg-migrate-item` (or explicit `margin-top: 0 !important; padding: 12px 16px !important;`) on the `.sls-cfg-item` wrapper.
- Add `margin: 0 !important; padding: 0 !important; line-height: 16px;` to `<label for="sls-cfg-cs-migrate-src">` and `<label for="sls-cfg-cs-migrate-dst">`.

### Step 3.2 — Override `.sls-cfg-item` First-Row Margin & Steam `.DialogControlsSection` Label Styles in `styles.css`
In [`res/inject-scripts/shell/import-lua/styles.css`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/styles.css#L26-L28), add:
```css
.sls-cfg-card > .sls-cfg-migrate-item { margin-top: 0 !important; padding: 12px 16px !important; }
.sls-cfg-card label { margin-top: 0; margin-bottom: 0; padding-top: 0; }
```
Note: Be careful not to alter `.sls-toggle-switch` inside `.sls-cfg-card` (use `margin-top: 0; padding-top: 0;` which does not affect `width: 38px; height: 22px; display: inline-block;` on `.sls-toggle-switch`).

### Step 3.3 — Bundle Scripts
Run `./install.sh bundle-scripts lua`.

---

## 4. Domain Invariants & Rules

- [`.agents/rules/steam_cef_ui_patterns.md`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/.agents/rules/steam_cef_ui_patterns.md#L65-L69): Preserve `#sls-cfg-cs-migrate-src`, `#sls-cfg-cs-migrate-dst`, `#sls-cfg-cs-migrate-scan-btn`, `#sls-cfg-cs-migrate-start-btn`, and `#sls-cfg-cs-migrate-status` IDs and `data-search` attributes.
- [`.agents/rules/test-regression.md`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/.agents/rules/test-regression.md): Add UI regression check verifying compact top spacing above `#sls-cfg-cs-migrate-src`.

---

## 5. Verification Plan

1. **Build & Bundle Sync**:
   - `./install.sh bundle-scripts lua`
   - `./install.sh build`
2. **Regression Test (run only when explicitly requested by user)**:
   - Extend `tests/integration/ui/test_uj05_config_editor_validation.py` to measure `label[for="sls-cfg-cs-migrate-src"].getBoundingClientRect().top - subtitle.getBoundingClientRect().bottom` (assert `< 26px`) and confirm both `<select>` controls remain horizontally aligned with `Scan Source` / `Start Migration` buttons.
   - Execute via `./install.sh test-ui`.

---

## 6. Risks & Edge Cases

- **Other `.sls-cfg-card` toggles**: Using a class-scoped rule `.sls-cfg-card > .sls-cfg-migrate-item` and setting `margin: 0 !important; padding: 0 !important;` directly on the two `<label>` tags avoids any accidental layout shift on checkbox/toggle `<label class="sls-toggle-switch">` elements in other cards.
- **Steam theme stylesheet specificity**: Steam injects `libraryroot.css` dynamically with `.DialogControlsSection` rules; using `!important` in `styles.css` (and inline on `.sls-cfg-migrate-item`) guarantees `margin-top: 0` wins over Steam's sheet.

