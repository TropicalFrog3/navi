#include <gtest/gtest.h>

#include "sync_manifest.hpp"
#include "sync_snapshot.hpp"
#include "paths.hpp"
#include "config.hpp"
#include "atomic_file.hpp"
#include "utils.hpp"
#include "migrations/config_migration.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

class SyncUnitTest : public ::testing::Test
{
protected:
    std::string m_testDir;

    void SetUp() override
    {
        m_testDir = "/tmp/unit_test_sync_" + std::to_string(getpid()) + "_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        fs::remove_all(m_testDir);
        fs::create_directories(m_testDir);
    }

    void TearDown() override
    {
        fs::remove_all(m_testDir);
    }
};

// ── Test 1: Deterministic Root SHA256 ─────────────────────────────────────────

TEST_F(SyncUnitTest, DeterministicManifestRootSha256)
{
    Sync::SyncFileEntry_t e1;
    e1.category = Sync::ESyncCategory::k_ManifestsAndLua;
    e1.appId = 440;
    e1.logicalPath = "steamapps://appmanifest_440.acf";
    e1.sha256Hex = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    e1.sizeBytes = 1024;
    e1.mtimeUnixSec = 1700000000;
    e1.generation = 5;
    e1.originDeviceId = "pc_alpha";

    Sync::SyncFileEntry_t e2;
    e2.category = Sync::ESyncCategory::k_CloudSaves;
    e2.appId = 730;
    e2.logicalPath = "userdata://12345/730/remote/save.dat";
    e2.sha256Hex = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    e2.sizeBytes = 2048;
    e2.mtimeUnixSec = 1700000010;
    e2.generation = 6;
    e2.originDeviceId = "deck_bravo";

    Sync::SyncFileEntry_t e3;
    e3.category = Sync::ESyncCategory::k_Configs;
    e3.appId = 0;
    e3.logicalPath = "config://navi.yaml";
    e3.sha256Hex = "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";
    e3.sizeBytes = 512;
    e3.mtimeUnixSec = 1700000020;
    e3.generation = 7;
    e3.originDeviceId = "pc_alpha";

    std::vector<Sync::SyncFileEntry_t> v1 = { e1, e2, e3 };
    std::vector<Sync::SyncFileEntry_t> v2 = { e3, e1, e2 };
    std::vector<Sync::SyncFileEntry_t> v3 = { e2, e3, e1 };

    std::string root1 = Sync::computeManifestRootSha256(v1);
    std::string root2 = Sync::computeManifestRootSha256(v2);
    std::string root3 = Sync::computeManifestRootSha256(v3);

    EXPECT_FALSE(root1.empty());
    EXPECT_EQ(root1.length(), 64u);
    EXPECT_EQ(root1, root2);
    EXPECT_EQ(root1, root3);

    // Altering any field must yield a distinct root hash
    Sync::SyncFileEntry_t e1Modified = e1;
    e1Modified.sizeBytes = 1025;
    std::vector<Sync::SyncFileEntry_t> vModified = { e1Modified, e2, e3 };
    std::string rootModified = Sync::computeManifestRootSha256(vModified);
    EXPECT_NE(root1, rootModified);
}

// ── Test 2: YAML Round-trip Serialization & bumpManifestGeneration ────────────

