# CloudRedirect v2.6.5 — UX & Backend Parity Specification (v2)

- **Status:** done
- **Outcome:** shipped — CloudRedirect v2.6.5 full UX parity verified across Gates 1–5 — 2026-11-07
- **Verification Report:** [`tests/reports/2026-11-07-gate05-cloudredirect-live-cdp.md`](../../tests/reports/2026-11-07-gate05-cloudredirect-live-cdp.md)
- **Date:** 2026-11-07
- **Supersedes:** [`cloudredirect-ux-parity-spec-v1.md`](cloudredirect-ux-parity-spec-v1.md)
- **Owner:** Navi CEF UI (`res/inject-scripts/shell/import-lua-script.js`, `res/inject-scripts/common/cloud-manager.js` / `res/inject-scripts/cloud/cloud_manager.js`) & Native Engine (`src/{model,presenter,view}`)
- **Scope:** Full feature and UX parity between Navi's `Cloud saves` settings tab and the prebuilt `CloudRedirect v2.6.5` binary engine (`res/cloud_redirect.so` / `res/cloud_redirect_cli`), covering atomic `~/.config/CloudRedirect/config.json` synchronization, launch notifications, experimental stats/achievements/playtime sync, Cloudflare R2 & Amazon S3 credential management, cross-provider save migration, orphaned save & blob scanning/adoption/GC, a global backups browser, and **Gate 5: Real-Time Live Steam Verification** (`./install.sh install --yes` + `/cef-cdp-injection` + `/jev`).
- **Related Specs:** [`navi-config-redesign-spec-v2.md`](../04-done/navi-config-redesign-spec-v2.md), [`cloudredirect-ux-parity-spec-v1.md`](cloudredirect-ux-parity-spec-v1.md)

---

## 1. Binary Contracts Verified (`res/cloud_redirect.so` v2.6.5+870afdb-dirty)

### 1.1 Engine Configuration (`~/.config/CloudRedirect/config.json`)
`cloud_redirect.so` (`EnsureInitialized` / `DoInit`) reads `~/.config/CloudRedirect/config.json` (or Flatpak equivalent under `~/.var/app/com.valvesoftware.Steam/.config/CloudRedirect/config.json`) with the following exact JSON keys:

| JSON Key | Type | Engine Default | Behavior in `res/cloud_redirect.so` |
|---|---|---|---|
| `provider` | `string` | `"local"` | Selects storage backend via `CreateCloudProvider`: `"local"` (offline local storage without warnings), `"gdrive"`, `"onedrive"`, `"r2"`, `"s3"`, or `"folder"`. Note: Navi's `"local-only"` string in `config.yaml` **must** be mapped to `"local"` in `config.json` to avoid `[Linux] WARNING: Unknown cloud provider 'local-only', falling back to local-only`. |
| `notifications_enabled` | `bool` | `true` | Controls desktop notification toast when `cloud_redirect.so` finishes `DoInit` (`CloudRedirect initialized successfully (all hooks active)` or init failure). |
| `stats_sync_enabled` | `bool` | `false` | Master gate for `StatsStore`, `StatsHandlers`, and background stats poller initialization (`[Stats] Stats sync disabled -- skipping StatsStore/StatsHandlers/poller init`). |
| `sync_achievements` | `bool` | `true` | Sub-gate read when `stats_sync_enabled` is `true` (`[Stats] Sync gates: achievements=%d, playtime=%d`). |
| `sync_playtime` | `bool` | `true` | Sub-gate read when `stats_sync_enabled` is `true` (`[Stats] Sync gates: achievements=%d, playtime=%d`). |

### 1.2 R2 & S3 Provider Credential Files (`0600` permissions)
`ResolveProviderTokenPath` and `S3Provider::Init` / `R2Provider::ParseExtraCredentials` resolve credentials in `~/.config/CloudRedirect/`:

- **Cloudflare R2 (`~/.config/CloudRedirect/r2_credentials.json`)**:
  - `account_id` (`string`, required unless custom `endpoint` is set; builds `https://<account_id>.r2.cloudflarestorage.com`)
  - `access_key_id` (`string`, required)
  - `secret_access_key` (`string`, required)
  - `bucket` (`string`, required)
  - `region` (`string`, optional, defaults to `"auto"`)
  - `endpoint` (`string`, optional override URL)
  - `key_prefix` (`string`, optional object key prefix)
