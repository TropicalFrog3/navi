# Handoff: Fix `DisableFamilyShareLock` for New Steam Families (`FamilyGroups`)

**Date:** 2026-11-13  
**Target Branch/Workspace:** `/home/nicolas/Documents/my-github/SLSsteam_n_ACCELA` (`plans/03-active/`)  
**Target Component:** `src/presenter/{apps.cpp,apps.hpp,hooks.cpp,fakeappid.cpp}`, `src/model/sdk/*`, `src/core/patterns.*`, `tests/unit/test_apps.cpp`

---

## 1. Problem Summary & Reproduction

With `DisableFamilyShareLock: true` (mapped to `g_config.disableFamilyLock`), playing a game shared via **New Steam Families (`FamilyGroups`)** while the library owner is active still triggers:
1. Toast notification: `"Family Sharing: This shared game is already in use and will close in 2 minutes."`
2. Termination modal: `"Family Sharing: Steam terminated Tape to Tape because Red_Frog3 is currently using this game library."`
3. Steam log (`~/.local/share/Steam/logs/librarysharing_log.txt`):
   `[2026-11-13 16:05:07] Stop playing appID 1566200, ownerID 1555636215, seconds left 176`

Live reproduction on host:
- Game: AppID `1566200` (*Tape to Tape*) — NOT unlocked by Navi (`Package 0` does not contain it).
- Family Group ID: `8560626`, lender `Red_Frog3` (`accountId = 1555636215`), local borrower (`accountId = 1254383366`, `steamId64 = 76561199214649094`).

---

## 2. Root Cause Proven

Live `[FamDiag]` capture in `~/.navi.log` + static ELF32 inspection of `/home/nicolas/.local/share/Steam/ubuntu12_32/steamclient.so` (SHA-256 `c770a8b13872fed128dd357eabc1d715260fadd9b16c627ae3395406e6ea37d4`) established why the existing `isFamilySharedApp()` check (`src/presenter/apps.cpp:1413`) returned `false` (`match=0`):

1. **`CUser::CheckAppOwnership` (`0x1a11570`) only flags legacy Family Sharing**:
   - In `CheckAppOwnership`, `pInfo->familyShared` (`+0x35`) and lender `pInfo->owner` (`+0x08`) are only populated when license flag bit `0x4000` is set.
   - For New Steam Families (`1566200`), `CheckAppOwnership` returned `true` with **`familyShared = 0`** and **`infoOwner = 1254383366`** (self).
   - Outbound `CMsgClientGamesPlayedWithDataBlob` (`0x1522`) also carried `pktOwner = 1254383366`.
   - Because `isFamilySharedApp()` returned `false`:
     - `1566200` was NOT stripped from `CMsgClientGamesPlayedWithDataBlob` (`0x1522`).
     - `k_EMsgClientRichPresenceUpload` (`0x1d4d`, `routing_app=1566200`) was NOT choked by `Apps::shouldChokeSendPacket`.

2. **How `steamclient.so` tracks New Steam Families (`FamilyGroups`) sharing**:
   - `CUser + 0x4f9c / +0x4fa0`: 64-bit `m_FamilyGroupID` (`!= 0` when in a Steam Family).
   - `CUser + 0x4fb8` (root node index) / `CUser + 0x4fcc` (node array base, stride `0x2c`, key `AppId_t` at `[node + 0x10]`): RB-tree mapping apps in the Family library.
   - `0x1a4f050(CUser* user, AppId_t appId)` (callers: `0x1181c4e`, `0x1184baa`, `0x1a4e57d`):
     - Looks up `appId` in `CUser+0x4fb8`/`0x4fcc`.
     - Requires `[node + 0x18] == 0`.
     - Scans `[node + 0x28]` structs of size `0x10` at `[node + 0x1c]` matching `[entry + 0x8] == [node + 0x14]` to read `[entry + 0x0]`.
     - Compares against `[user + 0x166]` (self `accountId`); returns `0` if equal, otherwise returns the non-zero lender `accountId`.
   - `0x1184ba0..0x1184be3`: Steam sets app flag `0x100` (`or DWORD PTR [esi+0x4], 0x100`) when `0x1a4f050(user, appId) != 0` **AND** `0x1a12980(user, appId) == 0` (user does not directly own the app).
   - `0x1a4e4c0` (vtable slot at VA `0x3243ee0` / file offset `0x3241ee0`, 6 args: `CUser*, AppId_t, uint32_t*, uint32_t*, uint32_t*, bool*`): checks `0x1a11570` + `0x1a12980`, then if `m_FamilyGroupID != 0` calls `0x1a4f050` and reads `CUser+0x4fb8`, else falls back to legacy RB-tree `CUser+0x1c64`/`0x1c78` and `info.familyShared`.
   - `0x1a4f100(CUser* user, AppId_t appId, uint32_t defaultOwner)` (sole direct caller `0x1a2ce8b`): resolves preferred/available/locked owner for shared-app launch (GOT strings at `0xd7f95c`, `0xd7f914`, `0xd7f8e4`: `"Launching shared app %u : using ... owner %u"`).
   - `0x15b349e..0x15b35cb`: IPC map handler for `IClientUser::GetFamilyCopyCounts` (hash `0x580b7b6a`) calls `IClientUser` vtable slot `256` (`[eax+0x400]`) with `(this, appId)` returning `bool` in `al` — verify exact `CClientUser` vtable slot / method semantics in next session before relying on it.

