#include <gtest/gtest.h>
#include "config_migration.hpp"
#include "cache_migration.hpp"
#include "legacy_migration.hpp"
#include "atomic_file.hpp"
#include "test_stubs.hpp"

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

// ==========================================
// Config Migration Tests
// ==========================================

TEST(MigrationTest, ConfigMigration)
{
    const std::string testDir = "/tmp/sls_test_config_mig_" + std::to_string(getpid());
    fs::remove_all(testDir);
    fs::create_directories(testDir);

    const std::string userConfig = testDir + "/config.yaml";
    const std::string diffYaml = testDir + "/config.diff.yaml";
    const std::string templatePath = testDir + "/template.yaml";

    // Write initial user config (v0)
    {
        std::ofstream f(userConfig);
        f << "LogLevels: \"0xff\"\n";
        f << "RyuuCookies: \"legacy_secret\"\n";
        f << "DownloadMode: \"accela\"\n";
        f << "DepotDownloaderPath: \"/opt/depot\"\n";
        f << "UseNativeInstall: false\n";
        f << "UserAppIds:\n  - 10\n  - 20\n";
    }

    // Write template config
    {
        std::ofstream f(templatePath);
        f << "DefaultNewSetting: true\n";
        f << "LogLevelsBasic: 1\n";
    }

    // Write diff yaml
    {
        std::ofstream f(diffYaml);
        f << "Migrations:\n";
        f << "  1:\n";
        f << "    \"$(LogLevels)$(LogLevelsBasic):\":\n";
        f << "      action: rename_and_convert\n";
        f << "      value_mapping:\n";
        f << "        \"0xff\": 5\n";
        f << "    \"$(RyuuCookies)$():\":\n";
        f << "      action: remove\n";
        f << "  2:\n";
        f << "    \"$(DownloadMode)$():\":\n";
        f << "      action: remove\n";
        f << "    \"$(DepotDownloaderPath)$():\":\n";
        f << "      action: remove\n";
        f << "    \"$(UseNativeInstall)$():\":\n";
        f << "      action: remove\n";
    }

    bool res = ConfigMigration::migrate(userConfig, diffYaml, templatePath);
    ASSERT_TRUE(res);

    // Verify migrated config
    YAML::Node migrated = YAML::LoadFile(userConfig);
    EXPECT_FALSE(migrated["LogLevels"]);
    ASSERT_TRUE(migrated["LogLevelsBasic"]);
    EXPECT_EQ(migrated["LogLevelsBasic"].as<int>(), 5);
    EXPECT_FALSE(migrated["RyuuCookies"]);
    EXPECT_FALSE(migrated["DownloadMode"]);
    EXPECT_FALSE(migrated["DepotDownloaderPath"]);
    EXPECT_FALSE(migrated["UseNativeInstall"]);
    EXPECT_TRUE(migrated["UserAppIds"]);
    EXPECT_TRUE(migrated["DefaultNewSetting"]);
    ASSERT_TRUE(migrated["ConfigVersion"]);
    EXPECT_EQ(migrated["ConfigVersion"].as<int>(), 2);

    // Verify backup exists
    const std::string backupDir = testDir + "/backups";
    ASSERT_TRUE(fs::exists(backupDir));
    bool foundBak = false;
    for (const auto& e : fs::directory_iterator(backupDir))
    {
        if (e.path().extension() == ".bak") foundBak = true;
    }
    EXPECT_TRUE(foundBak);

    fs::remove_all(testDir);
}

