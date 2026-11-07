#include <gtest/gtest.h>
#include "config.hpp"
#include "atomic_file.hpp"
#include "storeinject_view.hpp"

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

TEST_F(ConfigUnitTest, ApiKeysOverlayDirectConfigSync)
{
    ASSERT_TRUE(g_config.createFile());
    ASSERT_TRUE(g_config.loadSettings(true));

    // 1. Test updateApiAuth and loadSettings round-trip with non-empty keys
    EXPECT_TRUE(g_config.updateApiAuth("ryuu_secret_1", "dpbx_secret_2", "hubcap_secret_3"));
    EXPECT_TRUE(g_config.loadSettings(true));
    EXPECT_EQ(g_config.ryuuKey.get(), "ryuu_secret_1");
    EXPECT_EQ(g_config.depotBoxKey.get(), "dpbx_secret_2");
    EXPECT_EQ(g_config.hubcapKey.get(), "hubcap_secret_3");

    // 2. Test round-trip clearing non-empty keys back to empty string
    EXPECT_TRUE(g_config.updateApiAuth("", "", ""));
    EXPECT_TRUE(g_config.loadSettings(true));
    EXPECT_EQ(g_config.ryuuKey.get(), "");
    EXPECT_EQ(g_config.depotBoxKey.get(), "");
    EXPECT_EQ(g_config.hubcapKey.get(), "");

    // 3. Test StoreInjectView::renderApiKeysJson format and escaping
    std::string json = StoreInjectView::renderApiKeysJson("k_ryuu", "k_dpbx", "k_hubcap");
    EXPECT_EQ(json, "{\"success\":true,\"ryuu\":\"k_ryuu\",\"dpbx\":\"k_dpbx\",\"hubcap\":\"k_hubcap\"}");

    // Test escaping in renderApiKeysJson
    std::string jsonEsc = StoreInjectView::renderApiKeysJson("key\"with\"quotes", "key\\with\\backslash", "");
    EXPECT_EQ(jsonEsc, "{\"success\":true,\"ryuu\":\"key\\\"with\\\"quotes\",\"dpbx\":\"key\\\\with\\\\backslash\",\"hubcap\":\"\"}");

    // 4. Test store-page-script.js does not read provider.key from localStorage and fetches /api-keys
    std::filesystem::path scriptPath = "res/inject-scripts/store/store-page-script.js";
    if (!std::filesystem::exists(scriptPath)) {
        scriptPath = "res/inject-scripts/store-page-script.js";
    }
    ASSERT_TRUE(std::filesystem::exists(scriptPath));
    std::ifstream sf(scriptPath);
    ASSERT_TRUE(sf.is_open());
    std::string scriptContent((std::istreambuf_iterator<char>(sf)), std::istreambuf_iterator<char>());
    sf.close();

    EXPECT_EQ(scriptContent.find("localStorage.getItem(provider.key)"), std::string::npos);
    EXPECT_NE(scriptContent.find("/api-keys"), std::string::npos);
}

