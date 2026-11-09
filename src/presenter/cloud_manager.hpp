#pragma once

#include "sdk/sdk.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <filesystem>
#include <mutex>

namespace fs = std::filesystem;

namespace Navi::CloudRedirect
{

struct ProviderAuthStatus_t
{
    std::string provider;
    bool authenticated{false};
    std::string accountEmail;
    std::string rawJson;
};

struct CloudFileEntry_t
{
    std::string folder;
    std::string fileName;
    uint64_t sizeBytes{0};
    uint64_t modifiedTime{0};
    std::string remoteBlobPath;
    bool inRemoteCloud{false};
    bool inLocalMirror{false};
};

struct CloudAppSummary_t
{
    AppId_t appId{0};
    std::string gameTitle;
    uint32_t fileCount{0};
    uint64_t totalSizeBytes{0};
    bool redirected{false};
    bool inRemoteCloud{false};
    bool inLocalMirror{false};
    std::vector<AppId_t> childAppIds;
};

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

struct MigrationAppEntry_t
{
    std::string accountId;
    AppId_t appId{0};
    std::string gameTitle;
    uint32_t fileCount{0};
    uint64_t totalSizeBytes{0};
    uint64_t totalBytes{0};
};

struct MigrationScanResult_t
{
    bool success{false};
    std::string provider;
    uint32_t appCount{0};
    uint32_t totalApps{0};
    uint32_t fileCount{0};
    uint32_t totalFiles{0};
    uint64_t totalSizeBytes{0};
    uint64_t totalBytes{0};
    std::vector<MigrationAppEntry_t> apps;
    std::string error;
};

struct MigrationResult_t
{
    bool success{false};
    std::string sourceProvider;
    std::string targetProvider;
    std::string srcProvider;
    std::string dstProvider;
    uint32_t migrated{0};
    uint32_t skipped{0};
    uint32_t failed{0};
    uint32_t total{0};
    uint64_t totalBytes{0};
    std::string error;
};

struct OrphanAppEntry_t
{
    AppId_t appId{0};
    std::string gameTitle;
    uint32_t fileCount{0};
    uint64_t totalSizeBytes{0};
    bool inLocalStorage{false};
    bool inLocalMirror{false};
    bool inRemoteCloud{false};
    bool inGoldbergPrefix{false};
    bool inGoldberg{false};
};

struct GcResult_t
{
    bool success{false};
    AppId_t appId{0};
    uint32_t blobsDeleted{0};
    uint32_t totalBlobs{0};
    uint32_t deleted{0};
    uint32_t kept{0};
    uint32_t failed{0};
    bool localDeleted{false};
    bool localFilesDeleted{false};
    std::string error;
};

struct GlobalBackupEntry_t
{
    AppId_t appId{0};
    std::string gameTitle;
    std::string fileName;
    std::string fullPath;
    uint64_t sizeBytes{0};
    uint64_t timestamp{0};
};

/// Parsed summary of one stplug-in lua plugin collected during the single
/// library pass. Mirrors exactly what the legacy per-candidate scans needed:
/// ownership facts (depots/dlcs/manifests), child ids, the fallback-3 title
/// (first addappid line carrying a trailing -- comment) and all raw
/// "addappid(<digits>" ids used by ListCloudApps discovery.
struct ScanLuaFileRecord
{
    fs::path path;
    std::string stem;               ///< filename stem as listed on disk
    AppId_t stemAppId{0};           ///< stemAppId when stem is fully numeric, else 0
    bool readOk{false};             ///< content could be opened and read
    bool parsedValid{false};        ///< LuaParser::parse().valid
    std::string parsedAppId;        ///< LuaParser::parse().appId string
    std::string gameName;           ///< LuaParser::parse().gameName (lua owner title rule)
    std::string luaTitle;           ///< first addappid line with trailing -- comment
    std::vector<AppId_t> depots;    ///< digits-only keys of parsed.depots
    std::vector<AppId_t> dlcs;      ///< digits-only keys of parsed.dlcs
    std::vector<AppId_t> manifests; ///< digits-only keys of parsed.manifests (setManifestid)
    std::vector<AppId_t> allAddAppIds; ///< every "addappid(<digits>" in raw content
};

/// One library-map snapshot built by a single pass over all Steam library
/// appmanifests + stplug-in lua plugins. Depot->parent, child-depot lists and
/// title lookups become map lookups instead of per-candidate rescans of the
/// whole library. RED/GREEN guarded by
/// CloudManagerTest.InventoryScanIndexReadsLibraryOncePerPass.
struct ScanIndex
{
    fs::path steamRoot;                  ///< root the scan ran against (empty = none)
    std::vector<fs::path> steamAppsDirs; ///< GetSteamAppsDirs() snapshot, same order
    fs::path stplugDir;                  ///< <steamRoot>/config/stplug-in snapshot
    bool steamRootMissing{false};        ///< true when no usable steamRoot is configured