TEST(MigrationTest, CacheMigrationSafeguard)
{
    const std::string testDir = "/tmp/sls_test_cache_mig_" + std::to_string(getpid());
    fs::remove_all(testDir);
    fs::create_directories(testDir);

    // 1. Generic cache directory for testing migration
    const std::string oldCacheDir = testDir + "/legacy_depots";
    fs::create_directories(oldCacheDir);
    {
        std::ofstream f(oldCacheDir + "/123.dat");
        f << "dummy cache content\n";
    }

    // 2. Manifest, Lua, and Steam depotcache directories that must be STRICTLY preserved without any modification
    const std::string manifestDir = testDir + "/morrenus_manifests";
    fs::create_directories(manifestDir);
    {
        std::ofstream f(manifestDir + "/123.manifest");
        f << "untouchable manifest content\n";
    }
    const std::string luaDir = testDir + "/stplug-in";
    fs::create_directories(luaDir);
    {
        std::ofstream f(luaDir + "/test.lua");
        f << "untouchable lua script\n";
    }
    const std::string depotcacheDir = testDir + "/depotcache";
    fs::create_directories(depotcacheDir);
    {
        std::ofstream f(depotcacheDir + "/456_789.manifest");
        f << "untouchable steam depotcache manifest\n";
    }

    const std::string diffYaml = testDir + "/cache.diff.yaml";
    const std::string verFile = testDir + "/cache_version";

    {
        std::ofstream f(diffYaml);
        f << "CacheVersion: 1\n";
        f << "Migrations:\n";
        f << "  1:\n";
        // Valid migration for generic cache
        f << "    \"$(legacy_depots)$(depots_v2):\":\n";
        f << "      action: rename_dir\n";
        f << "      parent_path: \"" << testDir << "\"\n";
        f << "    \"$(*.dat)$(*.dat.cache):\":\n";
        f << "      action: rename_files\n";
        f << "      target_dir: \"" << testDir << "/depots_v2\"\n";
        // Attempted forbidden migrations on manifest, lua, and depotcache (must be skipped by safeguard)
        f << "    \"$(morrenus_manifests)$(tampered_manifests):\":\n";
        f << "      action: rename_dir\n";
        f << "      parent_path: \"" << testDir << "\"\n";
        f << "    \"$(stplug-in)$(tampered_luas):\":\n";
        f << "      action: rename_dir\n";
        f << "      parent_path: \"" << testDir << "\"\n";
        f << "    \"$(depotcache)$(tampered_depotcache):\":\n";
        f << "      action: rename_dir\n";
        f << "      parent_path: \"" << testDir << "\"\n";
    }

    bool res = CacheMigration::migrate(diffYaml, verFile);
    ASSERT_TRUE(res);

    // Verify generic directory was renamed
    EXPECT_FALSE(fs::exists(oldCacheDir));
    const std::string newCacheDir = testDir + "/depots_v2";
    EXPECT_TRUE(fs::exists(newCacheDir));

    // Verify file inside was renamed
    EXPECT_TRUE(fs::exists(newCacheDir + "/123.dat.cache"));

    // Verify that manifests, luas, and depotcache directories were STRICTLY PRESERVED and untouched
    EXPECT_TRUE(fs::exists(manifestDir + "/123.manifest"));
    EXPECT_FALSE(fs::exists(testDir + "/tampered_manifests"));
    EXPECT_TRUE(fs::exists(luaDir + "/test.lua"));
    EXPECT_FALSE(fs::exists(testDir + "/tampered_luas"));
    EXPECT_TRUE(fs::exists(depotcacheDir + "/456_789.manifest"));
    EXPECT_FALSE(fs::exists(testDir + "/tampered_depotcache"));

    // Verify version file
    std::ifstream vf(verFile);
    int v = 0;
    vf >> v;
    EXPECT_EQ(v, 1);

    fs::remove_all(testDir);
}

TEST(MigrationTest, MalformedConfig)
{
    const std::string testDir = "/tmp/sls_test_malformed_" + std::to_string(getpid());
    fs::remove_all(testDir);
    fs::create_directories(testDir);

    const std::string userConfig = testDir + "/config.yaml";
    const std::string diffYaml = testDir + "/config.diff.yaml";

    {
        std::ofstream f(userConfig);
        f << "LogLevels: [unclosed list\nInvalid: {syntax\n";
    }
    {
        std::ofstream f(diffYaml);
        f << "Migrations:\n  1:\n    \"$(LogLevels)$(LogLevelsBasic):\":\n      action: rename\n";
    }

    bool res = ConfigMigration::migrate(userConfig, diffYaml);
    EXPECT_FALSE(res);

    fs::remove_all(testDir);
}

