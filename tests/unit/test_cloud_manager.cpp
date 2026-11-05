#include <gtest/gtest.h>
#include "cloud_manager.hpp"
#include "cloud_view.hpp"
#include "config.hpp"

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

TEST(CloudManagerTest, ListRemoteCloudAppsAndFilesFiltersMetadataAndNormalizesBlobs)
{
    ScopedCloudManagerTestEnv env;
    auto& manager = CCloudManager::Instance();

    fs::path mockStorageDir = env.tempDir / "storage";
    fs::create_directories(mockStorageDir / "999001" / "250900" / "%WinAppDataRoaming%" / "GSE Saves" / "250900" / "remote");
    std::string saveContent = "isaac_save_payload_data";
    {
        std::ofstream ofs(mockStorageDir / "999001" / "250900" / "%WinAppDataRoaming%" / "GSE Saves" / "250900" / "remote" / "rep+persistentgamedata1.dat", std::ios::binary);
        ofs << saveContent;
    }

    fs::path mockCli = env.tempDir / "mock_cloud_cli.sh";
    {
        std::ofstream ofs(mockCli);
        ofs << "#!/bin/sh\n"
            << "if [ \"$1\" = \"list-remote-apps\" ]; then\n"
            << "  echo '{\"success\":true,\"apps\":[{\"app_id\":\"250900\",\"file_count\":5,\"total_size\":99999}]}'\n"
            << "  exit 0\n"
            << "fi\n"
            << "if [ \"$1\" = \"list-remote-app-files\" ]; then\n"
            << "  echo '{\"success\":true,\"complete\":true,\"files\":["
            << "{\"path\":\"999001/250900/cn.cloudredirect\",\"size\":84,\"modified_time\":1793655246},"
            << "{\"path\":\"999001/250900/state.cloudredirect\",\"size\":54993,\"modified_time\":1793655245},"
            << "{\"path\":\"999001/250900/blobs/blobs/92d331d6574c087a77b547fbcc1873392e796ff0/6e8f5607fe4d3fbeb84476929b42442c6ad8fc0ab5ef34ab65279f5e6f2d8bf7\",\"size\":54851,\"modified_time\":1793655244},"
            << "{\"path\":\"999001/250900/blobs/GSE Saves/250900/remote/rep+persistentgamedata1.dat/445f2f56a0c0874872cd2da0a6d1f33049462fbe\",\"size\":4108,\"modified_time\":1793655242},"
            << "{\"path\":\"999001/250900/blobs/GSE Saves/250900/remote/rep+gamestate1.dat/92d331d6574c087a77b547fbcc1873392e796ff0\",\"size\":54851,\"modified_time\":1793655237}"
            << "]}'\n"
            << "  exit 0\n"
            << "fi\n"
            << "exit 0\n";
    }
    chmod(mockCli.c_str(), 0755);

    manager.SetStorageDirForTest(mockStorageDir);
    manager.SetCliPathForTest(mockCli);

    auto files = manager.ListAppCloudFiles(250900, "gdrive", 999001);
    ASSERT_EQ(files.size(), 2u);
    EXPECT_EQ(files[0].folder, "WinAppDataRoaming");
    EXPECT_EQ(files[0].fileName, "GSE Saves/250900/remote/rep+gamestate1.dat");
    EXPECT_EQ(files[0].sizeBytes, 54851u);
    EXPECT_EQ(files[0].modifiedTime, 1793655237u);

    EXPECT_EQ(files[1].folder, "WinAppDataRoaming");
    EXPECT_EQ(files[1].fileName, "GSE Saves/250900/remote/rep+persistentgamedata1.dat");
    EXPECT_EQ(files[1].sizeBytes, 4108u);
    EXPECT_EQ(files[1].modifiedTime, 1793655242u);

    auto apps = manager.ListCloudApps("gdrive", 999001);
    ASSERT_EQ(apps.size(), 1u);
    EXPECT_EQ(apps[0].appId, 250900u);
    EXPECT_EQ(apps[0].fileCount, 2u);
    EXPECT_EQ(apps[0].totalSizeBytes, 54851u + 4108u);

    std::string outData, outName, outErr;
    bool readOk = manager.ReadAppCloudFile(250900, "GSE Saves/250900/remote/rep+persistentgamedata1.dat", "gdrive", 999001, outData, outName, outErr);
    EXPECT_TRUE(readOk) << outErr;
    EXPECT_EQ(outData, saveContent);
    EXPECT_EQ(outName, "rep+persistentgamedata1.dat");

    // Path traversal must be rejected
    bool traversalOk = manager.ReadAppCloudFile(250900, "../../etc/passwd", "gdrive", 999001, outData, outName, outErr);
    EXPECT_FALSE(traversalOk);

    std::string appsJson = Navi::CloudView::renderCloudAppsJson("gdrive", apps);
    EXPECT_NE(appsJson.find("\"appId\": 250900"), std::string::npos);
    EXPECT_NE(appsJson.find("\"fileCount\": 2"), std::string::npos);

    std::string filesJson = Navi::CloudView::renderAppCloudFilesJson(250900, "The Binding of Isaac: Rebirth", "gdrive", true, files);
    EXPECT_NE(filesJson.find("\"gameTitle\": \"The Binding of Isaac: Rebirth\""), std::string::npos);
    EXPECT_NE(filesJson.find("\"fileName\": \"GSE Saves/250900/remote/rep+persistentgamedata1.dat\""), std::string::npos);

    manager.SetStorageDirForTest({});
    manager.SetCliPathForTest({});
}

