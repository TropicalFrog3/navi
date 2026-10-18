#include "config_migration.hpp"
#include "cache_migration.hpp"
#include "legacy_migration.hpp"
#include "log.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdarg>

static int g_warnCount = 0;

// Provide minimal CLog stub so test_migration does not depend on log.cpp or g_config
void CLog::trace(const char*, const char*, const int, const char*, ...) {}
void CLog::traceOnce(const char*, const char*, const int, const char*, ...) {}
void CLog::once(const char*, const char*, const int, const char*, ...) {}
void CLog::debug(const char*, const char*, const int, const char*, ...) {}
void CLog::debugOnce(const char*, const char*, const int, const char*, ...) {}
void CLog::warn(const char*, const char*, const int, const char*, ...) { g_warnCount++; }
void CLog::error(const char*, const char*, const int, const char*, ...) {}
void CLog::info(const char*, const char*, const int, const char*, ...) {}
void CLog::notify(const char*, const char*, const int, const char*, ...) {}
void CLog::notifyLong(const char*, const char*, const int, const char*, ...) {}
void CLog::notifyWarn(const char*, const char*, const int, const char*, ...) {}
void CLog::notifyError(const char*, const char*, const int, const char*, ...) {}
void CLog::api(const char*, const char*, const int, const char*, ...) {}
void CLog::custom(const unsigned int, const char*, const char*, const int, const char*, ...) {}

CLog::CLog(const char* p) : path(p ? p : "") {}
CLog::~CLog() {}
CLog* CLog::createDefaultLog() { return new CLog("/dev/null"); }

std::unique_ptr<CLog> g_pLog = std::make_unique<CLog>("/dev/null");

void testConfigMigration()
{
    std::cout << "[TEST] Running ConfigMigration unit tests...\n" << std::flush;
    std::string testDir = "/tmp/sls_test_config_mig";
    std::filesystem::remove_all(testDir);
    std::filesystem::create_directories(testDir);

    std::string userConfig = testDir + "/config.yaml";
    std::string diffYaml = testDir + "/config.diff.yaml";
    std::string templatePath = testDir + "/template.yaml";

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

    std::cout << "[TEST] Calling ConfigMigration::migrate...\n" << std::flush;
    bool res = ConfigMigration::migrate(userConfig, diffYaml, templatePath);
    std::cout << "[TEST] Migrate returned: " << res << "\n" << std::flush;
    assert(res && "ConfigMigration::migrate should return true");

    // Verify migrated config
    YAML::Node migrated = YAML::LoadFile(userConfig);
    std::cout << "[TEST] Loaded migrated config\n" << std::flush;
    assert(!migrated["LogLevels"] && "LogLevels should have been removed");
    std::cout << "[TEST] LogLevels check passed\n" << std::flush;
    assert(migrated["LogLevelsBasic"] && "LogLevelsBasic should be present");
    std::cout << "[TEST] LogLevelsBasic check passed\n" << std::flush;
    assert(migrated["LogLevelsBasic"].as<int>() == 5 && "LogLevelsBasic should be converted to 5");
    std::cout << "[TEST] Value check passed\n" << std::flush;
    assert(!migrated["RyuuCookies"] && "RyuuCookies should have been removed");
    assert(!migrated["DownloadMode"] && "DownloadMode should have been removed");
    assert(!migrated["DepotDownloaderPath"] && "DepotDownloaderPath should have been removed");
    assert(!migrated["UseNativeInstall"] && "UseNativeInstall should have been removed");
    assert(migrated["UserAppIds"] && "UserAppIds should be preserved");
    assert(migrated["DefaultNewSetting"] && "DefaultNewSetting from template should be merged");
    assert(migrated["ConfigVersion"].as<int>() == 2 && "ConfigVersion should be updated to 2");
    std::cout << "[TEST] Config version check passed\n" << std::flush;

    // Verify backup exists
    std::string backupDir = testDir + "/backups";
    assert(std::filesystem::exists(backupDir) && "Backup directory should exist");
    bool foundBak = false;
    for (const auto& e : std::filesystem::directory_iterator(backupDir))
    {
        if (e.path().extension() == ".bak") foundBak = true;
    }
    assert(foundBak && "Timestamped backup file should exist");

    std::filesystem::remove_all(testDir);
    std::cout << "[TEST] ConfigMigration tests passed successfully!\n";
}

