/**
 * @file  test_cloud_pending_ops.cpp
 * @brief Unit tests for CPendingOpsQueue pending operation serialization
 */
#include <gtest/gtest.h>
#include "cloud_pending_ops.hpp"
#include "cloudredirect.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace Navi::CloudRedirect;

struct ScopedPendingOpsTestEnv
{
    fs::path tempDir;

    ScopedPendingOpsTestEnv()
    {
        tempDir = fs::path(std::string("/tmp/test_cloud_pending_ops_") + std::to_string(getpid()) + "_" +
                  std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::error_code ec;
        fs::create_directories(tempDir, ec);
    }

    ~ScopedPendingOpsTestEnv()
    {
        std::error_code ec;
        fs::remove_all(tempDir, ec);
    }
};

TEST(CloudPendingOpsTest, QueueSerializationAndLifecycle)
{
    ScopedPendingOpsTestEnv env;
    fs::path queueFile = env.tempDir / "cloud_pending_ops.json";

    auto& queue = CPendingOpsQueue::Instance();
    queue.SetQueueFilePathForTest(queueFile);

    EXPECT_FALSE(queue.HasPendingOperations());
    EXPECT_TRUE(queue.LoadAll().empty());

    // Enqueue operation 1
    PendingOperation_t op1;
    op1.id = "op_test_1";
    op1.appId = 480;
    op1.accountId = 12345;
    op1.type = PendingOpType::ResetCloudSave;
    op1.status = PendingOpStatus::Pending;
    op1.timestamp = 1000;
    op1.backupPath = "/tmp/backup1.tar.gz";

    EXPECT_TRUE(queue.Enqueue(op1));
    EXPECT_TRUE(queue.HasPendingOperations());

    auto loaded = queue.LoadAll();
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_EQ(loaded[0].id, "op_test_1");
    EXPECT_EQ(loaded[0].appId, 480u);
    EXPECT_EQ(loaded[0].accountId, 12345u);
    EXPECT_EQ(loaded[0].type, PendingOpType::ResetCloudSave);
    EXPECT_EQ(loaded[0].status, PendingOpStatus::Pending);
    EXPECT_EQ(loaded[0].backupPath, "/tmp/backup1.tar.gz");

    // Update status to Completed
    EXPECT_TRUE(queue.UpdateStatus("op_test_1", PendingOpStatus::Completed));
    EXPECT_FALSE(queue.HasPendingOperations());

    loaded = queue.LoadAll();
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_EQ(loaded[0].status, PendingOpStatus::Completed);

    // Enqueue operation 2
    PendingOperation_t op2;
    op2.id = "op_test_2";
    op2.appId = 730;
    op2.accountId = 12345;
    op2.type = PendingOpType::RestoreBackup;
    op2.status = PendingOpStatus::Pending;
    op2.timestamp = static_cast<uint64_t>(std::time(nullptr));
    op2.restoreSourcePath = "/tmp/restore2.tar.gz";

    EXPECT_TRUE(queue.Enqueue(op2));
    EXPECT_TRUE(queue.HasPendingOperations());

    loaded = queue.LoadAll();
    EXPECT_EQ(loaded.size(), 2u);

    // Prune completed (op1 timestamp is 1000, so older than maxAge)
    EXPECT_TRUE(queue.PruneCompleted(60));
    loaded = queue.LoadAll();
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_EQ(loaded[0].id, "op_test_2");

    queue.SetQueueFilePathForTest({});
}

TEST(CloudPendingOpsTest, AssertSteamIdleProcDetection)
{
    ScopedPendingOpsTestEnv env;
    fs::path mockProc = env.tempDir / "proc";
    std::error_code ec;
    fs::create_directories(mockProc, ec);

    CPendingOpsExecutor executor;
    executor.SetProcPathForTest(mockProc);

    // Empty proc: idle
    EXPECT_TRUE(executor.AssertSteamIdle());

    // Non-steam process (PID 100, comm="bash")
    fs::path pid100 = mockProc / "100";
    fs::create_directories(pid100, ec);
    std::ofstream(pid100 / "comm") << "bash\n";
    EXPECT_TRUE(executor.AssertSteamIdle());

    // Steam process running (PID 101, comm="steam")
    fs::path pid101 = mockProc / "101";
    fs::create_directories(pid101, ec);
    std::ofstream(pid101 / "comm") << "steam\n";
    EXPECT_FALSE(executor.AssertSteamIdle());

    // Clean up PID 101, add steamwebhelper (PID 102)
    fs::remove_all(pid101, ec);
    EXPECT_TRUE(executor.AssertSteamIdle());

    fs::path pid102 = mockProc / "102";
    fs::create_directories(pid102, ec);
    std::ofstream(pid102 / "comm") << "steamwebhelper\n";
    EXPECT_FALSE(executor.AssertSteamIdle());

    fs::remove_all(pid102, ec);
    EXPECT_TRUE(executor.AssertSteamIdle());
}

TEST(CloudPendingOpsTest, GranularResetPreservesProtectedDataAndResetsCnDat)
{
    ScopedPendingOpsTestEnv env;
    fs::path mockSteam = env.tempDir / "Steam";
    fs::path mockBackup = env.tempDir / "backups";
    std::error_code ec;

    uint32_t accountId = 98765;
    AppId_t appId = 480;

    // Create redirect storage app directory
    fs::path storageApp = mockSteam / "cloud_redirect" / "storage" / std::to_string(accountId) / std::to_string(appId);
    fs::create_directories(storageApp, ec);
    std::ofstream(storageApp / "save.dat") << "redirect save data";
    std::ofstream(storageApp / "cn.dat") << "42\n";

    // Create userdata app directories
    fs::path userdataApp = mockSteam / "userdata" / std::to_string(accountId) / std::to_string(appId);
    fs::path remoteDir = userdataApp / "remote";
    fs::path gamerecordingsDir = userdataApp / "gamerecordings";
    fs::create_directories(remoteDir, ec);
    fs::create_directories(gamerecordingsDir, ec);

    std::ofstream(remoteDir / "cloud_save.dat") << "remote save data";
    std::ofstream(userdataApp / "remotecache.vdf") << "\"remotecache\" { \"file\" \"save.dat\" }";
    std::ofstream(userdataApp / "localconfig.vdf") << "\"localconfig\" { \"UserSetting\" \"1\" }";
    std::ofstream(gamerecordingsDir / "clip.mp4") << "fake clip data";

    CPendingOpsExecutor executor(mockSteam);
    executor.SetBackupDirForTest(mockBackup);

    PendingOperation_t op;
    op.id = "op_reset_test";
    op.appId = appId;
    op.accountId = accountId;
    op.type = PendingOpType::ResetCloudSave;
    op.status = PendingOpStatus::Pending;
    op.timestamp = static_cast<uint64_t>(std::time(nullptr));

    EXPECT_TRUE(executor.ExecuteOperation(op));
    EXPECT_EQ(op.status, PendingOpStatus::Completed);

    // 1. Verify pre-operation backup tarball was created
    EXPECT_FALSE(op.backupPath.empty());
    EXPECT_TRUE(fs::exists(op.backupPath, ec));

    // 2. Verify scoped targets were wiped
    EXPECT_FALSE(fs::exists(storageApp / "save.dat", ec));
    EXPECT_FALSE(fs::exists(remoteDir / "cloud_save.dat", ec));
    EXPECT_FALSE(fs::exists(userdataApp / "remotecache.vdf", ec));

    // 3. Verify cn.dat was reset to 0
    EXPECT_TRUE(fs::exists(storageApp / "cn.dat", ec));
    std::ifstream cnIfs(storageApp / "cn.dat");
    std::string cnVal;
    cnIfs >> cnVal;
    EXPECT_EQ(cnVal, "0");

    // 4. CRITICAL INVARIANT: localconfig.vdf and gamerecordings/ preserved intact!
    EXPECT_TRUE(fs::exists(userdataApp / "localconfig.vdf", ec));
    std::ifstream lcIfs(userdataApp / "localconfig.vdf");
    std::stringstream lcBuf;
    lcBuf << lcIfs.rdbuf();
    EXPECT_NE(lcBuf.str().find("UserSetting"), std::string::npos);

    EXPECT_TRUE(fs::exists(gamerecordingsDir / "clip.mp4", ec));
}

TEST(CloudPendingOpsTest, RollbackOnFailureRestoresOriginalState)
{
    ScopedPendingOpsTestEnv env;
    fs::path mockSteam = env.tempDir / "Steam";
    fs::path mockBackup = env.tempDir / "backups";
    std::error_code ec;

    uint32_t accountId = 11111;
    AppId_t appId = 480;

    fs::path storageApp = mockSteam / "cloud_redirect" / "storage" / std::to_string(accountId) / std::to_string(appId);
    fs::create_directories(storageApp, ec);
    std::ofstream(storageApp / "important_save.dat") << "critical save file";

    CPendingOpsExecutor executor(mockSteam);
    executor.SetBackupDirForTest(mockBackup);
    executor.SetSimulateFailureForTest(true); // Trigger simulated failure & rollback

    PendingOperation_t op;
    op.id = "op_fail_test";
    op.appId = appId;
    op.accountId = accountId;
    op.type = PendingOpType::ResetCloudSave;
    op.status = PendingOpStatus::Pending;
    op.timestamp = static_cast<uint64_t>(std::time(nullptr));

    EXPECT_FALSE(executor.ExecuteOperation(op));
    EXPECT_EQ(op.status, PendingOpStatus::RolledBack);

    // Verify backup existed and original file is restored
    EXPECT_TRUE(fs::exists(storageApp / "important_save.dat", ec));
    std::ifstream ifs(storageApp / "important_save.dat");
    std::string content;
    std::getline(ifs, content);
    EXPECT_EQ(content, "critical save file");
}