TEST(CloudManagerTest, ResolveAppTitleFromManifestsAndAutocloudFallbacks)
{
    auto& manager = CCloudManager::Instance();

    // Standard Valve AppIDs
    EXPECT_EQ(manager.ResolveAppTitleFromManifests(7), "Steam Client");
    EXPECT_EQ(manager.ResolveAppTitleFromManifests(241100), "Steam Input Configs");
    EXPECT_EQ(manager.ResolveAppTitleFromManifests(250820), "SteamVR");

    // Invalid AppID
    EXPECT_EQ(manager.ResolveAppTitleFromManifests(0), "");

    // Test environment for Lua comment and compatdata fallbacks
    ScopedCloudManagerTestEnv env;
    fs::path steamRoot = env.tempDir / "steam";
    fs::path stplugDir = steamRoot / "config" / "stplug-in";
    std::error_code ec;
    fs::create_directories(stplugDir, ec);

    // 1. Test Lua comment fallback
    {
        std::ofstream ofs(stplugDir / "1001.lua");
        ofs << "addappid(1001, 1, 0) -- My Custom Lua Game\n";
    }

    manager.SetSteamRootForTest(steamRoot);
    EXPECT_EQ(manager.ResolveAppTitleFromManifests(1001), "My Custom Lua Game");

    // 2. Test compatdata autocloud fallback at depth <= 3
    fs::path pfxLocal = steamRoot / "steamapps" / "compatdata" / "1002" / "pfx" / "drive_c" / "users" / "steamuser" / "AppData" / "Local";
    fs::path saveDirDepth2 = pfxLocal / "Game_Studio" / "My_Cool_Game";
    fs::create_directories(saveDirDepth2, ec);
    {
        std::ofstream ofs(saveDirDepth2 / "steam_autocloud.vdf");
        ofs << "autocloud data\n";
    }
    EXPECT_EQ(manager.ResolveAppTitleFromManifests(1002), "My Cool Game");

    // 3. Test Lua comment check takes precedence over compatdata filesystem traversal
    {
        std::ofstream ofs(stplugDir / "1003.lua");
        ofs << "addappid(1003, 1, 0) -- Lua Preferred Title\n";
    }
    fs::path pfxLocal1003 = steamRoot / "steamapps" / "compatdata" / "1003" / "pfx" / "drive_c" / "users" / "steamuser" / "AppData" / "Local";
    fs::path saveDir1003 = pfxLocal1003 / "Compatdata_Fallback_Title";
    fs::create_directories(saveDir1003, ec);
    {
        std::ofstream ofs(saveDir1003 / "steam_autocloud.vdf");
        ofs << "autocloud data\n";
    }
    EXPECT_EQ(manager.ResolveAppTitleFromManifests(1003), "Lua Preferred Title");

    // 4. Test compatdata recursion depth bounded to max 3 levels (nested deeper than 3 is ignored)
    fs::path pfxLocal1004 = steamRoot / "steamapps" / "compatdata" / "1004" / "pfx" / "drive_c" / "users" / "steamuser" / "AppData" / "Local";
    fs::path tooDeepDir = pfxLocal1004 / "D1" / "D2" / "D3" / "D4" / "Deep_Game";
    fs::create_directories(tooDeepDir, ec);
    {
        std::ofstream ofs(tooDeepDir / "steam_autocloud.vdf");
        ofs << "autocloud data\n";
    }
    EXPECT_EQ(manager.ResolveAppTitleFromManifests(1004), "");

    // 5. Depot IDs referenced by a parent game's Lua plugin resolve to the game title.
    {
        std::ofstream ofs(stplugDir / "2001.lua");
        ofs << "addappid(2001, 1, 0) -- Parent Game Title\n"
               "addappid(2002, 1, \"depotkey\") -- Depot 2002\n"
               "setManifestid(2002, \"123456789\")\n";
    }
    EXPECT_EQ(manager.ResolveAppTitleFromManifests(2002), "Parent Game Title");

    // 6. If the parent plugin has no title comment, fall back to the parent app manifest.
    {
        std::ofstream ofs(stplugDir / "3001.lua");
        ofs << "addappid(3001, 1, 0)\n"
               "addappid(3002, 1, \"depotkey\") -- Depot 3002\n"
               "setManifestid(3002, \"123456789\")\n";
        fs::create_directories(steamRoot / "steamapps", ec);
        std::ofstream mf(steamRoot / "steamapps" / "appmanifest_3001.acf");
        mf << "\"AppState\"\n{\n\t\"appid\"\t\t\"3001\"\n\t\"name\"\t\t\"Manifest Parent Title\"\n}\n";
    }
    EXPECT_EQ(manager.ResolveAppTitleFromManifests(3002), "Manifest Parent Title");

    // 7. Depot IDs referenced by another game's InstalledDepots block resolve to the owning game.
    // e.g. Hollow Knight: Silksong (App 1030300) child depot 1030303
    {
        std::ofstream mf(steamRoot / "steamapps" / "appmanifest_1030300.acf");
        mf << "\"AppState\"\n"
              "{\n"
              "\t\"appid\"\t\t\"1030300\"\n"
              "\t\"name\"\t\t\"Hollow Knight: Silksong\"\n"
              "\t\"InstalledDepots\"\n"
              "\t{\n"
              "\t\t\"1030301\"\n"
              "\t\t{\n"
              "\t\t\t\"manifest\"\t\t\"1111111111111111111\"\n"
              "\t\t}\n"
              "\t\t\"1030303\"\n"
              "\t\t{\n"
              "\t\t\t\"manifest\"\t\t\"3333333333333333333\"\n"
              "\t\t}\n"
              "\t}\n"
              "}\n";
    }
    EXPECT_EQ(manager.ResolveAppTitleFromManifests(1030303), "Hollow Knight: Silksong");
    EXPECT_EQ(manager.ResolveAppTitleFromManifests(1030300), "Hollow Knight: Silksong");

    manager.SetSteamRootForTest({});
}

TEST(CloudManagerTest, ResolveAppTitleFromInstalledDepotsManifest)
{
    ScopedCloudManagerTestEnv env;
    auto& manager = CCloudManager::Instance();

    fs::path steamRoot = env.tempDir / "steam";
    fs::path steamApps = steamRoot / "steamapps";
    std::error_code ec;
    fs::create_directories(steamApps, ec);

    // Create appmanifest_1030300.acf with InstalledDepots mapping child depot 1030303 -> 1030300
    {
        std::ofstream mf(steamApps / "appmanifest_1030300.acf");
        mf << "\"AppState\"\n"
              "{\n"
              "\t\"appid\"\t\t\"1030300\"\n"
              "\t\"name\"\t\t\"Hollow Knight: Silksong\"\n"
              "\t\"InstalledDepots\"\n"
              "\t{\n"
              "\t\t\"1030301\"\n"
              "\t\t{\n"
              "\t\t\t\"manifest\"\t\t\"1111111111111111111\"\n"
              "\t\t}\n"
              "\t\t\"1030303\"\n"
              "\t\t{\n"
              "\t\t\t\"manifest\"\t\t\"3333333333333333333\"\n"
              "\t\t}\n"
              "\t}\n"
              "}\n";
    }

    // Create dummy appmanifest_1030303.acf with placeholder title "App 1030303"
    // to verify that placeholder titles are rejected and fallback scans parent InstalledDepots.
    {
        std::ofstream mf(steamApps / "appmanifest_1030303.acf");
        mf << "\"AppState\"\n"
              "{\n"
              "\t\"appid\"\t\t\"1030303\"\n"
              "\t\"name\"\t\t\"App 1030303\"\n"
              "}\n";
    }

    manager.SetSteamRootForTest(steamRoot);

    // Resolving depot 1030303 should ignore the placeholder "App 1030303"
    // and resolve to the parent game title "Hollow Knight: Silksong"
    EXPECT_EQ(manager.ResolveAppTitleFromManifests(1030303), "Hollow Knight: Silksong");

    // Parent app 1030300 itself should also resolve directly to "Hollow Knight: Silksong"
    EXPECT_EQ(manager.ResolveAppTitleFromManifests(1030300), "Hollow Knight: Silksong");

    // Non-existent depot should return empty string
    EXPECT_EQ(manager.ResolveAppTitleFromManifests(9999999), "");

    manager.SetSteamRootForTest({});
}