TEST(MigrationTest, MissingFiles)
{
    bool res = ConfigMigration::migrate("/nonexistent/path/config.yaml", "/nonexistent/diff.yaml");
    EXPECT_FALSE(res);

    bool cRes = CacheMigration::migrate("/nonexistent/cache.diff.yaml", "/nonexistent/cache_ver");
    EXPECT_FALSE(cRes);
}

// ==========================================
// Legacy SLSsteam -> Navi Migration Tests
// ==========================================

TEST(MigrationTest, LegacyMigrationFresh)
{
    const std::string testHome = "/tmp/test_legacy_fresh_" + std::to_string(getpid());
    fs::remove_all(testHome);

    auto paths = LegacyMigration::buildPaths(testHome);

    // Setup legacy config
    fs::create_directories(paths.legacyConfigDir);
    {
        std::ofstream f(paths.legacyConfigDir + "/config.yaml");
        f << "LogLevelsBasic: 1\nUserAppIds:\n  - 480\n";
    }
    {
        std::ofstream f(paths.legacyConfigDir + "/depot_keys.yaml");
        f << "1000: dummy_key\n";
    }

    // Setup legacy cache
    fs::create_directories(paths.legacyCacheDir);
    {
        std::ofstream f(paths.legacyCacheDir + "/app_480.cache");
        f << "cache_data_480\n";
    }

    // Setup legacy manifests
    fs::create_directories(paths.legacyManifestsDir);
    {
        std::ofstream f(paths.legacyManifestsDir + "/12345.manifest");
        f << "manifest_binary_data\n";
    }

    ASSERT_FALSE(fs::exists(paths.targetConfigDir));
    ASSERT_FALSE(fs::exists(paths.targetCacheDir));
    ASSERT_FALSE(fs::exists(paths.targetManifestsDir));

    bool ok = LegacyMigration::migrate(paths);
    ASSERT_TRUE(ok);

    // 1. Config migrated
    EXPECT_TRUE(fs::exists(paths.targetConfigDir + "/config.yaml"));
    EXPECT_TRUE(fs::exists(paths.targetConfigDir + "/depot_keys.yaml"));
    EXPECT_TRUE(fs::exists(paths.targetConfigDir + "/.migrated_from_slssteam"));

    // 2. Cache migrated
    EXPECT_TRUE(fs::exists(paths.targetCacheDir + "/app_480.cache"));

    // 3. Manifests migrated to both primary and alt locations
    EXPECT_TRUE(fs::exists(paths.targetManifestsDir + "/12345.manifest"));
    EXPECT_TRUE(fs::exists(paths.targetManifestsAltDir + "/12345.manifest"));
    EXPECT_TRUE(fs::exists(paths.targetShareDir + "/.migrated_from_slssteam"));

    // 4. Downgrade safety: legacy files NEVER deleted
    EXPECT_TRUE(fs::exists(paths.legacyConfigDir + "/config.yaml"));
    EXPECT_TRUE(fs::exists(paths.legacyConfigDir + "/depot_keys.yaml"));
    EXPECT_TRUE(fs::exists(paths.legacyCacheDir + "/app_480.cache"));
    EXPECT_TRUE(fs::exists(paths.legacyManifestsDir + "/12345.manifest"));

    fs::remove_all(testHome);
}

