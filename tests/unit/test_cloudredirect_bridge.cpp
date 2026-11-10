/**
 * @file  test_cloudredirect_bridge.cpp
 * @brief Unit tests for CloudRedirect native API endpoints and security
 */
#include <gtest/gtest.h>
#include "cloudredirect.hpp"
#include "config.hpp"
#include "atomic_file.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

struct ScopedTestEnv
{
    std::string tempDir;
    std::string origHome;
    std::string origXdgConfig;
    std::string origXdgData;
    std::string origFlatpakId;
    bool hadXdgConfig = false;
    bool hadXdgData = false;
    bool hadFlatpakId = false;

    ScopedTestEnv()
    {
        tempDir = "/tmp/unit_test_cr_" + std::to_string(getpid()) + "_" +
                  std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        std::error_code ec;
        fs::remove_all(tempDir, ec);
        fs::create_directories(tempDir + "/.config", ec);
        fs::create_directories(tempDir + "/.local/share", ec);

        const char* h = getenv("HOME");
        origHome = h ? h : "";

        const char* xc = getenv("XDG_CONFIG_HOME");
        if (xc)
        {
            hadXdgConfig = true;
            origXdgConfig = xc;
        }

        const char* xd = getenv("XDG_DATA_HOME");
        if (xd)
        {
            hadXdgData = true;
            origXdgData = xd;
        }

        const char* fp = getenv("FLATPAK_ID");
        if (fp)
        {
            hadFlatpakId = true;
            origFlatpakId = fp;
        }

        setenv("HOME", tempDir.c_str(), 1);
        unsetenv("XDG_CONFIG_HOME");
        unsetenv("XDG_DATA_HOME");
        unsetenv("FLATPAK_ID");
    }

    ~ScopedTestEnv()
    {
        if (!origHome.empty())
        {
            setenv("HOME", origHome.c_str(), 1);
        }
        if (hadXdgConfig)
        {
            setenv("XDG_CONFIG_HOME", origXdgConfig.c_str(), 1);
        }
        else
        {
            unsetenv("XDG_CONFIG_HOME");
        }
        if (hadXdgData)
        {
            setenv("XDG_DATA_HOME", origXdgData.c_str(), 1);
        }
        else
        {
            unsetenv("XDG_DATA_HOME");
        }
        if (hadFlatpakId)
        {
            setenv("FLATPAK_ID", origFlatpakId.c_str(), 1);
        }
        else
        {
            unsetenv("FLATPAK_ID");
        }

        std::error_code ec;
        fs::remove_all(tempDir, ec);
    }
};

TEST(CloudRedirectBridgeTest, PlatformTierDetectionAndPathsResolution)
{
    ScopedTestEnv env;
    auto& bridge = Navi::CloudRedirect::CCloudRedirectBridge::Instance();

    // Default detection on host
    Navi::CloudRedirect::PlatformTier tier = bridge.DetectPlatformTier();
    EXPECT_TRUE(tier == Navi::CloudRedirect::PlatformTier::Tier1_NativeDesktop ||
                tier == Navi::CloudRedirect::PlatformTier::Tier1_SteamOS);

    const auto& paths = bridge.GetPaths();
    EXPECT_EQ(paths.crConfigDir, fs::path(env.tempDir) / ".config" / "CloudRedirect");
    EXPECT_EQ(paths.slsSteamConfig, fs::path(env.tempDir) / ".config" / "SLSsteam" / "config.yaml");
    EXPECT_EQ(paths.crKillswitchFile, fs::path(env.tempDir) / ".config" / "CloudRedirect" / "disable");
    EXPECT_EQ(paths.naviEnabledFlag, fs::path(env.tempDir) / ".config" / "navi" / "cloud_enabled");
    EXPECT_EQ(paths.crConfigFile, fs::path(env.tempDir) / ".config" / "CloudRedirect" / "config.json");

    // Flatpak tier detection via FLATPAK_ID env
    setenv("FLATPAK_ID", "com.valvesoftware.Steam", 1);
    EXPECT_EQ(bridge.DetectPlatformTier(), Navi::CloudRedirect::PlatformTier::Tier2_Flatpak);
    const auto& fpPaths = bridge.GetPaths();
    EXPECT_EQ(fpPaths.tier, Navi::CloudRedirect::PlatformTier::Tier2_Flatpak);
    unsetenv("FLATPAK_ID");
}