TEST(CloudManagerTest, DiscoverAndListLuaAndManifestOnlyGames)
{
    ScopedCloudManagerTestEnv env;
    auto& manager = CCloudManager::Instance();

    fs::path steamRoot = env.tempDir / "steam";
    fs::path stplugDir = steamRoot / "config" / "stplug-in";
    fs::path depotcacheDir = steamRoot / "depotcache";
    std::error_code ec;
    fs::create_directories(stplugDir, ec);
    fs::create_directories(depotcacheDir, ec);

    const std::string luaContent = "addappid(480, 1, 0) -- Spacewar\nsetManifestid(481, \"123456789\")\n";
    const std::string provContent = "{\"provider\":\"gdrive\"}";
    const std::string mfContent = "binary-manifest-bytes-481";

    {
        std::ofstream ofs(stplugDir / "480.lua");
        ofs << luaContent;
    }
    {
        std::ofstream ofs(stplugDir / "480.provider");
        ofs << provContent;
    }
    {
        std::ofstream ofs(depotcacheDir / "481_123456789.manifest");
        ofs << mfContent;
    }

    manager.SetSteamRootForTest(steamRoot);

    // 1. App discovery in local-only mode should discover app 480 and title "Spacewar" from Lua comment
    auto apps = manager.ListCloudApps("local-only", 999001);
    bool found480 = false;
    for (const auto& app : apps)
    {
        if (app.appId == 480)
        {
            found480 = true;
            EXPECT_EQ(app.gameTitle, "Spacewar");
            EXPECT_EQ(app.fileCount, 3u);
            EXPECT_EQ(app.totalSizeBytes, luaContent.size() + provContent.size() + mfContent.size());
            break;
        }
    }
    EXPECT_TRUE(found480);

    // 2. Listing files should include the lua, provider, and manifest files
    auto files = manager.ListAppCloudFiles(480, "local-only", 999001);
    ASSERT_EQ(files.size(), 3u);

    bool hasLua = false, hasProv = false, hasMf = false;
    for (const auto& f : files)
    {
        if (f.fileName == "stplug-in/480.lua")
        {
            hasLua = true;
            EXPECT_EQ(f.folder, "stplug-in");
            EXPECT_EQ(f.sizeBytes, luaContent.size());
            EXPECT_TRUE(f.inLocalMirror);
        }
        else if (f.fileName == "stplug-in/480.provider")
        {
            hasProv = true;
            EXPECT_EQ(f.folder, "stplug-in");
            EXPECT_EQ(f.sizeBytes, provContent.size());
            EXPECT_TRUE(f.inLocalMirror);
        }
        else if (f.fileName == "depotcache/481_123456789.manifest")
        {
            hasMf = true;
            EXPECT_EQ(f.folder, "depotcache");
            EXPECT_EQ(f.sizeBytes, mfContent.size());
            EXPECT_TRUE(f.inLocalMirror);
        }
    }
    EXPECT_TRUE(hasLua);
    EXPECT_TRUE(hasProv);
    EXPECT_TRUE(hasMf);

    // 3. Reading files
    std::string outData, outName, outErr;
    bool readLuaOk = manager.ReadAppCloudFile(480, "stplug-in/480.lua", "local-only", 999001, outData, outName, outErr);
    EXPECT_TRUE(readLuaOk) << outErr;
    EXPECT_EQ(outData, luaContent);
    EXPECT_EQ(outName, "480.lua");

    bool readMfOk = manager.ReadAppCloudFile(480, "depotcache/481_123456789.manifest", "local-only", 999001, outData, outName, outErr);
    EXPECT_TRUE(readMfOk) << outErr;
    EXPECT_EQ(outData, mfContent);
    EXPECT_EQ(outName, "481_123456789.manifest");

    manager.SetSteamRootForTest({});
}

