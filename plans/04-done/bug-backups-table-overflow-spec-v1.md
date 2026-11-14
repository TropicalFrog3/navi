Outcome: shipped — Rebalanced Saved backup archives table columns (24%/28%/18%/10%/20%), added cell overflow/ellipsis containment and hover titles, wrapped row action buttons in an inline-flex container, and added UI geometry bounds regression assertions — 2026-11-13

# Bug Spec: Saved Backup Archives Table Overflowing List Items Content

- **Status:** Shipped (v1)
- **Category:** UI & UX Bug
- **Owning Layer:** CEF Shell Inject (`res/inject-scripts/shell/import-lua/`)
- **Source Wishlist Item:** `plans/01-backlog/dev-plan.md` — `- (in navi config tab) Saved backup archives: fix the overflowing list items content`

---

## 1. Problem Statement & Root Cause Analysis

Inside **Navi Config → Cloud saves → Backups → Saved backup archives** (`#sls-cfg-cs-backups-list`), rendered backup rows overflow their cells horizontally and clip or spill outside the table row boundary.

Inspection of `refreshBackupsList()` in [`res/inject-scripts/shell/import-lua/config-controller.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/config-controller.js#L635-L775) and table styles in [`res/inject-scripts/shell/import-lua/styles.css`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/styles.css#L76-L85) identifies three compounding causes:

1. **Undersized `ACTIONS` column (`10%`)**: `bTable` uses `table-layout: fixed; width: 100%` with header column widths:
   - `GAME`: `26%`
   - `BACKUP ARCHIVE`: `36%`
   - `DATE`: `18%`
   - `SIZE`: `10%`
   - `ACTIONS`: `10%`
   Within a ~900px dialog body (`.sls-cfg-section { max-width: 980px }`), `10%` is ~85–90px. `tdBActions` has `white-space: nowrap` and contains two `.sls-cfg-btn` buttons (`Restore` with `margin-right: 6px; padding: 4px 10px` + `Delete` with `padding: 4px 10px`, totaling ~135–155px), so the buttons overflow horizontally past the right edge of `tdBActions` and `.navi-cloud-table-region`.
2. **Unclipped `.navi-col-num` cells (`tdBDate`, `tdBSize`)**: `.navi-steam-cloud-table td.navi-col-num` (`styles.css:84`) sets `white-space: nowrap; font-variant-numeric: tabular-nums;`, while `tdBDate` (`config-controller.js:682-686`) and `tdBSize` (`config-controller.js:688-692`) omit `overflow: hidden; text-overflow: ellipsis; max-width: 0;`. Long localized timestamps from `fmtDate(b.timestamp)` (`toLocaleString()`) bleed over adjacent columns (`SIZE` / `ACTIONS`).
3. **Missing flex/text-overflow guard on `tdBGame` / `tdBFile` in narrow viewports**: Although `tdBGame` and `tdBFile` set `overflow: hidden; text-overflow: ellipsis; white-space: nowrap;`, without `max-width: 0` (or explicit `title` attributes for hover inspection) and balanced column percentages, narrow Steam Deck / 1280px viewports either crowd `ACTIONS` or truncate without tooltip visibility.

---

## 2. Target Files & Symbols

| File | Symbol / Selector | Change Summary |
|---|---|---|
| [`res/inject-scripts/shell/import-lua/config-controller.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/config-controller.js#L635-L775) | `refreshBackupsList()`, `bTable`, `tdBGame`, `tdBFile`, `tdBDate`, `tdBSize`, `tdBActions` | Rebalance column percentages (`24% / 28% / 18% / 10% / 20%`), clip numeric columns cleanly, add `title` tooltips on truncated text cells, and keep `ACTIONS` roomy enough for both buttons |
| [`res/inject-scripts/shell/import-lua/styles.css`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/styles.css#L78-L85) | `.navi-steam-cloud-table td`, `.navi-steam-cloud-table td.navi-col-num` | Ensure `box-sizing: border-box;` and safe overflow containment on `.navi-col-num` |
| [`res/inject-scripts/shell/import-lua-script.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua-script.js) | Bundled artifact | Regenerated via `./install.sh bundle-scripts lua` |
| [`tests/integration/ui/test_uj05_config_editor_validation.py`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/tests/integration/ui/test_uj05_config_editor_validation.py#L276-L281) | `test_uj05_cloudredirect_ux_parity_controls` (or new regression test) | Assert `cell.scrollWidth <= cell.clientWidth` and row action buttons stay inside table bounds |

---

## 3. Detailed Implementation Steps

### Step 3.1 — Rebalance Column Widths & Overflow Rules in `config-controller.js`
Update `refreshBackupsList()` in [`res/inject-scripts/shell/import-lua/config-controller.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/config-controller.js#L639-L728):

**Before (`lines 640–646`):**
```js
'<thead><tr>' +
    '<th scope="col" style="width: 26%; text-align: left;">GAME</th>' +
    '<th scope="col" style="width: 36%; text-align: left;">BACKUP ARCHIVE</th>' +
    '<th scope="col" style="width: 18%; text-align: right;">DATE</th>' +
    '<th scope="col" style="width: 10%; text-align: right;">SIZE</th>' +
    '<th scope="col" style="width: 10%; text-align: right;">ACTIONS</th>' +
'</tr></thead>';
```

**After:**
```js
'<thead><tr>' +
    '<th scope="col" style="width: 24%; text-align: left;">GAME</th>' +
    '<th scope="col" style="width: 28%; text-align: left;">BACKUP ARCHIVE</th>' +
    '<th scope="col" style="width: 18%; text-align: right;">DATE</th>' +
    '<th scope="col" style="width: 10%; text-align: right;">SIZE</th>' +
    '<th scope="col" style="width: 20%; min-width: 148px; text-align: right;">ACTIONS</th>' +
'</tr></thead>';
```

Also update cell styles in `refreshBackupsList()`:
- On `tdBGame` and `tdBFile`, add `tdBGame.title = bTitle + ' (' + bAppId + ')';` and `tdBFile.title = bFile;` alongside `overflow: hidden; text-overflow: ellipsis; white-space: nowrap;` (via Safe DOM `textContent`/`createElement`, never raw interpolated HTML).
- On `tdBDate` and `tdBSize`, add `overflow: hidden; text-overflow: ellipsis; max-width: 0;` and set `tdBDate.title = fmtDate(b.timestamp);`.
- On `tdBActions`, add `style.overflow = 'hidden';` (keeping `white-space: nowrap; text-align: right;`) and wrap `restoreBtn` + `deleteBtn` inside an inline-flex container `<div style="display: inline-flex; align-items: center; justify-content: flex-end; gap: 6px; max-width: 100%;">` so gap and button spacing stay consistent without spilling outside the `20%` track.

### Step 3.2 — Harden `.navi-steam-cloud-table` Cell Box Sizing in `styles.css`
In [`res/inject-scripts/shell/import-lua/styles.css`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/styles.css#L78-L85):
- Ensure `.navi-steam-cloud-table td` includes `box-sizing: border-box;` and `.navi-steam-cloud-table td.navi-col-num` includes `overflow: hidden; text-overflow: ellipsis; max-width: 0;`.

### Step 3.3 — Bundle Scripts
Run `./install.sh bundle-scripts lua` so [`res/inject-scripts/shell/import-lua-script.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua-script.js) stays in sync with `res/inject-scripts/shell/import-lua/`.

---

## 4. Domain Invariants & Rules

- [`.agents/rules/steam_cef_ui_patterns.md`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/.agents/rules/steam_cef_ui_patterns.md#L47-L51): **Safe DOM Construction (Anti-XSS)** — keep assigning dynamic strings (`bTitle`, `bFile`, timestamps) via `.textContent` / `.title` or `escapeHtml(...)` on already-constructed DOM nodes.
- [`.agents/rules/test-regression.md`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/.agents/rules/test-regression.md): Add a regression assertion checking geometry bounds on rendered backup table cells before closing out the bug.
- [`res/CONTEXT.md`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/CONTEXT.md): Bundle via `./install.sh bundle-scripts lua` and keep `tests/integration/test_bundle_sync.py` green.

---

## 5. Verification Plan

1. **Build & Bundle Sync**:
   - `./install.sh bundle-scripts lua`
   - `./install.sh build`
2. **Regression Test (run only when explicitly requested by user)**:
   - Extend `tests/integration/ui/test_uj05_config_editor_validation.py` (`test_uj05_cloudredirect_ux_parity_controls`) to populate a long archive name + long game title in `#sls-cfg-cs-backups-list` and assert for every `td` in `tr.navi-cloud-backup-row`:
     - `td.getBoundingClientRect().right <= table.getBoundingClientRect().right + 1`
     - `Restore` and `Delete` buttons remain within `tdBActions.getBoundingClientRect()` bounds (`btnRect.right <= tdRect.right + 1`, `btnRect.left >= tdRect.left - 1`).
   - Execute via `./install.sh test-ui` (per `tests/CONTEXT.md`).

---

## 6. Risks & Edge Cases

- **Very narrow desktop/mobile breakpoint (`@media (max-width: 767px)`)**: At 767px, `table-layout: fixed` with percentages gives `ACTIONS` `min-width: 148px` while `.navi-cloud-table-region` provides `overflow-x: auto` horizontal scroll fallback so buttons never overlap `SIZE` or clip off-screen.
- **Long non-ASCII game titles / archive paths**: `text-overflow: ellipsis` requires `overflow: hidden` + `white-space: nowrap` + `table-layout: fixed` (`max-width: 0` on table cells) — setting `title` preserves full readability on hover without layout breakage.