TEST_F(SyncUnitTest, ManifestYamlRoundTripAndBumpGeneration)
{
    Sync::SyncStateManifest_t manifest;
    manifest.schemaVersion = 1;
    manifest.steamId64 = 76561198000000001ULL;
    manifest.keyEpoch = 2;
    manifest.deviceId = "steamdeck_01";
    manifest.deviceHostname = "deck-jupiter";
    manifest.globalGeneration = 10;
    manifest.generatedAtUnixSec = 1700001000;
    manifest.vectorClock["steamdeck_01"] = 10;
    manifest.vectorClock["desktop_linux"] = 8;

    Sync::SyncFileEntry_t entry;
    entry.category = Sync::ESyncCategory::k_ManifestsAndLua;
    entry.appId = 440;
    entry.logicalPath = "steamapps://appmanifest_440.acf";
    entry.sha256Hex = "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef";
    entry.sizeBytes = 4096;
    entry.mtimeUnixSec = 1700000500;
    entry.generation = 10;
    entry.originDeviceId = "steamdeck_01";
    manifest.entries.push_back(entry);

    manifest.manifestRootSha256 = Sync::computeManifestRootSha256(manifest.entries);

    // Bump generation
    Sync::bumpManifestGeneration(manifest, "steamdeck_01");
    EXPECT_EQ(manifest.globalGeneration, 11u);
    EXPECT_EQ(manifest.vectorClock["steamdeck_01"], 11u);
    EXPECT_GE(manifest.generatedAtUnixSec, 1700001000);

    // Serialize to YAML
    std::string yaml = Sync::serializeManifestToYaml(manifest);
    EXPECT_FALSE(yaml.empty());

    // Deserialize
    Sync::SyncStateManifest_t deserialized;
    bool ok = Sync::deserializeManifestFromYaml(yaml, deserialized);
    ASSERT_TRUE(ok);

    EXPECT_EQ(deserialized.schemaVersion, manifest.schemaVersion);
    EXPECT_EQ(deserialized.steamId64, manifest.steamId64);
    EXPECT_EQ(deserialized.keyEpoch, manifest.keyEpoch);
    EXPECT_EQ(deserialized.deviceId, manifest.deviceId);
    EXPECT_EQ(deserialized.deviceHostname, manifest.deviceHostname);
    EXPECT_EQ(deserialized.globalGeneration, manifest.globalGeneration);
    EXPECT_EQ(deserialized.generatedAtUnixSec, manifest.generatedAtUnixSec);
    EXPECT_EQ(deserialized.manifestRootSha256, manifest.manifestRootSha256);
    EXPECT_EQ(deserialized.vectorClock.size(), manifest.vectorClock.size());
    EXPECT_EQ(deserialized.vectorClock["steamdeck_01"], 11u);
    EXPECT_EQ(deserialized.vectorClock["desktop_linux"], 8u);

    ASSERT_EQ(deserialized.entries.size(), 1u);
    const auto& de = deserialized.entries.front();
    EXPECT_EQ(de.category, Sync::ESyncCategory::k_ManifestsAndLua);
    EXPECT_EQ(de.appId, 440u);
    EXPECT_EQ(de.logicalPath, "steamapps://appmanifest_440.acf");
    EXPECT_EQ(de.sha256Hex, "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef");
    EXPECT_EQ(de.sizeBytes, 4096u);
    EXPECT_EQ(de.mtimeUnixSec, 1700000500);
    EXPECT_EQ(de.generation, 10u);
    EXPECT_EQ(de.originDeviceId, "steamdeck_01");
}

// ── Test 3: Path Normalization Across Desktop, Flatpak, SteamOS SD ─────────────