TEST(MigrationTest, LegacyMigrationConflict)
{
    const std::string testHome = "/tmp/test_legacy_conflict_" + std::to_string(getpid());
    fs::remove_all(testHome);

    auto paths = LegacyMigration::buildPaths(testHome);

    // Old config
    fs::create_directories(paths.legacyConfigDir);
    {
        std::ofstream f(paths.legacyConfigDir + "/config.yaml");
        f << "Version: old_config\nLogLevelsBasic: 1\n";
    }

    // New config already exists in target
    fs::create_directories(paths.targetConfigDir);
    {
        std::ofstream f(paths.targetConfigDir + "/config.yaml");
        f << "Version: new_config\nLogLevelsBasic: 5\n";
    }

    resetWarnCount();
    bool ok = LegacyMigration::migrate(paths);
    ASSERT_TRUE(ok);

    // New config wins
    std::ifstream in(paths.targetConfigDir + "/config.yaml");
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_NE(content.find("Version: new_config"), std::string::npos);
    EXPECT_GT(getWarnCount(), 0);

    // Marker written
    EXPECT_TRUE(fs::exists(paths.targetConfigDir + "/.migrated_from_slssteam"));

    // Legacy config untouched
    std::ifstream legacyIn(paths.legacyConfigDir + "/config.yaml");
    std::string legacyContent((std::istreambuf_iterator<char>(legacyIn)), std::istreambuf_iterator<char>());
    EXPECT_NE(legacyContent.find("Version: old_config"), std::string::npos);

    fs::remove_all(testHome);
}

TEST(MigrationTest, LegacyMigrationReadOnly)
{
    const std::string testHome = "/tmp/test_legacy_ro_" + std::to_string(getpid());
    fs::remove_all(testHome);

    auto paths = LegacyMigration::buildPaths(testHome);

    // Read-only source file
    fs::create_directories(paths.legacyConfigDir);
    const std::string oldFile = paths.legacyConfigDir + "/config.yaml";
    {
        std::ofstream f(oldFile);
        f << "ReadOnly: true\n";
    }
    fs::permissions(oldFile, fs::perms::owner_read | fs::perms::group_read | fs::perms::others_read);

    bool ok = LegacyMigration::migrate(paths);
    ASSERT_TRUE(ok);

    const std::string newFile = paths.targetConfigDir + "/config.yaml";
    ASSERT_TRUE(fs::exists(newFile));
    auto perms = fs::status(newFile).permissions();
    EXPECT_EQ(perms & fs::perms::owner_write, fs::perms::none);

    // Restore for cleanup
    fs::permissions(oldFile, fs::perms::owner_all);
    fs::permissions(newFile, fs::perms::owner_all);
    fs::remove_all(testHome);

    // Unwritable target directory
    const std::string testHome2 = "/tmp/test_legacy_unwritable_" + std::to_string(getpid());
    fs::remove_all(testHome2);
    auto paths2 = LegacyMigration::buildPaths(testHome2);

    fs::create_directories(paths2.legacyConfigDir);
    {
        std::ofstream f(paths2.legacyConfigDir + "/config.yaml");
        f << "Test: true\n";
    }

    fs::create_directories(paths2.targetConfigDir);
    fs::permissions(paths2.targetConfigDir, fs::perms::owner_read | fs::perms::owner_exec);

    resetWarnCount();
    bool unwritableRes = LegacyMigration::migrate(paths2);
    EXPECT_FALSE(unwritableRes);
    EXPECT_GT(getWarnCount(), 0);

    fs::permissions(paths2.targetConfigDir, fs::perms::owner_all);
    fs::remove_all(testHome2);
}

TEST(MigrationTest, LegacyMigrationPartialRecovery)
{
    const std::string testHome = "/tmp/test_legacy_partial_" + std::to_string(getpid());
    fs::remove_all(testHome);

    auto paths = LegacyMigration::buildPaths(testHome);

    fs::create_directories(paths.legacyConfigDir);
    {
        std::ofstream f(paths.legacyConfigDir + "/config.yaml");
        f << "Recovered: true\n";
    }
    {
        std::ofstream f(paths.legacyConfigDir + "/extra.yaml");
        f << "Extra: data\n";
    }

    std::string staleStaging = paths.targetConfigDir + ".staging";
    fs::create_directories(staleStaging);
    {
        std::ofstream f(staleStaging + "/broken_partial.tmp");
        f << "corrupted_incomplete_data\n";
    }

    ASSERT_FALSE(fs::exists(paths.targetConfigDir + "/.migrated_from_slssteam"));

    bool ok = LegacyMigration::migrate(paths);
    ASSERT_TRUE(ok);

    EXPECT_FALSE(fs::exists(staleStaging));
    EXPECT_TRUE(fs::exists(paths.targetConfigDir + "/config.yaml"));
    EXPECT_TRUE(fs::exists(paths.targetConfigDir + "/extra.yaml"));
    EXPECT_TRUE(fs::exists(paths.targetConfigDir + "/.migrated_from_slssteam"));

    fs::remove_all(testHome);
}

