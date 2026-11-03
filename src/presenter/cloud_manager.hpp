#pragma once

#include "sdk/sdk.hpp"
#include <string>
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

class CCloudManager
{
public:
    static CCloudManager& Instance();

    ProviderAuthStatus_t CheckAuthStatus(const std::string& provider);
    std::vector<std::string> ListBackups(AppId_t appId);
    uint64_t GetStorageUsageBytes(AppId_t appId);

    /// Performs a full cloud + local inventory scan of all apps with cloud files.
    std::vector<CloudAppSummary_t> ListCloudApps(const std::string& provider, uint32_t accountId = 0);

    /// Performs a full cloud + local file scan for a single appId.
    std::vector<CloudFileEntry_t> ListAppCloudFiles(AppId_t appId, const std::string& provider, uint32_t accountId = 0);

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
    /// by scanning installed app manifests (InstalledDepots) and Lua plugins.
    std::vector<AppId_t> GetChildDepotIdsForApp(AppId_t parentAppId) const;

    /// Returns the owning parent game App ID if depotId is an addon/child depot,
    /// or 0 if it is not an addon of another game.
    AppId_t GetParentAppIdForDepot(AppId_t depotId) const;

    /// Returns every Goldberg-emulator save directory discovered for appId.
    /// Searches env-derived paths only — no hardcoded user paths.
    /// Caller must hold no lock (acquires internally where needed).
    std::vector<fs::path> ResolveGoldbergSavePaths(AppId_t appId) const;

    void SetCliPathForTest(const fs::path& path);
    void SetBackupDirForTest(const fs::path& path);
    void SetStorageDirForTest(const fs::path& path);
    /// Override the steamapps root used by ResolveGoldbergSavePaths in tests.
    void SetSteamAppsRootForTest(const fs::path& path);
    /// Override the steam root used for scanning stplug-in, depotcache, and userdata in tests.
    void SetSteamRootForTest(const fs::path& path);

private:
    CCloudManager();
    ~CCloudManager() = default;
    CCloudManager(const CCloudManager&) = delete;
    CCloudManager& operator=(const CCloudManager&) = delete;

    fs::path GetCliPath() const;
    fs::path GetBackupDir() const;
    fs::path GetStorageDir() const;
    fs::path GetSteamRoot() const;
    std::string RunCliCommand(const std::vector<std::string>& args, int timeoutMs = 15000) const;
    std::vector<uint32_t> ResolveCandidateAccountIds(uint32_t preferredAccountId) const;
    std::string ResolveAppTitleFromManifestsInternal(AppId_t appId, int depth) const;

    std::vector<CloudFileEntry_t> ListAppCloudFilesDirect(AppId_t appId, const std::string& provider, uint32_t accountId) const;
    bool ReadAppCloudFileDirect(AppId_t appId,
                                const std::string& relativeFileName,
                                const std::string& provider,
                                uint32_t accountId,
                                std::string& outContent,
                                std::string& outDownloadName,
                                std::string& outError);

    mutable std::mutex m_mutex;
    fs::path m_cliPath;
    fs::path m_backupDir;
    fs::path m_storageDir;
    fs::path m_steamAppsRoot; ///< Override for tests; empty = auto-resolve
    fs::path m_steamRoot;     ///< Override for tests; empty = auto-resolve
};

} // namespace Navi::CloudRedirect


