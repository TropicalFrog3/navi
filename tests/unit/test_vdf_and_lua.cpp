#include <gtest/gtest.h>
#include "vdf.hpp"
#include "lua_parser.hpp"
#include "paths.hpp"

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

// ==========================================
// VDF Parser Tests
// ==========================================

TEST(VdfTest, ParseKeyValueAndSections)
{
    const std::string vdfContent =
        "\"AppState\"\n"
        "{\n"
        "    \"appid\" \"480\"\n"
        "    \"Universe\" \"1\"\n"
        "    \"name\" \"Spacewar\"\n"
        "    \"StateFlags\" \"4\"\n"
        "    \"UserConfig\"\n"
        "    {\n"
        "        \"language\" \"english\"\n"
        "        \"BetaKey\" \"public\"\n"
        "    }\n"
        "}\n";

    VDF::Node root;
    bool ok = VDF::parse(vdfContent, root);
    ASSERT_TRUE(ok);

    const VDF::Node* appState = root.getChild("AppState");
    ASSERT_NE(appState, nullptr);
    EXPECT_TRUE(appState->isSection());
    EXPECT_EQ(appState->getString("appid"), "480");
    EXPECT_EQ(appState->getString("Universe"), "1");
    EXPECT_EQ(appState->getString("name"), "Spacewar");
    EXPECT_EQ(appState->getString("StateFlags"), "4");
    EXPECT_EQ(appState->getString("nonexistent", "fallback"), "fallback");

    const VDF::Node* userConfig = appState->getChild("UserConfig");
    ASSERT_NE(userConfig, nullptr);
    EXPECT_TRUE(userConfig->isSection());
    EXPECT_EQ(userConfig->getString("language"), "english");
    EXPECT_EQ(userConfig->getString("BetaKey"), "public");
}

TEST(VdfTest, ParseFileAndComments)
{
    const std::string tempDir = "/tmp/unit_test_vdf_" + std::to_string(getpid());
    fs::remove_all(tempDir);
    fs::create_directories(tempDir);

    const std::string filePath = tempDir + "/appmanifest.vdf";
    {
        std::ofstream f(filePath);
        f << "// Comment at beginning\n";
        f << "\"libraryfolders\"\n";
        f << "{\n";
        f << "    \"0\"\n";
        f << "    {\n";
        f << "        \"path\" \"/home/user/games\"\n";
        f << "        \"label\" \"Main\"\n";
        f << "    }\n";
        f << "}\n";
    }

    VDF::Node root;
    bool ok = VDF::parseFile(filePath, root);
    ASSERT_TRUE(ok);

    const VDF::Node* libFolders = root.getChild("libraryfolders");
    ASSERT_NE(libFolders, nullptr);
    const VDF::Node* folder0 = libFolders->getChild("0");
    ASSERT_NE(folder0, nullptr);
    EXPECT_EQ(folder0->getString("path"), "/home/user/games");
    EXPECT_EQ(folder0->getString("label"), "Main");

    fs::remove_all(tempDir);
}

TEST(VdfTest, ParseInvalidSyntax)
{
    VDF::Node root;
    bool ok = VDF::parseFile("/nonexistent/path/missing.vdf", root);
    EXPECT_FALSE(ok);
}

// ==========================================
// Lua Parser Tests
// ==========================================