TEST(CloudManagerTest, GroupAddonsAndChildDepotsIntoMainGame)
{
    ScopedCloudManagerTestEnv env;
    auto& manager = CCloudManager::Instance();

    fs::path steamRoot = env.tempDir / "steam";
    fs::path steamApps = steamRoot / "steamapps";
    fs::path stplugDir = steamRoot / "config" / "stplug-in";
    fs::path mockStorageDir = env.tempDir / "storage";
    std::error_code ec;
    fs::create_directories(steamApps, ec);
    fs::create_directories(stplugDir, ec);

    // 1. Create parent manifest for Hollow Knight: Silksong (App 1030300) with child depot 1030303
    {
        std::ofstream mf(steamApps / "appmanifest_1030300.acf");
        mf << "\"AppState\"\n"
              "{\n"
              "\t\"appid\"\t\t\"1030300\"\n"
              "\t\"name\"\t\t\"Hollow Knight: Silksong\"\n"
              "\t\"InstalledDepots\"\n"
              "\t{\n"
              "\t\t\"1030301\"\n"
              "\t\t{\n"
              "\t\t\t\"manifest\"\t\t\"1111111111111111111\"\n"
              "\t\t}\n"
              "\t\t\"1030303\"\n"
              "\t\t{\n"
              "\t\t\t\"manifest\"\t\t\"3333333333333333333\"\n"
              "\t\t}\n"
              "\t}\n"
              "}\n";
    }

    // 2. Create Lua script for another game (2937910) with child depot 2937911 and common redistributable
    {
        std::ofstream ofs(stplugDir / "2937910.lua");
        ofs << "addappid(2937910, 1, 0) -- Don't Disturb Your STEPMOM\n"
            << "addappid(2937911, 1, \"depotkey\") -- Child Depot 2937911\n"
            << "addappid(228980, 1, \"redist\") -- Common Redistributables\n";
    }

    manager.SetSteamRootForTest(steamRoot);
    manager.SetStorageDirForTest(mockStorageDir);

    // 3. Test GetParentAppIdForDepot
    EXPECT_EQ(manager.GetParentAppIdForDepot(1030303), 1030300u);
    EXPECT_EQ(manager.GetParentAppIdForDepot(1030301), 1030300u);
    EXPECT_EQ(manager.GetParentAppIdForDepot(2937911), 2937910u);
    EXPECT_EQ(manager.GetParentAppIdForDepot(228980), 0u); // Shared redistributables ignored
    EXPECT_EQ(manager.GetParentAppIdForDepot(1030300), 0u); // Parent itself is not a child

    // 4. Test GetChildDepotIdsForApp
    auto hkChildren = manager.GetChildDepotIdsForApp(1030300);
    EXPECT_NE(std::find(hkChildren.begin(), hkChildren.end(), 1030303u), hkChildren.end());
    EXPECT_NE(std::find(hkChildren.begin(), hkChildren.end(), 1030301u), hkChildren.end());

    auto stepmomChildren = manager.GetChildDepotIdsForApp(2937910);
    EXPECT_NE(std::find(stepmomChildren.begin(), stepmomChildren.end(), 2937911u), stepmomChildren.end());
    EXPECT_EQ(std::find(stepmomChildren.begin(), stepmomChildren.end(), 228980u), stepmomChildren.end());

    // 5. Create storage save files for parent 1030300 and child depot 1030303
    fs::path parentStorage = mockStorageDir / "999001" / "1030300" / "remote";
    fs::path childStorage = mockStorageDir / "999001" / "1030303" / "remote";
    fs::create_directories(parentStorage, ec);
    fs::create_directories(childStorage, ec);

    std::string parentData = "parent_save_data";
    std::string childData = "child_save_data_1030303";
    {
        std::ofstream(parentStorage / "user1.dat") << parentData;
        std::ofstream(childStorage / "user2.dat") << childData;
    }

    // 6. Test ListCloudApps: verify child depot 1030303 is merged into 1030300
    auto apps = manager.ListCloudApps("local-only", 999001);

    bool foundChildAsTopLevel = false;
    const CloudAppSummary_t* hkSummary = nullptr;
    for (const auto& app : apps)
    {
        if (app.appId == 1030303) foundChildAsTopLevel = true;
        if (app.appId == 1030300) hkSummary = &app;
    }
    EXPECT_FALSE(foundChildAsTopLevel);
    ASSERT_NE(hkSummary, nullptr);
    EXPECT_EQ(hkSummary->gameTitle, "Hollow Knight: Silksong");
    EXPECT_EQ(hkSummary->fileCount, 2u);
    EXPECT_EQ(hkSummary->totalSizeBytes, parentData.size() + childData.size());
    EXPECT_NE(std::find(hkSummary->childAppIds.begin(), hkSummary->childAppIds.end(), 1030303u),
              hkSummary->childAppIds.end());

    // 7. Test ListAppCloudFiles: verify tagged folder label for child depot
    auto files = manager.ListAppCloudFiles(1030300, "local-only", 999001);
    ASSERT_EQ(files.size(), 2u);
    bool foundParentFile = false;
    bool foundChildFile = false;
    for (const auto& f : files)
    {
        if (f.fileName == "remote/user1.dat")
        {
            foundParentFile = true;
            EXPECT_EQ(f.folder, "SteamCloudRemote");
            EXPECT_EQ(f.sizeBytes, parentData.size());
        }
        else if (f.fileName == "remote/user2.dat")
        {
            foundChildFile = true;
            EXPECT_EQ(f.folder, "Depot 1030303 / SteamCloudRemote");
            EXPECT_EQ(f.sizeBytes, childData.size());
        }
    }
    EXPECT_TRUE(foundParentFile);
    EXPECT_TRUE(foundChildFile);

    // 8. Test ReadAppCloudFile for parent and child saves via parent appId
    std::string readContent, downloadName, err;
    bool readP = manager.ReadAppCloudFile(1030300, "remote/user1.dat", "local-only", 999001, readContent, downloadName, err);
    EXPECT_TRUE(readP) << err;
    EXPECT_EQ(readContent, parentData);
    EXPECT_EQ(downloadName, "user1.dat");

    bool readC = manager.ReadAppCloudFile(1030300, "remote/user2.dat", "local-only", 999001, readContent, downloadName, err);
    EXPECT_TRUE(readC) << err;
    EXPECT_EQ(readContent, childData);
    EXPECT_EQ(downloadName, "user2.dat");

    // 9. Test renderCloudAppsJson includes childAppIds
    std::string json = Navi::CloudView::renderCloudAppsJson("local-only", apps);
    EXPECT_NE(json.find("\"childAppIds\": ["), std::string::npos);
    EXPECT_NE(json.find("1030303"), std::string::npos);

    manager.SetSteamRootForTest({});
    manager.SetStorageDirForTest({});
}

/**
 * Scan-once regression test for ListCloudApps (cloud-apps-inventory-speedup D1).
 * Builds a small fixture library (manifests, lua plugins, depotcache manifests,
 * storage mirrors) with enough discovered candidates that today's per-candidate
 * rescans blow past any scan-once bound, then measures the counters across TWO
 * consecutive ListCloudApps passes. Also guards that the second pass still sees
 * library changes (invalidation, not a stale cache).
 */
