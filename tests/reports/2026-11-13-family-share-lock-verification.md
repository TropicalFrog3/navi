# Test Run — Family Share Lock fix for New Steam Families (`FamilyGroups`)

**Date:** 2026-11-13
**Component:** `src/presenter/apps.cpp`, `src/model/sdk/CUser.{hpp,cpp}`, `src/core/patterns.{hpp,cpp}`
**Config key:** `DisableFamilyShareLock` (`g_config.disableFamilyLock`)

---

## 1. Symptom under test

> "This account logged in on another computer" every 3-5 minutes when running games that
> interact with Steamworks after the 2026-11-03 maintenance.

Reproduction title: AppID `1566200` (*Tape to Tape*), shared via New Steam Families
(`FamilyGroups`) from `Red_Frog3` (`accountId 1555636215`), family group `8560626`,
local borrower `accountId 1254383366`.

## 2. Root cause

`CUser::CheckAppOwnership` reports New Steam Families apps as `familyShared = 0`,
`owner = self`, `realOwner = 0`. The original bypass only looked at those fields, so the
app was never classified as borrowed and stayed in the outbound
`CMsgClientGamesPlayedWithDataBlob` (`0x1522`) and `k_EMsgClientRichPresenceUpload`
(`0x1d4d`). Valve's CM then started the 180 s family-lock timer, which terminated the
game and disconnected the client every 3 minutes.

An intermediate attempt used `CUser::GetPreferredFamilyLender` (`0x1a4f050`). Live capture
proved it returns **0** for `1566200` even while the lender is actively playing, so that
approach was discarded.

## 3. Fix

`CUser::OwnsAppDirectly` (`0x1a12980`) walks every license for an app and returns `false`
when the only licenses present carry Steam's family-shared license flag (`0x4000`).
`isFamilySharedApp()` now ends with `return !usr->ownsAppDirectly(appId);` after the
legacy checks. The pattern is `optional` and the helper fails safe to `true`, so a missing
signature degrades to "never hide" rather than hiding owned games.

## 4. Live evidence (host, real Steam client)

Baseline: `tests/reports` run at 2026-11-13 16:54, Steam restarted with the fixed build
(libnavi `b058bef02773ac501b80fe244146e470257e9ee1a4f962516ad04f4e87357489`).

Lender active on the second device for the whole window.

| Check | Before fix | After fix |
|---|---|---|
| `Stop playing appID 1566200` lines | 16 (15:11–16:41, in 3-min cycles) | 16 — **no new lines** |
| Last lock line | `[2026-11-13 16:41:57] ... seconds left 56` | unchanged |
| `k_EMsgClientLoggedOff` / `LoggedInElsewhere` in `~/.navi.log` | recurring | **0** |
| Local game uptime during test | — | **6m54s** (`Tape to Tape.exe`, PID 228304) |

Previously the countdown logged at ~176 s, 116 s, 56 s, 0 s after every launch. The
post-fix session passed the full 180 s window twice with zero lock events, i.e. the CM
never saw the borrowed session.

## 5. Automated tests

| Suite | Result |
|---|---|
| `./bin/unit_tests` (all) | **112 passed** |
| `AppsFamilyShareTest.*` | **11 passed** |
| `pytest tests/integration/test_bundle_sync.py tests/integration/test_migration.py tests/integration/ui/test_uj05_config_editor_validation.py` | **81 passed** |
| `pytest tests/integration/ui/test_phase1_harness_smoke.py test_phase4_diagnostics_and_sla.py test_uj01_cloud_reset_input.py` | **12 passed** |

Note: a first run of the UI suites reported 5 failures + 1 error because Steam was
restarted mid-run while they were executing. Re-running them against a stable client
passed cleanly (12 passed), confirming the failures were harness flakiness from the
restart, not the change.

Regression tests added (`tests/unit/test_apps.cpp`):

- `SendGamesPlayedHidesNewSteamFamiliesBorrowedApp` — borrowed app with
  `familyShared = 0`, `owner = self`, no lender (`lender` returning 0) must be removed from
  `CMsgClientGamesPlayed` and its Rich Presence upload choked.
- `SendGamesPlayedKeepsDirectlyOwnedAppInsideFamilyGroup` — a directly owned app is never
  hidden, and an app with no license is never mistaken for borrowed.
- `SendGamesPlayedHidesLegacyFamilySharedByOwnerMismatch` — legacy Family Sharing reports
  `familyShared = 0` with a lender `owner`; the owner-mismatch branch must still hide the
  app even when the license check says the account holds a copy.
- `DefaultConfigEnablesFamilyShareLock` — default config, omitted-key fallback and
  `res/config.yaml` all keep `DisableFamilyShareLock: yes`.

Coverage (`make coverage`, identical run): `presenter/apps.cpp` 113/873 lines. Both
decision points of the new detection are exercised — `isFamilySharedApp` legacy
owner-mismatch return (line 1447, 1 hit) and `return !usr->ownsAppDirectly(appId)`
(line 1461, 10 hits). The Makefile has no coverage threshold gate; repo total is 48%
(6642/13792).

## 6. Config parity

`DisableFamilyShareLock` is on in all shipped/default surfaces:

- `res/config.yaml` → `DisableFamilyShareLock: yes`
- `src/model/config_default.hpp` → same
- `src/model/config.cpp` → fallback default `true`
- `res/inject-scripts/shell/import-lua/config-controller.js` → reset-to-defaults payload `true`
- regenerated `res/inject-scripts/shell/import-lua-script.js`
- live runtime `~/.config/navi/config.yaml` line 1 → `DisableFamilyShareLock: true`
