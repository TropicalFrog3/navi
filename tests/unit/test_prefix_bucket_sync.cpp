#include <gtest/gtest.h>

#include "cloud_manager.hpp"
#include "config.hpp"
#include "prefix_bucket_sync.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

namespace
{
    struct ScopedPrefixSyncTestEnv
    {
        fs::path tempDir;
        fs::path cloudRoot;
        fs::path stateDir;
        fs::path backupDir;
        fs::path steamappsDir;
        bool prevCloudEnabled = false;
        std::string prevProvider;
        bool prevPrefixEnabled = false;
        bool prevAtStartUp = false;
        bool prevOnLaunch = false;
        bool prevOnExit = false;
        unsigned int prevMaxFileMb = 64;

        ScopedPrefixSyncTestEnv()
            : prevCloudEnabled(g_config.cloudSavesEnabled.get()),
              prevProvider(g_config.cloudSavesProvider.get()),
              prevPrefixEnabled(g_config.cloudPrefixSyncEnabled.get()),
              prevAtStartUp(g_config.cloudPrefixSyncAtStartUp.get()),
              prevOnLaunch(g_config.cloudPrefixSyncOnLaunch.get()),
              prevOnExit(g_config.cloudPrefixSyncOnExit.get()),
              prevMaxFileMb(g_config.cloudPrefixSyncMaxFileMb.get())
        {
            tempDir = fs::path(
                std::string("/tmp/test_prefix_bucket_sync_") + std::to_string(getpid()) + "_" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            cloudRoot = tempDir / "CloudRedirect";
            stateDir = tempDir / "navi_state";
            backupDir = tempDir / "cloud_backups";
            steamappsDir = tempDir / "steamapps";

            std::error_code ec;
            fs::create_directories(cloudRoot, ec);
            fs::create_directories(stateDir, ec);
            fs::create_directories(backupDir, ec);
            fs::create_directories(steamappsDir, ec);

            PrefixBucketSync::ResetForTest();
            PrefixBucketSync::SetCloudRootForTest(cloudRoot);
            PrefixBucketSync::SetStateDirForTest(stateDir);
            PrefixBucketSync::SetBackupDirForTest(backupDir);
            PrefixBucketSync::SetBucketDiscoverForTestEnabled(false);

            g_config.cloudSavesEnabled = true;
            g_config.cloudSavesProvider = std::string("gdrive");
            g_config.cloudPrefixSyncEnabled = true;
            g_config.cloudPrefixSyncAtStartUp = true;
            g_config.cloudPrefixSyncOnLaunch = true;
            g_config.cloudPrefixSyncOnExit = true;
            g_config.cloudPrefixSyncMaxFileMb = 64u;

            // Seed a non-empty OAuth token file so providerConfigured() returns true
            writeFile(cloudRoot / "tokens_gdrive.json", "{\"refresh_token\":\"test_token\"}");
        }

        ~ScopedPrefixSyncTestEnv()
        {
            PrefixBucketSync::ResetForTest();
            g_config.cloudSavesEnabled = prevCloudEnabled;
            g_config.cloudSavesProvider = prevProvider;
            g_config.cloudPrefixSyncEnabled = prevPrefixEnabled;
            g_config.cloudPrefixSyncAtStartUp = prevAtStartUp;
            g_config.cloudPrefixSyncOnLaunch = prevOnLaunch;
            g_config.cloudPrefixSyncOnExit = prevOnExit;
            g_config.cloudPrefixSyncMaxFileMb = prevMaxFileMb;
            std::error_code ec;
            fs::remove_all(tempDir, ec);
        }

        static void writeFile(const fs::path& p, const std::string& content)
        {
            std::error_code ec;
            fs::create_directories(p.parent_path(), ec);
            std::ofstream ofs(p, std::ios::binary | std::ios::trunc);
            ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
        }

        static std::string readFile(const fs::path& p)
        {
            std::ifstream ifs(p, std::ios::binary);
            std::ostringstream ss;
            ss << ifs.rdbuf();
            return ss.str();
        }

        static uint32_t randomAppId(uint32_t seedOffset = 0)
        {
            std::mt19937 rng(static_cast<uint32_t>(
                std::chrono::steady_clock::now().time_since_epoch().count()) + seedOffset);
            std::uniform_int_distribution<uint32_t> dist(100000, 999999);
            return dist(rng);
        }
    };
}

TEST(PrefixBucketSyncTest, DiscoverBucketsAcrossUsersAndAppDataRoots)
{
    ScopedPrefixSyncTestEnv env;
    const uint32_t appA = ScopedPrefixSyncTestEnv::randomAppId(1);
    const uint32_t appB = appA + 7;
    const uint32_t appC = appA + 19;

    // 1. Standard steamuser + AppData/Roaming/GSE Saves/<appA>/remote
    fs::path pfxA = env.steamappsDir / "compatdata" / std::to_string(appA) / "pfx";
    fs::path remoteA = pfxA / "drive_c" / "users" / "steamuser" / "AppData" / "Roaming" /
                       "GSE Saves" / std::to_string(appA) / "remote";
    ScopedPrefixSyncTestEnv::writeFile(remoteA / "persistentgamedata1.dat", "isaac_save_payload");

    // System pseudo-user must be ignored even if it has matching directories
    fs::path publicIgnored = pfxA / "drive_c" / "users" / "Public" / "AppData" / "Roaming" /
                             "GSE Saves" / std::to_string(appA) / "remote";
    ScopedPrefixSyncTestEnv::writeFile(publicIgnored / "ignored.dat", "ignore_me");

    // 2. Custom Proton username + AppData/Local/Goldberg SteamEmu Saves/<appB>/remote
    fs::path pfxB = env.steamappsDir / "compatdata" / std::to_string(appB) / "pfx";
    fs::path remoteB = pfxB / "drive_c" / "users" / "customdeckuser" / "AppData" / "Local" /
                       "Goldberg SteamEmu Saves" / std::to_string(appB) / "remote";
    ScopedPrefixSyncTestEnv::writeFile(remoteB / "slot0" / "world.sav", "world_data");

    // 3. AppData/LocalLow/<CustomEmu>/<appC> direct files without a remote/ child
    fs::path pfxC = env.steamappsDir / "compatdata" / std::to_string(appC) / "pfx";
    fs::path directC = pfxC / "drive_c" / "users" / "steamuser" / "AppData" / "LocalLow" /
                       "AnyEmuWrapper" / std::to_string(appC);
    ScopedPrefixSyncTestEnv::writeFile(directC / "profile.bin", "profile_bytes");
    ScopedPrefixSyncTestEnv::writeFile(directC / "steam_autocloud.vdf", "metadata_only");

    auto buckets = PrefixBucketSync::DiscoverPrefixBuckets(
        {env.steamappsDir}, {appA, appB, appC});

    ASSERT_EQ(buckets.size(), 3u);

    bool foundA = false;
    bool foundB = false;
    bool foundC = false;
    for (const auto& b : buckets)
    {
        if (b.appId == appA)
        {
            foundA = true;
            EXPECT_STREQ(b.engineRootToken(), "%WinAppDataRoaming%");
            EXPECT_EQ(b.tailDirs, "GSE Saves/" + std::to_string(appA) + "/remote");
            EXPECT_EQ(b.cleanPathForRel("persistentgamedata1.dat"),
                      "GSE Saves/" + std::to_string(appA) + "/remote/persistentgamedata1.dat");
        }
        else if (b.appId == appB)
        {
            foundB = true;
            EXPECT_STREQ(b.engineRootToken(), "%WinAppDataLocal%");
            EXPECT_EQ(b.tailDirs, "Goldberg SteamEmu Saves/" + std::to_string(appB) + "/remote");
        }
        else if (b.appId == appC)
        {
            foundC = true;
            EXPECT_STREQ(b.engineRootToken(), "%WinAppDataLocalLow%");
            EXPECT_EQ(b.tailDirs, "AnyEmuWrapper/" + std::to_string(appC));
        }
    }
    EXPECT_TRUE(foundA);
    EXPECT_TRUE(foundB);
    EXPECT_TRUE(foundC);
}

TEST(PrefixBucketSyncTest, PushMirrorsStorageSeedsMetadataAndRollsBackCnOnFailure)
{
    ScopedPrefixSyncTestEnv env;
    const uint32_t accountId = 987654321u;
    const uint32_t appId = ScopedPrefixSyncTestEnv::randomAppId(42);
    PrefixBucketSync::SetAccountIdForTest(accountId);

    fs::path pfx = env.steamappsDir / "compatdata" / std::to_string(appId) / "pfx";
    fs::path userDir = pfx / "drive_c" / "users" / "steamuser";
    fs::path remoteDir = userDir / "AppData" / "Roaming" / "GSE Saves" / std::to_string(appId) / "remote";
    ScopedPrefixSyncTestEnv::writeFile(remoteDir / "save1.dat", "save_content_v1");

    PrefixBucketSync::PrefixBucket_t bucket;
    bucket.appId = appId;
    bucket.pfxDir = pfx;
    bucket.userDir = userDir;
    bucket.remoteDir = remoteDir;
    bucket.appDataSubName = "AppData/Roaming";
    bucket.tailDirs = "GSE Saves/" + std::to_string(appId) + "/remote";
    PrefixBucketSync::SetBucketsForTest({bucket});

    // Seed a stray blobs/ directory to verify pushBucket cleans it before publish-full-manifest
    fs::path appStorage = env.cloudRoot / "storage" / std::to_string(accountId) / std::to_string(appId);
    ScopedPrefixSyncTestEnv::writeFile(appStorage / "blobs" / "deadbeef", "stale_cas_blob");

    std::vector<std::string> capturedArgs;
    PrefixBucketSync::SetRunCliOverrideForTest(
        [&](const std::vector<std::string>& args, std::string& outJson) -> std::string {
            capturedArgs = args;
            outJson = "{\"success\":true}";
            return {};
        });

    auto res = PrefixBucketSync::RunJobForTest(appId, PrefixBucketSync::Mode::PushOnly, "unit_test_push");
    EXPECT_TRUE(res.ok);
    EXPECT_EQ(res.filesPushed, 1u);

    ASSERT_EQ(capturedArgs.size(), 5u);
    EXPECT_EQ(capturedArgs[0], "publish-full-manifest");
    EXPECT_EQ(capturedArgs[1], "gdrive");
    EXPECT_EQ(capturedArgs[2], std::to_string(accountId));
    EXPECT_EQ(capturedArgs[3], std::to_string(appId));
    EXPECT_EQ(capturedArgs[4], env.cloudRoot.string());

    // Verify blobs/ was removed so CAS cache files are never published as save paths
    EXPECT_FALSE(fs::exists(appStorage / "blobs"));

    // Verify mirrored storage file & metadata files
    fs::path mirrored = appStorage / "GSE Saves" / std::to_string(appId) / "remote" / "save1.dat";
    EXPECT_TRUE(fs::is_regular_file(mirrored));
    EXPECT_EQ(ScopedPrefixSyncTestEnv::readFile(mirrored), "save_content_v1");
    EXPECT_EQ(ScopedPrefixSyncTestEnv::readFile(appStorage / "root_token.cloudredirect"), "%WinAppDataRoaming%\n");
    EXPECT_EQ(ScopedPrefixSyncTestEnv::readFile(appStorage / "file_tokens.cloudredirect"),
              "GSE Saves/" + std::to_string(appId) + "/remote/save1.dat\t%WinAppDataRoaming%\n");
    EXPECT_EQ(ScopedPrefixSyncTestEnv::readFile(appStorage / "cn.cloudredirect"), "1");

    // Now simulate a failed publish-full-manifest and verify cn.cloudredirect rolls back to 1
    ScopedPrefixSyncTestEnv::writeFile(remoteDir / "save1.dat", "save_content_v2");
    PrefixBucketSync::SetRunCliOverrideForTest(
        [&](const std::vector<std::string>&, std::string& outJson) -> std::string {
            outJson = "{\"success\":false,\"error\":\"network_error\"}";
            return {};
        });

    auto failRes = PrefixBucketSync::RunJobForTest(appId, PrefixBucketSync::Mode::PushOnly, "unit_test_push_fail");
    EXPECT_FALSE(failRes.ok);
    EXPECT_EQ(ScopedPrefixSyncTestEnv::readFile(appStorage / "cn.cloudredirect"), "1");
}

TEST(PrefixBucketSyncTest, CrossDeviceRoundTripAndFreshDevice2Synthesis)
{
    ScopedPrefixSyncTestEnv env;
    const uint32_t accountId = 1122334455u;
    const uint32_t appId = ScopedPrefixSyncTestEnv::randomAppId(99);
    PrefixBucketSync::SetAccountIdForTest(accountId);

    const std::string cleanRel = "GSE Saves/" + std::to_string(appId) + "/remote/repentance_save.dat";
    const std::string payloadV1 = "device1_save_v1_payload";
    const std::string shaV1 = "1111111111111111111111111111111111111111";
    const std::string payloadV2 = "device2_save_v2_payload_updated";
    const std::string shaV2 = "2222222222222222222222222222222222222222";

    // --- Step 1: Device 2 is a brand-new installation where the Proton prefix only has
    // AppData/Roaming (the game has never created GSE Saves/<appId>/remote yet).
    fs::path device2Steamapps = env.tempDir / "device2_steamapps";
    fs::path device2UserRoaming = device2Steamapps / "compatdata" / std::to_string(appId) /
                                  "pfx" / "drive_c" / "users" / "steamuser" / "AppData" / "Roaming";
    std::error_code ec;
    fs::create_directories(device2UserRoaming, ec);

    // Initially no buckets exist on Device 2
    auto initialBuckets = PrefixBucketSync::DiscoverPrefixBuckets({device2Steamapps}, {appId});
    EXPECT_TRUE(initialBuckets.empty());

    // Simulate `sync-remote-app` downloading Device 1's cloud save into CloudRedirect storage
    // using the engine's `<cleanPath>/<sha1>` layout + `root_token.cloudredirect`.
    fs::path appStorage = env.cloudRoot / "storage" / std::to_string(accountId) / std::to_string(appId);
    PrefixBucketSync::SetRunCliOverrideForTest(
        [&](const std::vector<std::string>& args, std::string& outJson) -> std::string {
            if (!args.empty() && args[0] == "sync-remote-app")
            {
                ScopedPrefixSyncTestEnv::writeFile(appStorage / "root_token.cloudredirect", "%WinAppDataRoaming%\n");
                ScopedPrefixSyncTestEnv::writeFile(appStorage / "cn.cloudredirect", "3");
                ScopedPrefixSyncTestEnv::writeFile(appStorage / cleanRel / shaV1, payloadV1);
                outJson = "{\"success\":true}";
                return {};
            }
            outJson = "{\"success\":true}";
            return {};
        });

    // Even with an empty bucket cache, synthesizeBucketsFromStorage will inspect the downloaded
    // storage layout if we have a target prefix or if we synthesize into an existing bucket.
    // Let's register the synthesized bucket location on Device 2 and run PullOnly:
    PrefixBucketSync::PrefixBucket_t dev2Bucket;
    dev2Bucket.appId = appId;
    dev2Bucket.pfxDir = device2Steamapps / "compatdata" / std::to_string(appId) / "pfx";
    dev2Bucket.userDir = dev2Bucket.pfxDir / "drive_c" / "users" / "steamuser";
    dev2Bucket.remoteDir = device2UserRoaming / "GSE Saves" / std::to_string(appId) / "remote";
    dev2Bucket.appDataSubName = "AppData/Roaming";
    dev2Bucket.tailDirs = "GSE Saves/" + std::to_string(appId) + "/remote";
    PrefixBucketSync::SetBucketsForTest({dev2Bucket});

    auto pullRes1 = PrefixBucketSync::RunJobForTest(appId, PrefixBucketSync::Mode::PullOnly, "device2_first_pull");
    EXPECT_TRUE(pullRes1.ok);
    EXPECT_EQ(pullRes1.filesPulled, 1u);

    fs::path dev2SaveFile = dev2Bucket.remoteDir / "repentance_save.dat";
    ASSERT_TRUE(fs::is_regular_file(dev2SaveFile));
    EXPECT_EQ(ScopedPrefixSyncTestEnv::readFile(dev2SaveFile), payloadV1);

    // --- Step 2: User plays on Device 2, modifies the save, and exits (PushOnly).
    // Note that in `appStorage`, `<cleanRel>` is currently a DIRECTORY (`<cleanRel>/<shaV1>`)!
    // `pushBucket` must collapse `<cleanRel>` back into a regular file before `publish-full-manifest`.
    ScopedPrefixSyncTestEnv::writeFile(dev2SaveFile, payloadV2);

    auto pushRes2 = PrefixBucketSync::RunJobForTest(appId, PrefixBucketSync::Mode::PushOnly, "device2_exit_push");
    EXPECT_TRUE(pushRes2.ok);
    EXPECT_EQ(pushRes2.filesPushed, 1u);
    EXPECT_TRUE(fs::is_regular_file(appStorage / cleanRel));
    EXPECT_EQ(ScopedPrefixSyncTestEnv::readFile(appStorage / cleanRel), payloadV2);
    EXPECT_EQ(ScopedPrefixSyncTestEnv::readFile(appStorage / "cn.cloudredirect"), "4");

    // --- Step 3: Next pull on a device whose `<appStorage>/<cleanRel>` is a regular file from a prior push.
    // `prepareStorageForPull` must convert `<appStorage>/<cleanRel>` into `<cleanRel>/<sha>` BEFORE
    // `sync-remote-app` runs so `LocalStorage::WriteFileNoIncrement` does not fail with ENOTDIR.
    ScopedPrefixSyncTestEnv::writeFile(dev2SaveFile, payloadV1); // older local save to trigger conflict backup
    bool wasDirectoryDuringSync = false;
    PrefixBucketSync::SetRunCliOverrideForTest(
        [&](const std::vector<std::string>& args, std::string& outJson) -> std::string {
            if (!args.empty() && args[0] == "sync-remote-app")
            {
                wasDirectoryDuringSync = fs::is_directory(appStorage / cleanRel);
                ScopedPrefixSyncTestEnv::writeFile(appStorage / cleanRel / shaV2, payloadV2);
                ScopedPrefixSyncTestEnv::writeFile(appStorage / "cn.cloudredirect", "5");
            }
            outJson = "{\"success\":true}";
            return {};
        });

    auto pullRes2 = PrefixBucketSync::RunJobForTest(appId, PrefixBucketSync::Mode::PullOnly, "device1_return_pull");
    EXPECT_TRUE(pullRes2.ok);
    EXPECT_TRUE(wasDirectoryDuringSync);
    EXPECT_EQ(pullRes2.filesPulled, 1u);
    EXPECT_EQ(ScopedPrefixSyncTestEnv::readFile(dev2SaveFile), payloadV2);

    // Verify a conflict backup was created under backupDir/<timestamp>/<appId>/<cleanRel>
    bool foundConflictBackup = false;
    if (fs::exists(env.backupDir, ec))
    {
        for (const auto& entry : fs::recursive_directory_iterator(env.backupDir, ec))
        {
            if (entry.is_regular_file(ec) && entry.path().filename() == "repentance_save.dat")
            {
                foundConflictBackup = true;
                EXPECT_EQ(ScopedPrefixSyncTestEnv::readFile(entry.path()), payloadV1);
            }
        }
    }
    EXPECT_TRUE(foundConflictBackup);
}

TEST(PrefixBucketSyncTest, GuardrailsRespectDisabledConfigAndKillswitch)
{
    ScopedPrefixSyncTestEnv env;
    const uint32_t appId = ScopedPrefixSyncTestEnv::randomAppId(123);
    PrefixBucketSync::SetAccountIdForTest(424242u);

    g_config.cloudPrefixSyncEnabled = false;
    auto resDisabled = PrefixBucketSync::RunJobForTest(appId, PrefixBucketSync::Mode::Full, "test_disabled");
    EXPECT_FALSE(resDisabled.ok);
    EXPECT_EQ(resDisabled.message, "disabled");

    g_config.cloudPrefixSyncEnabled = true;
    ScopedPrefixSyncTestEnv::writeFile(env.cloudRoot / "disable", "1");
    auto resKillswitch = PrefixBucketSync::RunJobForTest(appId, PrefixBucketSync::Mode::Full, "test_killswitch");
    EXPECT_FALSE(resKillswitch.ok);
    EXPECT_EQ(resKillswitch.message, "killswitch");
}

TEST(PrefixBucketSyncTest, CloudManagerResolvesBothGseAndGoldbergSavePaths)
{
    ScopedPrefixSyncTestEnv env;
    const uint32_t appId = ScopedPrefixSyncTestEnv::randomAppId(555);

    fs::path gseRemote = env.steamappsDir / "compatdata" / std::to_string(appId) / "pfx" /
                         "drive_c" / "users" / "steamuser" / "AppData" / "Roaming" /
                         "GSE Saves" / std::to_string(appId) / "remote";
    fs::path goldbergRemote = env.steamappsDir / "compatdata" / std::to_string(appId) / "pfx" /
                              "drive_c" / "users" / "customuser" / "AppData" / "Local" /
                              "Goldberg SteamEmu Saves" / std::to_string(appId) / "remote";
    ScopedPrefixSyncTestEnv::writeFile(gseRemote / "a.sav", "1234");
    ScopedPrefixSyncTestEnv::writeFile(goldbergRemote / "b.sav", "567890");

    auto& mgr = Navi::CloudRedirect::CCloudManager::Instance();
    mgr.SetSteamAppsRootForTest(env.steamappsDir);
    mgr.SetStorageDirForTest(env.cloudRoot / "storage");

    auto resolved = mgr.ResolveGoldbergSavePaths(appId);
    EXPECT_GE(resolved.size(), 2u);
    EXPECT_EQ(mgr.GetStorageUsageBytes(appId), 10u);

    mgr.SetSteamAppsRootForTest({});
    mgr.SetStorageDirForTest({});
}

TEST(PrefixBucketSyncTest, LocalOnlyProviderMirrorsEmulatorSavesWithoutRemoteCli)
{
    ScopedPrefixSyncTestEnv env;
    const uint32_t appId = ScopedPrefixSyncTestEnv::randomAppId(777);
    const uint32_t accountId = 909090u;
    PrefixBucketSync::SetAccountIdForTest(accountId);
    g_config.cloudSavesProvider.set("local-only");

    fs::path gseRemote = env.steamappsDir / "compatdata" / std::to_string(appId) / "pfx" /
                         "drive_c" / "users" / "steamuser" / "AppData" / "Roaming" /
                         "GSE Saves" / std::to_string(appId) / "remote";
    ScopedPrefixSyncTestEnv::writeFile(gseRemote / "local_save.dat", "local_only_save_payload");

    auto buckets = PrefixBucketSync::DiscoverPrefixBuckets({env.steamappsDir}, {appId});
    ASSERT_EQ(buckets.size(), 1u);
    PrefixBucketSync::SetBucketsForTest(buckets);

    int cliInvocations = 0;
    PrefixBucketSync::SetRunCliOverrideForTest([&](const std::vector<std::string>&, std::string&) -> std::string {
        cliInvocations++;
        return "should not be called in local-only mode";
    });

    auto res = PrefixBucketSync::RunJobForTest(appId, PrefixBucketSync::Mode::Full, "test_local_only");
    EXPECT_TRUE(res.ok) << res.message;
    EXPECT_EQ(res.filesPushed, 1u);
    EXPECT_EQ(cliInvocations, 0);

    fs::path mirroredSave = env.cloudRoot / "storage" / std::to_string(accountId) / std::to_string(appId) /
                            "GSE Saves" / std::to_string(appId) / "remote" / "local_save.dat";
    EXPECT_EQ(ScopedPrefixSyncTestEnv::readFile(mirroredSave), "local_only_save_payload");
}

TEST(PrefixBucketSyncTest, PushAndPullLuaAndManifestOnlyGames)
{
    ScopedPrefixSyncTestEnv env;
    const uint32_t appId = 480;
    const uint32_t accountId = 123456u;
    PrefixBucketSync::SetAccountIdForTest(accountId);
    PrefixBucketSync::SetBucketDiscoverForTestEnabled(false);
    fs::path steamRoot = env.tempDir / "steam";
    PrefixBucketSync::SetSteamRootForTest(steamRoot);
    g_config.cloudSavesProvider.set("gdrive");

    const std::string luaContent = "addappid(480, 1, 0) -- Spacewar\nsetManifestid(481, \"999888777\")\n";
    const std::string provContent = "{\"provider\":\"gdrive\"}";
    const std::string mfContent = "binary-manifest-bytes-content";

    // 1. Create local files in Steam root
    ScopedPrefixSyncTestEnv::writeFile(steamRoot / "config" / "stplug-in" / "480.lua", luaContent);
    ScopedPrefixSyncTestEnv::writeFile(steamRoot / "config" / "stplug-in" / "480.provider", provContent);
    ScopedPrefixSyncTestEnv::writeFile(steamRoot / "depotcache" / "481_999888777.manifest", mfContent);

    std::vector<std::string> cliCalls;
    PrefixBucketSync::SetRunCliOverrideForTest([&](const std::vector<std::string>& args, std::string& outJson) -> std::string {
        if (!args.empty()) cliCalls.push_back(args[0]);
        outJson = "{\"success\":true}";
        return "";
    });

    // 2. Push should mirror files, register tokens, bump CN, and publish via CLI
    auto resPush = PrefixBucketSync::RunJobForTest(appId, PrefixBucketSync::Mode::PushOnly, "push_lua");
    EXPECT_TRUE(resPush.ok) << resPush.message;
    EXPECT_EQ(resPush.filesPushed, 3u);
    ASSERT_EQ(cliCalls.size(), 1u);
    EXPECT_EQ(cliCalls[0], "publish-full-manifest");

    fs::path storageDir = env.cloudRoot / "storage" / std::to_string(accountId) / std::to_string(appId);
    EXPECT_TRUE(fs::exists(storageDir / "stplug-in" / "480.lua"));
    EXPECT_TRUE(fs::exists(storageDir / "stplug-in" / "480.provider"));
    EXPECT_TRUE(fs::exists(storageDir / "depotcache" / "481_999888777.manifest"));
    EXPECT_EQ(ScopedPrefixSyncTestEnv::readFile(storageDir / "stplug-in" / "480.lua"), luaContent);
    EXPECT_EQ(ScopedPrefixSyncTestEnv::readFile(storageDir / "root_token.cloudredirect"), "%stplug-in%\n");
    std::string fileTokens = ScopedPrefixSyncTestEnv::readFile(storageDir / "file_tokens.cloudredirect");
    EXPECT_NE(fileTokens.find("stplug-in/480.lua\t%stplug-in%"), std::string::npos);
    EXPECT_NE(fileTokens.find("stplug-in/480.provider\t%stplug-in%"), std::string::npos);
    EXPECT_NE(fileTokens.find("depotcache/481_999888777.manifest\t%depotcache%"), std::string::npos);

    // 3. Clear local files to simulate a secondary device
    std::error_code ec;
    fs::remove_all(steamRoot / "config" / "stplug-in", ec);
    fs::remove_all(steamRoot / "depotcache", ec);
    EXPECT_FALSE(fs::exists(steamRoot / "config" / "stplug-in" / "480.lua"));

    cliCalls.clear();
    // 4. Pull should download from cloud and restore to stplug-in and depotcache
    auto resPull = PrefixBucketSync::RunJobForTest(appId, PrefixBucketSync::Mode::PullOnly, "pull_lua");
    EXPECT_TRUE(resPull.ok) << resPull.message;
    EXPECT_EQ(resPull.filesPulled, 3u);
    ASSERT_EQ(cliCalls.size(), 1u);
    EXPECT_EQ(cliCalls[0], "sync-remote-app");

    EXPECT_TRUE(fs::exists(steamRoot / "config" / "stplug-in" / "480.lua"));
    EXPECT_TRUE(fs::exists(steamRoot / "config" / "stplug-in" / "480.provider"));
    EXPECT_TRUE(fs::exists(steamRoot / "depotcache" / "481_999888777.manifest"));
    EXPECT_EQ(ScopedPrefixSyncTestEnv::readFile(steamRoot / "config" / "stplug-in" / "480.lua"), luaContent);
    EXPECT_EQ(ScopedPrefixSyncTestEnv::readFile(steamRoot / "config" / "stplug-in" / "480.provider"), provContent);
    EXPECT_EQ(ScopedPrefixSyncTestEnv::readFile(steamRoot / "depotcache" / "481_999888777.manifest"), mfContent);
}

TEST(PrefixBucketSyncTest, PushLuaAndManifestHandlesDirectoryAtDestinationWithoutThrow)
{
    ScopedPrefixSyncTestEnv env;
    const uint32_t appId = 480;
    const uint32_t accountId = 123456u;
    PrefixBucketSync::SetAccountIdForTest(accountId);
    PrefixBucketSync::SetBucketDiscoverForTestEnabled(false);
    fs::path steamRoot = env.tempDir / "steam";
    PrefixBucketSync::SetSteamRootForTest(steamRoot);
    g_config.cloudSavesProvider.set("gdrive");

    const std::string luaContent = "addappid(480, 1, 0) -- Spacewar\n";
    ScopedPrefixSyncTestEnv::writeFile(steamRoot / "config" / "stplug-in" / "480.lua", luaContent);

    // Simulate CAS or conflicting directory at the storage target path
    fs::path storageDir = env.cloudRoot / "storage" / std::to_string(accountId) / std::to_string(appId);
    fs::path dstDir = storageDir / "stplug-in" / "480.lua";
    std::error_code ec;
    fs::create_directories(dstDir, ec);
    ASSERT_TRUE(fs::is_directory(dstDir));

    PrefixBucketSync::SetRunCliOverrideForTest([&](const std::vector<std::string>&, std::string& outJson) -> std::string {
        outJson = "{\"success\":true}";
        return "";
    });

    // Push should not crash/throw std::ios_base::failure when hashing or copying to dst
    EXPECT_NO_THROW({
        auto resPush = PrefixBucketSync::RunJobForTest(appId, PrefixBucketSync::Mode::PushOnly, "push_lua_dir_conflict");
        EXPECT_TRUE(resPush.ok) << resPush.message;
    });

    EXPECT_TRUE(fs::is_regular_file(dstDir));
    EXPECT_EQ(ScopedPrefixSyncTestEnv::readFile(dstDir), luaContent);
}