TEST(CloudManagerTest, InventoryScanIndexReadsLibraryOncePerPass)
{
    ScopedCloudManagerTestEnv env;
    auto& manager = CCloudManager::Instance();

    fs::path steamRoot = env.tempDir / "steam";
    fs::path steamApps = steamRoot / "steamapps";
    fs::path stplugDir = steamRoot / "config" / "stplug-in";
    fs::path depotcacheDir = steamRoot / "depotcache";
    fs::path mockStorageDir = env.tempDir / "storage";
    std::error_code ec;
    fs::create_directories(steamApps, ec);
    fs::create_directories(stplugDir, ec);
    fs::create_directories(depotcacheDir, ec);

    // 8 parent games, each with an installed child depot and a storage mirror,
    // plus 4 lua-only games with child depots and depotcache manifests.
    constexpr unsigned kGames = 8;
    constexpr unsigned kLuaGames = 4;

    for (unsigned i = 0; i < kGames; ++i)
    {
        const std::string app = std::to_string(1500000u + i);
        const std::string child = std::to_string(1501000u + i);
        {
            std::ofstream mf(steamApps / ("appmanifest_" + app + ".acf"));
            mf << "\"AppState\"\n"
                  "{\n"
                  "\t\"appid\"\t\t\"" << app << "\"\n"
                  "\t\"name\"\t\t\"Triangle Zone " << i << "\"\n"
                  "\t\"InstalledDepots\"\n"
                  "\t{\n"
                  "\t\t\"" << child << "\"\n"
                  "\t\t{\n"
                  "\t\t\t\"manifest\"\t\t\"111111111111111111" << i << "\"\n"
                  "\t\t}\n"
                  "\t}\n"
                  "}\n";
        }
        fs::create_directories(mockStorageDir / "999001" / app / "remote", ec);
        { std::ofstream f(mockStorageDir / "999001" / app / "remote" / "save.dat"); f << "data-" << app; }
        fs::create_directories(mockStorageDir / "999001" / child / "remote", ec);
        { std::ofstream f(mockStorageDir / "999001" / child / "remote" / "child.dat"); f << "child-" << child; }
    }

    for (unsigned i = 0; i < kLuaGames; ++i)
    {
        const std::string app = std::to_string(1600000u + i);
        const std::string child0 = std::to_string(1601000u + i);
        const std::string child1 = std::to_string(1602000u + i);
        {
            std::ofstream lf(stplugDir / (app + ".lua"));
            lf << "addappid(" << app << ", 1, 0) -- Lua Game " << i << "\n"
               << "addappid(" << child0 << ", 1, \"dk0\")\n"
               << "addappid(" << child1 << ", 1, \"dk1\")\n";
        }
        fs::create_directories(mockStorageDir / "999001" / child0 / "remote", ec);
        { std::ofstream f(mockStorageDir / "999001" / child0 / "remote" / "save.bin"); f << "save-" << child0; }
    }

    unsigned manifestCount = 0;
    for (const auto& e : fs::directory_iterator(steamApps, ec))
        if (e.path().extension() == ".acf") manifestCount++;
    unsigned luaCount = 0;
    for (const auto& e : fs::directory_iterator(stplugDir, ec))
        if (e.path().extension() == ".lua") luaCount++;
    ASSERT_EQ(manifestCount, kGames);
    ASSERT_EQ(luaCount, kLuaGames);

    manager.SetSteamRootForTest(steamRoot);   // triggers production library paths
    manager.SetStorageDirForTest(mockStorageDir);

    manager.ResetScanCountersForTest();
    auto apps1 = manager.ListCloudApps("local-only", 999001);
    auto apps2 = manager.ListCloudApps("local-only", 999001);
    auto counters = manager.GetScanCountersForTest();

    // Sanity: discovery found all games (8 manifest parents + 4 lua parents)
    size_t merged = 0;
    for (const auto& a : apps1)
        merged += a.childAppIds.size();
    ASSERT_EQ(apps1.size(), kGames + kLuaGames);
    ASSERT_EQ(merged, kGames + 2u * kLuaGames); // each lua game has 2 child depots

    // Scan-once bounds: one manifest pass per ListCloudApps (+ margin for the
    // single title lookup pass that the index' title cache absorbs), lua the
    // same. Today: every candidate rescans every manifest (~games × manifests),
    // and every top-level app re-parses every lua twice.
    EXPECT_LE(counters.manifestFilesRead, static_cast<uint64_t>(manifestCount) * 3u)
        << "manifest reads=" << counters.manifestFilesRead;
    EXPECT_LE(counters.luaFilesParsed, static_cast<uint64_t>(luaCount) * 4u)
        << "lua parses=" << counters.luaFilesParsed;

    // Invalidation guard (green test, not scan-bound): after adding a new child
    // depot for game 0 both via manifest edit, the second pass reflects it.
    {
        std::ofstream mf(steamApps / "appmanifest_1500000.acf");
        mf << "\"AppState\"\n"
              "{\n"
              "\t\"appid\"\t\t\"1500000\"\n"
              "\t\"name\"\t\t\"Triangle Zone 0\"\n"
              "\t\"InstalledDepots\"\n"
              "\t{\n"
              "\t\t\"1501000\"\n"
              "\t\t{\n"
              "\t\t\t\"manifest\"\t\t\"1111111111111111110\"\n"
              "\t\t}\n"
              "\t\t\"1501007\"\n"
              "\t\t{\n"
              "\t\t\t\"manifest\"\t\t\"7777777777777777777\"\n"
              "\t\t}\n"
              "\t}\n"
              "}\n";
    }
    manager.ResetScanCountersForTest();
    auto apps3 = manager.ListCloudApps("local-only", 999001);
    const CloudAppSummary_t* tz0 = nullptr;
    for (const auto& a : apps3)
        if (a.appId == 1500000) tz0 = &a;
    ASSERT_NE(tz0, nullptr);
    EXPECT_NE(std::find(tz0->childAppIds.begin(), tz0->childAppIds.end(), 1501000u),
              tz0->childAppIds.end());
    EXPECT_NE(std::find(tz0->childAppIds.begin(), tz0->childAppIds.end(), 1501007u),
              tz0->childAppIds.end());

    manager.SetSteamRootForTest({});
    manager.SetStorageDirForTest({});
}

/**
 * Regression test for the live-library grouping inversion found while
 * verifying D1 against the real Steam library (2026-11-03 report addendum).
 *
 * Real file shape (e.g. 3856460.lua):
 *     addappid(3856461, 0, "<key>")   <- parsed appId = 3856461 (first addappid)
 *     setManifestid(3856461, "...")
 *     addappid(3856460)               <- stem app listed keyless (parser: dlc)
 *
 * Legacy FindDepotOwnerInLuaPlugins has TWO skip guards per file/depot query:
 *   1. stem == depot id   (the file named after the depot never owns it), and
 *   2. resolved parent == depot id.
 * Without guard 1 the index mapped parentOfDepot(3856460) = 3856461 and the
 * inventory flipped which app of the pair is listed with children.
 */