- **Amazon S3 / S3-Compatible (`~/.config/CloudRedirect/s3_credentials.json`)**:
  - `access_key_id` (`string`, required)
  - `secret_access_key` (`string`, required)
  - `bucket` (`string`, required)
  - `region` (`string`, optional, defaults to `"us-east-1"`)
  - `endpoint` (`string`, optional custom S3 endpoint URL)
  - `key_prefix` (`string`, optional object key prefix)

### 1.3 CLI Subcommands (`res/cloud_redirect_cli`)
- `cloud_redirect_cli auth-status <provider>` (`gdrive` | `onedrive` | `r2` | `s3`): Validates OAuth token or R2/S3 credentials + bucket connectivity; outputs JSON `{"authenticated":true|false,...}`. (Per `.agents/rules/cpp_hooking_patterns.md` Rule 8, exit code `0` = authenticated, exit code `1` = unauthenticated/informational).
- `cloud_redirect_cli scan-all <provider>` (`local` | `gdrive` | `onedrive` | `r2` | `s3`): Single-pass inventory scan across all accounts on `<provider>`, emitting newline-delimited status JSON objects followed by `{"type":"complete","apps":[{"account_id":"...","app_id":...,"file_count":...,"total_size":...}]}` or `{"type":"error","error":"..."}`.
- `cloud_redirect_cli migrate <src_provider> <dst_provider>`: Copies all cloud save blobs and metadata from `<src_provider>` to `<dst_provider>` (`local`, `gdrive`, `onedrive`, `r2`, `s3`), emitting streaming progress lines and final `{"type":"complete","migrated":<n>,"skipped":<n>,"failed":<n>,"total":<n>,"total_bytes":<bytes>}` or `{"type":"error","error":"..."}`.
- `cloud_redirect_cli gc-blobs <provider> <account_id> <app_id> <cloud_root>`: Deletes unreferenced SHA-256 blobs from `<provider>` for `<app_id>`, outputting `{"success":true,"blobs_deleted":<n>,"total":<m>}`.

---

## 2. Decisions & Feature Specification (7 Parity Items)

### Item 1: Syncing `~/.config/CloudRedirect/config.json`
- **Problem:** `CCloudRedirectBridge::EnsureLocalOnlyConfig()` only creates `~/.config/CloudRedirect/config.json` once if missing (and writes `"provider": "local-only"`), never updating it when the user changes provider, notification, or stats sync settings in Navi.
- **Specification:**
  1. Add `struct EngineConfig_t` and `bool CCloudRedirectBridge::SyncEngineConfig(const EngineConfig_t& cfg)` in [`src/presenter/cloudredirect.hpp`](../../src/presenter/cloudredirect.hpp) / [`src/presenter/cloudredirect.cpp`](../../src/presenter/cloudredirect.cpp):
     - Maps `provider` (`"local-only"` or `""` $\rightarrow$ `"local"`; `"gdrive"`, `"onedrive"`, `"r2"`, `"s3"` preserved).
     - Preserves any existing unrelated keys in `~/.config/CloudRedirect/config.json` (such as `token_path`, `token_paths`, `sync_path`, `upload_inflight_mb`) while atomically writing:
       ```json
       {
         "provider": "local",
         "notifications_enabled": true,
         "stats_sync_enabled": false,
         "sync_achievements": true,
         "sync_playtime": true
       }
       ```
       via `AtomicFile::write`.
  2. Update `CCloudRedirectBridge::EnsureLocalOnlyConfig()` to write `"provider": "local"` instead of `"local-only"`.
  3. Trigger `CCloudRedirectBridge::Instance().SyncEngineConfig(...)` automatically:
     - At the end of `CConfig::loadSettings()` in [`src/model/config.cpp`](../../src/model/config.cpp).
     - In `StoreInjectView::applyFullConfigJson()` in [`src/view/storeinject_view.cpp`](../../src/view/storeinject_view.cpp).
     - Whenever `/cloud/toggle`, `/cloud/provider/credentials`, or `/cloud/migrate` updates CloudSaves state in [`src/presenter/storeinject.cpp`](../../src/presenter/storeinject.cpp).