TEST_F(SyncUnitTest, PathResolutionAcrossLibraryMounts)
{
    const std::string mockSteamRoot = m_testDir + "/mock_steam";
    const std::string mockSdCard = m_testDir + "/mock_sdcard";

    fs::create_directories(mockSteamRoot + "/steamapps");
    fs::create_directories(mockSteamRoot + "/config/depotcache");
    fs::create_directories(mockSteamRoot + "/config/stplug-in");
    fs::create_directories(mockSteamRoot + "/userdata/12345/440/remote");
    fs::create_directories(mockSdCard + "/steamapps/compatdata/440/pfx/drive_c/users/steamuser/AppData");

    // Write libraryfolders.vdf pointing to SD card
    {
        std::ofstream vdf(mockSteamRoot + "/steamapps/libraryfolders.vdf");
        vdf << "\"libraryfolders\"\n{\n"
            << "  \"0\"\n  {\n    \"path\" \"" << mockSteamRoot << "\"\n  }\n"
            << "  \"1\"\n  {\n    \"path\" \"" << mockSdCard << "\"\n  }\n"
            << "}\n";
    }

    // Place appmanifest_440.acf on SD card
    const std::string sdAcf = mockSdCard + "/steamapps/appmanifest_440.acf";
    {
        std::ofstream f(sdAcf);
        f << "\"AppState\" { \"appid\" \"440\" }\n";
    }

    // Place appmanifest_730.acf in primary steamapps
    const std::string primaryAcf = mockSteamRoot + "/steamapps/appmanifest_730.acf";
    {
        std::ofstream f(primaryAcf);
        f << "\"AppState\" { \"appid\" \"730\" }\n";
    }

    // Place dummy files
    const std::string depotFile = mockSteamRoot + "/config/depotcache/441_123.manifest";
    { std::ofstream f(depotFile); f << "depot"; }

    const std::string pluginFile = mockSteamRoot + "/config/stplug-in/440.lua";
    { std::ofstream f(pluginFile); f << "lua"; }

    const std::string userFile = mockSteamRoot + "/userdata/12345/440/remote/save.dat";
    { std::ofstream f(userFile); f << "save"; }

    const std::string pfxFile = mockSdCard + "/steamapps/compatdata/440/pfx/drive_c/users/steamuser/AppData/save.dat";
    { std::ofstream f(pfxFile); f << "proton save"; }

    const uint64_t steamId64 = (1ULL << 32) | 12345ULL; // steam32 is 12345

    // 1. Library folders discovery
    auto libraries = Paths::findLibraryFolders(mockSteamRoot);
    ASSERT_GE(libraries.size(), 2u);
    EXPECT_EQ(libraries[0], fs::path(mockSteamRoot).lexically_normal().string());
    EXPECT_EQ(libraries[1], fs::path(mockSdCard).lexically_normal().string());

    // 2. App library folder resolution
    EXPECT_EQ(Paths::findAppLibraryFolder(440, mockSteamRoot), fs::path(mockSdCard).lexically_normal().string());
    EXPECT_EQ(Paths::findAppLibraryFolder(730, mockSteamRoot), fs::path(mockSteamRoot).lexically_normal().string());

    // 3. Resolve steamapps://
    std::string resAcf = Paths::resolveLogicalSyncPath("steamapps://appmanifest_440.acf", steamId64, 440, mockSteamRoot);
    EXPECT_EQ(resAcf, fs::path(sdAcf).lexically_normal().string());

    // 4. Resolve depotcache://
    std::string resDepot = Paths::resolveLogicalSyncPath("depotcache://441_123.manifest", steamId64, 440, mockSteamRoot);
    EXPECT_EQ(resDepot, fs::path(depotFile).lexically_normal().string());

    // 5. Resolve stplugin://
    std::string resPlugin = Paths::resolveLogicalSyncPath("stplugin://440.lua", steamId64, 440, mockSteamRoot);
    EXPECT_EQ(resPlugin, fs::path(pluginFile).lexically_normal().string());

    // 6. Resolve userdata:// with {steam32} placeholder
    std::string resUser1 = Paths::resolveLogicalSyncPath("userdata://{steam32}/440/remote/save.dat", steamId64, 440, mockSteamRoot);
    EXPECT_EQ(resUser1, fs::path(userFile).lexically_normal().string());

    // 7. Resolve userdata:// with explicit numeric steam32 matching steamId64
    std::string resUser2 = Paths::resolveLogicalSyncPath("userdata://12345/440/remote/save.dat", steamId64, 440, mockSteamRoot);
    EXPECT_EQ(resUser2, fs::path(userFile).lexically_normal().string());

    // 8. Resolve protonprefix://
    std::string resPfx = Paths::resolveLogicalSyncPath("protonprefix://440/drive_c/users/steamuser/AppData/save.dat", steamId64, 440, mockSteamRoot);
    EXPECT_EQ(resPfx, fs::path(pfxFile).lexically_normal().string());

    // 9. toLogicalSyncUri conversions
    EXPECT_EQ(Paths::toLogicalSyncUri(Sync::ESyncCategory::k_ManifestsAndLua, 440, "appmanifest_440.acf"), "steamapps://appmanifest_440.acf");
    EXPECT_EQ(Paths::toLogicalSyncUri(Sync::ESyncCategory::k_ManifestsAndLua, 440, "config/stplug-in/440.lua"), "stplugin://440.lua");
    EXPECT_EQ(Paths::toLogicalSyncUri(Sync::ESyncCategory::k_ManifestsAndLua, 440, "config/depotcache/441.manifest"), "depotcache://441.manifest");
    EXPECT_EQ(Paths::toLogicalSyncUri(Sync::ESyncCategory::k_CloudSaves, 440, "remote/save.dat", steamId64), "userdata://12345/440/remote/save.dat");
    EXPECT_EQ(Paths::toLogicalSyncUri(Sync::ESyncCategory::k_CloudSaves, 440, "drive_c/users/steamuser/AppData/save.dat", steamId64), "protonprefix://440/drive_c/users/steamuser/AppData/save.dat");
}

// ── Test 4: Security Invariants (Traversal, Steam32 mismatch, Symlink escape) ──

