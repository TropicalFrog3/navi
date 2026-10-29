#include <gtest/gtest.h>

#include "achievement_merger.hpp"
#include "atomic_file.hpp"
#include "paths.hpp"
#include "process_lock.hpp"
#include "sync_engine.hpp"
#include "sync_manifest.hpp"
#include "utils.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace
{
    std::string sha256Of(const std::vector<uint8_t>& bytes)
    {
        return Sync::computeBufferSha256Hex(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    }

    std::string sha256Of(const std::string& str)
    {
        return Sync::computeBufferSha256Hex(std::string_view(str.data(), str.size()));
    }
}

class SyncEngineAndMergeTest : public ::testing::Test
{
protected:
    std::string m_testDir;
    std::string m_configDir;
    std::string m_steamRoot;
    std::string m_procRoot;
    const uint64_t m_steamId64 = 76561198000000001ULL;
    const uint32_t m_steam32 = static_cast<uint32_t>(76561198000000001ULL & 0xFFFFFFFFULL);

    void SetUp() override
    {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        m_testDir = "/tmp/unit_test_sync_engine_" + std::to_string(getpid()) + "_" + std::to_string(now);
        m_configDir = m_testDir + "/config";
        m_steamRoot = m_testDir + "/steam";
        m_procRoot = m_testDir + "/proc";

        fs::remove_all(m_testDir);
        fs::create_directories(m_configDir);
        fs::create_directories(m_steamRoot + "/steamapps");
        fs::create_directories(m_procRoot);

        Sync::GameProcessLock::clearAllHookedRunningApps();
    }

    void TearDown() override
    {
        Sync::GameProcessLock::clearAllHookedRunningApps();
        fs::remove_all(m_testDir);
    }
};

// ── Test 1: Achievement Set Union (A ∪ B) & SyncEngine Merge ─────────────────

TEST_F(SyncEngineAndMergeTest, AchievementSetUnionMerger)
{
    // Device A state
    Sync::AchievementStatBlob_t blobA;
    blobA.appId = 440;
    blobA.achievements["Ach_1"] = {"Ach_1", true, 1000};
    blobA.achievements["Ach_2"] = {"Ach_2", true, 2000};
    blobA.achievements["Ach_3"] = {"Ach_3", false, 0};
    blobA.intStats["kills"] = 50;
    const auto bufA = Sync::serializeAchievementBinaryKV(blobA);

    // Device B state
    Sync::AchievementStatBlob_t blobB;
    blobB.appId = 440;
    blobB.achievements["Ach_1"] = {"Ach_1", false, 0};
    blobB.achievements["Ach_2"] = {"Ach_2", true, 1500};
    blobB.achievements["Ach_4"] = {"Ach_4", true, 3000};
    blobB.intStats["kills"] = 85;
    const auto bufB = Sync::serializeAchievementBinaryKV(blobB);

    // 1. Direct buffer merge test
    std::vector<uint8_t> mergedBuf;
    EXPECT_TRUE(Sync::mergeAchievementBuffersUnion(bufA, bufB, mergedBuf));

    Sync::AchievementStatBlob_t mergedBlob;
    EXPECT_TRUE(Sync::parseAchievementBinaryKV(mergedBuf, mergedBlob));
    EXPECT_EQ(mergedBlob.appId, 440u);

    EXPECT_TRUE(mergedBlob.achievements["Ach_1"].unlocked);
    EXPECT_EQ(mergedBlob.achievements["Ach_1"].unlockTimestamp, 1000u);

    EXPECT_TRUE(mergedBlob.achievements["Ach_2"].unlocked);
    EXPECT_EQ(mergedBlob.achievements["Ach_2"].unlockTimestamp, 1500u);

    EXPECT_FALSE(mergedBlob.achievements["Ach_3"].unlocked);
    EXPECT_EQ(mergedBlob.achievements["Ach_3"].unlockTimestamp, 0u);

    EXPECT_TRUE(mergedBlob.achievements["Ach_4"].unlocked);
    EXPECT_EQ(mergedBlob.achievements["Ach_4"].unlockTimestamp, 3000u);

    EXPECT_EQ(mergedBlob.intStats["kills"], 85);

    // 2. SyncEngine integration test
    const fs::path localAchPath = fs::path(m_steamRoot) / "userdata" / std::to_string(m_steam32) / "440" / "remote" / "UserGameStats.bin";
    fs::create_directories(localAchPath.parent_path());
    std::string_view svA(reinterpret_cast<const char*>(bufA.data()), bufA.size());
    EXPECT_TRUE(AtomicFile::write(localAchPath.string(), svA, true));

    Sync::SyncEngine engine(m_configDir, m_steamRoot, m_procRoot);

    Sync::SyncStateManifest_t remoteManifest;
    remoteManifest.steamId64 = m_steamId64;

    Sync::SyncFileEntry_t entry;
    entry.category = Sync::ESyncCategory::k_Achievements;
    entry.appId = 440;
    entry.logicalPath = "userdata://" + std::to_string(m_steam32) + "/440/remote/UserGameStats.bin";
    entry.sizeBytes = bufB.size();
    entry.sha256Hex = sha256Of(bufB);
    remoteManifest.entries.push_back(entry);

    std::unordered_map<std::string, std::vector<uint8_t>> payloads;
    payloads[entry.logicalPath] = bufB;

    Sync::SyncSelectionFilter_t filter;
    filter.allAchievements = true;

    Sync::SyncApplyReport_t report;
    const auto status = engine.applyRemoteSyncPackage(m_steamId64, remoteManifest, payloads, filter, {}, report);
    EXPECT_EQ(status, Sync::ESyncApplyStatus::k_Ok);

    // Verify written file on disk contains union
    std::ifstream resFile(localAchPath, std::ios::binary);
    std::vector<uint8_t> onDiskBytes((std::istreambuf_iterator<char>(resFile)), std::istreambuf_iterator<char>());
    resFile.close();

    Sync::AchievementStatBlob_t onDiskBlob;
    EXPECT_TRUE(Sync::parseAchievementBinaryKV(onDiskBytes, onDiskBlob));
    EXPECT_TRUE(onDiskBlob.achievements["Ach_1"].unlocked);
    EXPECT_EQ(onDiskBlob.achievements["Ach_1"].unlockTimestamp, 1000u);
    EXPECT_TRUE(onDiskBlob.achievements["Ach_2"].unlocked);
    EXPECT_EQ(onDiskBlob.achievements["Ach_2"].unlockTimestamp, 1500u);
    EXPECT_FALSE(onDiskBlob.achievements["Ach_3"].unlocked);
    EXPECT_TRUE(onDiskBlob.achievements["Ach_4"].unlocked);
    EXPECT_EQ(onDiskBlob.achievements["Ach_4"].unlockTimestamp, 3000u);
    EXPECT_EQ(onDiskBlob.intStats["kills"], 85);
}

// ── Test 2: Cloud Save 3-Way Conflict Detection & Resolution ─────────────────

TEST_F(SyncEngineAndMergeTest, CloudSaveConflictResolution)
{
    const fs::path savePath = fs::path(m_steamRoot) / "userdata" / std::to_string(m_steam32) / "400" / "remote" / "save.dat";
    fs::create_directories(savePath.parent_path());

    const std::string localContent = "LocalModData_V1";
    EXPECT_TRUE(AtomicFile::write(savePath.string(), localContent, true));

    const std::string remoteContent = "RemoteModData_V2";
    const std::vector<uint8_t> remotePayload(remoteContent.begin(), remoteContent.end());

    Sync::SyncEngine engine(m_configDir, m_steamRoot, m_procRoot);

    Sync::SyncStateManifest_t remoteManifest;
    remoteManifest.steamId64 = m_steamId64;

    Sync::SyncFileEntry_t entry;
    entry.category = Sync::ESyncCategory::k_CloudSaves;
    entry.appId = 400;
    entry.logicalPath = "userdata://" + std::to_string(m_steam32) + "/400/remote/save.dat";
    entry.sizeBytes = remotePayload.size();
    entry.sha256Hex = sha256Of(remotePayload);
    entry.mtimeUnixSec = 1710000000;
    remoteManifest.entries.push_back(entry);

    std::unordered_map<std::string, std::vector<uint8_t>> payloads;
    payloads[entry.logicalPath] = remotePayload;

    Sync::SyncSelectionFilter_t filter;
    filter.allCloudSaves = true;

    // First call: empty user conflict choices -> expects conflict reported
    Sync::SyncApplyReport_t report1;
    const auto status1 = engine.applyRemoteSyncPackage(m_steamId64, remoteManifest, payloads, filter, {}, report1);
    EXPECT_EQ(status1, Sync::ESyncApplyStatus::k_ConflictPendingUserChoice);
    ASSERT_EQ(report1.conflicts.size(), 1u);
    EXPECT_EQ(report1.conflicts[0].appId, 400u);
    EXPECT_EQ(report1.conflicts[0].logicalPath, entry.logicalPath);

    // Verify local file on disk was not modified
    std::ifstream f1(savePath);
    std::string s1((std::istreambuf_iterator<char>(f1)), std::istreambuf_iterator<char>());
    EXPECT_EQ(s1, localContent);

    // Second call: user chooses k_BackupLocalAndUseRemote
    std::unordered_map<std::string, Sync::EConflictChoice> choices;
    choices[entry.logicalPath] = Sync::EConflictChoice::k_BackupLocalAndUseRemote;

    Sync::SyncApplyReport_t report2;
    const auto status2 = engine.applyRemoteSyncPackage(m_steamId64, remoteManifest, payloads, filter, choices, report2);
    EXPECT_EQ(status2, Sync::ESyncApplyStatus::k_Ok);

    // Verify local file was overwritten with remote content
    std::ifstream f2(savePath);
    std::string s2((std::istreambuf_iterator<char>(f2)), std::istreambuf_iterator<char>());
    EXPECT_EQ(s2, remoteContent);

    // Verify local conflict archive copy was preserved
    const fs::path conflictBakPath = fs::path(m_configDir) / "sync_backups" / std::to_string(m_steamId64) / "conflicts" / "400_save.dat.conflict_bak";
    EXPECT_TRUE(fs::exists(conflictBakPath));
    std::ifstream fBak(conflictBakPath);
    std::string sBak((std::istreambuf_iterator<char>(fBak)), std::istreambuf_iterator<char>());
    EXPECT_EQ(sBak, localContent);
}

// ── Test 3: Active Game Process Lock & Deferred Queue Drain ──────────────────

TEST_F(SyncEngineAndMergeTest, ProcessLockAndDeferredDrain)
{
    // Mock /proc/9876 with SteamAppId=1091500
    const fs::path mockPidDir = fs::path(m_procRoot) / "9876";
    fs::create_directories(mockPidDir);
    const std::string envContent = "SteamAppId=1091500";
    std::ofstream envFile(mockPidDir / "environ", std::ios::binary);
    envFile.write(envContent.data(), envContent.size());
    envFile.put('\0');
    envFile.close();

    // Verify GameProcessLock detects it
    EXPECT_TRUE(Sync::GameProcessLock::isAppRunning(1091500, m_procRoot, m_steamRoot));
    EXPECT_FALSE(Sync::GameProcessLock::isAppRunning(400, m_procRoot, m_steamRoot));

    // Also test hooked apps
    Sync::GameProcessLock::setAppRunningFromHook(730, true);
    EXPECT_TRUE(Sync::GameProcessLock::isAppRunning(730, m_procRoot, m_steamRoot));
    Sync::GameProcessLock::clearAllHookedRunningApps();
    EXPECT_FALSE(Sync::GameProcessLock::isAppRunning(730, m_procRoot, m_steamRoot));

    Sync::SyncEngine engine(m_configDir, m_steamRoot, m_procRoot);

    Sync::SyncStateManifest_t remoteManifest;
    remoteManifest.steamId64 = m_steamId64;

    // Entry 1: App 1091500 (running)
    Sync::SyncFileEntry_t eRunning;
    eRunning.category = Sync::ESyncCategory::k_CloudSaves;
    eRunning.appId = 1091500;
    eRunning.logicalPath = "userdata://" + std::to_string(m_steam32) + "/1091500/remote/save.dat";
    const std::string payload1091500 = "CP2077_Payload";
    eRunning.sizeBytes = payload1091500.size();
    eRunning.sha256Hex = sha256Of(payload1091500);
    remoteManifest.entries.push_back(eRunning);

    // Entry 2: App 400 (not running)
    Sync::SyncFileEntry_t eNotRunning;
    eNotRunning.category = Sync::ESyncCategory::k_CloudSaves;
    eNotRunning.appId = 400;
    eNotRunning.logicalPath = "userdata://" + std::to_string(m_steam32) + "/400/remote/save.dat";
    const std::string payload400 = "Portal_Payload";
    eNotRunning.sizeBytes = payload400.size();
    eNotRunning.sha256Hex = sha256Of(payload400);
    remoteManifest.entries.push_back(eNotRunning);

    std::unordered_map<std::string, std::vector<uint8_t>> payloads;
    payloads[eRunning.logicalPath] = std::vector<uint8_t>(payload1091500.begin(), payload1091500.end());
    payloads[eNotRunning.logicalPath] = std::vector<uint8_t>(payload400.begin(), payload400.end());

    Sync::SyncSelectionFilter_t filter;
    filter.allCloudSaves = true;

    Sync::SyncApplyReport_t report;
    const auto status = engine.applyRemoteSyncPackage(m_steamId64, remoteManifest, payloads, filter, {}, report);
    EXPECT_EQ(status, Sync::ESyncApplyStatus::k_PartialDeferredOrConflict);
    ASSERT_EQ(report.deferredAppIds.size(), 1u);
    EXPECT_EQ(report.deferredAppIds[0], 1091500u);

    // App 400 was applied
    const fs::path path400 = fs::path(m_steamRoot) / "userdata" / std::to_string(m_steam32) / "400" / "remote" / "save.dat";
    EXPECT_TRUE(fs::exists(path400));

    // App 1091500 was deferred and not written
    const fs::path path1091500 = fs::path(m_steamRoot) / "userdata" / std::to_string(m_steam32) / "1091500" / "remote" / "save.dat";
    EXPECT_FALSE(fs::exists(path1091500));
    EXPECT_TRUE(engine.isAppDeferred(1091500));

    // Simulate game exit: delete mock proc dir
    fs::remove_all(mockPidDir);
    EXPECT_FALSE(Sync::GameProcessLock::isAppRunning(1091500, m_procRoot, m_steamRoot));

    // Drain deferred games
    const auto drainReport = engine.drainDeferredGamesIfExited(m_steamId64);
    EXPECT_EQ(drainReport.status, Sync::ESyncApplyStatus::k_Ok);
    EXPECT_FALSE(engine.isAppDeferred(1091500));
    EXPECT_TRUE(engine.getDeferredQueue().empty());

    // Verify App 1091500 file is now written
    EXPECT_TRUE(fs::exists(path1091500));
    std::ifstream fDrain(path1091500);
    std::string sDrain((std::istreambuf_iterator<char>(fDrain)), std::istreambuf_iterator<char>());
    EXPECT_EQ(sDrain, payload1091500);
}

// ── Test 4: Steam ACF Cache Coherence ────────────────────────────────────────

TEST_F(SyncEngineAndMergeTest, SteamAcfCacheCoherence)
{
    Sync::SyncEngine engine(m_configDir, m_steamRoot, m_procRoot);

    // Case A: New game .acf writes directly
    const std::string newAcfContent = "\"AppState\"\n{\n\t\"appid\"\t\"400\"\n\t\"buildid\"\t\"10\"\n}\n";
    const std::vector<uint8_t> newAcfBytes(newAcfContent.begin(), newAcfContent.end());

    Sync::SyncStateManifest_t manifestA;
    manifestA.steamId64 = m_steamId64;
    Sync::SyncFileEntry_t eNewAcf;
    eNewAcf.category = Sync::ESyncCategory::k_ManifestsAndLua;
    eNewAcf.appId = 400;
    eNewAcf.logicalPath = "steamapps://appmanifest_400.acf";
    eNewAcf.sizeBytes = newAcfBytes.size();
    eNewAcf.sha256Hex = sha256Of(newAcfBytes);
    manifestA.entries.push_back(eNewAcf);

    std::unordered_map<std::string, std::vector<uint8_t>> payloadsA;
    payloadsA[eNewAcf.logicalPath] = newAcfBytes;

    Sync::SyncSelectionFilter_t filter;
    filter.allManifestsAndLua = true;

    Sync::SyncApplyReport_t reportA;
    EXPECT_EQ(engine.applyRemoteSyncPackage(m_steamId64, manifestA, payloadsA, filter, {}, reportA), Sync::ESyncApplyStatus::k_Ok);
    EXPECT_TRUE(reportA.stagedAcfRestartRequiredAppIds.empty());

    const fs::path liveAcf400 = fs::path(m_steamRoot) / "steamapps" / "appmanifest_400.acf";
    EXPECT_TRUE(fs::exists(liveAcf400));

    // Case B: Existing game with changed buildid stages for restart
    const fs::path liveAcf1091500 = fs::path(m_steamRoot) / "steamapps" / "appmanifest_1091500.acf";
    const std::string oldAcfContent = "\"AppState\"\n{\n\t\"appid\"\t\"1091500\"\n\t\"buildid\"\t\"100\"\n}\n";
    EXPECT_TRUE(AtomicFile::write(liveAcf1091500.string(), oldAcfContent, false));

    const std::string incomingAcfContent = "\"AppState\"\n{\n\t\"appid\"\t\"1091500\"\n\t\"buildid\"\t\"200\"\n}\n";
    const std::vector<uint8_t> incomingAcfBytes(incomingAcfContent.begin(), incomingAcfContent.end());

    Sync::SyncStateManifest_t manifestB;
    manifestB.steamId64 = m_steamId64;
    Sync::SyncFileEntry_t eExistingAcf;
    eExistingAcf.category = Sync::ESyncCategory::k_ManifestsAndLua;
    eExistingAcf.appId = 1091500;
    eExistingAcf.logicalPath = "steamapps://appmanifest_1091500.acf";
    eExistingAcf.sizeBytes = incomingAcfBytes.size();
    eExistingAcf.sha256Hex = sha256Of(incomingAcfBytes);
    manifestB.entries.push_back(eExistingAcf);

    std::unordered_map<std::string, std::vector<uint8_t>> payloadsB;
    payloadsB[eExistingAcf.logicalPath] = incomingAcfBytes;

    Sync::SyncApplyReport_t reportB;
    EXPECT_EQ(engine.applyRemoteSyncPackage(m_steamId64, manifestB, payloadsB, filter, {}, reportB), Sync::ESyncApplyStatus::k_Ok);
    ASSERT_EQ(reportB.stagedAcfRestartRequiredAppIds.size(), 1u);
    EXPECT_EQ(reportB.stagedAcfRestartRequiredAppIds[0], 1091500u);

    // Live file was NOT modified yet
    std::ifstream liveF(liveAcf1091500);
    std::string liveRead((std::istreambuf_iterator<char>(liveF)), std::istreambuf_iterator<char>());
    EXPECT_EQ(liveRead, oldAcfContent);

    // Staged file was created in pending_acf/
    const fs::path pendingAcf = fs::path(m_configDir) / "pending_acf" / "appmanifest_1091500.acf";
    EXPECT_TRUE(fs::exists(pendingAcf));

    // Commit staged files on shutdown
    EXPECT_TRUE(Sync::commitStagedAcfFilesOnShutdown(m_configDir, m_steamRoot));

    // Now live file has incoming content and staged file is removed
    EXPECT_FALSE(fs::exists(pendingAcf));
    std::ifstream committedF(liveAcf1091500);
    std::string committedRead((std::istreambuf_iterator<char>(committedF)), std::istreambuf_iterator<char>());
    EXPECT_EQ(committedRead, incomingAcfContent);
}

// ── Test 5: Automatic .bak Rollback on Failure & Account Mismatch ─────────────

TEST_F(SyncEngineAndMergeTest, RollbackAndAccountMismatchGuard)
{
    Sync::SyncEngine engine(m_configDir, m_steamRoot, m_procRoot);

    // Case A: SteamId mismatch rejection
    Sync::SyncStateManifest_t badManifest;
    badManifest.steamId64 = 76561198999999999ULL;

    Sync::SyncApplyReport_t reportA;
    EXPECT_EQ(engine.applyRemoteSyncPackage(m_steamId64, badManifest, {}, {}, {}, reportA), Sync::ESyncApplyStatus::k_SteamIdMismatch);

    // Case B: Simulated mid-sync failure triggers snapshot rollback
    const fs::path targetSave = fs::path(m_steamRoot) / "userdata" / std::to_string(m_steam32) / "400" / "remote" / "state.dat";
    fs::create_directories(targetSave.parent_path());
    const std::string originalData = "UncorruptedOriginalData_400";
    EXPECT_TRUE(AtomicFile::write(targetSave.string(), originalData, true));

    Sync::SyncStateManifest_t manifestB;
    manifestB.steamId64 = m_steamId64;

    Sync::SyncFileEntry_t entry;
    entry.category = Sync::ESyncCategory::k_CloudSaves;
    entry.appId = 400;
    entry.logicalPath = "userdata://" + std::to_string(m_steam32) + "/400/remote/state.dat";
    const std::string badData = "CorruptedIncomingData";
    entry.sizeBytes = badData.size();
    entry.sha256Hex = sha256Of(badData);
    manifestB.entries.push_back(entry);

    std::unordered_map<std::string, std::vector<uint8_t>> payloads;
    payloads[entry.logicalPath] = std::vector<uint8_t>(badData.begin(), badData.end());

    Sync::SyncSelectionFilter_t filter;
    filter.allCloudSaves = true;

    // Enable write failure simulation
    engine.setSimulateWriteFailure(true);

    Sync::SyncApplyReport_t reportB;
    std::unordered_map<std::string, Sync::EConflictChoice> choices;
    choices[entry.logicalPath] = Sync::EConflictChoice::k_KeepRemote;

    const auto status = engine.applyRemoteSyncPackage(m_steamId64, manifestB, payloads, filter, choices, reportB);
    EXPECT_EQ(status, Sync::ESyncApplyStatus::k_IoError);

    // Verify original file on disk is byte-for-byte restored
    EXPECT_TRUE(fs::exists(targetSave));
    std::ifstream resF(targetSave);
    std::string resStr((std::istreambuf_iterator<char>(resF)), std::istreambuf_iterator<char>());
    EXPECT_EQ(resStr, originalData);
}