TEST(CloudRedirectBridgeTest, SetEnabledStateTransitions)
{
    ScopedTestEnv env;
    auto& bridge = Navi::CloudRedirect::CCloudRedirectBridge::Instance();
    const auto& paths = bridge.GetPaths();

    // Initially neither flag exists -> not enabled
    EXPECT_FALSE(bridge.IsEnabled());

    // Enable CloudRedirect
    EXPECT_TRUE(bridge.SetEnabledState(true));
    EXPECT_TRUE(fs::exists(paths.naviEnabledFlag));
    EXPECT_FALSE(fs::exists(paths.crKillswitchFile));
    EXPECT_TRUE(bridge.IsEnabled());

    // Disable CloudRedirect
    EXPECT_TRUE(bridge.SetEnabledState(false));
    EXPECT_TRUE(fs::exists(paths.crKillswitchFile));
    EXPECT_FALSE(fs::exists(paths.naviEnabledFlag));
    EXPECT_FALSE(bridge.IsEnabled());

    // Re-enable CloudRedirect
    EXPECT_TRUE(bridge.SetEnabledState(true));
    EXPECT_TRUE(fs::exists(paths.naviEnabledFlag));
    EXPECT_FALSE(fs::exists(paths.crKillswitchFile));
    EXPECT_TRUE(bridge.IsEnabled());
}

TEST(CloudRedirectBridgeTest, SyncSlsSteamConfigSetUnionAndSingleKey)
{
    ScopedTestEnv env;
    auto& bridge = Navi::CloudRedirect::CCloudRedirectBridge::Instance();
    const auto& paths = bridge.GetPaths();

    // Initial sync with two apps
    EXPECT_TRUE(bridge.SyncSlsSteamConfig({12345, 67890}));
    EXPECT_TRUE(fs::exists(paths.slsSteamConfig));

    std::ifstream ifs(paths.slsSteamConfig);
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    ifs.close();

    EXPECT_NE(content.find("# >>> NAVI CLOUD SHIM (DO NOT EDIT MANUALLY)"), std::string::npos);
    EXPECT_NE(content.find("# <<< NAVI CLOUD SHIM"), std::string::npos);
    EXPECT_NE(content.find("DisableCloud: no"), std::string::npos);

    YAML::Node doc = YAML::Load(content);
    EXPECT_TRUE(doc["AdditionalApps"].IsSequence());
    EXPECT_EQ(doc["AdditionalApps"].size(), 2u);

    // Verify single AdditionalApps key
    size_t count = 0, pos = 0;
    while ((pos = content.find("AdditionalApps:", pos)) != std::string::npos)
    {
        ++count;
        pos += 15;
    }
    EXPECT_EQ(count, 1u);

    // Now prepend user settings to simulate existing user config
    std::string userFile = "UseWhitelist: no\nAppIds:\n  - 440\n\n" + content;
    EXPECT_TRUE(AtomicFile::write(paths.slsSteamConfig.string(), userFile));

    // Sync with additional app 99999
    EXPECT_TRUE(bridge.SyncSlsSteamConfig({99999}));

    YAML::Node updatedDoc = YAML::LoadFile(paths.slsSteamConfig.string());
    EXPECT_EQ(updatedDoc["UseWhitelist"].as<std::string>(), "no");
    EXPECT_EQ(updatedDoc["AppIds"].size(), 1u);
    EXPECT_TRUE(updatedDoc["AdditionalApps"].IsSequence());
    // Merged: 12345, 67890, 99999
    EXPECT_EQ(updatedDoc["AdditionalApps"].size(), 3u);

    std::ifstream ifs2(paths.slsSteamConfig);
    std::string updatedContent((std::istreambuf_iterator<char>(ifs2)), std::istreambuf_iterator<char>());
    ifs2.close();

    // Verify exactly one AdditionalApps key exists in updated content
    count = 0;
    pos = 0;
    while ((pos = updatedContent.find("AdditionalApps:", pos)) != std::string::npos)
    {
        ++count;
        pos += 15;
    }
    EXPECT_EQ(count, 1u);
}