    /// depotId -> owning parent from the InstalledDepots pass. First manifest in
    /// scan order wins, exactly mirroring the legacy per-depot scan.
    std::unordered_map<AppId_t, AppId_t> parentOfDepotFromManifests;
    /// appId -> child depots from the same InstalledDepots pass (order preserved).
    std::unordered_map<AppId_t, std::vector<AppId_t>> childrenOfAppFromManifests;
    /// appmanifest appId -> first "name" passing IsPlaceholderAppTitle(name)
    /// (the 1-arg legacy rule used for owner titles).
    std::unordered_map<AppId_t, std::string> manifestTitleOf;
    /// appmanifest appId -> first "name" passing IsPlaceholderAppTitle(name, appId)
    /// (the 2-arg legacy rule used by direct title resolution).
    std::unordered_map<AppId_t, std::string> manifestTitleOfStrict;

    /// depotId -> owning parent from one lua-plugins pass. The first owner whose
    /// gameName passes IsPlaceholderAppTitle wins; when none has a usable title,
    /// the first-referenced owner is used (exact legacy FindDepotOwnerInLuaPlugins rule).
    std::unordered_map<AppId_t, AppId_t> parentOfDepotFromLua;
    /// depotId -> the winning lua owner's gameName (empty when untitled).
    std::unordered_map<AppId_t, std::string> luaOwnerTitleOf;

    /// gameId -> child ids (depots + dlcs + manifests, order preserved) read from
    /// the <gameId>.lua stem file (legacy GetChildDepotIdsForApp step 2).
    std::unordered_map<AppId_t, std::vector<AppId_t>> childrenOfAppFromLuaStem;
    /// gameId -> child ids merged from other .lua files whose parsed appId matches
    /// (legacy GetChildDepotIdsForApp step 3, scan order preserved).
    std::unordered_map<AppId_t, std::vector<AppId_t>> childrenOfAppFromLuaMapped;
    /// gameId -> lua fallback title (fallback 3: first addappid line with a
    /// trailing -- comment in the <gameId>.lua stem file).
    std::unordered_map<AppId_t, std::string> luaTitleOfStem;

    /// Per-file lua records in scan order (ListCloudApps discovery + title lookups).
    std::vector<ScanLuaFileRecord> luaFiles;
};

/// Test-only scan instrumentation: how many library files one inventory
/// pass opened/parsed. Lets unit tests pin the scan-once behavior of
/// ListCloudApps (RED/GREEN for perf regressions). Counters are process-wide
/// and must be reset via ResetScanCountersForTest() before each measurement.
struct ScanCounters
{
    uint64_t manifestFilesRead{0};  ///< appmanifest_*.acf file opens in depot/title scans
    uint64_t luaFilesParsed{0};     ///< stplug-in lua files read+parsed in depot scans
    uint64_t fileUpsertProbes{0};   ///< candidate entries compared in file upserts (D3: linear vs hash lookups)
    uint64_t fileDedupProbes{0};    ///< candidate entries compared in child-file dedup (D3)
};


class CCloudManager
{
public:
    static constexpr AppId_t kCloudCatalogAppId = 999999999;
    static constexpr const char* kCloudCatalogFileName = "navi_cloud_catalog.json";