TEST(MigrationTest, LegacyMigrationIdempotency)
{
    const std::string testHome = "/tmp/test_legacy_idempotent_" + std::to_string(getpid());
    fs::remove_all(testHome);

    auto paths = LegacyMigration::buildPaths(testHome);

    fs::create_directories(paths.legacyConfigDir);
    {
        std::ofstream f(paths.legacyConfigDir + "/config.yaml");
        f << "Initial: v1\n";
    }

    bool firstRun = LegacyMigration::migrate(paths);
    ASSERT_TRUE(firstRun);
    EXPECT_TRUE(fs::exists(paths.targetConfigDir + "/.migrated_from_slssteam"));

    // User modifies target config
    {
        std::ofstream f(paths.targetConfigDir + "/config.yaml");
        f << "UpdatedByNaviUser: v2\n";
    }

    resetWarnCount();
    bool secondRun = LegacyMigration::migrate(paths);
    ASSERT_TRUE(secondRun);

    std::ifstream in(paths.targetConfigDir + "/config.yaml");
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_NE(content.find("UpdatedByNaviUser: v2"), std::string::npos);

    EXPECT_TRUE(fs::exists(paths.legacyConfigDir + "/config.yaml"));
    std::ifstream legacyIn(paths.legacyConfigDir + "/config.yaml");
    std::string legacyContent((std::istreambuf_iterator<char>(legacyIn)), std::istreambuf_iterator<char>());
    EXPECT_NE(legacyContent.find("Initial: v1"), std::string::npos);

    fs::remove_all(testHome);
}

// ==========================================
// Atomic Staging Tests
// ==========================================

TEST(MigrationTest, AtomicWriteAndCopy)
{
    const std::string testDir = "/tmp/sls_test_atomic_staging_" + std::to_string(getpid());
    fs::remove_all(testDir);
    fs::create_directories(testDir);

    const std::string target = testDir + "/sub/dir/test.txt";

    // 1. Basic atomic write creating nested directories
    bool ok = AtomicFile::write(target, "Hello Atomic World!\n");
    ASSERT_TRUE(ok);
    ASSERT_TRUE(fs::exists(target));

    {
        std::ifstream f(target);
        std::string line;
        std::getline(f, line);
        EXPECT_EQ(line, "Hello Atomic World!");
    }

    // 2. Overwrite atomically
    ok = AtomicFile::write(target, "Updated Atomic Content!\n");
    ASSERT_TRUE(ok);
    {
        std::ifstream f(target);
        std::string line;
        std::getline(f, line);
        EXPECT_EQ(line, "Updated Atomic Content!");
    }

    // 3. Atomic copy
    const std::string copyDst = testDir + "/copy/test_copied.txt";
    ok = AtomicFile::copy(target, copyDst);
    ASSERT_TRUE(ok);
    ASSERT_TRUE(fs::exists(copyDst));
    {
        std::ifstream f(copyDst);
        std::string line;
        std::getline(f, line);
        EXPECT_EQ(line, "Updated Atomic Content!");
    }

    // 4. Stream write
    const std::string streamDst = testDir + "/stream.txt";
    ok = AtomicFile::writeStream(streamDst, [](std::ostream& os) {
        os << "Line 1\nLine 2\n";
        return true;
    });
    ASSERT_TRUE(ok);
    {
        std::ifstream f(streamDst);
        std::string l1, l2;
        std::getline(f, l1);
        std::getline(f, l2);
        EXPECT_EQ(l1, "Line 1");
        EXPECT_EQ(l2, "Line 2");
    }

    // 5. Cleanup stale temp files helper
    std::ofstream dummyTmp(testDir + "/rogue.tmp");
    dummyTmp << "stale";
    dummyTmp.close();
    ASSERT_TRUE(fs::exists(testDir + "/rogue.tmp"));
    AtomicFile::cleanupStaleTempFiles(testDir);
    EXPECT_FALSE(fs::exists(testDir + "/rogue.tmp"));

    fs::remove_all(testDir);
}

