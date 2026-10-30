#include <gtest/gtest.h>
#include "cloud_manager.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace Navi::CloudRedirect;

struct ScopedCloudManagerTestEnv
{
    fs::path tempDir;

    ScopedCloudManagerTestEnv()
    {
        tempDir = fs::path(std::string("/tmp/test_cloud_manager_") + std::to_string(getpid()) + "_" +
                  std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::error_code ec;
        fs::create_directories(tempDir, ec);
    }

    ~ScopedCloudManagerTestEnv()
    {
        std::error_code ec;
        fs::remove_all(tempDir, ec);
    }
};

TEST(CloudManagerTest, ExitCodeOneUnauthenticatedStateHandling)
{
    ScopedCloudManagerTestEnv env;
    auto& manager = CCloudManager::Instance();

    // 1. Create a mock CLI script that exits with code 1 and prints JSON {"authenticated":false}
    fs::path mockCli1 = env.tempDir / "mock_cli_unauth.sh";
    {
        std::ofstream ofs(mockCli1);
        ofs << "#!/bin/sh\n"
            << "echo '[INFO] Initializing'\n"
            << "echo '{\"authenticated\":false,\"token_path\":\"/path/to/tokens_gdrive.json\"}'\n"
            << "exit 1\n";
    }
    chmod(mockCli1.c_str(), 0755);

    manager.SetCliPathForTest(mockCli1);

    auto status1 = manager.CheckAuthStatus("gdrive");
    EXPECT_EQ(status1.provider, "gdrive");
    EXPECT_FALSE(status1.authenticated); // Must be parsed as valid false, not an error!
    EXPECT_NE(status1.rawJson.find("\"authenticated\":false"), std::string::npos);

    // 2. Create a mock CLI script that exits with code 0 and prints authenticated JSON
    fs::path mockCli2 = env.tempDir / "mock_cli_auth.sh";
    {
        std::ofstream ofs(mockCli2);
        ofs << "#!/bin/sh\n"
            << "echo '{\"authenticated\":true,\"account_email\":\"gamer@example.com\"}'\n"
            << "exit 0\n";
    }
    chmod(mockCli2.c_str(), 0755);

    manager.SetCliPathForTest(mockCli2);

    auto status2 = manager.CheckAuthStatus("gdrive");
    EXPECT_TRUE(status2.authenticated);
    EXPECT_EQ(status2.accountEmail, "gamer@example.com");

    manager.SetCliPathForTest({});
}

TEST(CloudManagerTest, ListBackupsDescendingOrder)
{
    ScopedCloudManagerTestEnv env;
    auto& manager = CCloudManager::Instance();

    AppId_t appId = 480;
    fs::path appBackupDir = env.tempDir / std::to_string(appId);
    std::error_code ec;
    fs::create_directories(appBackupDir, ec);

    std::ofstream(appBackupDir / "pre-reset-1000.tar.gz") << "archive1";
    std::ofstream(appBackupDir / "pre-reset-2000.tar.gz") << "archive2";
    std::ofstream(appBackupDir / "manual-1500.tar.gz") << "archive3";
    std::ofstream(appBackupDir / "notes.txt") << "not a tarball";

    manager.SetBackupDirForTest(env.tempDir);

    auto backups = manager.ListBackups(appId);
    ASSERT_EQ(backups.size(), 3u);

    // Verify sorted descending
    EXPECT_EQ(backups[0], "pre-reset-2000.tar.gz");
    EXPECT_EQ(backups[1], "pre-reset-1000.tar.gz");
    EXPECT_EQ(backups[2], "manual-1500.tar.gz");

    // Non-existent appId returns empty list
    auto emptyBackups = manager.ListBackups(99999);
    EXPECT_TRUE(emptyBackups.empty());

    manager.SetBackupDirForTest({});
}

TEST(CloudManagerTest, GetStorageUsageBytesCalculation)
{
    ScopedCloudManagerTestEnv env;
    auto& manager = CCloudManager::Instance();

    AppId_t appId = 480;
    std::string acctId = "12345";

    // Set up mock storage: <storageDir>/<acct>/<appId>/
    fs::path mockStorageDir = env.tempDir / "storage";
    fs::path appStorage = mockStorageDir / acctId / std::to_string(appId);
    std::error_code ec;
    fs::create_directories(appStorage, ec);

    std::string payload500(500, 'A');
    std::string payload1500(1500, 'B');

    std::ofstream(appStorage / "file1.bin") << payload500;
    std::ofstream(appStorage / "file2.bin") << payload1500;

    manager.SetStorageDirForTest(mockStorageDir);

    uint64_t usage = manager.GetStorageUsageBytes(appId);
    EXPECT_EQ(usage, 2000u);

    // Other apps report 0
    EXPECT_EQ(manager.GetStorageUsageBytes(730), 0u);

    manager.SetStorageDirForTest({});
}