### Item 2: Launch Notifications (`notifications_enabled`)
- **Config Schema (`config.yaml` & `CConfig`)**:
  - Add `MTVariable<bool> cloudNotificationsEnabled{true};` to [`CConfig`](../../src/model/config.hpp) under `CloudSaves`.
  - Parse `CloudSaves.NotificationsEnabled` (default `true`) in [`CConfig::loadSettings`](../../src/model/config.cpp), [`res/config.yaml`](../../res/config.yaml), [`src/core/config_default.hpp`](../../src/core/config_default.hpp), and serialize/deserialize `CloudSaves.NotificationsEnabled` in [`StoreInjectView::renderFullConfigJson`](../../src/view/storeinject_view.cpp) and [`StoreInjectView::applyFullConfigJson`](../../src/view/storeinject_view.cpp).
- **UI (`res/inject-scripts/shell/import-lua-script.js`)**:
  - In the `Status` card of `Cloud saves`, add a setting row:
    - **Title:** `Launch notifications`
    - **Description:** `Show a desktop notification when CloudRedirect loads into Steam.`
    - **Control:** Toggle `sls-cfg-CloudSaves-NotificationsEnabled` bound to `cfg.CloudSaves.NotificationsEnabled` (default `true`) and included in dirty-state tracking and `gatherPayload()`.

### Item 3: Stats Sync (`stats_sync_enabled`, `sync_achievements`, `sync_playtime`)
- **Config Schema (`config.yaml` & `CConfig`)**:
  - Add to [`CConfig`](../../src/model/config.hpp):
    - `MTVariable<bool> cloudStatsSyncEnabled{false};`
    - `MTVariable<bool> cloudSyncAchievements{true};`
    - `MTVariable<bool> cloudSyncPlaytime{true};`
  - Persist under `CloudSaves.StatsSync` in `config.yaml`:
    ```yaml
    CloudSaves:
      NotificationsEnabled: true
      StatsSync:
        Enabled: false
        SyncAchievements: true
        SyncPlaytime: true
    ```
  - Serialize/deserialize `CloudSaves.StatsSync.{Enabled, SyncAchievements, SyncPlaytime}` in `StoreInjectView::renderFullConfigJson` and `StoreInjectView::applyFullConfigJson`, and sync to `~/.config/CloudRedirect/config.json` (`stats_sync_enabled`, `sync_achievements`, `sync_playtime`).
- **UI (`res/inject-scripts/shell/import-lua-script.js`)**:
  - Add a `Stats sync` card in `Cloud saves` (placed right after `Wine / Emulator save sync`):
    - **Amber Experimental Callout Banner:** `"Experimental — synchronizes unlocked achievements and playtime across devices via your cloud provider. Changes require a Steam restart."`
    - **Row 1:** `Enable Stats Sync` (`sls-cfg-StatsSync-Enabled`, default `false`) — *"Intercepts and syncs Steam UserGameStats and playtime records."*
    - **Row 2:** `Sync Achievements` (`sls-cfg-StatsSync-Achievements`, default `true`, visually dimmed when `Enable Stats Sync` is off) — *"Merge unlocked achievements across devices."*
    - **Row 3:** `Sync Playtime` (`sls-cfg-StatsSync-Playtime`, default `true`, visually dimmed when `Enable Stats Sync` is off) — *"Sync total playtime counters across devices."*

### Item 4: R2 & S3 Credentials UI + Backend
- **Backend (`CCloudManager`, `CloudView`, `storeinject.cpp`)**:
  - Add `struct ProviderCredentials_t` in [`src/presenter/cloud_manager.hpp`](../../src/presenter/cloud_manager.hpp):
    ```cpp
    struct ProviderCredentials_t
    {
        std::string provider;        // "r2" or "s3"
        bool configured{false};
        std::string accountId;       // R2 only
        std::string accessKeyId;
        std::string secretAccessKey; // Masked ("********") on GET; written on POST if non-empty and not masked
        bool hasSecretKey{false};
        std::string bucket;
        std::string region;
        std::string endpoint;
        std::string keyPrefix;
    };
    ```
  - Add `CCloudManager::GetProviderCredentials(const std::string& provider)` and `CCloudManager::SaveProviderCredentials(const ProviderCredentials_t& creds, std::string& outError)`:
    - Reads/writes `~/.config/CloudRedirect/r2_credentials.json` or `~/.config/CloudRedirect/s3_credentials.json` via `AtomicFile::write` and enforces `chmod(path, 0600)`.
    - If `secretAccessKey` in `SaveProviderCredentials` is empty or `"********"`, preserves the existing `secret_access_key` on disk if present.
    - Validates required fields (`bucket`, `access_key_id`, `secret_access_key`, plus `account_id` or `endpoint` for `r2`).
  - Extend `COAuthEngine::SignOut` / `CCloudManager::DeleteProviderCredentials(provider)` so clicking `Disconnect` on `r2` or `s3` removes `{r2,s3}_credentials.json`.
  - Add REST endpoints in [`src/presenter/storeinject.cpp`](../../src/presenter/storeinject.cpp):
    - `GET /cloud/provider/credentials?provider=r2|s3` $\rightarrow$ returns masked credentials JSON (never leaks raw `secret_access_key` over HTTP).
    - `POST /cloud/provider/credentials` (requires `X-Navi-Auth`) $\rightarrow$ writes `0600` `{r2,s3}_credentials.json`, syncs engine config if active provider matches, runs `CCloudManager::Instance().CheckAuthStatus(provider)` to verify bucket connectivity, and returns `{"success":true,"authenticated":...,"message":"..."}`.