TEST(CloudRedirectBridgeTest, RevertSlsSteamConfigSubtractionAndCleanReversion)
{
    ScopedTestEnv env;
    auto& bridge = Navi::CloudRedirect::CCloudRedirectBridge::Instance();
    const auto& paths = bridge.GetPaths();

    // Scenario 1: Only Navi shim existed
    EXPECT_TRUE(bridge.SyncSlsSteamConfig({12345}));
    EXPECT_TRUE(fs::exists(paths.slsSteamConfig));
    EXPECT_TRUE(bridge.RevertSlsSteamConfig({12345}));
    // File must be completely unlinked when no user keys remain
    EXPECT_FALSE(fs::exists(paths.slsSteamConfig));

    // Scenario 2: User had settings, but no user AdditionalApps
    std::string userContent = "UseWhitelist: no\nAppIds:\n  - 440\n";
    EXPECT_TRUE(AtomicFile::write(paths.slsSteamConfig.string(), userContent));
    EXPECT_TRUE(bridge.SyncSlsSteamConfig({12345}));
    EXPECT_TRUE(bridge.RevertSlsSteamConfig({12345}));
    EXPECT_TRUE(fs::exists(paths.slsSteamConfig));

    YAML::Node doc2 = YAML::LoadFile(paths.slsSteamConfig.string());
    EXPECT_EQ(doc2["UseWhitelist"].as<std::string>(), "no");
    EXPECT_EQ(doc2["AppIds"].size(), 1u);

    std::ifstream ifs2(paths.slsSteamConfig);
    std::string content2((std::istreambuf_iterator<char>(ifs2)), std::istreambuf_iterator<char>());
    ifs2.close();
    // Sentinels stripped
    EXPECT_EQ(content2.find("# >>> NAVI CLOUD SHIM"), std::string::npos);

    // Scenario 3: User had non-managed app in AdditionalApps
    std::string userWithApps = "AdditionalApps:\n  - 440\n";
    EXPECT_TRUE(AtomicFile::write(paths.slsSteamConfig.string(), userWithApps));
    EXPECT_TRUE(bridge.SyncSlsSteamConfig({12345}));
    EXPECT_TRUE(bridge.RevertSlsSteamConfig({12345}));
    EXPECT_TRUE(fs::exists(paths.slsSteamConfig));

    YAML::Node doc3 = YAML::LoadFile(paths.slsSteamConfig.string());
    EXPECT_TRUE(doc3["AdditionalApps"].IsSequence());
    EXPECT_EQ(doc3["AdditionalApps"].size(), 1u);
    EXPECT_EQ(doc3["AdditionalApps"][0].as<uint32_t>(), 440u);
}

TEST(CloudRedirectBridgeTest, GetTokenPathReturnsTokensProviderJson)
{
    ScopedTestEnv env;
    auto& bridge = Navi::CloudRedirect::CCloudRedirectBridge::Instance();
    const auto& paths = bridge.GetPaths();

    EXPECT_EQ(bridge.GetTokenPath("gdrive"), paths.crConfigDir / "tokens_gdrive.json");
    EXPECT_EQ(bridge.GetTokenPath("onedrive"), paths.crConfigDir / "tokens_onedrive.json");
    EXPECT_EQ(bridge.GetTokenPath("r2"), paths.crConfigDir / "tokens_r2.json");
    EXPECT_EQ(bridge.GetTokenPath("s3"), paths.crConfigDir / "tokens_s3.json");
}

TEST(CloudRedirectBridgeTest, EnsureLocalOnlyConfigHandlesMissing)
{
    ScopedTestEnv env;
    auto& bridge = Navi::CloudRedirect::CCloudRedirectBridge::Instance();
    const auto& paths = bridge.GetPaths();

    EXPECT_FALSE(fs::exists(paths.crConfigFile));
    EXPECT_TRUE(bridge.EnsureLocalOnlyConfig());
    EXPECT_TRUE(fs::exists(paths.crConfigFile));

    std::ifstream ifs(paths.crConfigFile);
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    ifs.close();
    EXPECT_NE(content.find("\"local\""), std::string::npos);

    // Idempotent
    EXPECT_TRUE(bridge.EnsureLocalOnlyConfig());
    EXPECT_TRUE(fs::exists(paths.crConfigFile));
}