---

## 3. Current Workspace State (Dirty Working Tree + Diagnostics)

Modified/untracked files (`git status -s`):
- Modified:
  - `Makefile`
  - `res/config.yaml`
  - `res/inject-scripts/shell/import-lua-script.js`
  - `res/inject-scripts/shell/import-lua/config-sections.js`
  - `src/model/config.cpp`
  - `src/model/config_default.hpp`
  - `src/presenter/apps.cpp`
  - `src/presenter/apps.hpp`
  - `src/presenter/fakeappid.cpp`
  - `src/presenter/hooks.cpp`
  - `tests/CONTEXT.md`
  - `tests/unit/test_stubs.cpp`
  - `tests/unit/test_stubs.hpp`
- Untracked:
  - `tests/unit/test_apps.cpp`

**Important cleanup items already in the working tree:**
1. Temporary `[FamDiag]` `LOG_INFO` instrumentation is still active in:
   - `src/presenter/hooks.cpp` (L409-412, L475-479, L523-543, L735-785)
   - `src/presenter/apps.cpp` (L1459-1463, L1487-1494, L1557-1561, L1665, L1671-1672)
   - Also check `src/presenter/apps.cpp:1575`: restore accidental `const auto app` -> `auto app` in `sendPICSInfoRequest`.
2. `~/.local/share/navi/libnavi.so` currently has the `[FamDiag]` diagnostic build installed (and `/tmp/checkown_p2.asm`, `/tmp/checkown_mid.asm`, `/tmp/fam1.asm` may remain in `/tmp`).

**Tooling note on this host:**
- `rg` is not installed (`grep -rn` works).
- In REA (`rea-agents@6.7.0`), `hopper` rejects with `unsupported_hopper_build`, and `ghidra`/`ida`/`kuna`/`rizin` are not installed/configured. Bounded `objdump -d -M intel --start-address=... --stop-address=...` and Python ELF scripts work fast for static analysis; Frida (`list_frida_processes`, etc.) is available if runtime observation is needed.

---

## 4. Exact Next Steps to Resume

1. **Finish inspecting the shared-app launch & outbound telemetry flow in `steamclient.so`**:
   - Disassemble `0x1a2cd07..0x1a2d150` (rest of the caller at `0x1a2ce8b` around `"Launching shared app %u : ..."`), plus callers of `0x1a4e4c0` / `IClientUser` vtable slot `256` (`[eax+0x400]` at `0x15b35cb`) and `0x1a4f100`.
   - Confirm whether `CClientUser` exposes a clean public/IPC method (`IClientUser` vtable slot `256` / `GetFamilyCopyCounts` / `GetSharedAppLockInfo` at `0x15c809d`/`0x17694e4`) OR whether a verified `Pattern_t` / SDK helper for `CUser::GetPreferredFamilyLender` (`0x1a4f050`) + `CUser::OwnsAppDirectly` (`0x1a12980`) (or `0x1a4e4c0` vtable method) is cleaner and safer across Steam updates. Remember `.agents/rules/cpp_hooking_patterns.md` (no `regparm` signature mismatch, target module `&g_modSteamClient`, null guards).
   - Inspect outbound `Cloud.SignalAppLaunchIntent#1` and `k_EMsgClientGamesPlayed*` construction in `steamclient.so` to check if `Cloud.SignalAppLaunchIntent#1` (seen right before `GamesPlayedWithDataBlob` in `~/.navi.log`) or anything else leaks shared-app launch state to Valve's CM once `GamesPlayed*` and `RichPresenceUpload` are actually stripped.
   - Check why `CMsgClientGamesPlayedWithDataBlob` had `pktOwner == 1254383366` (self) instead of `1555636215` — verify whether `CUser` populates `game_id` / `owner_id` elsewhere under `FamilyGroups`, or omits `owner_id` when using New Steam Families!

2. **Remove temporary `[FamDiag]` logging**:
   - Strip all `[FamDiag]` blocks from `src/presenter/hooks.cpp` and `src/presenter/apps.cpp`, and restore `src/presenter/apps.cpp:1575` (`auto app`).