    static CCloudManager& Instance();

    ProviderAuthStatus_t CheckAuthStatus(const std::string& provider);
    ProviderCredentials_t GetProviderCredentials(const std::string& provider) const;
    bool SaveProviderCredentials(const ProviderCredentials_t& creds, std::string& outError);
    bool DeleteProviderCredentials(const std::string& provider);

    MigrationScanResult_t ScanProviderForMigration(const std::string& provider);
    MigrationResult_t MigrateProviderData(const std::string& srcProvider, const std::string& dstProvider, bool switchActiveProvider = true);

    std::vector<OrphanAppEntry_t> ScanOrphanedSaves(const std::string& provider, uint32_t accountId = 0);
    GcResult_t CleanOrphanedApp(AppId_t appId, const std::string& provider, bool deleteLocalFiles, uint32_t accountId = 0);
    GcResult_t GarbageCollectAppBlobs(AppId_t appId, const std::string& provider, uint32_t accountId = 0);

    std::vector<std::string> ListBackups(AppId_t appId);
    std::vector<GlobalBackupEntry_t> ListAllBackups();
    bool DeleteBackup(AppId_t appId, const std::string& fileName);
    uint32_t PruneBackupsForApp(AppId_t appId, uint32_t keepCount);
    bool DeleteAllBackups();
    uint64_t GetStorageUsageBytes(AppId_t appId);

    /// Performs a full cloud + local inventory scan of all apps with cloud files.
    std::vector<CloudAppSummary_t> ListCloudApps(const std::string& provider, uint32_t accountId = 0);

    /// Performs a full cloud + local file scan for a single appId.
    std::vector<CloudFileEntry_t> ListAppCloudFiles(AppId_t appId, const std::string& provider, uint32_t accountId = 0);

    /// Stages all local save files, Proton auto-cloud saves, Goldberg saves, and
    /// stplug-in .lua/.provider + depotcache .manifest files for appId (and its folded
    /// child depots) into <storageDir>/<accountId>/<appId>/ with updated tokens and CN.
    bool StageAppFilesForCloudSync(AppId_t appId, uint32_t accountId = 0);

    /// Rebuilds <storageDir>/<accountId>/999999999/navi_cloud_catalog.json from all
    /// remote-confirmed redirected apps and publishes it to the remote cloud provider.
    bool UpdateAndPublishCloudCatalog(const std::string& provider, uint32_t accountId = 0);

    /// Stages all local files for appId, publishes <appId> to the remote cloud provider,
    /// and updates + publishes the account's navi_cloud_catalog.json (999999999).
    bool SyncAppToCloudAndUpdateCatalog(AppId_t appId, const std::string& provider, uint32_t accountId = 0);

    /// Reads the raw bytes of a cloud save file for download (syncing from remote provider if needed).
    bool ReadAppCloudFile(AppId_t appId,
                          const std::string& relativeFileName,
                          const std::string& provider,
                          uint32_t accountId,
                          std::string& outContent,
                          std::string& outDownloadName,
                          std::string& outError);

    /// Resolves a game's display title from Steam's appmanifest_<appId>.acf across library
    /// folders, falling back to appinfo/Lua metadata and the owning game for depot IDs.
    std::string ResolveAppTitleFromManifests(AppId_t appId) const;

    /// Returns all child depot IDs and DLCs associated with a parent game,
    /// by scanning installed app manifests (InstalledDepots), Lua plugins, and config DlcData.
    std::vector<AppId_t> GetChildDepotIdsForApp(AppId_t parentAppId) const;

    /// Returns the owning parent game App ID if depotId is an addon/child depot,
    /// or 0 if it is not an addon of another game (checks manifests, Lua plugins, and config DlcData).
    AppId_t GetParentAppIdForDepot(AppId_t depotId) const;

    /// Returns every Goldberg-emulator save directory discovered for appId.
    /// Searches env-derived paths only — no hardcoded user paths.
    /// Caller must hold no lock (acquires internally where needed).
    std::vector<fs::path> ResolveGoldbergSavePaths(AppId_t appId) const;

