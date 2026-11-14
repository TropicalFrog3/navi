# Feature Spec: Dynamic Purchase Date from Lua/Manifest File Timestamp

- **Status:** Spec v1
- **Category:** Core Feature
- **Owning Layer:** C++ Model (`src/model/config.hpp`, `src/model/config.cpp`) & Presenter (`src/presenter/apps.cpp`)
- **Source Wishlist Item:** `plans/01-backlog/dev-plan.md` — `- fix purchase date (right is hard coded date, needs to be the date the user added the lua / manifest file)`

---

## 1. Problem Statement & Root Cause Analysis

In Steam's Game Details view for unlocked games, the **Purchased on** date currently shows the exact same fallback date (Package 0 / epoch `0` or whatever Steam's base ownership call wrote) for every unlocked game unless the user manually hand-edits `SubscriptionTimestamps` in `~/.config/navi/config.yaml`.

Inspection of [`src/presenter/apps.cpp`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/src/presenter/apps.cpp#L729-L760) (`Apps::unlockApp`) and [`lines 989–995`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/src/presenter/apps.cpp#L989-L995) (`Apps::checkAppOwnership`):
```cpp
const auto times = g_config.subscriptionTimestamps.get();
if (times.contains(appId))
{
    pInfo->purchaseTime = times.at(appId);
}
```
Root gaps:
1. Only explicit `SubscriptionTimestamps` overrides in `config.yaml` alter `pInfo->purchaseTime`; otherwise Steam leaves a fixed/shared default timestamp from Package 0 on every unlocked game.
2. `Apps::unlockApp` (`apps.cpp:729-760`) sets ownership flags (`subId`, `owner`, `ownsLicense`, `licensePermanent`) but never populates `info->purchaseTime` from the `.lua` / `.manifest` file creation/addition timestamp.
3. [`CConfig::scanLuaPluginsAndUpdateConfig()`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/src/model/config.cpp#L1073-L1200) scans `<steamRoot>/config/stplug-in/` for `*.lua` and `*.manifest` files on startup (`main.cpp:196`), on file-watcher reload (`config.cpp:120`), and after download/removal (`storeinject.cpp:675, 2368`), yet discards the file's modification time (`mtime` / Unix timestamp when the user added the file).

---

## 2. Target Files & Symbols

| File | Symbol / Selector | Change Summary |
|---|---|---|
| [`src/model/config.hpp`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/src/model/config.hpp#L63-L65) | `CConfig::luaAddedTimestamps`, `CConfig::getAppPurchaseTimestamp(AppId_t)` | Add thread-safe `MTVariable<std::unordered_map<AppId_t, uint32_t>> luaAddedTimestamps` cache and lookup method `getAppPurchaseTimestamp(AppId_t appId) const` |
| [`src/model/config.cpp`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/src/model/config.cpp#L1073-L1200) | `scanLuaPluginsAndUpdateConfig()`, `CConfig::getAppPurchaseTimestamp()`, `CConfig::resolveFileTimestamp()` | Record each `.lua` / `.manifest` file's earliest/actual `mtime` (via POSIX `stat()` on Linux `-m32`) per extracted AppID into `g_config.luaAddedTimestamps`, and support on-demand lookup fallback |
| [`src/presenter/apps.cpp`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/src/presenter/apps.cpp#L729-L760) & [`989–995`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/src/presenter/apps.cpp#L989-L995) | `Apps::checkAppOwnership()`, `Apps::unlockApp()` | Resolve `uint32_t ts = g_config.getAppPurchaseTimestamp(appId);` (preferring explicit `SubscriptionTimestamps`, falling back to `luaAddedTimestamps`) and assign `pInfo->purchaseTime = ts;` when `ts > 0` |
| [`tests/unit/test_config.cpp`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/tests/unit/test_config.cpp) & [`tests/unit/test_apps.cpp`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/tests/unit/test_apps.cpp) | Unit tests | Add GoogleTest coverage for `.lua`/`.manifest` `mtime` extraction, earliest timestamp retention across multiple depots for the same game, and `SubscriptionTimestamps` precedence |

---

## 3. Detailed Implementation Steps

### Step 3.1 — Add Timestamp Storage & Resolver in `src/model/config.hpp` & `src/model/config.cpp`
1. In [`src/model/config.hpp`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/src/model/config.hpp#L63-L65), add:
   ```cpp
   MTVariable<std::unordered_map<AppId_t, uint32_t>> luaAddedTimestamps;
   ```
   and declare:
   ```cpp
   uint32_t getAppPurchaseTimestamp(AppId_t appId) const;
   ```
2. In [`src/model/config.cpp`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/src/model/config.cpp), add a helper using `<sys/stat.h>` (`::stat(path.c_str(), &st)` — reliable on 32-bit x86 Linux without `std::chrono::clock_cast` ABI caveats) returning `static_cast<uint32_t>(st.st_mtime)` when `st.st_mtime > 0` (note: also check `depotcache/` if a manifest exists there when `stplug-in/` only has manifests or vice versa).
3. In [`scanLuaPluginsAndUpdateConfig()`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/src/model/config.cpp#L1084-L1198):
   - Build a local `std::unordered_map<AppId_t, uint32_t> scannedTimestamps;` during the existing `directory_iterator(pluginDir)` pass (zero extra directory walks).
   - For each `.lua` file (`lines 1093–1143`) and `.manifest` file (`lines 1144–1157`):
     - Compute `uint32_t fileTs = readFileMtimeSeconds(path);`.
     - If `fileTs > 0`, record `scannedTimestamps[appId]` (preferring `.lua` file `mtime` for the game, or keeping the earliest non-zero timestamp `std::min` if multiple files map to the same `appId`, so re-adding a DLC manifest later doesn't overwrite the original `.lua` date).
   - Atomically assign `g_config.luaAddedTimestamps.set(std::move(scannedTimestamps));`.
4. Implement `CConfig::getAppPurchaseTimestamp(AppId_t appId) const`:
   - Step 1 (explicit user override wins): if `subscriptionTimestamps.get()` contains `appId` (and value > 0), return it.
   - Step 2 (cached `.lua`/`.manifest` timestamp): if `luaAddedTimestamps.get()` contains `appId`, return it.
   - Step 3 (on-demand fallback): check `<pluginDir>/<appId>.lua` `mtime`; if present, cache & return it; else return `0`.
   - Note: `pInfo->purchaseTime` is a `uint32_t` field in Steam's `AppOwnershipInfo_t` (`RTime32`), representing Unix epoch seconds.

### Step 3.2 — Apply Dynamic Timestamp in `src/presenter/apps.cpp`
1. Update [`Apps::checkAppOwnership`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/src/presenter/apps.cpp#L989-L995):
   ```cpp
   const uint32_t purchaseTs = g_config.getAppPurchaseTimestamp(appId);
   if (purchaseTs > 0)
   {
       pInfo->purchaseTime = purchaseTs;
   }
   ```
2. Update [`Apps::unlockApp`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/src/presenter/apps.cpp#L729-L760):
   - Ensure unlocked apps routed directly through `unlockApp(...)` also receive `const uint32_t purchaseTs = g_config.getAppPurchaseTimestamp(appId); if (purchaseTs > 0) info->purchaseTime = purchaseTs;`.
   - Call `Licenses::queueRefresh()` or verify `scanLuaPluginsAndUpdateConfig()` already runs on file watcher / `/manual-install` / `/download` completion so Steam refreshes license purchase times when a new `.lua` is added.

### Step 3.3 — Add Unit Tests (`tests/unit/test_config.cpp` & `tests/unit/test_apps.cpp`)
- Create temporary `.lua` files (`440.lua`, `730.lua`) with distinct `utimensat` / `filesystem::last_write_time` timestamps (e.g., `2024-05-01` vs. `2025-11-10`).
- Call `scanLuaPluginsAndUpdateConfig()` and assert:
  - `g_config.getAppPurchaseTimestamp(440)` returns `440.lua`'s exact `mtime`.
  - `g_config.getAppPurchaseTimestamp(730)` returns `730.lua`'s exact `mtime` (different from `440`).
  - Adding an explicit `SubscriptionTimestamps[440] = 1700000000` overrides the `.lua` file `mtime`.
  - Calling `Apps::checkAppOwnership` populates `pInfo->purchaseTime` with the resolved timestamp.

---

## 4. Domain Invariants & Rules

- [`src/model/mtvar.hpp`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/src/model/mtvar.hpp) / C++ concurrency: Always copy or read `MTVariable` maps thread-safely (`luaAddedTimestamps.get()` returns by value / snapshot under lock) because `checkAppOwnership` runs on Steam's client thread while `onFileChange` and HTTP handlers run on worker threads.
- **32-bit `-m32` time_t safety**: Use `static_cast<uint32_t>(st.st_mtime)` cleanly (guarding `st.st_mtime > 0` before narrowing).
- [`.agents/rules/test-regression.md`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/.agents/rules/test-regression.md): Add unit regression tests in `tests/unit/test_config.cpp` and `tests/unit/test_apps.cpp`.

---

## 5. Verification Plan

1. **Build**:
   - `./install.sh build` (verify clean 32-bit `-m32` compilation and unit test binary link).
2. **Test Gates (run only when explicitly requested by user)**:
   - `./install.sh test` — verify all existing and newly added `test_config.cpp` / `test_apps.cpp` cases pass.
   - `./install.sh test-gate07` — verify Bubblewrap integration harness stays green.

---

## 6. Risks & Edge Cases

- **Zip extraction preserving vs. resetting timestamps**: When importing a `.zip` bundle in the browser (`manual-install.js`), client-side `extractZipFiles()` creates `new File([blob], fileName, ...)` which sends file bytes to `/manual-install`; writing `.lua` / `.manifest` to `stplug-in/` gives the file the current wall-clock time when the user added it (which is the exact desired behavior: "the date the user added the lua / manifest file"). Note: avoid preserving stale upstream zip archive dates from 2019 if a user drops a zip today, OR verify whether `handleManualInstall` writes newly created files with current `mtime`. Writing directly creates current `mtime` = date added!
- **Apps listed manually in `AppIds`/`AdditionalApps` without any `.lua`/`.manifest` file**: `getAppPurchaseTimestamp()` returns `0`, leaving Steam's default `pInfo->purchaseTime` untouched for genuinely owned or manually added apps.
- **`std::filesystem` vs. symlinked `stplug-in/`**: `std::filesystem::directory_iterator` does not fail on symlinks, and POSIX `::stat()` follows symlinks to get the target `.lua` file's actual `mtime` cleanly.