TEST_F(SyncUnitTest, SecurityInvariantsRejection)
{
    const std::string mockSteamRoot = m_testDir + "/mock_steam";
    const std::string outsideDir = m_testDir + "/outside";

    fs::create_directories(mockSteamRoot + "/userdata/12345/440/remote");
    fs::create_directories(mockSteamRoot + "/config/depotcache");
    fs::create_directories(outsideDir);

    const std::string secretFile = outsideDir + "/secret.txt";
    { std::ofstream f(secretFile); f << "classified"; }

    const uint64_t steamId64 = (1ULL << 32) | 12345ULL;

    // 1. Path traversal attacks
    EXPECT_EQ(Paths::resolveLogicalSyncPath("steamapps://../evil.txt", steamId64, 440, mockSteamRoot), "");
    EXPECT_EQ(Paths::resolveLogicalSyncPath("depotcache://../../etc/passwd", steamId64, 440, mockSteamRoot), "");
    EXPECT_EQ(Paths::resolveLogicalSyncPath("stplugin:///etc/shadow", steamId64, 440, mockSteamRoot), "");
    EXPECT_EQ(Paths::resolveLogicalSyncPath("userdata://12345/440/remote/../../evil.txt", steamId64, 440, mockSteamRoot), "");
    EXPECT_EQ(Paths::resolveLogicalSyncPath("protonprefix://440/drive_c/users/steamuser/../../../evil.txt", steamId64, 440, mockSteamRoot), "");

    // 2. Null byte injection
    std::string nullUri = "steamapps://appmanifest_440.acf";
    nullUri.push_back('\0');
    nullUri += "evil";
    EXPECT_EQ(Paths::resolveLogicalSyncPath(nullUri, steamId64, 440, mockSteamRoot), "");

    // 3. Cross-account steam32 mismatch and zero steamId64 rejection
    EXPECT_EQ(Paths::resolveLogicalSyncPath("userdata://99999/440/remote/save.dat", steamId64, 440, mockSteamRoot), "");
    EXPECT_EQ(Paths::resolveLogicalSyncPath("userdata://{steam32}/440/remote/save.dat", 0ULL, 440, mockSteamRoot), "");
    EXPECT_EQ(Paths::resolveLogicalSyncPath("userdata://0/440/remote/save.dat", 0ULL, 440, mockSteamRoot), "");

    // 4. Symlink escapes: Directory symlink pointing outside base root
    const fs::path symlinkDir = mockSteamRoot + "/userdata/12345/440/remote/symlink_outside";
    std::error_code ec;
    fs::create_directory_symlink(outsideDir, symlinkDir, ec);
    ASSERT_FALSE(ec);

    EXPECT_EQ(Paths::resolveLogicalSyncPath("userdata://12345/440/remote/symlink_outside/secret.txt", steamId64, 440, mockSteamRoot), "");

    // 5. Direct file symlink pointing outside base root
    const fs::path symlinkFile = mockSteamRoot + "/userdata/12345/440/remote/link_file.txt";
    fs::create_symlink(secretFile, symlinkFile, ec);
    ASSERT_FALSE(ec);

    EXPECT_EQ(Paths::resolveLogicalSyncPath("userdata://12345/440/remote/link_file.txt", steamId64, 440, mockSteamRoot), "");
}

// ── Test 5: SyncSnapshotManager Backup, Verification, Rollback, Pruning ────────

