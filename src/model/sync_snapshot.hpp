#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace Sync
{
    struct SnapshotRecord_t
    {
        std::string snapshotId;
        uint64_t steamId64 = 0;
        int64_t createdAtUnixSec = 0;
        std::vector<std::pair<std::string, std::string>> backedUpFiles; /* (originalAbsPath, backupCopyPath) */
        std::vector<std::string> nonExistentTargets; /* targets that did not exist before sync and should be removed on rollback */
    };

    class SyncSnapshotManager
    {
    public:
        /**
         * Creates pre-sync backups of all target files:
         * 1. Creates sibling <targetAbsPath>.bak copy
         * 2. Creates snapshot archive copy in <backupRootDir>/<steamId64>/<snapshotId>/files/
         * 3. Verifies SHA-256 integrity between target and backup copy
         * 4. Records nonexistent targets for rollback cleanup
         * 5. Atomically writes snapshot.yaml metadata
         */
        static bool createPreSyncBackup(
            const std::string& backupRootDir,
            uint64_t steamId64,
            const std::vector<std::string>& targetAbsPaths,
            SnapshotRecord_t& outRecord);

        /**
         * Restores all backed-up files to original locations and removes
         * files that were marked as non-existent before sync.
         */
        static bool restoreSnapshot(
            const std::string& backupRootDir,
            uint64_t steamId64,
            const std::string& snapshotId);

        /**
         * Prunes snapshots in <backupRootDir>/<steamId64>, keeping at most maxRetained newest snapshots.
         */
        static void pruneOldSnapshots(
            const std::string& backupRootDir,
            uint64_t steamId64,
            size_t maxRetained = 5);
    };
} // namespace Sync
