# Bug Spec: Clarify / Rework "Redirect Save Files" Toggle in Navi Config

- **Status:** Spec v1
- **Category:** UI & UX Bug
- **Owning Layer:** CEF Shell Inject (`res/inject-scripts/shell/import-lua/`)
- **Source Wishlist Item:** `plans/01-backlog/dev-plan.md` — `- (in navi config tab) what the heck is the redirect save files toggle is??`

---

## 1. Problem Statement & Root Cause Analysis

In **Navi Config → Cloud saves → Status**, the first setting row currently reads:
- **Title:** `Redirect save files`
- **Description:** `Also called CloudRedirect. Turning this on or off needs a Steam restart.`

Users do not immediately know what "Redirect save files" or the internal codename "CloudRedirect" does, why it would be enabled or disabled, or what happens to their game saves when toggled. Additionally, the `"Navi service"` row immediately below (`config-sections.js:24`) uses ambiguous wording (`"Navi moves saves in and out of Steam Cloud for you."`) that does not clearly separate what the master switch turns on/off vs. live daemon status.

Exact location: [`res/inject-scripts/shell/import-lua/config-sections.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/config-sections.js#L14-L29), paired with event wiring and status updates in [`res/inject-scripts/shell/import-lua/config-controller.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/config-controller.js#L255-L263) (`applyStatus`), lines [`1283-1305`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/config-controller.js#L1283-L1305) (`masterBtn.onclick`), and lines [`1737-1743`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/config-controller.js#L1737-L1743) (`masterToggle.onchange`).

---

## 2. Target Files & Symbols

| File | Symbol / Selector | Change Summary |
|---|---|---|
| [`res/inject-scripts/shell/import-lua/config-sections.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/config-sections.js#L14-L29) | `card1Content` (`#sls-cfg-cs-master-toggle`, `#sls-cfg-cs-master-btn`, `#sls-cfg-cs-master-status`) | Replace jargon-heavy `"Redirect save files"` / `"Also called CloudRedirect..."` copy with clear, self-explanatory title and description while keeping all DOM IDs and search tokens intact |
| [`res/inject-scripts/shell/import-lua-script.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua-script.js) | Bundled artifact | Regenerated via `./install.sh bundle-scripts lua` |
| [`tests/integration/ui/test_uj05_config_editor_validation.py`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/tests/integration/ui/test_uj05_config_editor_validation.py) | Regression check | Verify the rewritten Cloud Saves master toggle renders expected user-facing copy and `#sls-cfg-cs-master-toggle` remains interactive |

---

## 3. Detailed Implementation Steps

### Step 3.1 — Rework Copy & Description in `config-sections.js`
Update lines 14–29 in [`res/inject-scripts/shell/import-lua/config-sections.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/config-sections.js#L14-L29):

**Before (`lines 14–20`):**
```js
var card1Content = makeSettingRow('Redirect save files', 'Also called CloudRedirect. Turning this on or off needs a Steam restart.',
        '<div style="display: flex; align-items: center; gap: 10px;">' +
            '<span id="sls-cfg-cs-master-status" style="font-size: 13px; color: #8a929e;">' + (cs.Enabled !== false ? 'On · ' + managedCount + ' games' : 'Off') + '</span>' +
            '<button id="sls-cfg-cs-master-btn" class="sls-cfg-btn" type="button" style="display: none;">' + (cs.Enabled !== false ? 'Turn off' : 'Turn on') + '</button>' +
            makeToggle('sls-cfg-cs-master-toggle', cs.Enabled !== false) +
        '</div>',
        'CloudRedirect master redirect save files') +
```

**After:**
```js
var card1Content = makeSettingRow(
        'Cloud save storage for unlocked games',
        'Intercepts game save writes for unlocked, spoofed, and family-shared games and stores them in your selected folder or cloud account (via CloudRedirect) instead of Valve Steam Cloud. Requires a Steam restart to take effect.',
        '<div style="display: flex; align-items: center; gap: 10px;">' +
            '<span id="sls-cfg-cs-master-status" style="font-size: 13px; color: #8a929e;">' + (cs.Enabled !== false ? 'Enabled · ' + managedCount + ' games' : 'Disabled') + '</span>' +
            '<button id="sls-cfg-cs-master-btn" class="sls-cfg-btn" type="button" style="display: none;">' + (cs.Enabled !== false ? 'Turn off' : 'Turn on') + '</button>' +
            makeToggle('sls-cfg-cs-master-toggle', cs.Enabled !== false) +
        '</div>',
        'CloudRedirect master redirect save files enable cloud saves storage') +
```

Also refine `config-controller.js` line 263 (`applyStatus`) so `#sls-cfg-cs-master-status` consistently uses `'Enabled · ' + csState.managedAppsCount + ' games'` / `'Disabled'` (or `'On · ...'` / `'Off'` consistently across initial render and status poll).
Check `tests/` (`grep -rn "Redirect save files\|sls-cfg-cs-master" tests/`) — zero existing tests depend on the old `"Redirect save files"` string.

### Step 3.2 — Bundle Scripts
Run `./install.sh bundle-scripts lua`.

---

## 4. Domain Invariants & Rules

- [`.agents/rules/steam_cef_ui_patterns.md`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/.agents/rules/steam_cef_ui_patterns.md#L69): **Preserve Action Hooks** — keep `#sls-cfg-cs-master-toggle`, `#sls-cfg-cs-master-btn`, and `#sls-cfg-cs-master-status` unchanged so `config-controller.js` (`masterToggle.onchange` -> `masterBtn.click()` -> `NaviCloudManager.toggleMaster(...)`) and existing UI tests work identically.
- Keep `CloudRedirect` and `redirect save files` in `data-search` so users searching either legacy term still locate the toggle in `#sls-cfg-search`.
- [`.agents/rules/test-regression.md`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/.agents/rules/test-regression.md): Add UI assertion for the clarified toggle label and toggle state update.

---

## 5. Verification Plan

1. **Build & Bundle Sync**:
   - `./install.sh bundle-scripts lua`
   - `./install.sh build`
2. **Regression Test (run only when explicitly requested by user)**:
   - Assert in `tests/integration/ui/test_uj05_config_editor_validation.py` that `#sls-cfg-cs-master-toggle` is reachable via `#sls-cfg-search` using `"cloud"` and `"redirect"` queries, displays the self-explanatory copy, and preserves toggle interactivity.
   - Execute via `./install.sh test-ui`.

---

## 6. Risks & Edge Cases

- **Consistency with `config-controller.js` status text**: Make sure initial HTML in `config-sections.js` line 16 and dynamic updates in `config-controller.js` line 263 use identical phrasing (`Enabled · N games` / `Disabled`), preventing text flicker when `/cloud/status` resolves.
- **Line-wrap layout**: Even with the longer description, `makeSettingRow` (`config-helpers.js:18`) wraps text cleanly inside `flex: 1 1 260px` on the left while pinning the status label + toggle switch on the right.

