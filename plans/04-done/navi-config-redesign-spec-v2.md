# Navi Config Page — Redesign & Feature Logic Spec (v2)

- **Status:** done (verified with ./install.sh build)
- **Date:** 2026-11-05
- **Owner:** Navi CEF UI (`res/inject-scripts/shell/import-lua-script.js`) & Native Engine
- **Scope:** Modernized UI layouts matching design mockups, exact CSS design tokens & components, variable inventory restoration, deletion of obsolete gap cards, and C++ backend extensions (`DeleteAllBackups`, `PrefixSync` serialization, and `Live Provider Key Verification`).
- **Supersedes:** `plans/04-done/navi-config-copy-spec-v1.md`

---

## 1. Decisions Locked for v2

| # | Decision | Source |
|---|---|---|
| 1 | **Exact CSS Alignment**: Align all CSS styles (dark slate surface palette `#14171d`/`#1c202a`, rounded borders `6px`/`8px`, toggle switches, inset inputs, warning callouts, badges, modal dialogs) to match the mockup images. | User instruction |
| 2 | **Image 6 (Diagnostics)**: Remove `Copy diagnostics` button and `Log detail` dropdown select; keep preset chips and bitmask flag checkboxes. | User instruction |
| 3 | **Image 7 (Danger zone)**: Implement new feature logic for `Delete all backups` with confirmation dialog and backend C++ deletion. | User instruction |
| 4 | **Cloud saves**: Do not include `Back up before resetting` or `Backups to keep`; add Wine/Emulator prefix save sync (`PrefixSync`). | User instruction |
| 5 | **Unlocking**: Restore `Automation` card (`NotifyInit`, `API`, `MaxSchemaTries`); fix H1 heading to display `Unlocking`. | User instruction |
| 6 | **Account**: Restore `Unowned game status` (`UnownedStatus`); remove strict 17-digit validation toast on `Steam ID override`. | User instruction |
| 7 | **Library**: Restore `App tokens` (`AppTokens`), `Legacy CD keys` (`CDKeys`), and `Denuvo account locks` (`DenuvoGames`); cleanly separate `FakeOffline`, `FakeAppIds`, and `GameTitles`. | User instruction |
| 8 | **Backend Gaps Dropped**: Remove `Steam Web API key` from Provider keys and remove `AutoCrack` card from Library. | User instruction |
| 9 | **Live Key Validation**: Add live provider key verification with error banner on rejection. | User instruction |

---

## 2. CSS Design Tokens

```css
:root {
  --sls-bg-shell: #14171d;
  --sls-bg-sidebar: #191c24;
  --sls-bg-card: #1c202a;
  --sls-bg-item: #171a22;
  --sls-bg-input: #11141a;
  --sls-border-subtle: rgba(255, 255, 255, 0.06);
  --sls-border-card: rgba(255, 255, 255, 0.08);
  --sls-border-input: rgba(255, 255, 255, 0.12);
  --sls-border-focus: #1a9fff;
  --sls-accent-blue: #1a9fff;
  --sls-accent-blue-hover: #38bdf8;
  --sls-accent-green: #22c55e;
  --sls-accent-green-bg: rgba(34, 197, 94, 0.14);
  --sls-accent-amber: #f59e0b;
  --sls-accent-amber-bg: rgba(245, 158, 11, 0.14);
  --sls-accent-red: #ef4444;
  --sls-accent-red-bg: rgba(239, 68, 68, 0.14);
  --sls-text-primary: #ffffff;
  --sls-text-secondary: #e2e5e8;
  --sls-text-muted: #8a929e;
  --sls-text-dim: #626975;
  --sls-radius-sm: 4px;
  --sls-radius-md: 6px;
  --sls-radius-lg: 8px;
  --sls-radius-pill: 9999px;
}
```

---

## 3. Backend C++ Extensions

1. **`DeleteAllBackups`**:
   - `CCloudManager::DeleteAllBackups()` in `src/presenter/cloud_manager.{hpp,cpp}`.
   - Endpoint `POST /cloud/backups/delete-all` in `src/presenter/storeinject.cpp`.
2. **`PrefixSync` Serialization**:
   - Update `StoreInjectView::renderFullConfigJson` and `applyFullConfigJson` in `src/view/storeinject_view.cpp` to serialize/deserialize `CloudSaves.PrefixSync`.
3. **`Live Provider Key Verification`**:
   - Endpoint `POST /api/keys/verify` in `src/presenter/storeinject.cpp`.

---

## 4. Verification Gates

1. `./install.sh build` (C++20 `-m32` clean compilation and packaging).