TEST_F(SyncUnitTest, SnapshotManagerBackupRestoreAndPrune)
{
    const std::string backupRoot = m_testDir + "/backups";
    const uint64_t steamId64 = 76561198000000001ULL;

    const std::string file1 = m_testDir + "/live_file1.txt";
    const std::string file2 = m_testDir + "/live_file2.bin";
    const std::string file3 = m_testDir + "/live_file3_nonexistent.txt";

    const std::string origData1 = "Original text content for file 1";
    const std::string origData2 = std::string("Binary\0Data\xFF\xFE", 8);

    {
        std::ofstream f(file1);
        f << origData1;
    }
    {
        std::ofstream f(file2, std::ios::binary);
        f.write(origData2.data(), origData2.size());
    }

    Sync::SnapshotRecord_t record;
    bool backedUp = Sync::SyncSnapshotManager::createPreSyncBackup(
        backupRoot, steamId64, { file1, file2, file3 }, record);

    ASSERT_TRUE(backedUp);
    EXPECT_FALSE(record.snapshotId.empty());
    EXPECT_EQ(record.steamId64, steamId64);
    ASSERT_EQ(record.backedUpFiles.size(), 2u);
    ASSERT_EQ(record.nonExistentTargets.size(), 1u);
    EXPECT_EQ(record.nonExistentTargets.front(), file3);

    // Verify sibling .bak files created and match content
    EXPECT_TRUE(fs::exists(file1 + ".bak"));
    EXPECT_TRUE(fs::exists(file2 + ".bak"));
    EXPECT_EQ(Utils::getFileSHA256(file1.c_str()), Utils::getFileSHA256((file1 + ".bak").c_str()));
    EXPECT_EQ(Utils::getFileSHA256(file2.c_str()), Utils::getFileSHA256((file2 + ".bak").c_str()));

    // Verify snapshot.yaml exists
    const fs::path snapYaml = fs::path(backupRoot) / std::to_string(steamId64) / record.snapshotId / "snapshot.yaml";
    EXPECT_TRUE(fs::exists(snapYaml));

    // Simulate corruption and partial sync file creation
    {
        std::ofstream f(file1);
        f << "Corrupted data overwrote file 1!";
    }
    {
        std::ofstream f(file2, std::ios::binary);
        f << "Corrupted binary data!";
    }
    {
        std::ofstream f(file3);
        f << "Incomplete new file downloaded during aborted sync";
    }
    EXPECT_TRUE(fs::exists(file3));

    // Restore snapshot
    bool restored = Sync::SyncSnapshotManager::restoreSnapshot(backupRoot, steamId64, record.snapshotId);
    ASSERT_TRUE(restored);

    // Verify byte-for-byte recovery
    {
        std::ifstream f(file1);
        std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        EXPECT_EQ(content, origData1);
    }
    {
        std::ifstream f(file2, std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        EXPECT_EQ(content, origData2);
    }

    // Verify nonexistent target file was cleanly removed
    EXPECT_FALSE(fs::exists(file3));

    // Test pruning old snapshots: create 6 more snapshots (total 7)
    for (int i = 0; i < 6; ++i)
    {
        Sync::SnapshotRecord_t extra;
        Sync::SyncSnapshotManager::createPreSyncBackup(backupRoot, steamId64, { file1 }, extra);
    }

    // Before pruning: 7 snapshots
    size_t countBefore = 0;
    for (const auto& entry : fs::directory_iterator(fs::path(backupRoot) / std::to_string(steamId64)))
    {
        if (entry.is_directory()) countBefore++;
    }
    EXPECT_EQ(countBefore, 7u);

    // Prune to retain max 5
    Sync::SyncSnapshotManager::pruneOldSnapshots(backupRoot, steamId64, 5);

    size_t countAfter = 0;
    for (const auto& entry : fs::directory_iterator(fs::path(backupRoot) / std::to_string(steamId64)))
    {
        if (entry.is_directory()) countAfter++;
    }
    EXPECT_EQ(countAfter, 5u);
}

// ── Test 6: CConfig Sync* loading & ConfigMigration Template Merge ─────────────

TEST_F(SyncUnitTest, ConfigLoadingAndMigrationTemplateMerge)
{
    // 1. Test CConfig loading of sync settings
    const std::string configPath = m_testDir + "/config.yaml";
    {
        std::ofstream f(configPath);
        f << "SyncRole: \"overrider\"\n"
          << "SyncTopology: \"p2p_and_cloud\"\n"
          << "SyncCloudProvider: \"webdav\"\n"
          << "SyncWebDavUrl: \"https://dav.example.com/remote\"\n"
          << "SyncWebDavUser: \"navi_sync_user\"\n"
          << "SyncFilter:\n"
          << "  AllManifestsAndLua: true\n"
          << "  ManifestAppIds:\n"
          << "    - 440\n"
          << "    - 730\n"
          << "  AllConfigs: false\n"
          << "  ConfigKeys:\n"
          << "    - \"FakeName\"\n"
          << "  AllCloudSaves: false\n"
          << "  CloudSaveAppIds:\n"
          << "    - 440\n"
          << "  AllAchievements: true\n"
          << "  AchievementAppIds: []\n"
          << "  AllApiKeys: false\n"
          << "  ProviderNames:\n"
          << "    - \"ryuu\"\n";
    }

    // Set HOME to mock dir so CConfig points to our config.yaml
    const char* origHome = getenv("HOME");
    std::string origHomeStr = origHome ? origHome : "";
    setenv("HOME", m_testDir.c_str(), 1);
    fs::create_directories(m_testDir + "/.config/navi");
    fs::copy_file(configPath, m_testDir + "/.config/navi/config.yaml", fs::copy_options::overwrite_existing);

    CConfig testConfig;
    testConfig.loadSettings(true);

    EXPECT_EQ(testConfig.syncRole.get(), "overrider");
    EXPECT_EQ(testConfig.syncTopology.get(), "p2p_and_cloud");
    EXPECT_EQ(testConfig.syncCloudProvider.get(), "webdav");
    EXPECT_EQ(testConfig.syncWebDavUrl.get(), "https://dav.example.com/remote");
    EXPECT_EQ(testConfig.syncWebDavUser.get(), "navi_sync_user");

    const auto filter = testConfig.syncFilter.get();
    EXPECT_TRUE(filter.allManifestsAndLua);
    EXPECT_TRUE(filter.isAppSelected(Sync::ESyncCategory::k_ManifestsAndLua, 440));
    EXPECT_TRUE(filter.isAppSelected(Sync::ESyncCategory::k_ManifestsAndLua, 999)); // Because all is true
    EXPECT_FALSE(filter.allConfigs);
    EXPECT_TRUE(filter.isConfigSelected("FakeName"));
    EXPECT_FALSE(filter.isConfigSelected("DisableCloud"));
    EXPECT_FALSE(filter.allCloudSaves);
    EXPECT_TRUE(filter.isAppSelected(Sync::ESyncCategory::k_CloudSaves, 440));
    EXPECT_FALSE(filter.isAppSelected(Sync::ESyncCategory::k_CloudSaves, 730));
    EXPECT_TRUE(filter.allAchievements);
    EXPECT_TRUE(filter.isAppSelected(Sync::ESyncCategory::k_Achievements, 1234));
    EXPECT_FALSE(filter.allApiKeys);
    EXPECT_TRUE(filter.isProviderSelected("ryuu"));
    EXPECT_FALSE(filter.isProviderSelected("depotbox"));

    // Restore HOME
    if (!origHomeStr.empty())
        setenv("HOME", origHomeStr.c_str(), 1);

    // 2. Test ConfigMigration::migrate with template merge
    const std::string userCfg = m_testDir + "/user_config.yaml";
    const std::string diffCfg = m_testDir + "/diff.yaml";
    const std::string templCfg = m_testDir + "/template.yaml";

    {
        std::ofstream f(userCfg);
        f << "ConfigVersion: 1\n"
          << "LogLevels: 16\n"
          << "DisableCloud: false\n"
          << "FakeName: \"CustomPersona\"\n";
    }
    {
        std::ofstream f(diffCfg);
        f << "Migrations:\n"
          << "  2:\n"
          << "    $(OldUnusedKey)$():\n"
          << "      action: remove\n";
    }
    {
        std::ofstream f(templCfg);
        f << "LogLevels: 255\n"
          << "DisableCloud: true\n"
          << "SyncRole: \"disabled\"\n"
          << "SyncTopology: \"cloud\"\n"
          << "SyncCloudProvider: \"none\"\n"
          << "SyncWebDavUrl: \"\"\n"
          << "SyncWebDavUser: \"\"\n"
          << "SyncFilter:\n"
          << "  AllManifestsAndLua: false\n"
          << "  ManifestAppIds: []\n";
    }

    bool migrated = ConfigMigration::migrate(userCfg, diffCfg, templCfg);
    ASSERT_TRUE(migrated);

    YAML::Node migratedYaml = YAML::LoadFile(userCfg);
    // User settings preserved
    EXPECT_EQ(migratedYaml["ConfigVersion"].as<uint32_t>(), 2u);
    EXPECT_EQ(migratedYaml["LogLevels"].as<uint32_t>(), 16u);
    EXPECT_EQ(migratedYaml["DisableCloud"].as<bool>(), false);
    EXPECT_EQ(migratedYaml["FakeName"].as<std::string>(), "CustomPersona");

    // Template defaults populated
    EXPECT_EQ(migratedYaml["SyncRole"].as<std::string>(), "disabled");
    EXPECT_EQ(migratedYaml["SyncTopology"].as<std::string>(), "cloud");
    EXPECT_EQ(migratedYaml["SyncCloudProvider"].as<std::string>(), "none");
    ASSERT_TRUE(migratedYaml["SyncFilter"].IsDefined());
    EXPECT_EQ(migratedYaml["SyncFilter"]["AllManifestsAndLua"].as<bool>(), false);
}