void testCacheMigration()
{
    std::cout << "[TEST] Running CacheMigration unit tests...\n";
    std::string testDir = "/tmp/sls_test_cache_mig";
    std::filesystem::remove_all(testDir);
    std::filesystem::create_directories(testDir);

    // 1. Generic cache directory for testing migration
    std::string oldCacheDir = testDir + "/legacy_depots";
    std::filesystem::create_directories(oldCacheDir);
    {
        std::ofstream f(oldCacheDir + "/123.dat");
        f << "dummy cache content\n";
    }

    // 2. Manifest, Lua, and Steam depotcache directories that must be STRICTLY preserved without any modification
    std::string manifestDir = testDir + "/morrenus_manifests";
    std::filesystem::create_directories(manifestDir);
    {
        std::ofstream f(manifestDir + "/123.manifest");
        f << "untouchable manifest content\n";
    }
    std::string luaDir = testDir + "/stplug-in";
    std::filesystem::create_directories(luaDir);
    {
        std::ofstream f(luaDir + "/test.lua");
        f << "untouchable lua script\n";
    }
    std::string depotcacheDir = testDir + "/depotcache";
    std::filesystem::create_directories(depotcacheDir);
    {
        std::ofstream f(depotcacheDir + "/456_789.manifest");
        f << "untouchable steam depotcache manifest\n";
    }

    std::string diffYaml = testDir + "/cache.diff.yaml";
    std::string verFile = testDir + "/cache_version";

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
    assert(res && "CacheMigration::migrate should return true");

    // Verify generic directory was renamed
    assert(!std::filesystem::exists(oldCacheDir) && "Old generic cache dir should be migrated");
    std::string newCacheDir = testDir + "/depots_v2";
    assert(std::filesystem::exists(newCacheDir) && "New generic cache dir should exist");

    // Verify file inside was renamed
    assert(std::filesystem::exists(newCacheDir + "/123.dat.cache") && "File should be renamed to .dat.cache");

    // Verify that manifests, luas, and depotcache directories were STRICTLY PRESERVED and untouched
    assert(std::filesystem::exists(manifestDir + "/123.manifest") && "Manifest directory and files MUST be untouched");
    assert(!std::filesystem::exists(testDir + "/tampered_manifests") && "Manifest migration MUST NOT occur");
    assert(std::filesystem::exists(luaDir + "/test.lua") && "Lua directory and files MUST be untouched");
    assert(!std::filesystem::exists(testDir + "/tampered_luas") && "Lua migration MUST NOT occur");
    assert(std::filesystem::exists(depotcacheDir + "/456_789.manifest") && "Steam depotcache MUST be untouched");
    assert(!std::filesystem::exists(testDir + "/tampered_depotcache") && "Depotcache migration MUST NOT occur");

    // Verify version file
    std::ifstream vf(verFile);
    int v = 0;
    vf >> v;
    assert(v == 1 && "CacheVersion should be 1");

    std::filesystem::remove_all(testDir);
    std::cout << "[TEST] CacheMigration tests passed successfully with manifest/lua preservation!\n";
}

void testMalformedConfigMigration()
{
    std::cout << "[TEST] Running Malformed Config test...\n";
    std::string testDir = "/tmp/sls_test_malformed";
    std::filesystem::remove_all(testDir);
    std::filesystem::create_directories(testDir);

    std::string userConfig = testDir + "/config.yaml";
    std::string diffYaml = testDir + "/config.diff.yaml";

    {
        std::ofstream f(userConfig);
        f << "LogLevels: [unclosed list\nInvalid: {syntax\n";
    }
    {
        std::ofstream f(diffYaml);
        f << "Migrations:\n  1:\n    \"$(LogLevels)$(LogLevelsBasic):\":\n      action: rename\n";
    }

    bool res = ConfigMigration::migrate(userConfig, diffYaml);
    assert(!res && "ConfigMigration should fail cleanly on malformed YAML");
    std::cout << "[TEST] Malformed config safely rejected!\n";

    std::filesystem::remove_all(testDir);
}

