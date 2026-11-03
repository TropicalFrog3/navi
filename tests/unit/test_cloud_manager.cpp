#include <gtest/gtest.h>
#include "cloud_manager.hpp"
#include "cloud_view.hpp"

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

    // 1. App discovery should discover app 480 and title "Spacewar" from Lua comment
    auto apps = manager.ListCloudApps("gdrive", 999001);
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
    auto files = manager.ListAppCloudFiles(480, "gdrive", 999001);
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
    bool readLuaOk = manager.ReadAppCloudFile(480, "stplug-in/480.lua", "gdrive", 999001, outData, outName, outErr);
    EXPECT_TRUE(readLuaOk) << outErr;
    EXPECT_EQ(outData, luaContent);
    EXPECT_EQ(outName, "480.lua");

    bool readMfOk = manager.ReadAppCloudFile(480, "depotcache/481_123456789.manifest", "gdrive", 999001, outData, outName, outErr);
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


