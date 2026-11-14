Outcome: shipped — Styled #sls-cfg-discard-btn as a secondary .sls-cfg-btn button with border, background fill, padding, hover state, and UI regression assertions — 2026-11-13

# Bug Spec: Style "Discard Changes" as a Button Instead of Plain Text

- **Status:** Shipped (v1)
- **Category:** UI & UX Bug
- **Owning Layer:** CEF Shell Inject (`res/inject-scripts/shell/import-lua/`)
- **Source Wishlist Item:** `plans/01-backlog/dev-plan.md` — `- (in navi config tab) make the discard changes a button instead of a text that you can click, it confuse the user.`

---

## 1. Problem Statement & Root Cause Analysis

When a user edits any field in **Navi Config**, the sidebar footer reveals `#sls-cfg-discard-btn` (`config-controller.js:2057`), which currently looks like unstyled muted grey text instead of an interactive button.

Inspection of [`res/inject-scripts/shell/import-lua/config-sections.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/config-sections.js#L482-L485):
```html
<div style="display: flex; justify-content: space-between; align-items: center; padding: 0 2px;">
    <button id="sls-cfg-discard-btn" type="button" style="display: none; background: none; border: none; color: #8a929e; font-size: 12px; cursor: pointer; padding: 0;">Discard changes</button>
    <span style="color: #64748b; font-size: 11px; margin-left: auto;">Ctrl+S</span>
</div>
```
Because `class="sls-cfg-btn"` is omitted and inline styles override `background: none; border: none; padding: 0;`, it has no background fill, no border, no border-radius, and no padding/hover affordance, making it visually indistinguishable from plain status text.

---

## 2. Target Files & Symbols

| File | Symbol / Selector | Change Summary |
|---|---|---|
| [`res/inject-scripts/shell/import-lua/config-sections.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/config-sections.js#L482-L485) | `#sls-cfg-discard-btn` footer row | Add `class="sls-cfg-btn"` and secondary button styling (fill, border, padding, border-radius); keep clean spacing alongside the `Ctrl+S` hint |
| [`res/inject-scripts/shell/import-lua/styles.css`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/styles.css#L49-L57) | `#sls-cfg-discard-btn`, `.sls-cfg-btn` | Add subtle hover/active styling for `#sls-cfg-discard-btn` matching `.sls-cfg-btn` secondary chrome |
| [`res/inject-scripts/shell/import-lua-script.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua-script.js) | Bundled artifact | Regenerated via `./install.sh bundle-scripts lua` |

---

## 3. Detailed Implementation Steps

### Step 3.1 — Restyle `#sls-cfg-discard-btn` in `config-sections.js`
Update [`res/inject-scripts/shell/import-lua/config-sections.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/config-sections.js#L482-L485):

**Before:**
```html
'<div style="display: flex; justify-content: space-between; align-items: center; padding: 0 2px;">' +
    '<button id="sls-cfg-discard-btn" type="button" style="display: none; background: none; border: none; color: #8a929e; font-size: 12px; cursor: pointer; padding: 0;">Discard changes</button>' +
    '<span style="color: #64748b; font-size: 11px; margin-left: auto;">Ctrl+S</span>' +
'</div>' +
```

**After:**
```html
'<div style="display: flex; justify-content: space-between; align-items: center; gap: 8px;">' +
    '<button id="sls-cfg-discard-btn" class="sls-cfg-btn" type="button" style="display: none; background-color: rgba(255, 255, 255, 0.06); border: 1px solid rgba(255, 255, 255, 0.14); border-radius: 6px; color: #e2e8f0; font-size: 12px; font-weight: 500; padding: 6px 12px; line-height: 16px; cursor: pointer; flex: 1 1 auto; text-align: center;">Discard changes</button>' +
    '<span style="color: #64748b; font-size: 11px; margin-left: auto; white-space: nowrap; flex-shrink: 0;">Ctrl+S</span>' +
'</div>' +
```

### Step 3.2 — Hover/Focus Polish in `styles.css`
In [`res/inject-scripts/shell/import-lua/styles.css`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/styles.css#L49-L57), add:
```css
#sls-cfg-discard-btn:hover { background-color: rgba(255, 255, 255, 0.12) !important; border-color: rgba(255, 255, 255, 0.24) !important; color: #ffffff !important; }
```
Verify `setDirtyChrome(isDirtyNow, changed)` in [`config-controller.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/config-controller.js#L2057) continues toggling `discardBtn.style.display = isDirtyNow ? 'inline-block' : 'none';` cleanly without clearing class or styling.

### Step 3.3 — Bundle Scripts
Run `./install.sh bundle-scripts lua`.

---

## 4. Domain Invariants & Rules

- [`tests/integration/ui/pages/import_config_page.py`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/tests/integration/ui/pages/import_config_page.py#L294): Keep `id="sls-cfg-discard-btn"` and `display: none` when clean / `inline-block` when dirty (`is_discard_visible()` checks style/computed display).
- [`.agents/rules/test-regression.md`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/.agents/rules/test-regression.md): Add UI regression check verifying `#sls-cfg-discard-btn` renders visible button border, padding, and non-transparent background when dirty.

---

## 5. Verification Plan

1. **Build & Bundle Sync**:
   - `./install.sh bundle-scripts lua`
   - `./install.sh build`
2. **Regression Test (run only when explicitly requested by user)**:
   - Extend `tests/integration/ui/test_uj05_config_editor_validation.py` so after modifying a field, it asserts `#sls-cfg-discard-btn` is visible, has `.sls-cfg-btn`, non-zero border width, horizontal/vertical padding, non-transparent `backgroundColor`, and still reverts dirty changes when confirmed.
   - Execute via `./install.sh test-ui`.

---

## 6. Risks & Edge Cases

- **Sidebar width constraint (`230px` / `200px` at `<1280px`)**: Inside `.sls-cfg-saveblock` (`padding: 0 16px`), available width is ~168px–198px. With `Discard changes` (`flex: 1 1 auto; padding: 6px 12px;`) and `Ctrl+S` (`~36px`), total width fits comfortably on one row even on 200px sidebar widths.
- **Sticky footer on `<768px` mobile layout**: `.sls-cfg-saveblock` becomes horizontal/sticky on mobile viewports (`styles.css:93`); compact `6px 12px` padding keeps footer height well balanced.