void testMissingFiles()
{
    std::cout << "[TEST] Running Missing Files test...\n";
    bool res = ConfigMigration::migrate("/nonexistent/path/config.yaml", "/nonexistent/diff.yaml");
    assert(!res && "ConfigMigration should return false for nonexistent files");
    
    bool cRes = CacheMigration::migrate("/nonexistent/cache.diff.yaml", "/nonexistent/cache_ver");
    assert(!cRes && "CacheMigration should return false for nonexistent files");
    std::cout << "[TEST] Missing files safely handled!\n";
}

void testLegacyMigrationFresh()
{
    std::cout << "[TEST] Running LegacyMigration Fresh Migration test...\n";
    std::string testHome = "/tmp/test_legacy_fresh";
    std::filesystem::remove_all(testHome);

    auto paths = LegacyMigration::buildPaths(testHome);

    // Setup legacy config
    std::filesystem::create_directories(paths.legacyConfigDir);
    {
        std::ofstream f(paths.legacyConfigDir + "/config.yaml");
        f << "LogLevelsBasic: 1\nUserAppIds:\n  - 480\n";
    }
    {
        std::ofstream f(paths.legacyConfigDir + "/depot_keys.yaml");
        f << "1000: dummy_key\n";
    }

    // Setup legacy cache
    std::filesystem::create_directories(paths.legacyCacheDir);
    {
        std::ofstream f(paths.legacyCacheDir + "/app_480.cache");
        f << "cache_data_480\n";
    }

    // Setup legacy manifests
    std::filesystem::create_directories(paths.legacyManifestsDir);
    {
        std::ofstream f(paths.legacyManifestsDir + "/12345.manifest");
        f << "manifest_binary_data\n";
    }

    // Pre-condition: targets and markers do not exist
    assert(!std::filesystem::exists(paths.targetConfigDir));
    assert(!std::filesystem::exists(paths.targetCacheDir));
    assert(!std::filesystem::exists(paths.targetManifestsDir));

    bool ok = LegacyMigration::migrate(paths);
    assert(ok && "LegacyMigration::migrate should return true for fresh migration");

    // 1. Config migrated
    assert(std::filesystem::exists(paths.targetConfigDir + "/config.yaml"));
    assert(std::filesystem::exists(paths.targetConfigDir + "/depot_keys.yaml"));
    assert(std::filesystem::exists(paths.targetConfigDir + "/.migrated_from_slssteam"));

    // 2. Cache migrated
    assert(std::filesystem::exists(paths.targetCacheDir + "/app_480.cache"));

    // 3. Manifests migrated to both primary and alt locations
    assert(std::filesystem::exists(paths.targetManifestsDir + "/12345.manifest"));
    assert(std::filesystem::exists(paths.targetManifestsAltDir + "/12345.manifest"));
    assert(std::filesystem::exists(paths.targetShareDir + "/.migrated_from_slssteam"));

    // 4. Downgrade safety: legacy files NEVER deleted
    assert(std::filesystem::exists(paths.legacyConfigDir + "/config.yaml"));
    assert(std::filesystem::exists(paths.legacyConfigDir + "/depot_keys.yaml"));
    assert(std::filesystem::exists(paths.legacyCacheDir + "/app_480.cache"));
    assert(std::filesystem::exists(paths.legacyManifestsDir + "/12345.manifest"));

    std::filesystem::remove_all(testHome);
    std::cout << "[TEST] Fresh LegacyMigration test passed!\n";
}

