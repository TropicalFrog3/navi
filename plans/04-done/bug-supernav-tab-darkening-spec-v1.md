# Bug Spec: Stop Darkening Other SuperNav Tabs When Clicking Navi Config

- **Status:** Spec v1
- **Category:** UI & UX Bug
- **Owning Layer:** CEF Shell Inject (`res/inject-scripts/shell/import-lua/`)
- **Source Wishlist Item:** `plans/01-backlog/dev-plan.md` — `- when clicking navi config tab the other tabs get darken, it is not supposed to be darken or changes`

---

## 1. Problem Statement & Root Cause Analysis

When a user clicks **Navi Config** (`#sls-config-top-tab`) in Steam's top SuperNav bar, all other native Steam SuperNav tabs (`STORE`, `LIBRARY`, `COMMUNITY`, `<USER PROFILE>`, `CONSOLE`) visibly darken and lose their normal hover brightness.

Inspection of [`res/inject-scripts/shell/import-lua/styles.css`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/styles.css#L8-L15) reveals the exact rule causing this:
```css
body:has(#sls-top-tab.__STEAM_TAB_ACTIVE_CLASS__, #sls-config-top-tab.__STEAM_TAB_ACTIVE_CLASS__) .__STEAM_TAB_BASE_CLASS__:not(#sls-top-tab):not(#sls-config-top-tab) .__STEAM_TAB_TEXT_CLASS__ {
    color: rgb(150, 150, 150) !important;
    text-shadow: none !important;
    font-weight: normal !important;
}
body:has(#sls-top-tab.__STEAM_TAB_ACTIVE_CLASS__, #sls-config-top-tab.__STEAM_TAB_ACTIVE_CLASS__) .__STEAM_TAB_BASE_CLASS__:not(#sls-top-tab):not(#sls-config-top-tab) .__STEAM_TAB_TEXT_CLASS__::after {
    display: none !important;
}
```
Why this misbehaves:
1. It matches **every** native SuperNav tab (`.__STEAM_TAB_BASE_CLASS__:not(#sls-top-tab):not(#sls-config-top-tab)`), not just the single tab that currently carries `__STEAM_TAB_ACTIVE_CLASS__` (`_1gqEjB5QsKT_NftD1dEsdZ`).
2. Steam's native inactive SuperNav tab text color is `#b8bcbf` / `#dcdedf` (substantially brighter than `rgb(150, 150, 150)`), and hovering any tab normally transitions text color to `#ffffff`. Because of `!important`, `rgb(150, 150, 150)` forces all tabs darker and blocks `:hover` feedback while `#sls-config-top-tab` is active.
3. All we actually want visually when `#sls-config-top-tab` (or `#sls-top-tab`) is active is to suppress the blue active underline (`::after`) and blue active highlight on whichever native tab still holds `__STEAM_TAB_ACTIVE_CLASS__`, without darkening inactive tabs or breaking `:hover`.

---

## 2. Target Files & Symbols

| File | Symbol / Selector | Change Summary |
|---|---|---|
| [`res/inject-scripts/shell/import-lua/styles.css`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/styles.css#L8-L15) | `body:has(...) .__STEAM_TAB_BASE_CLASS__...` | Remove the blanket `color: rgb(150, 150, 150) !important;` override on all non-Navi tabs; scope active-state neutralization strictly to `. __STEAM_TAB_BASE_CLASS__.__STEAM_TAB_ACTIVE_CLASS__:not(#sls-top-tab):not(#sls-config-top-tab):not(:hover)` using native inactive styling (`#b8bcbf` / `#dcdedf`) and hiding only its `::after` active underline |
| [`res/inject-scripts/shell/import-lua-script.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua-script.js) | Bundled artifact | Regenerated via `./install.sh bundle-scripts lua` (runs `styles.js` placeholder substitution) |
| [`tests/integration/ui/test_uj05_config_editor_validation.py`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/tests/integration/ui/test_uj05_config_editor_validation.py) | Regression check | Assert inactive native SuperNav tab colors are unchanged (never `rgb(150, 150, 150)`) before vs. after opening `#sls-config-top-tab` |

---

## 3. Detailed Implementation Steps

### Step 3.1 — Fix SuperNav Active Override Selectors in `styles.css`
Update lines 8–15 of [`res/inject-scripts/shell/import-lua/styles.css`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/styles.css#L8-L15):

**Before:**
```css
body:has(#sls-top-tab.__STEAM_TAB_ACTIVE_CLASS__, #sls-config-top-tab.__STEAM_TAB_ACTIVE_CLASS__) .__STEAM_TAB_BASE_CLASS__:not(#sls-top-tab):not(#sls-config-top-tab) .__STEAM_TAB_TEXT_CLASS__ {
    color: rgb(150, 150, 150) !important;
    text-shadow: none !important;
    font-weight: normal !important;
}
body:has(#sls-top-tab.__STEAM_TAB_ACTIVE_CLASS__, #sls-config-top-tab.__STEAM_TAB_ACTIVE_CLASS__) .__STEAM_TAB_BASE_CLASS__:not(#sls-top-tab):not(#sls-config-top-tab) .__STEAM_TAB_TEXT_CLASS__::after {
    display: none !important;
}
```

**After:**
```css
body:has(#sls-top-tab.__STEAM_TAB_ACTIVE_CLASS__, #sls-config-top-tab.__STEAM_TAB_ACTIVE_CLASS__) .__STEAM_TAB_BASE_CLASS__.__STEAM_TAB_ACTIVE_CLASS__:not(#sls-top-tab):not(#sls-config-top-tab):not(:hover) .__STEAM_TAB_TEXT_CLASS__ {
    color: #dcdedf;
    text-shadow: none;
    font-weight: normal;
}
body:has(#sls-top-tab.__STEAM_TAB_ACTIVE_CLASS__, #sls-config-top-tab.__STEAM_TAB_ACTIVE_CLASS__) .__STEAM_TAB_BASE_CLASS__.__STEAM_TAB_ACTIVE_CLASS__:not(#sls-top-tab):not(#sls-config-top-tab) .__STEAM_TAB_TEXT_CLASS__::after {
    display: none !important;
}
```
Notice:
- Inactive tabs (`STORE`, `COMMUNITY`, etc., which do not have `__STEAM_TAB_ACTIVE_CLASS__`) are **untouched** — their computed color and hover transitions remain identical to native Steam.
- Only the single native tab carrying `__STEAM_TAB_ACTIVE_CLASS__` (`LIBRARY`) drops its blue underline (`::after`) and active text glow while `Navi Config` is open, without forcing dark `rgb(150, 150, 150)` or overriding `:hover`.

### Step 3.2 — Bundle Scripts
Run `./install.sh bundle-scripts lua` so `styles.js` compiles the updated CSS tokens (`__STEAM_TAB_BASE_CLASS__` -> `_2Lu3d-5qLmW4i19ysTt2jT`, `__STEAM_TAB_ACTIVE_CLASS__` -> `_1gqEjB5QsKT_NftD1dEsdZ`, `__STEAM_TAB_TEXT_CLASS__` -> `_19axKcqYRuaJ8vdYKYmtTQ`).

---

## 4. Domain Invariants & Rules

- [`res/inject-scripts/shell/import-lua/utils.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/utils.js#L36-L40): Keep Steam hashed SuperNav class constants centralized (`STEAM_TAB_BASE_CLASS`, `STEAM_TAB_ACTIVE_CLASS`, `STEAM_TAB_TEXT_CLASS`) and replaced through [`styles.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/styles.js#L10-L12).
- [`.agents/rules/test-regression.md`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/.agents/rules/test-regression.md): Add UI color-preservation check for inactive SuperNav tabs.

---

## 5. Verification Plan

1. **Build & Bundle Sync**:
   - `./install.sh bundle-scripts lua`
   - `./install.sh build`
2. **Regression Test (run only when explicitly requested by user)**:
   - Update `tests/integration/ui/test_uj05_config_editor_validation.py` (using the Steam SuperNav fixture tabs in `tests/integration/ui/fixtures/steam_library_sp.html`) to sample `getComputedStyle(inactiveTabText).color` on `STORE` / `COMMUNITY` before clicking `#sls-config-top-tab` and assert it equals the post-click computed color (not `rgb(150, 150, 150)`), while the formerly active `LIBRARY` tab loses its `::after` indicator cleanly.
   - Execute via `./install.sh test-ui`.

---

## 6. Risks & Edge Cases

- **Steam Client Theme Color Changes**: Using `#dcdedf` without `!important` only softens the single `.__STEAM_TAB_ACTIVE_CLASS__` tab's highlight when not hovered, or alternatively inspecting the sibling inactive tab's computed style in `steam_library_sp.html` confirms Steam's exact native color (`#dcdedf` / `rgb(220, 222, 223)`). Inactive tabs never match `.__STEAM_TAB_ACTIVE_CLASS__` so they remain 100% native under any Steam theme.
- **Test fixture (`steam_library_sp.html`)**: Verify during implementation how `steam_library_sp.html` styles native SuperNav tabs so the regression test captures a true pre/post comparison.

