# Feature Spec: Default Spoofed Game Titles (`GameTitles`) to Sync with CloudRedirect When Enabled

- **Status:** Spec v1
- **Category:** Core Feature
- **Owning Layer:** C++ Model (`src/model/config.cpp`), Presenter (`src/presenter/cloudredirect.cpp`, `src/presenter/cloud_manager.cpp`, `src/presenter/storeinject.cpp`)
- **Source Wishlist Item:** `plans/01-backlog/dev-plan.md` — `- default: make all spoofed game title saved with cloud redirect if cloud redirect is enable`

---

## 1. Problem Statement & Root Cause Analysis

When **CloudRedirect** (`CloudSaves.Enabled`) is turned on, Navi automatically computes the default set of AppIDs whose saves are redirected by calling [`CConfig::GetEffectiveCloudRedirectedApps()`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/src/model/config.cpp#L575-L605) and checks per-app eligibility via [`CConfig::IsAppCloudRedirected(AppId_t)`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/src/model/config.cpp#L607-L643).

Today, those two functions automatically include:
- `cloudSavesManagedApps` (`CloudSaves.ManagedApps`)
- `addedAppIds` (`AdditionalApps` / Lua-unlocked apps)
- `fakeAppIds` (both `realId` and `fakeId` for Spacewar/AppID spoofing)
- `appIds` (when `useWhiteList` is `true`)

**Root Gaps:**
1. **`GameTitles` (spoofed game titles) are completely omitted** from both `GetEffectiveCloudRedirectedApps()` (`config.cpp:575-605`) and `IsAppCloudRedirected()` (`config.cpp:607-643`). Consequently:
   - `CCloudRedirectBridge::SyncSlsSteamConfig(g_config.GetEffectiveCloudRedirectedApps())` (`cloudredirect.cpp:352`, called from `main.cpp:208`, `config.cpp:124, 850`, `storeinject.cpp:1275`, `storeinject_view.cpp:1039`) does **not** add `GameTitles` AppIDs to SLSsteam/CloudRedirect's default managed app list unless the user manually toggles each one!
   - `CCloudManager::UpdateAndPublishCloudCatalog()` (`cloud_manager.cpp:3930`: `if (!g_config.IsAppCloudRedirected(appId)) continue;`) and `/cloud/apps` (`storeinject.cpp:1052`: `a.redirected = g_config.IsAppCloudRedirected(a.appId);`) treat `GameTitles` apps without Lua files as non-redirected by default.
2. **Cloud-catalog sync of `GameTitles` themselves + local-only `GameTitles` visibility**: `UpdateAndPublishCloudCatalog()` (`cloud_manager.cpp:3802-4097`) only discovers apps via `remoteConfirmedApps`, whereas `local-only` or newly added `GameTitles` entries (and the spoofed title mapping itself) are not persisted inside `999999999/navi_cloud_catalog.json` for cross-device sync or listed in `ListCloudApps` when cloud save files exist.

---

## 2. Target Files & Symbols

| File | Symbol / Selector | Change Summary |
|---|---|---|
| [`src/model/config.cpp`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/src/model/config.cpp#L575-L643) | `CConfig::GetEffectiveCloudRedirectedApps()`, `CConfig::IsAppCloudRedirected(AppId_t)` | Include every `appId` from `gameTitles.get()` (with non-empty title) by default whenever `cloudSavesEnabled` is `true` and `appId` is not in `cloudSavesDisabledApps` |
| [`src/presenter/cloud_manager.cpp`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/src/presenter/cloud_manager.cpp#L2540-L2780) & [`3266–4097`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/src/presenter/cloud_manager.cpp#L3266-L4097) | `ListCloudApps()`, `LoadCloudCatalog()`, `UpdateAndPublishCloudCatalog()` | Include `gameTitles` AppIDs in local save discovery candidates (`ListCloudApps`), serialize `"gameTitles"` map in `999999999/navi_cloud_catalog.json`, and restore missing remote `GameTitles` entries on `LoadCloudCatalog` when `cloudSavesEnabled` is `true` |
| [`src/view/storeinject_view.cpp`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/src/view/storeinject_view.cpp#L1030-L1045) & [`src/presenter/storeinject.cpp`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/src/presenter/storeinject.cpp#L1011-L1062) | `/config/full` POST, `/cloud/apps`, `/cloud/apps/:id/toggle` | Verify `/config/full` continues triggering `SyncSlsSteamConfig(g_config.GetEffectiveCloudRedirectedApps())` (`storeinject_view.cpp:1039`) and opt-out (`cloudSavesDisabledApps`) still works cleanly per game |
| [`tests/unit/test_config.cpp`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/tests/unit/test_config.cpp) & [`tests/unit/test_cloud_manager.cpp`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/tests/unit/test_cloud_manager.cpp) | Unit tests | Add GoogleTest cases for `GameTitles` default inclusion, `DisabledApps` opt-out override, `cloudSavesEnabled == false` suppression, and catalog persistence |

---

## 3. Detailed Implementation Steps

### Step 3.1 — Include `gameTitles` in `GetEffectiveCloudRedirectedApps()` & `IsAppCloudRedirected()` (`src/model/config.cpp`)
1. In [`CConfig::GetEffectiveCloudRedirectedApps()`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/src/model/config.cpp#L592-L605):
   ```cpp
   for (const auto& [appId, title] : self->gameTitles.get())
   {
       if (!title.empty())
       {
           addIfEnabled(appId);
       }
   }
   ```
   (Notice `addIfEnabled` on `line 585` already enforces `id != 0 && !disabled.contains(id)` so users can still explicitly turn off redirection for a spoofed-title game via `/cloud/apps/:id/toggle`, which adds `appId` to `cloudSavesDisabledApps`.)
2. In [`CConfig::IsAppCloudRedirected(AppId_t appId)`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/src/model/config.cpp#L620-L643):
   ```cpp
   const auto titles = self->gameTitles.get();
   if (auto it = titles.find(appId); it != titles.end() && !it->second.empty())
   {
       return true;
   }
   ```
   placed after `cloudSavesEnabled` and `cloudSavesDisabledApps` checks (`lines 610–618`), matching the exact semantics of `fakeAppIds` and `addedAppIds`.

### Step 3.2 — Include Spoofed `GameTitles` in CloudRedirect Discovery & Catalog (`src/presenter/cloud_manager.cpp`)
1. In [`CCloudManager::ListCloudApps()`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/src/presenter/cloud_manager.cpp#L2540-L2615):
   - Seed `addCandidate(appId)` for every `[appId, title]` in `g_config.gameTitles.get()` when `g_config.cloudSavesEnabled.get()` and `!title.empty()`, so any local save folders (`userdata/<acct>/<appId>/`, `cloud_redirect/<acct>/<appId>/`, `compatdata/<appId>/`) for a spoofed-title game appear in **Your Steam Cloud** automatically.
2. In [`UpdateAndPublishCloudCatalog()`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/src/presenter/cloud_manager.cpp#L3919-L4045) & [`LoadCloudCatalog()`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/src/presenter/cloud_manager.cpp#L3373-L3436):
   - Persist non-empty `g_config.gameTitles.get()` entries into `"gameTitles": { "<appId>": "<escapedTitle>", ... }` in `999999999/navi_cloud_catalog.json` when `g_config.cloudSavesEnabled.get()` is `true`.
   - On `LoadCloudCatalog(..., syncRemote=true)`, restore any missing `gameTitles` entries from the catalog into `g_config.gameTitles` (updating `config.yaml` safely without clobbering existing local overrides) and re-run `SyncSlsSteamConfig(g_config.GetEffectiveCloudRedirectedApps())`.
   - Ensure `storeinject.cpp:2537` (`/config/full` POST) triggers catalog publish when `gameTitles` changes while CloudRedirect is enabled and authenticated with a remote provider (sharing the same non-blocking catalog publish coordinator as `cloudredirect-provider-keys-sync-spec-v1.md`).

### Step 3.3 — Add Unit Tests (`tests/unit/test_config.cpp` & `tests/unit/test_cloud_manager.cpp`)
- **`tests/unit/test_config.cpp`**:
  1. Set `g_config.cloudSavesEnabled = true`, add `{480, "My Spoofed Spacewar"}` to `g_config.gameTitles`, assert `g_config.IsAppCloudRedirected(480) == true` and `g_config.GetEffectiveCloudRedirectedApps().contains(480)`.
  2. Insert `480` into `g_config.cloudSavesDisabledApps`, assert `g_config.IsAppCloudRedirected(480) == false` and `480` is excluded from `GetEffectiveCloudRedirectedApps()`.
  3. Set `g_config.cloudSavesEnabled = false`, clear `cloudSavesDisabledApps`, assert `g_config.IsAppCloudRedirected(480) == false` and `GetEffectiveCloudRedirectedApps().empty()`.
  4. Verify an entry with empty title (`{480, ""}`) does NOT spuriously mark `480` as redirected.
- **`tests/unit/test_cloud_manager.cpp`**:
  1. Verify `SyncSlsSteamConfig(g_config.GetEffectiveCloudRedirectedApps())` writes spoofed `GameTitles` AppID `480` into `SLSsteam/config.yaml`'s `AdditionalApps`.
  2. Verify `navi_cloud_catalog.json` serializes & restores `"gameTitles"` when `cloudSavesEnabled` is `true`.

---

## 4. Domain Invariants & Rules

- [`src/model/config.cpp`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/src/model/config.cpp#L575-L643): `GetEffectiveCloudRedirectedApps()` and `IsAppCloudRedirected(AppId_t)` MUST agree 100% on every `AppId_t` (`GetEffectiveCloudRedirectedApps().contains(id) == IsAppCloudRedirected(id)` for every `id != 0`).
- **Opt-out preservation**: Users can still disable CloudRedirect for a specific spoofed-title game (`cloudSavesDisabledApps` via `/cloud/apps/:id/toggle` in [`storeinject.cpp`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/src/presenter/storeinject.cpp#L1260-L1275)); never bypass `cloudSavesDisabledApps`.
- [`src/presenter/cloudredirect.cpp`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/src/presenter/cloudredirect.cpp#L352): Updating `SLSsteam/config.yaml` applies cleanly when `SyncSlsSteamConfig()` runs; keep `cloud_redirect.so` runtime injection rules (`.agents/rules/cpp_hooking_patterns.md` §7) untouched.
- [`.agents/rules/test-regression.md`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/.agents/rules/test-regression.md): Author unit tests alongside model/presenter changes.

---

## 5. Verification Plan

1. **Build**:
   - `./install.sh build`
2. **Test Gates (run only when explicitly requested by user)**:
   - `./install.sh test` — verify `test_config.cpp`, `test_cloudredirect_bridge.cpp`, and `test_cloud_manager.cpp` pass.
   - `./install.sh test-gate07` — verify Bubblewrap CloudRedirect integration suite passes cleanly.

---

## 6. Risks & Edge Cases

- **AppID `0` or whitespace-only titles**: Ignore `appId == 0` (already filtered by `addIfEnabled`) and ignore empty/whitespace-only titles in `gameTitles` so malformed map entries don't pollute `AdditionalApps` in `SLSsteam/config.yaml`.
- **Removing a `GameTitles` entry**: Note `CCloudRedirectBridge::SyncSlsSteamConfig` (`cloudredirect.cpp:367-395`) merges existing `SLSsteam/config.yaml` `AdditionalApps` entries; removing a title drops it from Navi's `GetEffectiveCloudRedirectedApps()`, matching the existing behavior of `AdditionalApps` / `FakeAppIds`.
- **Shared coordinate with `cloudredirect-provider-keys-sync-spec-v1.md`**: Both specs extend `999999999/navi_cloud_catalog.json` (`"providerKeys"` and `"gameTitles"`). Coordinate JSON parsing/serialization in `cloud_manager.cpp` so both top-level keys coexist cleanly without breaking the existing `SplitCatalogJsonObjectArray(existingContent, "apps")` parser (`cloud_manager.cpp:3138`).

