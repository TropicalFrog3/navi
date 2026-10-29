#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "achievement_merger.hpp"
#include "process_lock.hpp"
#include "sync_manifest.hpp"

namespace Sync
{
    enum class EConflictChoice : uint8_t
    {
        k_PendingUserChoice = 0,
        k_KeepLocal = 1,
        k_KeepRemote = 2,
        k_BackupLocalAndUseRemote = 3
    };

    struct SaveConflict_t
    {
        uint32_t appId = 0;
        std::string logicalPath;
        std::string localSha256;
        int64_t localMtimeUnixSec = 0;
        uint64_t localSizeBytes = 0;
        std::string remoteSha256;
        int64_t remoteMtimeUnixSec = 0;
        uint64_t remoteSizeBytes = 0;

        bool operator==(const SaveConflict_t&) const = default;
    };

    enum class ESyncApplyStatus : uint8_t
    {
        k_Ok = 0,
        k_SteamIdMismatch,
        k_BackupFailed,
        k_ConflictPendingUserChoice,
        k_DeferredGameRunning,
        k_PartialDeferredOrConflict,
        k_IoError,
        k_CryptoError
    };

    struct SyncApplyReport_t
    {
        ESyncApplyStatus status = ESyncApplyStatus::k_Ok;
        std::string snapshotId;
        std::vector<std::string> appliedLogicalPaths;
        std::vector<uint32_t> deferredAppIds;
        std::vector<SaveConflict_t> conflicts;
        std::vector<uint32_t> stagedAcfRestartRequiredAppIds;
    };

    /**
     * Checks if incoming .acf differs from existing .acf in buildid or installed depot manifests.
     * Returns false if existingAcfContent is empty.
     */
    bool doesAcfRequireStagedShutdownCommit(
        const std::string& existingAcfContent,
        const std::string& incomingAcfContent);

    /**
     * Moves any staged <configDir>/pending_acf/appmanifest_<appId>.acf to
     * <library_folder>/steamapps/appmanifest_<appId>.acf on Steam shutdown.
     */
    bool commitStagedAcfFilesOnShutdown(
        const std::string& configDir,
        const std::string& steamRootOverride = "");

    class SyncEngine
    {
    public:
        SyncEngine(
            std::string configDir,
            std::string steamRootOverride = "",
            std::string procRootOverride = "/proc");

        ESyncApplyStatus applyRemoteSyncPackage(
            uint64_t localSteamId64,
            const SyncStateManifest_t& remoteManifest,
            const std::unordered_map<std::string, std::vector<uint8_t>>& plaintextPayloadsByLogicalPath,
            const SyncSelectionFilter_t& filter,
            const std::unordered_map<std::string, EConflictChoice>& userConflictChoices,
            SyncApplyReport_t& outReport);

        SyncApplyReport_t drainDeferredGamesIfExited(
            uint64_t localSteamId64,
            const std::unordered_map<std::string, EConflictChoice>& userConflictChoices = {});

        void setSimulateWriteFailure(bool enable) { simulateWriteFailure_ = enable; }
        bool isAppDeferred(uint32_t appId) const;
        const std::unordered_map<uint32_t, std::vector<std::pair<SyncFileEntry_t, std::vector<uint8_t>>>>& getDeferredQueue() const { return deferredQueue_; }
        const std::unordered_map<std::string, std::string>& getLastSyncedSha256() const { return lastSyncedSha256_; }
        void setLastSyncedSha256(const std::string& logicalPath, const std::string& sha) { lastSyncedSha256_[logicalPath] = sha; }

    private:
        std::string configDir_;
        std::string steamRootOverride_;
        std::string procRootOverride_;
        bool simulateWriteFailure_ = false;

        std::unordered_map<std::string, std::string> lastSyncedSha256_;
        std::unordered_map<uint32_t, std::vector<std::pair<SyncFileEntry_t, std::vector<uint8_t>>>> deferredQueue_;

        void loadBaseState(uint64_t steamId64);
        void saveBaseState(uint64_t steamId64);
        std::string resolveDestPath(const SyncFileEntry_t& entry, uint64_t steamId64) const;
    };
} // namespace Sync