TEST_F(ConfigUnitTest, FullConfigJsonCloudSavesRoundTrip)
{
    ASSERT_TRUE(g_config.createFile());
    ASSERT_TRUE(g_config.loadSettings(true));

    const std::string configPath = g_config.getPath();

    // 1. Initial renderFullConfigJson: check CloudSaves fields and default values
    std::string initialJson = StoreInjectView::renderFullConfigJson(configPath);
    EXPECT_NE(initialJson.find("\"CloudSaves\": {"), std::string::npos);
    EXPECT_NE(initialJson.find("\"Enabled\": true"), std::string::npos);
    EXPECT_NE(initialJson.find("\"Provider\": \"local-only\""), std::string::npos);
    EXPECT_NE(initialJson.find("\"AutoBackupBeforeReset\": true"), std::string::npos);
    EXPECT_NE(initialJson.find("\"BackupRetentionCount\": 5"), std::string::npos);
    EXPECT_NE(initialJson.find("\"ManagedApps\": []"), std::string::npos);
    EXPECT_NE(initialJson.find("\"PrefixSync\": {"), std::string::npos);
    EXPECT_NE(initialJson.find("\"SyncAtStartup\": true"), std::string::npos);

    // 2. Apply config update with CloudSaves
    std::string updateJson = R"({
        "CloudSaves": {
            "Enabled": true,
            "Provider": "gdrive",
            "AutoBackupBeforeReset": false,
            "BackupRetentionCount": 10,
            "ManagedApps": [480, 730],
            "PrefixSync": {
                "Enabled": true,
                "SyncAtStartup": false,
                "SyncOnLaunch": false,
                "SyncOnExit": true,
                "MaxFileMb": 128
            }
        }
    })";

    std::string err;
    bool ok = StoreInjectView::applyFullConfigJson(configPath, updateJson, err);
    EXPECT_TRUE(ok) << err;

    // Verify g_config in-memory state updated
    EXPECT_TRUE(g_config.cloudSavesEnabled.get());
    EXPECT_EQ(g_config.cloudSavesProvider.get(), "gdrive");
    EXPECT_FALSE(g_config.cloudAutoBackupBeforeReset.get());
    EXPECT_EQ(g_config.cloudBackupRetentionCount.get(), 10u);
    EXPECT_TRUE(g_config.cloudPrefixSyncEnabled.get());
    EXPECT_FALSE(g_config.cloudPrefixSyncAtStartUp.get());
    EXPECT_FALSE(g_config.cloudPrefixSyncOnLaunch.get());
    EXPECT_TRUE(g_config.cloudPrefixSyncOnExit.get());
    EXPECT_EQ(g_config.cloudPrefixSyncMaxFileMb.get(), 128u);
    auto managed = g_config.cloudSavesManagedApps.get();
    EXPECT_EQ(managed.size(), 2u);
    EXPECT_TRUE(managed.contains(480));
    EXPECT_TRUE(managed.contains(730));

    // Verify written YAML file on disk contains updated CloudSaves
    std::ifstream f(configPath);
    ASSERT_TRUE(f.is_open());
    std::string yamlContent((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();
    EXPECT_NE(yamlContent.find("CloudSaves:"), std::string::npos);
    EXPECT_NE(yamlContent.find("Provider: gdrive"), std::string::npos);

    // Verify renderFullConfigJson reflects new state
    std::string updatedJson = StoreInjectView::renderFullConfigJson(configPath);
    EXPECT_NE(updatedJson.find("\"Enabled\": true"), std::string::npos);
    EXPECT_NE(updatedJson.find("\"Provider\": \"gdrive\""), std::string::npos);
    EXPECT_NE(updatedJson.find("\"AutoBackupBeforeReset\": false"), std::string::npos);
    EXPECT_NE(updatedJson.find("\"BackupRetentionCount\": 10"), std::string::npos);
    EXPECT_NE(updatedJson.find("\"ManagedApps\": [480, 730]"), std::string::npos);
    EXPECT_NE(updatedJson.find("\"SyncAtStartup\": false"), std::string::npos);
    EXPECT_NE(updatedJson.find("\"MaxFileMb\": 128"), std::string::npos);

    // 3. Provider validation and retention clamping
    std::string clampedJson = R"({
        "CloudSaves": {
            "Enabled": true,
            "Provider": "unsupported_provider",
            "AutoBackupBeforeReset": true,
            "BackupRetentionCount": 999,
            "ManagedApps": [480]
        }
    })";
    EXPECT_TRUE(StoreInjectView::applyFullConfigJson(configPath, clampedJson, err)) << err;
    // Provider shouldn't update to unsupported_provider, remains gdrive
    EXPECT_EQ(g_config.cloudSavesProvider.get(), "gdrive");
    // Retention clamped to 100
    EXPECT_EQ(g_config.cloudBackupRetentionCount.get(), 100u);
    auto managedClamped = g_config.cloudSavesManagedApps.get();
    EXPECT_EQ(managedClamped.size(), 1u);
    EXPECT_TRUE(managedClamped.contains(480));
}

TEST(ShutdownTest, CleanShutdownAndCdpTargetFiltering)
{
    auto readSrc = [](const std::filesystem::path& path) -> std::string {
        std::ifstream f(path);
        if (!f.is_open()) return "";
        return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    };

    std::string mainSrc = readSrc("src/presenter/main.cpp");
    ASSERT_FALSE(mainSrc.empty());
    EXPECT_NE(mainSrc.find("la_objclose"), std::string::npos);
    EXPECT_NE(mainSrc.find("__attribute__((destructor))"), std::string::npos);
    EXPECT_NE(mainSrc.find("StoreInject::shutdown()"), std::string::npos);

    std::string storeInjectSrc = readSrc("src/presenter/storeinject.cpp");
    ASSERT_FALSE(storeInjectSrc.empty());
    EXPECT_NE(storeInjectSrc.find("g_serverFd"), std::string::npos);
    EXPECT_NE(storeInjectSrc.find("shutdown("), std::string::npos);
    EXPECT_NE(storeInjectSrc.find("checkCounter++ >= 100"), std::string::npos);

    std::string cdpInjectSrc = readSrc("src/view/cdpinject.cpp");
    ASSERT_FALSE(cdpInjectSrc.empty());
    EXPECT_NE(cdpInjectSrc.find("page.type = jsonGetString(obj, \"type\");"), std::string::npos);

    std::string removeLuaSrc = readSrc("src/presenter/removelua.cpp");
    ASSERT_FALSE(removeLuaSrc.empty());
    EXPECT_NE(removeLuaSrc.find("page.type != \"page\""), std::string::npos);
    EXPECT_NE(removeLuaSrc.find("about:blank"), std::string::npos);
}