- **UI (`res/inject-scripts/shell/import-lua-script.js`)**:
  - Inside the `Where saves are stored` card (`#sls-cfg-cs-auth-block`), when `r2` or `s3` is selected in `#sls-cfg-cs-provider`:
    - Hide OAuth browser login copy and render the inline S3/R2 credential form (`#sls-cfg-cs-creds-form`):
      - `Account ID` (`#sls-cfg-cs-cred-account-id`, shown when `provider === 'r2'`, placeholder `Cloudflare Account ID`)
      - `Bucket` (`#sls-cfg-cs-cred-bucket`, required)
      - `Access Key ID` (`#sls-cfg-cs-cred-access-key`, required)
      - `Secret Access Key` (`#sls-cfg-cs-cred-secret-key`, password input with `Show`/`Hide` button, placeholder `Leave blank to keep existing secret` when `hasSecretKey` is true)
      - `Region` (`#sls-cfg-cs-cred-region`, placeholder `auto` for R2, `us-east-1` for S3)
      - `Custom Endpoint` (`#sls-cfg-cs-cred-endpoint`, optional URL override)
      - `Key Prefix` (`#sls-cfg-cs-cred-prefix`, optional folder prefix)
      - Action button `Save & Test Connection` (`#sls-cfg-cs-cred-save-btn`) which POSTs to `/cloud/provider/credentials` and refreshes provider status badge (`Connected ✓` / `Not Connected`).

### Item 5: Migration (`From` $\rightarrow$ `To` Provider)
- **Backend (`CCloudManager`, `CloudView`, `storeinject.cpp`)**:
  - Add `struct MigrationScanResult_t` and `struct MigrationResult_t` in [`src/presenter/cloud_manager.hpp`](../../src/presenter/cloud_manager.hpp):
    - `CCloudManager::ScanProviderForMigration(const std::string& provider)`: Invokes `cloud_redirect_cli scan-all <normalized_provider>` (mapping `"local-only"` $\rightarrow$ `"local"`), parses the final `"type":"complete"` JSON line, resolves game titles via `ResolveAppTitleFromManifests`, and returns app count, total file count, total bytes, and per-app summary list.
    - `CCloudManager::MigrateProviderData(const std::string& srcProvider, const std::string& dstProvider)`: Validates `src != dst`, invokes `cloud_redirect_cli migrate <norm_src> <norm_dst>` with a 120-second timeout, parses the final `"type":"complete"` or `"type":"error"` JSON line, and on success updates `g_config.cloudSavesProvider` and `~/.config/CloudRedirect/config.json` to `dstProvider`.
  - Add REST endpoints in [`src/presenter/storeinject.cpp`](../../src/presenter/storeinject.cpp):
    - `GET /cloud/migrate/scan?provider=<src>` $\rightarrow$ returns `{"success":true,"provider":"...","appCount":...,"fileCount":...,"totalSizeBytes":...,"apps":[...]}`.
    - `POST /cloud/migrate` (requires `X-Navi-Auth`, body `{"sourceProvider":"...","targetProvider":"...","switchActiveProvider":true}`) $\rightarrow$ runs migration and returns `{"success":true,"migrated":...,"skipped":...,"failed":...,"total":...,"totalBytes":...}`.