3. **RED -> GREEN TDD (`.agents/rules/test-regression.md`)**:
   - Add RED unit tests in `tests/unit/test_apps.cpp` covering:
     - New Steam Families app where `CheckAppOwnership` returns `familyShared == 0` and `owner == self` (`1254383366`), while the Family Group sharing signal marks it as borrowed/shared -> must be detected as Family-Shared (stripped from `CMsgClientGamesPlayed*`, choked in `shouldChokeSendPacket`, `unlockApp` skipped, etc.).
     - Directly owned app (`0x1a12980` / direct license) even inside a Family Group -> must NOT be treated as borrowed/choked.
     - Any additional outbound leak found in Step 1 (e.g., `Cloud.SignalAppLaunchIntent#1` if proven).
   - Run `./install.sh build && ./bin/unit_tests` to confirm RED failure, then implement the GREEN fix and re-run `./install.sh build && ./bin/unit_tests`.

4. **Live host verification (`1566200`, *Tape to Tape*)**:
   - Run `./install.sh install`, restart Steam, and launch `1566200` while `Red_Frog3` uses the library.
   - Verify via `~/.navi.log` and `~/.local/share/Steam/logs/librarysharing_log.txt` that `1566200` is detected as Family-Shared, stripped from `CMsgClientGamesPlayed*` / `RichPresenceUpload`, and no 2-minute closing warning / `Logged In Elsewhere` kick occurs.

5. **Final verification & close-out**:
   - Run full verification (`./install.sh build`, `./bin/unit_tests`, targeted `pytest -q tests/integration/test_bundle_sync.py tests/integration/test_migration.py tests/integration/ui/test_uj05_config_editor_validation.py`, `make coverage`), check `git status -s` & `git diff`.
   - If creating/renaming source files or editing `@brief`, run `python3 scripts/dev/sync-manifests.py` via subagent (`.agents/rules/manifest-sync.md`).
   - Clean up `/tmp/*.asm` and close out / archive spec in `plans/04-done/` per `plans-pipeline` (`Outcome: ...`).

---

## 5. Outcome

**Status:** Resolved and live-verified on 2026-11-13.

**What actually fixed it:** the `GetPreferredFamilyLender` (`0x1a4f050`) approach from
section 4.1 was implemented, unit-tested, and then **disproved live**: instrumenting
`isFamilySharedApp()` on a real launch showed `app=1566200 direct=0 lender=0` even while
`Red_Frog3` was playing the game. That function is a `std::map`-style lookup on
`CUser+0x4fb8`/`0x4fcc` that bails out and returns 0 when `[node+0x18] != 0`, so it is not
a usable "is this borrowed" signal. It was removed again (pattern, helper, stubs).

The reliable signal is `CUser::OwnsAppDirectly` (`0x1a12980`, pattern kept as `optional`,
fail-safe `true`). Its disassembly shows it scans every license for the app and only
returns `false` when all matching licenses carry Steam's family-shared license flag
(`test BYTE PTR [edi+0x19], 0x40`). `isFamilySharedApp()` therefore ends with
`return !usr->ownsAppDirectly(appId);` after the legacy `familyShared` / owner-mismatch
checks. This covers legacy Family Sharing and New Steam Families while leaving directly
owned, F2P and free-weekend licenses visible.

**Live evidence** (`tests/reports/2026-11-13-family-share-lock-verification.md`):

- Before: 16 `Stop playing appID 1566200, ownerID 1555636215` lines, in 3-minute cycles
  from 15:11 through `16:41:57`.
- After (fixed build, lender active on the second device): game ran **6m54s**
  (`Tape to Tape.exe` PID 228304) with **no new lock line** and **0**
  `k_EMsgClientLoggedOff` / `LoggedInElsewhere` markers in `~/.navi.log`.

**Config:** `DisableFamilyShareLock` stays ON in every default surface — `res/config.yaml`,
`src/model/config_default.hpp`, the `src/model/config.cpp` fallback, and the inject-script
reset payload (`config-controller.js` + regenerated `import-lua-script.js`). The live
runtime `~/.config/navi/config.yaml` line 1 is `DisableFamilyShareLock: true`.

**Tests:** `./bin/unit_tests` → 112 passed (11 in `AppsFamilyShareTest.*`, including the new
`SendGamesPlayedHidesNewSteamFamiliesBorrowedApp` regression that asserts detection works
with `lender == 0`, plus `SendGamesPlayedHidesLegacyFamilySharedByOwnerMismatch` for the
legacy owner-mismatch branch). Integration: 81 passed (bundle sync, migration, config
editor UI) and 12 passed (phase1/phase4/uj01 UI, after re-running on a stable client).
`make coverage`: both new decision points in `isFamilySharedApp` are executed (lines 1447
and 1461); no threshold gate exists in the Makefile.

**Remaining cleanup performed:** all `[FamDiag]`/`[FamProbe]`/`[OwnProbe]`/`[PktProbe]`
instrumentation removed, `getFamilyLender` and its test stubs deleted, temp scripts under
`/tmp/opencode/` removed, manifests verified in sync.