TEST(LuaParserTest, ParseCompletePlugin)
{
    const std::string luaContent =
        "addappid(1234560, 1, 0)                  -- Game Title\n"
        "addappid(1234561, 1, \"af524ae28cb634f861e43a8dc0f9dbc6002bb303ec8e8167e02a8c92fa57d1e9\") -- Base Game [WINDOWS]\n"
        "addappid(9999901)                        -- DLC Expansion\n"
        "setManifestid(1234561, \"2530054926824748916\", 45812938472)\n"
        "addtoken(1234560, \"secret_app_token_xyz\")\n";

    auto result = LuaParser::parse(luaContent);
    EXPECT_TRUE(result.valid);
    EXPECT_EQ(result.appId, "1234560");
    EXPECT_EQ(result.gameName, "Game Title");
    EXPECT_EQ(result.appToken, "secret_app_token_xyz");

    // Depots with keys
    ASSERT_EQ(result.depots.count("1234561"), 1u);
    const auto& depot = result.depots["1234561"];
    EXPECT_EQ(depot.depotId, "1234561");
    EXPECT_EQ(depot.key, "af524ae28cb634f861e43a8dc0f9dbc6002bb303ec8e8167e02a8c92fa57d1e9");
    EXPECT_EQ(depot.manifestGid, "2530054926824748916");
    EXPECT_EQ(depot.sizeBytes, "45812938472");

    // DLC without keys
    ASSERT_EQ(result.dlcs.count("9999901"), 1u);
    EXPECT_EQ(result.dlcs["9999901"], "DLC Expansion");

    // Manifest maps
    EXPECT_EQ(result.manifests["1234561"], "2530054926824748916");
    EXPECT_EQ(result.manifestSizes["1234561"], "45812938472");
}

TEST(LuaParserTest, ParseFileAndInvalidContent)
{
    const std::string tempDir = "/tmp/unit_test_lua_" + std::to_string(getpid());
    fs::remove_all(tempDir);
    fs::create_directories(tempDir);

    const std::string luaPath = tempDir + "/test_game.lua";
    {
        std::ofstream f(luaPath);
        f << "addappid(480, 1, 0) -- Spacewar\nsetManifestid(480, \"1234567890\")\n";
    }

    auto result = LuaParser::parseFile(luaPath);
    EXPECT_TRUE(result.valid);
    EXPECT_EQ(result.appId, "480");
    EXPECT_EQ(result.gameName, "Spacewar");

    // Invalid file / empty content
    auto invalidRes = LuaParser::parseFile(tempDir + "/nonexistent.lua");
    EXPECT_FALSE(invalidRes.valid);

    auto emptyRes = LuaParser::parse("-- just a comment without addappid\n");
    EXPECT_FALSE(emptyRes.valid);

    fs::remove_all(tempDir);
}

// ==========================================
// Paths Detection Tests
// ==========================================

TEST(PathsTest, DetectSteamRootAndSubdirs)
{
    const std::string tempHome = "/tmp/unit_test_paths_" + std::to_string(getpid());
    fs::remove_all(tempHome);

    const char* origHome = getenv("HOME");
    std::string origHomeStr = origHome ? origHome : "";
    setenv("HOME", tempHome.c_str(), 1);

    // When no Steam exists, findSteamRoot returns empty
    EXPECT_EQ(Paths::findSteamRoot(), "");
    EXPECT_EQ(Paths::stplugDir(), "");
    EXPECT_EQ(Paths::depotcacheDir(), "");

    // Create ~/.local/share/Steam/steamui
    std::string fakeSteam = tempHome + "/.local/share/Steam";
    fs::create_directories(fakeSteam + "/steamui");

    EXPECT_EQ(Paths::findSteamRoot(), fakeSteam);

    std::string pluginDir = Paths::stplugDir();
    EXPECT_EQ(pluginDir, fakeSteam + "/config/stplug-in");
    EXPECT_TRUE(fs::exists(pluginDir));

    std::string cacheDir = Paths::depotcacheDir();
    EXPECT_EQ(cacheDir, fakeSteam + "/depotcache");
    EXPECT_TRUE(fs::exists(cacheDir));

    if (!origHomeStr.empty())
    {
        setenv("HOME", origHomeStr.c_str(), 1);
    }
    fs::remove_all(tempHome);
}