TEST(CloudManagerTest, InventoryLuaSelflistedStemAppDoesNotInvertOwnership)
{
    ScopedCloudManagerTestEnv env;
    auto& manager = CCloudManager::Instance();

    fs::path steamRoot = env.tempDir / "steam";
    fs::path stplugDir = steamRoot / "config" / "stplug-in";
    std::error_code ec;
    fs::create_directories(stplugDir, ec);

    constexpr AppId_t kSelfApp = 3856460;
    constexpr AppId_t kChildDepot = 3856461;

    {
        std::ofstream lf(stplugDir / (std::to_string(kSelfApp) + ".lua"));
        lf << "addappid(" << kChildDepot << ", 0, \"23db969267704646917ace252094fce4d1e37e7e039bccae85a33a958038fc4b\") -- Cursed Words: The Word Game That Isn't\n"
           << "setManifestid(" << kChildDepot << ", \"9170837311699582332\")\n"
           << "addappid(" << kSelfApp << ")\n";
    }

    manager.SetSteamRootForTest(steamRoot);
    manager.SetStorageDirForTest({});

    // 1. The stem app is never owned by anything from its own file (guard 1).
    EXPECT_EQ(manager.GetParentAppIdForDepot(kSelfApp), 0u);

    // 2. The child depot's owner is also not resolvable from this file: the
    //    parsed appId equals the queried depot (guard 2 shot-circuits it).
    EXPECT_EQ(manager.GetParentAppIdForDepot(kChildDepot), 0u);

    // 3. The stem app still collects its child depots via its own file.
    const std::vector<AppId_t> children = manager.GetChildDepotIdsForApp(kSelfApp);
    EXPECT_NE(std::find(children.begin(), children.end(), kChildDepot), children.end());

    // 4. Title falls back to the lua trailing comment (fallback 3).
    EXPECT_EQ(manager.ResolveAppTitleFromManifests(kSelfApp),
              "Cursed Words: The Word Game That Isn't");

    manager.SetSteamRootForTest({});
    manager.SetStorageDirForTest({});
}

/**
 * D3 regression gate (spec test #4, renamed as `UpsertFileIndexIsLinearNotQuadratic`):
 * upsertFile must resolve a logical fileName with an O(1) lookup so that an app
 * listing N mirrored files performs O(N) upsert work, not N*(N-1)/2 comparisons.
 * Instrumented via ScanCounters::fileUpsertProbes (counted string comparisons).
 */
TEST(CloudManagerTest, UpsertFileIndexIsLinearNotQuadratic)
{
    ScopedCloudManagerTestEnv env;
    auto& manager = CCloudManager::Instance();

    constexpr AppId_t kApp = 5100100;
    constexpr uint32_t kAcct = 999003;
    constexpr size_t kMirrorFiles = 1200;

    fs::path storageDir = env.tempDir / "storage";
    fs::path appDir = storageDir / std::to_string(kAcct) / std::to_string(kApp);
    std::error_code ec;
    fs::create_directories(appDir, ec);

    for (size_t i = 0; i < kMirrorFiles; ++i)
    {
        std::ofstream f(appDir / ("saves_slot_" + std::to_string(i) + ".dat"));
        f << "x" << i;
    }
    // One duplicate-name upsert from a different source exercises the merge path
    // (remote listing is not reachable in unit mode, so emulate the upsert twice
    // via storage files with different capitalization-free relPaths is not
    // possible; the important property is the lookup cost, covered by probes).

    manager.SetStorageDirForTest(storageDir);
    manager.SetSteamRootForTest({});

    manager.ResetScanCountersForTest();
    std::vector<CloudFileEntry_t> files = manager.ListAppCloudFiles(kApp, "local-only", kAcct);
    ASSERT_EQ(files.size(), kMirrorFiles);

    const auto counters = manager.GetScanCountersForTest();
    // Linear expectation: one lookup per upsert (N inserts), plus bounded extra
    // probes for metadata/merge touches. Quadratic legacy code performs
    // N*(N-1)/2 = 719,400 comparisons for this fixture - far past the bound.
    EXPECT_LT(counters.fileUpsertProbes, 4 * kMirrorFiles)
        << "fileUpsertProbes=" << counters.fileUpsertProbes
        << " for " << kMirrorFiles << " mirror files (expected ~O(N));";

    manager.SetStorageDirForTest({});
    manager.SetSteamRootForTest({});
}

/**
 * D3 (spec): `ListAppCloudFiles`'s child-file dedup must not linearly scan
 * `results` per child file. Legacy code performed O(baseFiles * childFiles)
 * comparisons; the fix is an O(1) set lookup per child file, instrumented via
 * ScanCounters::fileDedupProbes.
 */
TEST(CloudManagerTest, ChildFileDedupIsLinearNotQuadratic)
{
    ScopedCloudManagerTestEnv env;
    auto& manager = CCloudManager::Instance();

    constexpr AppId_t kBaseApp = 6200100;
    constexpr AppId_t kChildDepot = 6200101;
    constexpr uint32_t kAcct = 999004;
    constexpr size_t kBaseFiles = 600;
    constexpr size_t kChildFiles = 600;
    constexpr size_t kOverlap = 20;

    fs::path steamRoot = env.tempDir / "steam";
    fs::path stplugDir = steamRoot / "config" / "stplug-in";
    std::error_code ec;
    fs::create_directories(stplugDir, ec);
    {
        std::ofstream lf(stplugDir / (std::to_string(kBaseApp) + ".lua"));
        lf << "addappid(" << kBaseApp << ")\n"
           << "addappid(" << kChildDepot << ")\n";
    }

    fs::path storageDir = env.tempDir / "storage";
    fs::path baseDir = storageDir / std::to_string(kAcct) / std::to_string(kBaseApp);
    fs::path childDir = storageDir / std::to_string(kAcct) / std::to_string(kChildDepot);
    fs::create_directories(baseDir, ec);
    fs::create_directories(childDir, ec);
    for (size_t i = 0; i < kBaseFiles; ++i)
    {
        std::ofstream f(baseDir / ("base_" + std::to_string(i) + ".dat"));
        f << "b" << i;
    }
    for (size_t i = 0; i < kChildFiles; ++i)
    {
        const bool overlap = i < kOverlap;
        std::ofstream f(childDir / ((overlap ? "base_" + std::to_string(i) + ".dat"
                                             : "child_" + std::to_string(i) + ".dat")));
        f << "c" << i;
    }

    manager.SetSteamRootForTest(steamRoot);
    manager.SetStorageDirForTest(storageDir);

    manager.ResetScanCountersForTest();
    std::vector<CloudFileEntry_t> files = manager.ListAppCloudFiles(kBaseApp, "local-only", kAcct);

    // Dedupe contract: child-overlap fileNames must appear exactly once.
    // (Counts are fixture-dependent - e.g. 3d lists the base app's own lua as
    // an extra entry - so instead assert there is NO duplicate fileName.)
    EXPECT_EQ(std::adjacent_find(files.begin(), files.end(),
                                 [](const CloudFileEntry_t& a, const CloudFileEntry_t& b) {
                                     return a.fileName == b.fileName;
                                 }),
              files.end());

    const auto counters = manager.GetScanCountersForTest();
    // Linear expectation: one set lookup per child file (+ a bounded number of
    // extra probes). Legacy linear scanning performs kChildFiles * ~kBaseFiles
    // comparisons (>= 300,000 for this fixture) - far past the bound.
    EXPECT_LT(counters.fileDedupProbes, 3 * kChildFiles)
        << "fileDedupProbes=" << counters.fileDedupProbes
        << " for " << kChildFiles << " child files (expected ~O(N));";

    manager.SetSteamRootForTest({});
    manager.SetStorageDirForTest({});
}