- **UI (`res/inject-scripts/shell/import-lua-script.js`)**:
  - Add a `Migrate cloud saves` card in `Cloud saves`:
    - `From (source)` dropdown (`#sls-cfg-cs-migrate-src`: `local-only`, `gdrive`, `onedrive`, `r2`, `s3`) and `To (destination)` dropdown (`#sls-cfg-cs-migrate-dst`).
    - `Scan Source` button (`#sls-cfg-cs-migrate-scan-btn`) calling `GET /cloud/migrate/scan?provider=<src>` and displaying a pre-flight summary (`X games · Y files · Z MB ready to copy`).
    - `Start Migration` button (`#sls-cfg-cs-migrate-start-btn`) with confirmation modal, progress state, and completion summary banner (`Migrated N files (M skipped, 0 failed)`).

### Item 6: Scan Orphans (Unredirected Save Directories & Unreferenced Cloud Blobs)
- **Backend (`CCloudManager`, `CloudView`, `storeinject.cpp`)**:
  - Add `struct OrphanAppEntry_t` in [`src/presenter/cloud_manager.hpp`](../../src/presenter/cloud_manager.hpp):
    ```cpp
    struct OrphanAppEntry_t
    {
        AppId_t appId{0};
        std::string gameTitle;
        uint32_t fileCount{0};
        uint64_t totalSizeBytes{0};
        bool inLocalStorage{false};
        bool inRemoteCloud{false};
        bool inGoldbergPrefix{false};
    };
    ```
  - Add `CCloudManager::ScanOrphanedSaves(const std::string& provider, uint32_t accountId = 0)`:
    - Scans all discovered save locations (`ListCloudApps(provider, accountId)`, `<storageDir>/<accountId>/*`, and Goldberg/GSE prefix directories) and filters for `appId != 0 && appId != kCloudCatalogAppId && !g_config.IsAppCloudRedirected(appId)` where `fileCount > 0`.
  - Add `CCloudManager::GarbageCollectAppBlobs(AppId_t appId, const std::string& provider, uint32_t accountId = 0)`:
    - Calls `cloud_redirect_cli gc-blobs <norm_provider> <acctId> <appId> <storageDir>` to prune unreferenced SHA blobs on the remote provider, and if the app is an unredirected orphan being cleaned, optionally removes its unredirected `<storageDir>/<acctId>/<appId>` directory.
  - Add REST endpoints in [`src/presenter/storeinject.cpp`](../../src/presenter/storeinject.cpp):
    - `GET /cloud/orphans?provider=<prov>` $\rightarrow$ returns `{"success":true,"orphans":[...]}`.
    - `POST /cloud/orphans/clean` (requires `X-Navi-Auth`, body `{"appId":12345,"provider":"...","deleteFiles":false}`) $\rightarrow$ runs `gc-blobs` (or deletes orphaned save dir when `deleteFiles: true`) and returns `{"success":true,"blobsDeleted":...}`.
- **UI (`res/inject-scripts/shell/import-lua-script.js`)**:
  - In the `Your Steam Cloud` card header toolbar (next to `Refresh list`), add a `Scan Orphans` button (`#sls-cfg-cs-scan-orphans-btn`).
  - Clicking `Scan Orphans` fetches `GET /cloud/orphans` and renders an inline collapsible `Orphaned saves & unreferenced blobs` panel (`#sls-cfg-cs-orphans-panel`) above `#sls-cfg-managed-apps-list` listing each discovered non-redirected AppID with:
    - Game title, AppID, file count, total size, and location badges (`Local Storage`, `Remote Cloud`, `Wine Prefix`).
    - **Adopt (Redirect)** button: Calls `POST /cloud/apps/<appId>/toggle` with `{"redirect":true}`, moving the game into active redirected cloud saves.
    - **Clean / GC Blobs** button: Calls `POST /cloud/orphans/clean` to garbage-collect unreferenced blobs or remove orphaned storage.

