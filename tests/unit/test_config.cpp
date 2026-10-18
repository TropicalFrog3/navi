#include <gtest/gtest.h>
#include "config.hpp"
#include "atomic_file.hpp"

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

class ConfigUnitTest : public ::testing::Test
{
protected:
    std::string m_tempDir;
    std::string m_origHome;
    std::string m_origXdg;
    bool m_hadXdg = false;

    void SetUp() override
    {
        m_tempDir = "/tmp/unit_test_config_" + std::to_string(getpid());
        fs::remove_all(m_tempDir);
        fs::create_directories(m_tempDir + "/.config");

        const char* h = getenv("HOME");
        m_origHome = h ? h : "";

        const char* x = getenv("XDG_CONFIG_HOME");
        if (x)
        {
            m_hadXdg = true;
            m_origXdg = x;
        }

        setenv("HOME", m_tempDir.c_str(), 1);
        unsetenv("XDG_CONFIG_HOME");
    }

    void TearDown() override
    {
        if (!m_origHome.empty())
        {
            setenv("HOME", m_origHome.c_str(), 1);
        }
        if (m_hadXdg)
        {
            setenv("XDG_CONFIG_HOME", m_origXdg.c_str(), 1);
        }
        else
        {
            unsetenv("XDG_CONFIG_HOME");
        }
        fs::remove_all(m_tempDir);
    }
};

TEST_F(ConfigUnitTest, GetDirAndPath)
{
    std::string dir = g_config.getDir();
    EXPECT_EQ(dir, m_tempDir + "/.config/navi");

    std::string path = g_config.getPath();
    EXPECT_EQ(path, m_tempDir + "/.config/navi/config.yaml");

    // Test with XDG_CONFIG_HOME set
    std::string customXdg = m_tempDir + "/custom_xdg";
    setenv("XDG_CONFIG_HOME", customXdg.c_str(), 1);
    EXPECT_EQ(g_config.getDir(), customXdg + "/navi");
    EXPECT_EQ(g_config.getPath(), customXdg + "/navi/config.yaml");
    unsetenv("XDG_CONFIG_HOME");
}

TEST_F(ConfigUnitTest, CreateFileAndLoadSettings)
{
    // Create config file
    bool ok = g_config.createFile();
    ASSERT_TRUE(ok);
    ASSERT_TRUE(fs::exists(g_config.getPath()));

    // Load settings from freshly created file
    bool loaded = g_config.loadSettings(true);
    EXPECT_TRUE(loaded);
}

TEST_F(ConfigUnitTest, AddAndRemoveAdditionalAppId)
{
    ASSERT_TRUE(g_config.createFile());
    ASSERT_TRUE(g_config.loadSettings(true));

    uint32_t testAppId = 730;
    bool added = g_config.addAdditionalAppId(testAppId);
    EXPECT_TRUE(added);
    EXPECT_TRUE(g_config.isAddedAppId(testAppId));

    // Adding existing appId should return true without duplicate
    bool addedAgain = g_config.addAdditionalAppId(testAppId);
    EXPECT_TRUE(addedAgain);

    // Remove appId
    bool removed = g_config.removeAdditionalAppId(testAppId);
    EXPECT_TRUE(removed);
    EXPECT_FALSE(g_config.isAddedAppId(testAppId));
}

TEST_F(ConfigUnitTest, UpdateApiAuth)
{
    ASSERT_TRUE(g_config.createFile());

    bool ok = g_config.updateApiAuth("key_alpha", "key_beta", "key_gamma");
    EXPECT_TRUE(ok);

    std::ifstream f(g_config.getPath());
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    EXPECT_NE(content.find("RyuuKey: \"key_alpha\""), std::string::npos);
    EXPECT_NE(content.find("DepotBoxKey: \"key_beta\""), std::string::npos);
    EXPECT_NE(content.find("HubcapKey: \"key_gamma\""), std::string::npos);
}

TEST_F(ConfigUnitTest, ShouldExcludeAppId)
{
    // 1E9 and higher are Steam internal
    EXPECT_TRUE(g_config.shouldExcludeAppId(1000000000u));
    EXPECT_TRUE(g_config.shouldExcludeAppId(2000000000u));

    // Whitelist mode test
    g_config.useWhiteList = true;
    std::unordered_set<AppId_t> allowed = { 480, 570 };
    g_config.appIds = allowed;

    EXPECT_FALSE(g_config.shouldExcludeAppId(480));
    EXPECT_FALSE(g_config.shouldExcludeAppId(570));
    EXPECT_TRUE(g_config.shouldExcludeAppId(730));

    // Blacklist mode test
    g_config.useWhiteList = false;
    std::unordered_set<AppId_t> blocked = { 480 };
    g_config.appIds = blocked;

    EXPECT_TRUE(g_config.shouldExcludeAppId(480));
    EXPECT_FALSE(g_config.shouldExcludeAppId(570));
    EXPECT_FALSE(g_config.shouldExcludeAppId(730));
}

TEST_F(ConfigUnitTest, ScanLuaPluginsAndUpdateConfig)
{
    // Setup SteamRoot stplug-in directory
    std::string steamDir = m_tempDir + "/.local/share/Steam";
    std::string pluginDir = steamDir + "/config/stplug-in";
    fs::create_directories(pluginDir);

    // Create config file first
    ASSERT_TRUE(g_config.createFile());
    ASSERT_TRUE(g_config.loadSettings(true));

    // Write a dummy lua file with addappid(12345)
    std::string luaPath = pluginDir + "/12345.lua";
    {
        std::ofstream f(luaPath);
        f << "addappid(12345)\nsetManifestid(12345, \"99999\")\n";
    }

    scanLuaPluginsAndUpdateConfig();

    EXPECT_TRUE(g_config.isAddedAppId(12345));

    // Clean up lua file and scan again; stale AppID should be removed
    fs::remove(luaPath);
    scanLuaPluginsAndUpdateConfig();
    EXPECT_FALSE(g_config.isAddedAppId(12345));
}