TEST(CloudManagerTest, FoldDlcAppIdsIntoParentGameUsingDlcData)
{
    ScopedCloudManagerTestEnv env;
    auto& manager = CCloudManager::Instance();

    fs::path steamRoot = env.tempDir / "steam";
    fs::path mockStorageDir = env.tempDir / "storage";
    std::error_code ec;
    fs::create_directories(steamRoot, ec);
    fs::create_directories(mockStorageDir, ec);

    const AppId_t parentAppId = 500000;
    const AppId_t dlcAppId = 500001;

    // Configure DlcData mapping in g_config (no manifest or lua linkage exists)
    std::unordered_map<AppId_t, CConfig::CDlcData> dlcMap;
    CConfig::CDlcData dlcEntry;
    dlcEntry.parentId = parentAppId;
    dlcEntry.dlcIds[dlcAppId] = "Cool Expansion Pack";
    dlcMap[parentAppId] = dlcEntry;
    g_config.dlcData.set(dlcMap);

    manager.SetSteamRootForTest(steamRoot);
    manager.SetStorageDirForTest(mockStorageDir);

    // 1. Verify child->parent reverse lookup via DlcData
    EXPECT_EQ(manager.GetParentAppIdForDepot(dlcAppId), parentAppId);

    // 2. Verify parent->children forward lookup via DlcData
    auto children = manager.GetChildDepotIdsForApp(parentAppId);
    EXPECT_NE(std::find(children.begin(), children.end(), dlcAppId), children.end());

    // 3. Create save files for both parent game and DLC addon
    fs::path parentStorage = mockStorageDir / "999002" / std::to_string(parentAppId) / "remote";
    fs::path dlcStorage = mockStorageDir / "999002" / std::to_string(dlcAppId) / "remote";
    fs::create_directories(parentStorage, ec);
    fs::create_directories(dlcStorage, ec);

    const std::string parentData = "parent_game_save_data";
    const std::string dlcData = "dlc_addon_save_data";
    {
        std::ofstream(parentStorage / "save.dat") << parentData;
        std::ofstream(dlcStorage / "dlc_save.dat") << dlcData;
    }

    // 4. Test ListCloudApps: DLC should be folded into parent, not listed standalone
    auto apps = manager.ListCloudApps("local-only", 999002);
    bool dlcFoundAsTopLevel = false;
    const CloudAppSummary_t* parentSummary = nullptr;
    for (const auto& app : apps)
    {
        if (app.appId == dlcAppId) dlcFoundAsTopLevel = true;
        if (app.appId == parentAppId) parentSummary = &app;
    }
    EXPECT_FALSE(dlcFoundAsTopLevel);
    ASSERT_NE(parentSummary, nullptr);
    EXPECT_EQ(parentSummary->fileCount, 2u);
    EXPECT_EQ(parentSummary->totalSizeBytes, parentData.size() + dlcData.size());
    EXPECT_NE(std::find(parentSummary->childAppIds.begin(), parentSummary->childAppIds.end(), dlcAppId),
              parentSummary->childAppIds.end());

    // 5. Test ListAppCloudFiles: DLC save must appear under parent game with tagged folder label
    auto files = manager.ListAppCloudFiles(parentAppId, "local-only", 999002);
    ASSERT_EQ(files.size(), 2u);
    bool foundParentFile = false;
    bool foundDlcFile = false;
    for (const auto& f : files)
    {
        if (f.fileName == "remote/save.dat")
        {
            foundParentFile = true;
            EXPECT_EQ(f.sizeBytes, parentData.size());
        }
        else if (f.fileName == "remote/dlc_save.dat")
        {
            foundDlcFile = true;
            EXPECT_EQ(f.folder, "Depot 500001 / SteamCloudRemote");
            EXPECT_EQ(f.sizeBytes, dlcData.size());
        }
    }
    EXPECT_TRUE(foundParentFile);
    EXPECT_TRUE(foundDlcFile);

    // 6. Test ReadAppCloudFile: downloading DLC cloud file using parent appId
    std::string readContent, downloadName, err;
    bool ok = manager.ReadAppCloudFile(parentAppId, "remote/dlc_save.dat", "local-only", 999002, readContent, downloadName, err);
    EXPECT_TRUE(ok) << err;
    EXPECT_EQ(readContent, dlcData);
    EXPECT_EQ(downloadName, "dlc_save.dat");

    // Clean up test environment
    g_config.dlcData.set({});
    manager.SetSteamRootForTest({});
    manager.SetStorageDirForTest({});
}

