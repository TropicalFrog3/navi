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

class CCloudManager
{
public:
    static CCloudManager& Instance();

    ProviderAuthStatus_t CheckAuthStatus(const std::string& provider);
    std::vector<std::string> ListBackups(AppId_t appId);
    uint64_t GetStorageUsageBytes(AppId_t appId);

    void SetCliPathForTest(const fs::path& path);
    void SetBackupDirForTest(const fs::path& path);
    void SetStorageDirForTest(const fs::path& path);

private:
    CCloudManager();
    ~CCloudManager() = default;
    CCloudManager(const CCloudManager&) = delete;
    CCloudManager& operator=(const CCloudManager&) = delete;

    fs::path GetCliPath() const;
    fs::path GetBackupDir() const;
    fs::path GetStorageDir() const;

    mutable std::mutex m_mutex;
    fs::path m_cliPath;
    fs::path m_backupDir;
    fs::path m_storageDir;
};

} // namespace Navi::CloudRedirect