TEST(MigrationTest, AtomicMoveOrMergeDirectory)
{
    const std::string testDir = "/tmp/sls_test_merge_staging_" + std::to_string(getpid());
    fs::remove_all(testDir);
    fs::create_directories(testDir);

    const std::string stagingDir = testDir + "/downloading/12345";
    const std::string commonDir  = testDir + "/common/AwesomeGame";

    // Setup existing game directory with an existing mod, a save file, and an older executable
    fs::create_directories(commonDir + "/saves");
    fs::create_directories(commonDir + "/bin");
    AtomicFile::write(commonDir + "/saves/save1.dat", "UserSaveData");
    AtomicFile::write(commonDir + "/custom_mod.cfg", "ModConfig=1");
    AtomicFile::write(commonDir + "/bin/game.exe", "OldExeVersion1");

    // Setup staging directory with updated executable and new game assets
    fs::create_directories(stagingDir + "/bin");
    fs::create_directories(stagingDir + "/data");
    AtomicFile::write(stagingDir + "/bin/game.exe", "NewExeVersion2");
    AtomicFile::write(stagingDir + "/data/level1.pak", "Level1Data");

    // Perform atomic move and merge
    bool ok = AtomicFile::moveOrMergeDirectory(stagingDir, commonDir);
    ASSERT_TRUE(ok);

    // Staging directory should be completely gone
    EXPECT_FALSE(fs::exists(stagingDir));

    // Existing user files must be preserved!
    {
        std::ifstream f(commonDir + "/saves/save1.dat");
        std::string content;
        f >> content;
        EXPECT_EQ(content, "UserSaveData");
    }
    {
        std::ifstream f(commonDir + "/custom_mod.cfg");
        std::string content;
        f >> content;
        EXPECT_EQ(content, "ModConfig=1");
    }

    // Matching files must be overwritten with new version!
    {
        std::ifstream f(commonDir + "/bin/game.exe");
        std::string content;
        f >> content;
        EXPECT_EQ(content, "NewExeVersion2");
    }

    // New files must be present!
    EXPECT_TRUE(fs::exists(commonDir + "/data/level1.pak"));

    fs::remove_all(testDir);
}

TEST(MigrationTest, AtomicCancellationSafety)
{
    const std::string testDir = "/tmp/sls_test_cancel_staging_" + std::to_string(getpid());
    fs::remove_all(testDir);
    fs::create_directories(testDir);

    const std::string stagingDir = testDir + "/downloading/99999";
    const std::string commonDir  = testDir + "/common/MyGame";

    // Existing installed game files
    fs::create_directories(commonDir);
    AtomicFile::write(commonDir + "/game.exe", "OriginalCleanExe");
    AtomicFile::write(commonDir + "/data.bin", "OriginalCleanData");

    // Download starts into staging directory
    fs::create_directories(stagingDir);
    AtomicFile::write(stagingDir + "/partial_chunk.tmp", "IncompleteData");

    // User cancels download
    AtomicFile::removePath(stagingDir);

    EXPECT_FALSE(fs::exists(stagingDir));

    // Existing directory remains uncorrupted
    EXPECT_TRUE(fs::exists(commonDir + "/game.exe"));
    EXPECT_TRUE(fs::exists(commonDir + "/data.bin"));
    {
        std::ifstream f(commonDir + "/game.exe");
        std::string content;
        f >> content;
        EXPECT_EQ(content, "OriginalCleanExe");
    }
    {
        std::ifstream f(commonDir + "/data.bin");
        std::string content;
        f >> content;
        EXPECT_EQ(content, "OriginalCleanData");
    }

    fs::remove_all(testDir);
}