void testLegacyMigrationOldAndNewConflict()
{
    std::cout << "[TEST] Running LegacyMigration Old + New Conflict test...\n";
    std::string testHome = "/tmp/test_legacy_conflict";
    std::filesystem::remove_all(testHome);

    auto paths = LegacyMigration::buildPaths(testHome);

    // Old config
    std::filesystem::create_directories(paths.legacyConfigDir);
    {
        std::ofstream f(paths.legacyConfigDir + "/config.yaml");
        f << "Version: old_config\nLogLevelsBasic: 1\n";
    }

    // New config already exists in target
    std::filesystem::create_directories(paths.targetConfigDir);
    {
        std::ofstream f(paths.targetConfigDir + "/config.yaml");
        f << "Version: new_config\nLogLevelsBasic: 5\n";
    }

    g_warnCount = 0;
    bool ok = LegacyMigration::migrate(paths);
    assert(ok && "LegacyMigration::migrate should succeed when both configs exist");

    // New config wins! (Not overwritten)
    std::ifstream in(paths.targetConfigDir + "/config.yaml");
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    assert(content.find("Version: new_config") != std::string::npos && "New config MUST win over legacy config");
    assert(g_warnCount > 0 && "Warning MUST be logged when both old and new configs exist");

    // Marker written
    assert(std::filesystem::exists(paths.targetConfigDir + "/.migrated_from_slssteam"));

    // Legacy config untouched
    std::ifstream legacyIn(paths.legacyConfigDir + "/config.yaml");
    std::string legacyContent((std::istreambuf_iterator<char>(legacyIn)), std::istreambuf_iterator<char>());
    assert(legacyContent.find("Version: old_config") != std::string::npos && "Legacy config MUST be preserved");

    std::filesystem::remove_all(testHome);
    std::cout << "[TEST] Old + New Conflict test passed!\n";
}

void testLegacyMigrationReadOnly()
{
    std::cout << "[TEST] Running LegacyMigration Read-Only test...\n";
    std::string testHome = "/tmp/test_legacy_readonly";
    std::filesystem::remove_all(testHome);

    auto paths = LegacyMigration::buildPaths(testHome);

    // Subtest 1: Read-only source file
    std::filesystem::create_directories(paths.legacyConfigDir);
    const std::string oldFile = paths.legacyConfigDir + "/config.yaml";
    {
        std::ofstream f(oldFile);
        f << "ReadOnly: true\n";
    }
    // Set source file to read-only
    std::filesystem::permissions(oldFile,
        std::filesystem::perms::owner_read | std::filesystem::perms::group_read | std::filesystem::perms::others_read);

    bool ok = LegacyMigration::migrate(paths);
    assert(ok && "Migration of read-only source config should succeed");

    const std::string newFile = paths.targetConfigDir + "/config.yaml";
    assert(std::filesystem::exists(newFile));
    auto perms = std::filesystem::status(newFile).permissions();
    assert((perms & std::filesystem::perms::owner_write) == std::filesystem::perms::none &&
           "Read-only file permissions should be preserved on target");

    // Restore permissions for cleanup
    std::filesystem::permissions(oldFile, std::filesystem::perms::owner_all);
    std::filesystem::permissions(newFile, std::filesystem::perms::owner_all);
    std::filesystem::remove_all(testHome);

    // Subtest 2: Unwritable target directory
    std::string testHome2 = "/tmp/test_legacy_unwritable";
    std::filesystem::remove_all(testHome2);
    auto paths2 = LegacyMigration::buildPaths(testHome2);

    std::filesystem::create_directories(paths2.legacyConfigDir);
    {
        std::ofstream f(paths2.legacyConfigDir + "/config.yaml");
        f << "Test: true\n";
    }

    // Create target dir as unwritable (read-only directory)
    std::filesystem::create_directories(paths2.targetConfigDir);
    std::filesystem::permissions(paths2.targetConfigDir,
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec);

    // Must handle gracefully without crashing or throwing unhandled exception
    g_warnCount = 0;
    bool unwritableRes = LegacyMigration::migrate(paths2);
    assert(!unwritableRes && "Migration into unwritable directory should return false gracefully");
    assert(g_warnCount > 0 && "Warning must be logged when destination is unwritable");

    // Restore permissions for cleanup
    std::filesystem::permissions(paths2.targetConfigDir, std::filesystem::perms::owner_all);
    std::filesystem::remove_all(testHome2);

    std::cout << "[TEST] Read-Only test passed!\n";
}