    void SetCliPathForTest(const fs::path& path);
    void SetBackupDirForTest(const fs::path& path);
    void SetStorageDirForTest(const fs::path& path);
    void SetCredentialsDirForTest(const fs::path& path);
    /// Override the steamapps root used by ResolveGoldbergSavePaths in tests.
    void SetSteamAppsRootForTest(const fs::path& path);
    /// Override the steam root used for scanning stplug-in, depotcache, and userdata in tests.
    void SetSteamRootForTest(const fs::path& path);

    /// Test-only scan instrumentation (see ScanCounters). Counters accumulate
    /// parse-free library file opens across ListCloudApps/ListAppCloudFiles/title
    /// and depot-resolution scans. Reset with ResetScanCountersForTest().
    ScanCounters GetScanCountersForTest() const;
    void ResetScanCountersForTest();

private:
    CCloudManager();
    ~CCloudManager() = default;
    CCloudManager(const CCloudManager&) = delete;
    CCloudManager& operator=(const CCloudManager&) = delete;

    fs::path GetCliPath() const;
    fs::path GetBackupDir() const;
    fs::path GetStorageDir() const;
    fs::path GetCredentialsDir() const;
    fs::path GetSteamRoot() const;
    std::string RunCliCommand(const std::vector<std::string>& args, int timeoutMs = 15000) const;
    std::vector<uint32_t> ResolveCandidateAccountIds(uint32_t preferredAccountId) const;

    /// Builds (or reuses, when the library stamp is unchanged) the one-pass
    /// library scan index used by all depot/child/title lookups below.
    std::shared_ptr<const ScanIndex> ObtainScanIndex() const;
    uint64_t ComputeScanIndexStamp() const;

    AppId_t GetParentAppIdForDepot(AppId_t depotId, const ScanIndex& index) const;
    std::vector<AppId_t> GetChildDepotIdsForApp(AppId_t parentAppId, const ScanIndex& index) const;
    std::string ResolveAppTitleFromManifestsInternal(AppId_t appId, int depth, const ScanIndex& index) const;

    std::vector<CloudFileEntry_t> ListAppCloudFiles(AppId_t appId, const std::string& provider, uint32_t accountId, const ScanIndex& index) const;
    std::vector<CloudFileEntry_t> ListAppCloudFilesDirect(AppId_t appId, const std::string& provider, uint32_t accountId) const;
    bool ReadAppCloudFileDirect(AppId_t appId,
                                const std::string& relativeFileName,
                                const std::string& provider,
                                uint32_t accountId,
                                std::string& outContent,
                                std::string& outDownloadName,
                                std::string& outError);

    struct CatalogCache
    {
        std::string provider;
        uint32_t accountId{0};
        fs::path storageRoot;
        uint64_t loadedAtMs{0};
        std::unordered_map<AppId_t, CloudAppSummary_t> summaries;
        std::unordered_map<AppId_t, std::vector<CloudFileEntry_t>> filesByApp;
    };

    bool LoadCloudCatalog(const std::string& provider,
                          uint32_t accountId,
                          bool syncRemote,
                          std::unordered_map<AppId_t, CloudAppSummary_t>& outSummaries,
                          std::unordered_map<AppId_t, std::vector<CloudFileEntry_t>>& outFilesByApp) const;
    void InvalidateCloudCatalogCache() const;

    mutable std::mutex m_mutex;
    fs::path m_cliPath;
    fs::path m_backupDir;
    fs::path m_storageDir;
    fs::path m_credentialsDir;
    fs::path m_steamAppsRoot; ///< Override for tests; empty = auto-resolve
    fs::path m_steamRoot;     ///< Override for tests; empty = auto-resolve

    mutable std::mutex m_scanIndexMutex;
    mutable std::shared_ptr<const ScanIndex> m_scanIndexCache;
    mutable uint64_t m_scanIndexStamp{0};

    mutable std::mutex m_catalogMutex;
    mutable CatalogCache m_catalogCache;
};

} // namespace Navi::CloudRedirect