TEST(CloudManagerTest, RemoteProviderUsesCloudCatalogAndFiltersOutLocalOnlyApps)
{
    ScopedCloudManagerTestEnv env;
    auto& manager = CCloudManager::Instance();

    fs::path steamRoot = env.tempDir / "steam";
    fs::path stplugDir = steamRoot / "config" / "stplug-in";
    fs::path depotcacheDir = steamRoot / "depotcache";
    fs::path userdataDir = steamRoot / "userdata" / "999001";
    fs::path mockStorageDir = env.tempDir / "storage";
    std::error_code ec;
    fs::create_directories(stplugDir, ec);
    fs::create_directories(depotcacheDir, ec);
    fs::create_directories(userdataDir / "480" / "remote", ec);
    fs::create_directories(userdataDir / "730" / "remote", ec);
    fs::create_directories(mockStorageDir / "999001" / "730", ec);

    // App 480 (Spacewar): has local save + lua + manifest, and will be synced to remote cloud
    const std::string save480 = "spacewar_remote_save_payload";
    const std::string lua480 = "addappid(480, 1, 0) -- Spacewar\nsetManifestid(480, \"111222\")\n";
    const std::string mf480 = "manifest_binary_480_111222";
    {
        std::ofstream(userdataDir / "480" / "remote" / "save1.dat") << save480;
        std::ofstream(stplugDir / "480.lua") << lua480;
        std::ofstream(depotcacheDir / "480_111222.manifest") << mf480;
    }

    // App 730 (Counter-Strike 2): has local save + lua + local storage folder, but NOT in remote cloud
    {
        std::ofstream(userdataDir / "730" / "remote" / "cs2.dat") << "local_only_cs2_save";
        std::ofstream(stplugDir / "730.lua") << "addappid(730, 1, 0) -- Counter-Strike 2\n";
        std::ofstream(mockStorageDir / "999001" / "730" / "unpushed.dat") << "unpushed";
    }

    // Mock CLI that returns ["480", "999999999"] for list-remote-app-ids and succeeds on publish/sync
    fs::path mockCli = env.tempDir / "mock_catalog_cli.sh";
    {
        std::ofstream ofs(mockCli);
        ofs << "#!/bin/sh\n"
            << "if [ \"$1\" = \"list-remote-app-ids\" ]; then\n"
            << "  echo '{\"success\":true,\"app_ids\":[\"480\",\"999999999\"]}'\n"
            << "  exit 0\n"
            << "fi\n"
            << "if [ \"$1\" = \"publish-full-manifest\" ]; then\n"
            << "  echo '{\"success\":true,\"files_published\":1}'\n"
            << "  exit 0\n"
            << "fi\n"
            << "if [ \"$1\" = \"sync-remote-app\" ]; then\n"
            << "  echo '{\"success\":true,\"synced\":true}'\n"
            << "  exit 0\n"
            << "fi\n"
            << "exit 0\n";
    }
    chmod(mockCli.c_str(), 0755);

    manager.SetSteamRootForTest(steamRoot);
    manager.SetStorageDirForTest(mockStorageDir);
    manager.SetCliPathForTest(mockCli);

    // 1. StageAppFilesForCloudSync stages save + lua + manifest into storage/999001/480
    EXPECT_TRUE(manager.StageAppFilesForCloudSync(480, 999001));
    EXPECT_TRUE(fs::is_regular_file(mockStorageDir / "999001" / "480" / "stplug-in" / "480.lua", ec));
    EXPECT_TRUE(fs::is_regular_file(mockStorageDir / "999001" / "480" / "depotcache" / "480_111222.manifest", ec));
    EXPECT_TRUE(fs::is_regular_file(mockStorageDir / "999001" / "480" / "file_tokens.cloudredirect", ec));
    EXPECT_TRUE(fs::is_regular_file(mockStorageDir / "999001" / "480" / "cn.cloudredirect", ec));

    // 2. SyncAppToCloudAndUpdateCatalog publishes 480 and writes + publishes 999999999/navi_cloud_catalog.json
    EXPECT_TRUE(manager.SyncAppToCloudAndUpdateCatalog(480, "gdrive", 999001));
    fs::path catalogFile = mockStorageDir / "999001" / "999999999" / "navi_cloud_catalog.json";
    EXPECT_TRUE(fs::is_regular_file(catalogFile, ec));

    // 3. Remote provider ("gdrive") must ONLY return remote-confirmed app 480 (from catalog),
    //    ignoring local-only app 730 and never listing reserved catalog appId 999999999.
    auto remoteApps = manager.ListCloudApps("gdrive", 999001);
    ASSERT_EQ(remoteApps.size(), 1u);
    EXPECT_EQ(remoteApps[0].appId, 480u);
    EXPECT_EQ(remoteApps[0].gameTitle, "Spacewar");
    EXPECT_EQ(remoteApps[0].fileCount, 3u);
    EXPECT_EQ(remoteApps[0].totalSizeBytes, save480.size() + lua480.size() + mf480.size());
    EXPECT_TRUE(remoteApps[0].inRemoteCloud);

    // 4. ListAppCloudFiles for remote provider reads from the catalog (includes save + lua + manifest)
    auto remoteFiles = manager.ListAppCloudFiles(480, "gdrive", 999001);
    ASSERT_EQ(remoteFiles.size(), 3u);
    for (const auto& f : remoteFiles)
    {
        EXPECT_TRUE(f.inRemoteCloud);
    }

    // 5. Local provider ("local-only") still discovers both 480 and 730 locally, and excludes 999999999
    auto localApps = manager.ListCloudApps("local-only", 999001);
    bool foundLocal480 = false;
    bool foundLocal730 = false;
    bool foundCatalogApp = false;
    for (const auto& a : localApps)
    {
        if (a.appId == 480) foundLocal480 = true;
        if (a.appId == 730) foundLocal730 = true;
        if (a.appId == CCloudManager::kCloudCatalogAppId) foundCatalogApp = true;
    }
    EXPECT_TRUE(foundLocal480);
    EXPECT_TRUE(foundLocal730);
    EXPECT_FALSE(foundCatalogApp);

    // 6. When remote provider has 0 apps in cloud, ListCloudApps("gdrive", 999001) returns empty {}
    fs::path emptyCli = env.tempDir / "mock_empty_cli.sh";
    {
        std::ofstream ofs(emptyCli);
        ofs << "#!/bin/sh\n"
            << "echo '{\"success\":true,\"app_ids\":[]}'\n"
            << "exit 0\n";
    }
    chmod(emptyCli.c_str(), 0755);
    manager.SetCliPathForTest(emptyCli);

    auto emptyRemoteApps = manager.ListCloudApps("gdrive", 999001);
    EXPECT_TRUE(emptyRemoteApps.empty());

    manager.SetSteamRootForTest({});
    manager.SetStorageDirForTest({});
    manager.SetCliPathForTest({});
}





