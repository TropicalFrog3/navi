#pragma once

#include "sdk/sdk.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <filesystem>
#include <mutex>

namespace fs = std::filesystem;

namespace Navi::CloudRedirect
{

enum class PendingOpType : uint8_t
{
    ResetCloudSave,    ///< Granular wipe of redirect storage + remote save cache
    RestoreBackup      ///< Restore save files from pre-existing tarball
};

enum class PendingOpStatus : uint8_t
{
    Pending,
    InProgress,
    Completed,
    Failed,
    RolledBack
};

struct PendingOperation_t
{
    std::string id;
    AppId_t appId{0};
    uint32_t accountId{0};
    PendingOpType type{PendingOpType::ResetCloudSave};
    PendingOpStatus status{PendingOpStatus::Pending};
    uint64_t timestamp{0};
    fs::path backupPath;
    fs::path restoreSourcePath;
    uint32_t retries{0};
    std::string lastError;
};

class CPendingOpsQueue
{
public:
    static CPendingOpsQueue& Instance();

    bool Enqueue(const PendingOperation_t& op);
    std::vector<PendingOperation_t> LoadAll() const;
    bool HasPendingOperations() const;
    bool UpdateStatus(const std::string& opId, PendingOpStatus status, const std::string& error = "");
    bool PruneCompleted(uint64_t maxAgeSeconds = 86400);

    // Testability helper
    void SetQueueFilePathForTest(const fs::path& path);
    void ClearQueueForTest();

private:
    CPendingOpsQueue() = default;
    ~CPendingOpsQueue() = default;
    CPendingOpsQueue(const CPendingOpsQueue&) = delete;
    CPendingOpsQueue& operator=(const CPendingOpsQueue&) = delete;

    fs::path GetQueueFilePath() const;

    mutable std::mutex m_mutex;
    fs::path m_testQueuePath;
};

class CPendingOpsExecutor
{
public:
    explicit CPendingOpsExecutor(fs::path steamRoot = {});

    bool AssertSteamIdle() const;
    int ExecuteAllPending();
    bool ExecuteOperation(PendingOperation_t& op);

    // Testability helpers
    void SetProcPathForTest(const fs::path& path);
    void SetBackupDirForTest(const fs::path& path);
    void SetSimulateFailureForTest(bool simFail);

private:
    bool CreatePreOpBackup(const PendingOperation_t& op, fs::path& outBackupFile);
    bool PerformGranularReset(const PendingOperation_t& op);
    bool PerformRestore(const PendingOperation_t& op);
    bool RollbackFromBackup(const PendingOperation_t& op);

    fs::path m_steamRoot;
    fs::path m_testProcPath;
    fs::path m_testBackupDir;
    bool m_simulateFailure{false};
};

std::string PendingOpTypeToString(PendingOpType type);
PendingOpType StringToPendingOpType(const std::string& str);
std::string PendingOpStatusToString(PendingOpStatus status);
PendingOpStatus StringToPendingOpStatus(const std::string& str);

} // namespace Navi::CloudRedirect