### Item 7: Global Backups View & Visible Retention Controls
- **Backend (`CCloudManager`, `CloudView`, `storeinject.cpp`)**:
  - Add `struct GlobalBackupEntry_t` in [`src/presenter/cloud_manager.hpp`](../../src/presenter/cloud_manager.hpp):
    ```cpp
    struct GlobalBackupEntry_t
    {
        AppId_t appId{0};
        std::string gameTitle;
        std::string fileName;
        std::string fullPath;
        uint64_t sizeBytes{0};
        uint64_t timestamp{0};
    };
    ```
  - Add `CCloudManager::ListAllBackups()` and `CCloudManager::DeleteBackup(AppId_t appId, const std::string& fileName)`:
    - Walks `<backupDir>/<appId>/*.tar.gz`, parses timestamps from `manual-<ts>.tar.gz` / `auto-<ts>.tar.gz` (falling back to file `mtime`), resolves `gameTitle` via `ResolveAppTitleFromManifests(appId)`, and sorts newest-first.
    - `DeleteBackup` validates `fileName` has no path traversal (`/` or `..`) and ends with `.tar.gz`, then removes `<backupDir>/<appId>/<fileName>`.
    - Enforce `g_config.cloudBackupRetentionCount` automatically after creating a new backup in `POST /cloud/apps/<id>/backup` by pruning the oldest `.tar.gz` archives for that `appId` beyond the retention count.
  - Add REST endpoints in [`src/presenter/storeinject.cpp`](../../src/presenter/storeinject.cpp):
    - `GET /cloud/backups` $\rightarrow$ returns `{"success":true,"backups":[{"appId":...,"gameTitle":"...","fileName":"...","fullPath":"...","sizeBytes":...,"timestamp":...}]}`.
    - `POST /cloud/backups/delete` (requires `X-Navi-Auth`, body `{"appId":12345,"fileName":"manual-1700000000.tar.gz"}`) $\rightarrow$ deletes the specified archive and returns `{"success":true}`.
- **UI (`res/inject-scripts/shell/import-lua-script.js`)**:
  - Replace the hidden inputs `#sls-cfg-CloudRetention` and `#sls-cfg-CloudAutoBackup` with a dedicated **`Backups`** card in `Cloud saves`:
    - **Row 1:** `Back up before resetting` (`#sls-cfg-CloudAutoBackup`, toggle bound to `CloudSaves.AutoBackupBeforeReset`, default `true`) — *"Automatically create a .tar.gz backup archive before resetting a game's saves."*
    - **Row 2:** `Backups to keep per game` (`#sls-cfg-CloudRetention`, number input `min="1" max="100"`, bound to `CloudSaves.BackupRetentionCount`, default `5`) — *"Older backups beyond this limit are pruned automatically."*
    - **Global Backups Browser Table (`#sls-cfg-cs-backups-list`)**:
      - Toolbar with search filter input (`#sls-cfg-cs-backups-search`, filtering by game title, AppID, or filename) and `Refresh backups` button (`#sls-cfg-cs-backups-refresh`).
      - Lists all `.tar.gz` backups across all games with columns: **GAME**, **BACKUP ARCHIVE**, **DATE**, **SIZE**, and **ACTIONS** (`Restore` button queueing `POST /cloud/apps/<appId>/restore` + `Delete` button calling `POST /cloud/backups/delete`).

---

## 3. Owning MVC Layers & Invariants Touched

| Layer | Files Modified | Responsibility |
|---|---|---|
| **Model** (`src/model/`, `res/`) | `src/model/config.hpp`, `src/model/config.cpp`, `src/core/config_default.hpp`, `res/config.yaml`, `res/config.diff.yaml` | `CConfig` state variables (`cloudNotificationsEnabled`, `cloudStatsSyncEnabled`, `cloudSyncAchievements`, `cloudSyncPlaytime`), YAML load/defaults, trigger `SyncEngineConfig` on config reload. |
| **Presenter** (`src/presenter/`) | `src/presenter/cloudredirect.hpp`, `src/presenter/cloudredirect.cpp`, `src/presenter/cloud_manager.hpp`, `src/presenter/cloud_manager.cpp`, `src/presenter/storeinject.cpp` | `CCloudRedirectBridge::SyncEngineConfig`, R2/S3 `0600` credential persistence, CLI wrappers (`scan-all`, `migrate`, `gc-blobs`), orphan discovery, global backup listing/deletion/retention pruning, and `/cloud/*` HTTP endpoints. |
| **View** (`src/view/`, `res/inject-scripts/`) | `src/view/storeinject_view.cpp`, `src/view/cloud_view.hpp`, `src/view/cloud_view.cpp`, `res/inject-scripts/common/cloud-manager.js` (`res/inject-scripts/cloud/cloud_manager.js`), `res/inject-scripts/shell/import-lua-script.js` | JSON serialization for `/config/full` and `/cloud/*` responses; CEF UI cards (`Status` launch notifications toggle, `Where saves are stored` R2/S3 form, `Stats sync` card, `Migrate cloud saves` card, `Scan Orphans` toolbar + panel, `Backups` card). |
| **Tests** (`tests/`) | `tests/unit/test_cloudredirect_bridge.cpp`, `tests/unit/test_cloud_manager.cpp`, `tests/unit/test_cloud_rest_api.cpp`, `tests/unit/test_config_full_json.cpp`, `tests/integration/ui/test_uj10_cloud_saves.py`, `tests/reports/YYYY-MM-DD-gate05-cloudredirect-live-cdp.md` | Unit, integration, and live CEF CDP + Jev verification coverage for all 7 parity items. |