TEST(PathsTest, ClearAppCache)
{
    const std::string tempHome = "/tmp/unit_test_paths_appcache_" + std::to_string(getpid());
    fs::remove_all(tempHome);

    const char* origHome = getenv("HOME");
    std::string origHomeStr = origHome ? origHome : "";
    const char* origXdg = getenv("XDG_CONFIG_HOME");
    std::string origXdgStr = origXdg ? origXdg : "";

    setenv("HOME", tempHome.c_str(), 1);
    setenv("XDG_CONFIG_HOME", (tempHome + "/.config").c_str(), 1);

    // Create directories and files for AppID 480
    fs::create_directories(tempHome + "/.cache/navi/downloads/480_extracted");
    fs::create_directories(tempHome + "/.config/navi/cache");
    fs::create_directories(tempHome + "/.local/share/navi/cache/manifests");
    fs::create_directories(tempHome + "/.local/share/Steam/config/stplug-in");
    fs::create_directories("/tmp/sls_manual_dir_480");

    {
        std::ofstream(tempHome + "/.cache/navi/downloads/480.zip") << "dummy zip";
        std::ofstream(tempHome + "/.cache/navi/downloads/480_extracted/file.txt") << "extracted";
        std::ofstream(tempHome + "/.config/navi/cache/ticket_480.yaml") << "ticket";
        std::ofstream(tempHome + "/.config/navi/cache/encryptedTicket_480.yaml") << "encrypted ticket";
        std::ofstream(tempHome + "/.local/share/navi/cache/480.zip") << "local zip";
        std::ofstream(tempHome + "/.local/share/navi/cache/ticket_480.yaml") << "local ticket";
        std::ofstream(tempHome + "/.local/share/navi/cache/encryptedTicket_480.yaml") << "local enc ticket";
        std::ofstream("/tmp/sls_lua_480") << "tmp lua";
        std::ofstream("/tmp/sls_manual_dir_480/dummy.txt") << "dummy";

        // Files that MUST be preserved
        std::ofstream(tempHome + "/.cache/navi/downloads/730.zip") << "app 730 zip";
        std::ofstream(tempHome + "/.config/navi/cache/ticket_730.yaml") << "app 730 ticket";
        std::ofstream(tempHome + "/.local/share/Steam/config/stplug-in/480.lua") << "installed lua";
        std::ofstream(tempHome + "/.local/share/navi/cache/manifests/480_123.manifest") << "manifest data";
        std::ofstream("/tmp/sls_lua_730") << "tmp lua 730";
    }

    bool res = Paths::clearAppCache(480);
    EXPECT_TRUE(res);

    // Verify 480 cache files removed
    EXPECT_FALSE(fs::exists(tempHome + "/.cache/navi/downloads/480.zip"));
    EXPECT_FALSE(fs::exists(tempHome + "/.cache/navi/downloads/480_extracted"));
    EXPECT_FALSE(fs::exists(tempHome + "/.config/navi/cache/ticket_480.yaml"));
    EXPECT_FALSE(fs::exists(tempHome + "/.config/navi/cache/encryptedTicket_480.yaml"));
    EXPECT_FALSE(fs::exists(tempHome + "/.local/share/navi/cache/480.zip"));
    EXPECT_FALSE(fs::exists(tempHome + "/.local/share/navi/cache/ticket_480.yaml"));
    EXPECT_FALSE(fs::exists(tempHome + "/.local/share/navi/cache/encryptedTicket_480.yaml"));
    EXPECT_FALSE(fs::exists("/tmp/sls_lua_480"));
    EXPECT_FALSE(fs::exists("/tmp/sls_manual_dir_480"));

    // Verify other app files, .lua plugins, and manifests are preserved
    EXPECT_TRUE(fs::exists(tempHome + "/.cache/navi/downloads/730.zip"));
    EXPECT_TRUE(fs::exists(tempHome + "/.config/navi/cache/ticket_730.yaml"));
    EXPECT_TRUE(fs::exists(tempHome + "/.local/share/Steam/config/stplug-in/480.lua"));
    EXPECT_TRUE(fs::exists(tempHome + "/.local/share/navi/cache/manifests/480_123.manifest"));
    EXPECT_TRUE(fs::exists("/tmp/sls_lua_730"));

    // Cleanup
    fs::remove("/tmp/sls_lua_730");
    if (!origHomeStr.empty()) setenv("HOME", origHomeStr.c_str(), 1);
    else unsetenv("HOME");
    if (!origXdgStr.empty()) setenv("XDG_CONFIG_HOME", origXdgStr.c_str(), 1);
    else unsetenv("XDG_CONFIG_HOME");
    fs::remove_all(tempHome);
}

