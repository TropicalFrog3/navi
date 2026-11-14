# Bug Spec: Fix Library "+ Add Game" Button Spacing & Add Quick-Add Buttons to All Library Cards

- **Status:** Spec v1
- **Category:** UI & UX Bug
- **Owning Layer:** CEF Shell Inject (`res/inject-scripts/shell/import-lua/`)
- **Source Wishlist Item:** `plans/01-backlog/dev-plan.md` — `- (in navi config tab) Library section: fix the add game button (there's no margin with the sub title (not appealing for the eyes))` & `- fix no other card section have add game why??`

---

## 1. Problem Statement & Root Cause Analysis

In **Navi Config → Library** (`#sls-cfg-sec-overrides`), there are two related UI/UX issues:

1. **No margin / broken flex direction on `Unlocked games` (`+ Add game`)**:
   - In [`res/inject-scripts/shell/import-lua/styles.css`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/styles.css#L27), `.sls-cfg-item` defaults to `display: flex; flex-direction: row; align-items: center; justify-content: space-between; gap: 14px; flex-wrap: wrap;`.
   - In [`res/inject-scripts/shell/import-lua/config-sections.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/config-sections.js#L304-L312), the `Unlocked games` `.sls-cfg-item` container omits `flex-direction: column; align-items: stretch;`. As a result, its three children — (a) the title + `#sls-cfg-add-app-btn` wrapper, (b) the subtitle (`"One AppID per line. Adding a game also includes its DLC."`), and (c) `<textarea id="sls-cfg-AppIds">` — are laid out horizontally along a flex row before wrapping! This squeezes the header wrapper against the subtitle with zero separation between `+ Add game` and the surrounding text.
   - Similarly, `makeTextareaItem(...)` in [`res/inject-scripts/shell/import-lua/config-helpers.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/config-helpers.js#L49-L58) relies on `.sls-cfg-item` without `flex-direction: column; align-items: stretch;`, causing the title, description, and textarea inside every `Library` item to fight the default horizontal flex row!

2. **Missing `+ Add ...` quick-add buttons on all other `Library` card sections**:
   - Currently, only `#sls-cfg-AppIds` (`Unlocked games`) has a quick-add dialog button (`#sls-cfg-add-app-btn` wired via `slsConfirmDialog` in [`config-controller.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/config-controller.js#L1935-L1955)).
   - The remaining `Library` items — `Extra games` (`AdditionalApps`), `Show as offline` (`FakeOffline`), `Play as another game` (`FakeAppIds`), `Game names` (`GameTitles`), `Lock to a version` (`ManifestIds`), `Skip these depots` (`DepotBlacklist`), `App tokens` (`AppTokens`), and `Legacy CD keys` (`CDKeys`) — all call `makeTextareaItem(...)`, which has no action-button support, leaving users to hand-format comma lists or `Key: Value` YAML syntax without helper prompts.

---

## 2. Target Files & Symbols

| File | Symbol / Selector | Change Summary |
|---|---|---|
| [`res/inject-scripts/shell/import-lua/config-helpers.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/config-helpers.js#L49-L58) | `makeTextareaItem(id, label, desc, content, placeholder, rows, opts)` | Force vertical column flex layout (`flex-direction: column; align-items: stretch; gap: 6px;`), render optional `opts.addBtn` in a spaced flex header (`gap: 16px`), and support inline error slots cleanly |
| [`res/inject-scripts/shell/import-lua/config-sections.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/config-sections.js#L304-L334) | `appListsContent`, `downloadContent`, `advancedContent`, `yamlContent` | Use `makeTextareaItem` (or matching column flex layout) for `Unlocked games` (`#sls-cfg-add-app-btn` with proper `16px` gap) and add `+ Add ...` buttons to `Extra games`, `Show as offline`, `Play as another game`, `Game names`, `Lock to a version`, `Skip these depots`, `App tokens`, `Legacy CD keys` |
| [`res/inject-scripts/shell/import-lua/config-dialogs.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/config-dialogs.js#L26-L117) | `slsConfirmDialog(opts)` | Optionally support `opts.inputs` (two-field modal, e.g., `AppID` + `Fake AppID` / `Game title` / `ManifestID` / `Token` / `CD Key`) while staying backward-compatible with `opts.input` |
| [`res/inject-scripts/shell/import-lua/config-controller.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/config-controller.js#L1935-L1955) | `#sls-cfg-add-*` click handlers | Refactor `#sls-cfg-add-app-btn` and wire quick-add buttons for the remaining `Library` textareas using a shared helper |
| [`res/inject-scripts/shell/import-lua-script.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua-script.js) | Bundled artifact | Regenerated via `./install.sh bundle-scripts lua` |

---

## 3. Detailed Implementation Steps

### Step 3.1 — Fix `makeTextareaItem` Layout & Add Optional Button Support (`config-helpers.js`)
Update `makeTextareaItem` in [`res/inject-scripts/shell/import-lua/config-helpers.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/config-helpers.js#L49-L58):
- Add `flex-direction: column; align-items: stretch; gap: 6px;` to `.sls-cfg-item`.
- Accept optional 7th parameter `opts`: `{ addBtnId, addBtnLabel }`.
- Render a full-width top header row with stacked title + subtitle on the left and optional button on the right:
  ```html
  <div style="display: flex; justify-content: space-between; align-items: flex-start; gap: 16px; width: 100%; margin-bottom: 6px;">
      <div style="flex: 1 1 auto; min-width: 0;">
          <div style="font-size: 14px; font-weight: 500; color: #ffffff; line-height: 19px;">...</div>
          <div style="font-size: 12.5px; color: #8a929e; line-height: 17px; margin-top: 3px;">...</div>
      </div>
      <!-- optional: -->
      <button id="..." type="button" class="sls-cfg-btn" style="padding: 5px 12px; font-size: 12px; flex-shrink: 0; margin-left: 8px; white-space: nowrap;">+ Add ...</button>
  </div>
  ```
This guarantees generous separation (`gap: 16px` + `margin-left: 8px`) between the button and both the title and subtitle, preventing them from ever touching.

### Step 3.2 — Convert `Unlocked games` & Add Quick-Add Buttons in `config-sections.js`
Update lines 304–323 of [`res/inject-scripts/shell/import-lua/config-sections.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/config-sections.js#L304-L323) to pass `opts` to `makeTextareaItem` across the Library cards:
1. `sls-cfg-AppIds` (`Unlocked games`): `{ addBtnId: 'sls-cfg-add-app-btn', addBtnLabel: '+ Add game' }`
2. `sls-cfg-AdditionalApps` (`Extra games`): `{ addBtnId: 'sls-cfg-add-extra-app-btn', addBtnLabel: '+ Add game' }`
3. `sls-cfg-FakeOffline` (`Show as offline`): `{ addBtnId: 'sls-cfg-add-offline-btn', addBtnLabel: '+ Add game' }`
4. `sls-cfg-FakeAppIds` (`Play as another game`): `{ addBtnId: 'sls-cfg-add-fakeappid-btn', addBtnLabel: '+ Add mapping' }`
5. `sls-cfg-GameTitles` (`Game names`): `{ addBtnId: 'sls-cfg-add-gametitle-btn', addBtnLabel: '+ Add title' }`
6. `sls-cfg-ManifestIds` (`Lock to a version`): `{ addBtnId: 'sls-cfg-add-manifest-btn', addBtnLabel: '+ Add version' }`
7. `sls-cfg-DepotBlacklist` (`Skip these depots`): `{ addBtnId: 'sls-cfg-add-depot-blacklist-btn', addBtnLabel: '+ Add depot' }`
8. `sls-cfg-AppTokens` (`App tokens`): `{ addBtnId: 'sls-cfg-add-apptoken-btn', addBtnLabel: '+ Add token' }`
9. `sls-cfg-CDKeys` (`Legacy CD keys`): `{ addBtnId: 'sls-cfg-add-cdkey-btn', addBtnLabel: '+ Add key' }`

(Leave `DlcDataYaml` and `DenuvoGamesYaml` under `Raw YAML` as multi-line YAML editors, or add an example template button if desired, while ensuring both also use the fixed column flex `makeTextareaItem` layout.)

### Step 3.3 — Extend `slsConfirmDialog` & Wire Quick-Add Handlers (`config-dialogs.js`, `config-controller.js`)
- In [`config-dialogs.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/config-dialogs.js#L55-L76), support `opts.inputs` (array of `{ key, label, placeholder }`) cleanly alongside existing `opts.input`, passing a values map `{ [key]: val }` to `opts.onConfirm`.
- In [`config-controller.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/config-controller.js#L1935-L1955), implement a single table-driven loop registering all 9 `#sls-cfg-add-*` buttons without duplicated handler code:
  - Numeric list fields (`AppIds`, `AdditionalApps`, `FakeOffline`, `DepotBlacklist`) prompt for `AppID` / `DepotID`, validate digits, append using comma or newline consistent with existing text (calling `el.dispatchEvent(new Event('input', { bubbles: true }))` or `checkDirty()`). Note: `AppIds` / `AdditionalApps` / `FakeOffline` / `DepotBlacklist` run live validation via `input` event — dispatching `'input'` ensures validation & `checkDirty()` both trigger!
  - Key-value fields (`FakeAppIds`, `GameTitles`, `ManifestIds`, `AppTokens`, `CDKeys`) prompt for key + value (`<id>: <value>`), append on a new line, and trigger `checkDirty()`.

### Step 3.4 — Bundle Scripts
Run `./install.sh bundle-scripts lua`.

---

## 4. Domain Invariants & Rules

- [`res/inject-scripts/shell/import-lua/config-controller.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/config-controller.js#L1935): Keep exact ID `#sls-cfg-add-app-btn` and dialog flow for `Unlocked games`.
- Code quality: Use a single data-driven table/helper in `config-controller.js` for all 9 quick-add buttons — zero copy-pasted click handler blocks.
- [`.agents/rules/test-regression.md`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/.agents/rules/test-regression.md): Add layout geometry & quick-add functional assertions.

---

## 5. Verification Plan

1. **Build & Bundle Sync**:
   - `./install.sh bundle-scripts lua`
   - `./install.sh build`
2. **Regression Test (run only when explicitly requested by user)**:
   - In `tests/integration/ui/test_uj05_config_editor_validation.py`, navigate to `sls-cfg-sec-overrides`:
     - Assert `#sls-cfg-add-app-btn`'s bounding rect left edge is at least `16px` to the right of both the `Unlocked games` title text and subtitle right edge (and/or positioned above `<textarea>` in a clean header row without overlap).
     - Assert quick-add buttons exist on all Library cards (`Games`, `Downloads`, `Advanced maps`) and clicking e.g. `#sls-cfg-add-fakeappid-btn` appends a mapping to `#sls-cfg-FakeAppIds` and sets dirty state.
   - Execute via `./install.sh test-ui`.

---

## 6. Risks & Edge Cases

- **Inline validation sync**: `collectAndSaveConfig()` (`config-controller.js:2100`) parses array fields with `.split(/[\s,]+/)`, whereas `formatArrayForEditor()` (`config-helpers.js:35`) joins with `', '`. Appending with `\n` or `, ` both parse identically; appending `, ` when inline or `\n` cleanly matches existing entries. Also ensure live validation (`#sls-cfg-err-sls-cfg-AppIds`, etc.) updates when a quick-add button inserts text.
- **Steam Deck narrow viewport (`767px`)**: Setting `flex-shrink: 0` on the quick-add button and `min-width: 0` on the left text block prevents button shrinking while keeping text wrapping cleanly.