void testLegacyMigrationPartialRecovery()
{
    std::cout << "[TEST] Running LegacyMigration Partial Recovery test...\n";
    std::string testHome = "/tmp/test_legacy_partial";
    std::filesystem::remove_all(testHome);

    auto paths = LegacyMigration::buildPaths(testHome);

    // Setup legacy config
    std::filesystem::create_directories(paths.legacyConfigDir);
    {
        std::ofstream f(paths.legacyConfigDir + "/config.yaml");
        f << "Recovered: true\n";
    }
    {
        std::ofstream f(paths.legacyConfigDir + "/extra.yaml");
        f << "Extra: data\n";
    }

    // Simulate leftover stale staging directory from an earlier crash/interruption
    std::string staleStaging = paths.targetConfigDir + ".staging";
    std::filesystem::create_directories(staleStaging);
    {
        std::ofstream f(staleStaging + "/broken_partial.tmp");
        f << "corrupted_incomplete_data\n";
    }

    // Ensure marker does NOT exist
    assert(!std::filesystem::exists(paths.targetConfigDir + "/.migrated_from_slssteam"));

    bool ok = LegacyMigration::migrate(paths);
    assert(ok && "Migration with leftover staging should succeed");

    // Stale staging directory cleaned up
    assert(!std::filesystem::exists(staleStaging) && "Leftover staging directory should be cleaned up");

    // Complete migration succeeded atomically
    assert(std::filesystem::exists(paths.targetConfigDir + "/config.yaml"));
    assert(std::filesystem::exists(paths.targetConfigDir + "/extra.yaml"));
    assert(std::filesystem::exists(paths.targetConfigDir + "/.migrated_from_slssteam"));

    std::filesystem::remove_all(testHome);
    std::cout << "[TEST] Partial Recovery test passed!\n";
}

void testLegacyMigrationIdempotency()
{
    std::cout << "[TEST] Running LegacyMigration Idempotency test...\n";
    std::string testHome = "/tmp/test_legacy_idempotent";
    std::filesystem::remove_all(testHome);

    auto paths = LegacyMigration::buildPaths(testHome);

    // Initial setup
    std::filesystem::create_directories(paths.legacyConfigDir);
    {
        std::ofstream f(paths.legacyConfigDir + "/config.yaml");
        f << "Initial: v1\n";
    }

    bool firstRun = LegacyMigration::migrate(paths);
    assert(firstRun && "First migration run should succeed");
    assert(std::filesystem::exists(paths.targetConfigDir + "/.migrated_from_slssteam"));

    // User modifies target config
    {
        std::ofstream f(paths.targetConfigDir + "/config.yaml");
        f << "UpdatedByNaviUser: v2\n";
    }

    g_warnCount = 0;
    // Re-run migration (idempotent call)
    bool secondRun = LegacyMigration::migrate(paths);
    assert(secondRun && "Re-run of migration MUST succeed (idempotent)");

    // Target config MUST NOT be overwritten by legacy config
    std::ifstream in(paths.targetConfigDir + "/config.yaml");
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    assert(content.find("UpdatedByNaviUser: v2") != std::string::npos &&
           "Target config must remain unchanged on re-run");

    // Old config remains untouched
    assert(std::filesystem::exists(paths.legacyConfigDir + "/config.yaml"));
    std::ifstream legacyIn(paths.legacyConfigDir + "/config.yaml");
    std::string legacyContent((std::istreambuf_iterator<char>(legacyIn)), std::istreambuf_iterator<char>());
    assert(legacyContent.find("Initial: v1") != std::string::npos &&
           "Legacy config must remain completely untouched");

    std::filesystem::remove_all(testHome);
    std::cout << "[TEST] Idempotency test passed!\n";
}

int main()
{
    testConfigMigration();
    testCacheMigration();
    testMalformedConfigMigration();
    testMissingFiles();
    testLegacyMigrationFresh();
    testLegacyMigrationOldAndNewConflict();
    testLegacyMigrationReadOnly();
    testLegacyMigrationPartialRecovery();
    testLegacyMigrationIdempotency();
    std::cout << "[ALL UNIT TESTS PASSED]\n";
    return 0;
}