### Domain Invariants Honored (`.agents/rules/*`)
1. **`.agents/rules/cpp_hooking_patterns.md` (Rule 7 & Rule 8)**:
   - Prebuilt `res/cloud_redirect.so` and `res/cloud_redirect_cli` remain unmodified and co-located.
   - Out-of-process restore/reset operations continue to enqueue into `CPendingOpsQueue` (`cloud_pending_ops.json`) rather than mutating live active Steam save files in-process.
   - `CCloudManager` CLI callers never treat exit code `1` from `cloud_redirect_cli` as a fatal process error; JSON output is parsed via `ExtractJsonSubstring` / line-by-line JSON extraction.
2. **`.agents/rules/steam_cef_ui_patterns.md`**:
   - All CEF UI controls use the dark slate token palette (`#14171d` / `#1c202a` / `#171a22`) from `navi-config-redesign-spec-v2.md`, keyboard-accessible focus states, and dirty-bar tracking (`checkDirty()`).
3. **`.agents/rules/test-regression.md`**:
   - Unit and integration tests are authored alongside the backend and UI changes to lock in the contracts.

---

## 4. Verification Plan (Gates 1–5)

### Gate 1: Unified 32-bit Build
```bash
./install.sh build
```

### Gate 2: Googletest Unit Suite
```bash
make test
```
- Verify `SyncEngineConfig` maps `"local-only"` $\rightarrow$ `"local"` and writes `notifications_enabled`, `stats_sync_enabled`, `sync_achievements`, and `sync_playtime`.
- Verify `/config/full` round-trips `CloudSaves.NotificationsEnabled` and `CloudSaves.StatsSync.{Enabled, SyncAchievements, SyncPlaytime}`.
- Verify R2/S3 credential save/load masks `secretAccessKey` on read, preserves existing secret when masked, and enforces `0600` permissions.
- Verify `ScanProviderForMigration`, `MigrateProviderData`, `ScanOrphanedSaves`, `GarbageCollectAppBlobs`, `ListAllBackups`, `DeleteBackup`, and backup retention pruning.

### Gate 3 & Gate 4: Gate 0.7 & CloudRedirect Integration Checks
```bash
make test-gate07
make check-cloudredirect
pytest -q tests/integration
```

---

### Gate 5: Real-Time Live Steam Verification (`./install.sh install --yes` + `/cef-cdp-injection` + `/jev`)

#### Step 5.1 — Live Local Deployment (`./install.sh install --yes`)
- Run `./install.sh install --yes` to compile the 32-bit `libnavi.so`, install `res/inject-scripts/shell/import-lua-script.js` and `res/inject-scripts/cloud/cloud_manager.js` (`res/inject-scripts/common/cloud-manager.js`) into `~/.local/share/navi/`, and ensure Steam is running with CEF remote debugging (`127.0.0.1:8080`) and Navi REST (`127.0.0.1:9001`).