TEST(PathsTest, ClearAllCache)
{
    const std::string tempHome = "/tmp/unit_test_paths_allcache_" + std::to_string(getpid());
    fs::remove_all(tempHome);

    const char* origHome = getenv("HOME");
    std::string origHomeStr = origHome ? origHome : "";
    const char* origXdg = getenv("XDG_CONFIG_HOME");
    std::string origXdgStr = origXdg ? origXdg : "";

    setenv("HOME", tempHome.c_str(), 1);
    setenv("XDG_CONFIG_HOME", (tempHome + "/.config").c_str(), 1);

    fs::create_directories(tempHome + "/.cache/navi/downloads");
    fs::create_directories(tempHome + "/.config/navi/cache");
    fs::create_directories(tempHome + "/.local/share/navi/cache/manifests");
    fs::create_directories(tempHome + "/.local/share/Steam/config/stplug-in");
    fs::create_directories("/tmp/sls_manual_dir_999");

    {
        std::ofstream(tempHome + "/.cache/navi/downloads/480.zip") << "zip 480";
        std::ofstream(tempHome + "/.cache/navi/downloads/730.zip") << "zip 730";
        std::ofstream(tempHome + "/.config/navi/cache/ticket_480.yaml") << "t 480";
        std::ofstream(tempHome + "/.config/navi/cache/ticket_730.yaml") << "t 730";
        std::ofstream(tempHome + "/.local/share/navi/cache/random_cache.bin") << "bin";
        std::ofstream("/tmp/sls_lua_999") << "tmp lua";
        std::ofstream("/tmp/sls_manual_dir_999/dummy.txt") << "manual";

        // Protected files: .lua plugin and manifests/
        std::ofstream(tempHome + "/.local/share/Steam/config/stplug-in/480.lua") << "installed lua";
        std::ofstream(tempHome + "/.local/share/navi/cache/manifests/depot_manifest.bin") << "manifest bin";
    }

    bool res = Paths::clearAllCache();
    EXPECT_TRUE(res);

    // Verify all caches removed
    EXPECT_FALSE(fs::exists(tempHome + "/.cache/navi/downloads/480.zip"));
    EXPECT_FALSE(fs::exists(tempHome + "/.cache/navi/downloads/730.zip"));
    EXPECT_FALSE(fs::exists(tempHome + "/.config/navi/cache/ticket_480.yaml"));
    EXPECT_FALSE(fs::exists(tempHome + "/.config/navi/cache/ticket_730.yaml"));
    EXPECT_FALSE(fs::exists(tempHome + "/.local/share/navi/cache/random_cache.bin"));
    EXPECT_FALSE(fs::exists("/tmp/sls_lua_999"));
    EXPECT_FALSE(fs::exists("/tmp/sls_manual_dir_999"));

    // Verify protected files preserved
    EXPECT_TRUE(fs::exists(tempHome + "/.local/share/Steam/config/stplug-in/480.lua"));
    EXPECT_TRUE(fs::exists(tempHome + "/.local/share/navi/cache/manifests/depot_manifest.bin"));

    // Cleanup
    if (!origHomeStr.empty()) setenv("HOME", origHomeStr.c_str(), 1);
    else unsetenv("HOME");
    if (!origXdgStr.empty()) setenv("XDG_CONFIG_HOME", origXdgStr.c_str(), 1);
    else unsetenv("XDG_CONFIG_HOME");
    fs::remove_all(tempHome);
}

