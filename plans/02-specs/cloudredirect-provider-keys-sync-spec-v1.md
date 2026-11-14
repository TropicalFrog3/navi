# Feature Spec: Save and Synchronize Provider API Keys in CloudRedirect

- **Status:** Spec v1
- **Category:** Core Feature
- **Owning Layer:** C++ Presenter (`src/presenter/cloud_manager.*`, `src/presenter/storeinject.cpp`), Model (`src/model/config.cpp`), View (`src/view/storeinject_view.cpp`)
- **Source Wishlist Item:** `plans/01-backlog/dev-plan.md` — `- implement provider keys save in cloud redirect`

---

## 1. Problem Statement & Architecture Overview

Provider API keys (`RyuuKey`, `DepotBoxKey`, `HubcapKey`) are stored only locally inside `~/.config/navi/config.yaml` (`CConfig::ryuuKey`, `CConfig::depotBoxKey`, `CConfig::hubcapKey`). If a user signs in to **CloudRedirect** on a fresh device (for example a Steam Deck after configuring Navi on Desktop, or after a clean install), their game saves and `navi_cloud_catalog.json` metadata sync across devices, **but their provider API keys are missing** and must be re-pasted manually.

Inspection of the existing CloudRedirect catalog infrastructure in [`src/presenter/cloud_manager.hpp`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/src/presenter/cloud_manager.hpp#L224-L265) and [`src/presenter/cloud_manager.cpp`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/src/presenter/cloud_manager.cpp#L3266-L4097) reveals the natural synchronization vehicle:

1. `CCloudManager` already maintains a synthetic metadata app (`kCloudCatalogAppId = 999999999`, `kCloudCatalogFileName = "navi_cloud_catalog.json"`) under `<storageRoot>/<accountId>/999999999/`, published via `publish-full-manifest` (`UpdateAndPublishCloudCatalog`, `cloud_manager.cpp:3802-4097`) and pulled via `sync-remote-app` (`LoadCloudCatalog`, `cloud_manager.cpp:3266-3436`).
2. Notice TWO critical gaps today:
   - `navi_cloud_catalog.json` only serializes `"apps": [...]` and has **no** `"providerKeys"` section (`ryuuKey`, `depotBoxKey`, `hubcapKey`).
   - [`LoadCloudCatalog`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/src/presenter/cloud_manager.cpp#L3421-L3424) returns `false` early if `outSummaries.empty()` (`apps` is empty) — so a user who only configured provider keys before syncing any game saves would never have catalog metadata restored!
   - Saving provider keys via `/set-api-keys` (`storeinject.cpp:1549-1597`) or `/config/full` POST (`storeinject.cpp:2527-2562`) updates `config.yaml` locally (`g_config.updateApiAuth(...)` / `StoreInjectView::applyFullConfigJson(...)`) without publishing `999999999/navi_cloud_catalog.json` to CloudRedirect.

---

## 2. Target Files & Symbols

| File | Symbol / Selector | Change Summary |
|---|---|---|
| [`src/presenter/cloud_manager.hpp`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/src/presenter/cloud_manager.hpp#L259-L265) | `CCloudManager::PublishMetadataCatalogIfAuthenticated(...)` / `RestoreMetadataFromCloudCatalog(...)` | Expose clean helpers (built on `UpdateAndPublishCloudCatalog` / `LoadCloudCatalog`) for syncing catalog metadata (`providerKeys` + `gameTitles` + `apps`) |
| [`src/presenter/cloud_manager.cpp`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/src/presenter/cloud_manager.cpp#L3266-L4097) | `LoadCloudCatalog()`, `UpdateAndPublishCloudCatalog()`, `MergeProviderKeysFromCatalog()` | Serialize `"providerKeys": { "ryuuKey", "depotBoxKey", "hubcapKey" }` inside `navi_cloud_catalog.json`; merge non-empty remote keys into `g_config` when local keys are empty (or preserve existing remote keys on pre-sync); stop rejecting catalog loads when `apps` is empty if `providerKeys` is present |
| [`src/presenter/storeinject.cpp`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/src/presenter/storeinject.cpp#L907-L1010) & [`1549–1597`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/src/presenter/storeinject.cpp#L1549-L1597) & [`2527–2562`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/src/presenter/storeinject.cpp#L2527-L2562) | `/set-api-keys`, `/config/full` POST, `/cloud/provider/status`, `/cloud/provider/credentials` POST | After saving API keys (`RyuuKey`/`DepotBoxKey`/`HubcapKey`), dispatch background catalog publish when CloudRedirect is enabled + remote provider authenticated; after provider OAuth sign-in / credential verification confirms `authenticated == true`, pull catalog metadata (`syncRemote=true`) once to restore saved keys |
| [`tests/unit/test_cloud_manager.cpp`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/tests/unit/test_cloud_manager.cpp) | Unit tests | Add GoogleTest coverage for `providerKeys` serialization, restore when local keys are empty, non-clobbering when remote keys exist before local keys are set, and zero-app catalog restore |

---

## 3. Detailed Implementation Steps

### Step 3.1 — Extend `navi_cloud_catalog.json` Schema & Merge Logic in `src/presenter/cloud_manager.cpp`
1. **Extract & Restore `"providerKeys"` in `LoadCloudCatalog()` (`cloud_manager.cpp:3266-3436`)**:
   - After reading `existingContent` (`lines 3329–3371`), extract the `"providerKeys"` JSON object slice (if present) and parse `ryuuKey`, `depotBoxKey`, `hubcapKey` using existing `ExtractCatalogJsonString(...)` (`cloud_manager.cpp:3094`).
   - Add a dedicated internal helper `RestoreMetadataFromCatalogJson(const std::string& catalogJson)`:
     - Only apply a remote key if the corresponding local key (`g_config.ryuuKey.get()`, `g_config.depotBoxKey.get()`, `g_config.hubcapKey.get()`) is **empty** and the remote value is non-empty (so explicit local edits or clears made on the current machine aren't silently overwritten by an older pull unless the user hasn't set them yet; when the user intentionally deletes a key via `/config/full` / `/set-api-keys`, the subsequent push clears it remotely).
     - When any empty local key is filled from remote, call `g_config.updateApiAuth(mergedRyuu, mergedDepotBox, mergedHubcap)` and update `g_config.ryuuKey`, `g_config.depotBoxKey`, `g_config.hubcapKey` in memory!
     - Never log raw key values (`LOG_INFO` should only say e.g. `"Restored %zu provider key(s) from CloudRedirect catalog"` — honoring the existing privacy contract in `config-sections.js:225`: `"Keys are never shown in logs"`).
   - Remove/relax the `if (outSummaries.empty()) return false;` check (`line 3421`) so callers syncing metadata-only catalogs (when `"providerKeys"` or `"gameTitles"` were restored cleanly even with `apps: []`) still treat the catalog load as valid metadata!
2. **Prevent Remote Key Clobbering + Serialize `"providerKeys"` in `UpdateAndPublishCloudCatalog()` (`cloud_manager.cpp:3802-4097`)**:
   - Before building `js` (`line 4002`), read local keys (`g_config.ryuuKey.get()`, `g_config.depotBoxKey.get()`, `g_config.hubcapKey.get()`).
   - Write `"providerKeys"` cleanly using `EscapeCatalogJsonString(...)`:
     ```json
     "providerKeys": {
       "ryuuKey": "...",
       "depotBoxKey": "...",
       "hubcapKey": "..."
     },
     ```
   - Ensure `navi_cloud_catalog.json` continues to be written inside `<storageRoot>/<acct>/999999999/` with `root_token.cloudredirect` and `file_tokens.cloudredirect`, and published via `publish-full-manifest` (`line 4074`) when `isRemoteProvider` is true (always guard with `if (!g_config.cloudSavesEnabled.get()) return false;` when called from config hooks).

### Step 3.2 — Wire Push & Pull Triggers cleanly in `src/presenter/storeinject.cpp` and `CCloudManager`
1. **On Save (Push)**:
   - In `/set-api-keys` (`storeinject.cpp:1583`) and `/config/full` POST (`storeinject.cpp:2537` — specifically when `ryuuKey`, `depotBoxKey`, or `hubcapKey` changed compared to pre-POST values):
     - If `g_config.cloudSavesEnabled.get()`, enqueue or detach a lightweight worker/helper call to `CCloudManager::Instance().UpdateAndPublishCloudCatalog(g_config.cloudSavesProvider.get(), acctId)` (remember: never hold the HTTP socket open waiting up to 30s on `publish-full-manifest`! Send the HTTP 200 response first, then publish asynchronously with a guard against concurrent publishes).
2. **On Provider Login / Startup (Pull)**:
   - On startup (`main.cpp` after `CCloudRedirectBridge` init, or inside a non-blocking startup sync thread when `cloudSavesEnabled` is true and provider != `local-only`) and right after OAuth / S3/R2 credential sign-in succeeds (`/cloud/provider/credentials` POST or first authenticated `/cloud/provider/status` transition), call `LoadCloudCatalog(provider, acctId, /*syncRemote=*/true, ...)` so restored provider keys appear immediately without requiring manual re-entry!
3. **Reload UI After Restore**:
   - Consider having [`config-controller.js`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/res/inject-scripts/shell/import-lua/config-controller.js) refresh `#sls-cfg-RyuuKey`, `#sls-cfg-DepotBoxKey`, `#sls-cfg-HubcapKey` via `GET /api-keys` if provider auth state transitions from unauthenticated to authenticated cleanly while Navi Config is open.

### Step 3.3 — Add Unit Tests (`tests/unit/test_cloud_manager.cpp`)
- Stub `cloud_redirect_cli` to capture the published `999999999/navi_cloud_catalog.json` content and serve it back on `sync-remote-app`.
- Verify:
  1. `UpdateAndPublishCloudCatalog()` writes escaped `ryuuKey`, `depotBoxKey`, `hubcapKey` into `999999999/navi_cloud_catalog.json` under `"providerKeys"`.
  2. `LoadCloudCatalog(..., syncRemote=true)` restores empty local keys into `g_config` and persists them to `config.yaml` via `updateApiAuth(...)`, even when `apps` array is empty.
  3. Existing non-empty local keys are not overwritten during a passive sync.
  4. Special characters (`"`, `\`) inside keys survive JSON round-trip cleanly.

---

## 4. Domain Invariants & Rules

- [`.agents/rules/cpp_hooking_patterns.md`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/.agents/rules/cpp_hooking_patterns.md#L69-L80): Never log provider API key values in plaintext anywhere in `LOG_INFO`/`LOG_WARN`/`LOG_DEBUG`; honor `cloud_redirect_cli` exit-code semantics (treat `0`/`1` carefully via `ExtractJsonSubstring`).
- **Config File Privacy & Security**: Do NOT store CloudRedirect provider OAuth tokens or S3 secrets inside `navi_cloud_catalog.json` (those stay in `0600` `~/.config/CloudRedirect/`); only `RyuuKey`, `DepotBoxKey`, and `HubcapKey` are included under `"providerKeys"`, stored in the user's own authenticated private CloudRedirect storage bucket/drive.
- **No Blocking on Localhost HTTP Handlers**: Keep `storeinject.cpp` HTTP responses (`/set-api-keys`, `/config/full`) instantaneous; run `publish-full-manifest` in a serialized background thread or guarded worker so CEF UI save buttons never stall on network timeouts.
- [`.agents/rules/test-regression.md`](file:///home/nicolas/Documents/my-github/SLSsteam_n_ACCELA/.agents/rules/test-regression.md): Add unit tests in `tests/unit/test_cloud_manager.cpp`.

---

## 5. Verification Plan

1. **Build**:
   - `./install.sh build`
2. **Test Gates (run only when explicitly requested by user)**:
   - `./install.sh test` — verify all `test_cloud_manager.cpp` unit tests pass.
   - `./install.sh test-gate07` — verify Bubblewrap integration harness stays green.
   - `./install.sh test-ui` — verify `test_uj05_config_editor_validation.py` still passes (especially `Provider keys` save + CloudRedirect sign-in flow).

---

## 6. Risks & Edge Cases

- **Concurrent `UpdateAndPublishCloudCatalog` calls**: Add a mutex or single-flight flag inside `CCloudManager` protecting `999999999` staging + `publish-full-manifest` CLI execution so rapid `/set-api-keys` + `/config/full` calls cannot race on `navi_cloud_catalog.json` or `blobs/`.
- **Offline / `local-only` provider**: When provider is `"local-only"`, still update `999999999/navi_cloud_catalog.json` on disk (without running `publish-full-manifest`), so if the user later migrates from `local-only` to `gdrive` / `r2`, their `providerKeys` migrate with their catalog.
- **AccountId `0` fallback when Steam hasn't logged in yet**: When updating keys before `g_currentSteamId` is set, check `ResolveCandidateAccountIds(0)` safely and skip remote catalog publish with a non-fatal debug log rather than crashing or writing under `0/`.