#### Step 5.2 — Autonomous CEF CDP Navigation & DOM Inspection (`cef-cdp-injection` skill)
- Connect to `http://127.0.0.1:8080/json` (`title == "Steam"`), open the Navi Config page (`#sls-import-lua-tab` / `sls-cfg-sec-cloudsaves`) via `Runtime.evaluate` (`returnByValue: True`, `awaitPromise: True`, `userGesture: True`).
- Navigate and exercise all 7 parity features in the live Steam client DOM:
  1. **`Status` card Launch Notifications toggle**: Inspect and toggle `#sls-cfg-CloudSaves-NotificationsEnabled`, save config, and verify live disk state of `~/.config/CloudRedirect/config.json` (`"provider": "local"`, `"notifications_enabled"`).
  2. **`Stats sync` card**: Inspect and toggle `#sls-cfg-StatsSync-Enabled`, `#sls-cfg-StatsSync-Achievements`, and `#sls-cfg-StatsSync-Playtime`, save config, and verify live disk state of `~/.config/CloudRedirect/config.json` (`stats_sync_enabled`, `sync_achievements`, `sync_playtime`).
  3. **`Where saves are stored` R2/S3 credential form**: Switch `#sls-cfg-cs-provider` to `r2` and `s3` using the native value setter + `change` event, inspect `#sls-cfg-cs-creds-form` inputs (`#sls-cfg-cs-cred-account-id`, `#sls-cfg-cs-cred-bucket`, `#sls-cfg-cs-cred-access-key`, `#sls-cfg-cs-cred-secret-key`, `#sls-cfg-cs-cred-region`, `#sls-cfg-cs-cred-endpoint`, `#sls-cfg-cs-cred-prefix`, `#sls-cfg-cs-cred-save-btn`), and verify `GET /cloud/provider/credentials` secret masking (`"********"`, `hasSecretKey`).
  4. **`Migrate cloud saves` card**: Inspect `#sls-cfg-cs-migrate-src` and `#sls-cfg-cs-migrate-dst`, click `#sls-cfg-cs-migrate-scan-btn`, and verify the live `GET /cloud/migrate/scan` summary rendered in the DOM.
  5. **`Your Steam Cloud` `Scan Orphans` button**: Click `#sls-cfg-cs-scan-orphans-btn` and verify `#sls-cfg-cs-orphans-panel` renders live `GET /cloud/orphans` telemetry.
  6. **`Backups` card**: Inspect `#sls-cfg-CloudAutoBackup`, `#sls-cfg-CloudRetention`, `#sls-cfg-cs-backups-search`, and `#sls-cfg-cs-backups-refresh`, and verify `#sls-cfg-cs-backups-list` renders live `GET /cloud/backups` entries.

#### Step 5.3 — Jev-Driven Navigation & Outcome Checkpoints (`jev` / `jev-act` / `jev-eval` / `jev-decision`)
- Use Jev (`python3 /home/nicolas/.gemini/config/skills/jev-decision/scripts/decide.py`) during the live CDP session for:
  - **Next-Action Selection (`jev-act` UI step pattern)**: Given fresh observed CEF DOM state (active tab, visible cards, form dirty state, REST status), choose the next legal UI verification action from the bounded candidate set (`open_cloud_saves_tab`, `verify_status_notifications`, `verify_stats_sync`, `verify_r2_s3_credentials`, `verify_migration_scan`, `verify_orphan_scan`, `verify_backups_browser`, `complete_verification`).
  - **Batch Outcome Verification & Goal-Drift Checkpoint (`jev` / `jev-eval` rubric pattern)**: Evaluate each of the 7 live parity checks (`yes/no` `noul` + `1..5` `score` rubric) against the captured CDP DOM telemetry and `~/.config/CloudRedirect/config.json` disk state.
- Write the live verification report to `tests/reports/YYYY-MM-DD-gate05-cloudredirect-live-cdp.md`.

---

## 5. Risks & Mitigations

| Risk | Severity | Mitigation |
|---|---|---|
| **`"local-only"` vs `"local"` provider mismatch in `config.json`** | Medium | Keep `"local-only"` in Navi's `config.yaml` and CEF UI for backward compatibility, and translate `"local-only"` $\rightarrow$ `"local"` inside `CCloudRedirectBridge::SyncEngineConfig` and `CCloudManager` CLI invocations (`scan-all`, `migrate`). |
| **Secret key exposure via REST API** | High | `GET /cloud/provider/credentials` never returns `secret_access_key` in plaintext (returns `"********"` when non-empty and `"hasSecretKey": true`). Credential files are written with `chmod 0600`. |
| **Long-running `cloud_redirect_cli migrate` blocking HTTP socket** | Medium | Use a dedicated 120-second timeout in `RunCliCommand` for `migrate` and parse the final newline-delimited JSON object (`"type":"complete"` or `"type":"error"`), while disabling the UI button with a spinner during migration. |
| **Path traversal in backup delete/restore** | High | Reject any `fileName` containing `/`, `\`, or `..` or not ending in `.tar.gz` before resolving `<backupDir>/<appId>/<fileName>`. |
| **Live CEF DOM / disk state drift during Gate 5** | Medium | Restore original user settings in `config.yaml` and `~/.config/CloudRedirect/config.json` (and clean up any temporary test credential files) at the end of Step 5.2, verified by Jev's final `jev-eval` checkpoint. |