TEST(CloudRedirectBridgeTest, SyncAndReadEngineConfigRoundTripAndExtraKeyPreservation)
{
    ScopedTestEnv env;
    auto& bridge = Navi::CloudRedirect::CCloudRedirectBridge::Instance();
    const auto& paths = bridge.GetPaths();

    // Seed an existing config.json with an extra custom key
    fs::create_directories(paths.crConfigDir);
    EXPECT_TRUE(AtomicFile::write(paths.crConfigFile, "{\n  \"provider\": \"local\",\n  \"custom_folder_path\": \"/tmp/custom\"\n}\n"));

    Navi::CloudRedirect::EngineConfig_t cfg;
    cfg.provider = "local-only";
    cfg.notificationsEnabled = false;
    cfg.statsSyncEnabled = true;
    cfg.syncAchievements = false;
    cfg.syncPlaytime = true;

    EXPECT_TRUE(bridge.SyncEngineConfig(cfg));

    // Read back via ReadEngineConfig
    auto readCfg = bridge.ReadEngineConfig();
    EXPECT_EQ(readCfg.provider, "local");
    EXPECT_FALSE(readCfg.notificationsEnabled);
    EXPECT_TRUE(readCfg.statsSyncEnabled);
    EXPECT_FALSE(readCfg.syncAchievements);
    EXPECT_TRUE(readCfg.syncPlaytime);

    // Verify extra key was preserved and remote providers write their exact name
    std::ifstream ifs(paths.crConfigFile);
    std::string raw((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    ifs.close();
    EXPECT_NE(raw.find("\"custom_folder_path\": \"/tmp/custom\""), std::string::npos);
    EXPECT_NE(raw.find("\"provider\": \"local\""), std::string::npos);

    cfg.provider = "r2";
    cfg.notificationsEnabled = true;
    EXPECT_TRUE(bridge.SyncEngineConfig(cfg));
    auto readR2 = bridge.ReadEngineConfig();
    EXPECT_EQ(readR2.provider, "r2");
    EXPECT_TRUE(readR2.notificationsEnabled);
}

TEST(CloudRedirectBridgeTest, DetectStandaloneInstallation)
{
    ScopedTestEnv env;
    auto& bridge = Navi::CloudRedirect::CCloudRedirectBridge::Instance();
    const auto& paths = bridge.GetPaths();

    EXPECT_FALSE(bridge.DetectStandaloneInstallation());

    // Creating standalone db file triggers detection
    fs::create_directories(paths.crConfigDir);
    EXPECT_TRUE(AtomicFile::write((paths.crConfigDir / "cloud_redirect.db").string(), "dummy db"));
    EXPECT_TRUE(bridge.DetectStandaloneInstallation());
}

TEST(CloudRedirectBridgeTest, ConfigLoadsCloudSavesYamlNode)
{
    ScopedTestEnv env;

    std::string customYaml = R"(
DisableCloud: yes
CloudSaves:
  Enabled: true
  Provider: "gdrive"
  ManagedApps:
    - 480
    - 12345
  AutoBackupBeforeReset: false
  BackupRetentionCount: 10
)";

    EXPECT_TRUE(AtomicFile::write(g_config.getPath(), customYaml));
    EXPECT_TRUE(g_config.loadSettings());

    EXPECT_TRUE(g_config.cloudSavesEnabled.get());
    EXPECT_EQ(g_config.cloudSavesProvider.get(), "gdrive");
    EXPECT_FALSE(g_config.cloudAutoBackupBeforeReset.get());
    EXPECT_EQ(g_config.cloudBackupRetentionCount.get(), 10u);

    const auto managed = g_config.cloudSavesManagedApps.get();
    EXPECT_EQ(managed.size(), 2u);
    EXPECT_TRUE(managed.contains(480));
    EXPECT_TRUE(managed.contains(12345));

    EXPECT_TRUE(g_config.IsAppCloudRedirected(480));
    EXPECT_TRUE(g_config.IsAppCloudRedirected(12345));
    EXPECT_FALSE(g_config.IsAppCloudRedirected(99999));
    EXPECT_FALSE(g_config.IsAppCloudRedirected(0));

    // When globally disabled, helper returns false
    g_config.cloudSavesEnabled.set(false);
    EXPECT_FALSE(g_config.IsAppCloudRedirected(480));
}

TEST(CloudRedirectBridgeTest, SpoofedAppsDefaultToCloudRedirectedWithProviderOrLocal)
{
    ScopedTestEnv env;

    std::string spoofedYaml = R"(
DisableCloud: yes
AdditionalApps:
  - 250900
  - 3856460
FakeAppIds:
  111222: 480
CloudSaves:
  Enabled: true
  Provider: "local-only"
  ManagedApps: []
  DisabledApps:
    - 3856460
)";

    EXPECT_TRUE(AtomicFile::write(g_config.getPath(), spoofedYaml));
    EXPECT_TRUE(g_config.loadSettings());

    EXPECT_TRUE(g_config.IsAppCloudRedirected(250900));
    EXPECT_TRUE(g_config.IsAppCloudRedirected(111222));
    EXPECT_TRUE(g_config.IsAppCloudRedirected(480));
    EXPECT_FALSE(g_config.IsAppCloudRedirected(3856460));
    EXPECT_FALSE(g_config.IsAppCloudRedirected(999999));

    const auto effective = g_config.GetEffectiveCloudRedirectedApps();
    EXPECT_TRUE(effective.contains(250900));
    EXPECT_TRUE(effective.contains(111222));
    EXPECT_TRUE(effective.contains(480));
    EXPECT_FALSE(effective.contains(3856460));
}

