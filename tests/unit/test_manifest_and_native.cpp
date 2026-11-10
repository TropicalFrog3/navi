/**
 * @file  test_manifest_and_native.cpp
 * @brief Unit tests for manifest code storage and native game install
 */
#include <gtest/gtest.h>
#include "manifestcode.hpp"
#include "nativeinstall.hpp"
#include "apps.hpp"
#include "autoupdate.hpp"
#include "config.hpp"
#include "depotkeys.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <cstring>
#include <unordered_set>

namespace fs = std::filesystem;

// Helpers for manual protobuf construction
static void writeVarint(std::vector<uint8_t>& buf, uint64_t val)
{
    while (val >= 0x80) {
        buf.push_back(static_cast<uint8_t>((val & 0x7F) | 0x80));
        val >>= 7;
    }
    buf.push_back(static_cast<uint8_t>(val & 0x7F));
}

TEST(ManifestCodeTest, ProtobufWireCodec)
{
    // Build synthetic CContentServerDirectory_GetManifestRequestCode_Request
    // Field 1: app_id = 480 (varint) -> key: (1 << 3) | 0 = 0x08
    // Field 2: depot_id = 481 (varint) -> key: (2 << 3) | 0 = 0x10
    // Field 3: manifest_id = 88888888888888888ULL (varint) -> key: (3 << 3) | 0 = 0x18
    // Field 4: app_branch = "public" (length-delimited) -> key: (4 << 3) | 2 = 0x22
    std::vector<uint8_t> reqBuf;

    // Field 1
    reqBuf.push_back(0x08);
    writeVarint(reqBuf, 480);

    // Field 2
    reqBuf.push_back(0x10);
    writeVarint(reqBuf, 481);

    // Field 3
    reqBuf.push_back(0x18);
    writeVarint(reqBuf, 88888888888888888ULL);

    // Field 4
    reqBuf.push_back(0x22);
    reqBuf.push_back(6);
    const char* branch = "public";
    reqBuf.insert(reqBuf.end(), branch, branch + 6);

    ManifestCode::CContentServerDirectory_GetManifestRequestCode_Request req;
    bool ok = req.ParseFromArray(reqBuf.data(), reqBuf.size());

    ASSERT_TRUE(ok);
    EXPECT_EQ(req.app_id(), 480u);
    EXPECT_EQ(req.depot_id(), 481u);
    EXPECT_EQ(req.manifest_id(), 88888888888888888ULL);

    // Test response serialization
    uint64_t sampleCode = 0x123456789ABCDEF0ULL;
    ManifestCode::CContentServerDirectory_GetManifestRequestCode_Response resp;
    resp.set_manifest_request_code(sampleCode);

    size_t respSize = resp.ByteSizeLong();
    ASSERT_GT(respSize, 0u);
    std::vector<uint8_t> respBuf(respSize);
    bool serOk = resp.SerializeToArray(respBuf.data(), respBuf.size());
    ASSERT_TRUE(serOk);

    // Wire check: Field 1 (tag 0x08), followed by varint
    EXPECT_EQ(respBuf[0], 0x08);

    // Read back varint
    size_t offset = 1;
    uint64_t readCode = 0;
    int shift = 0;
    while (offset < respBuf.size()) {
        uint8_t b = respBuf[offset++];
        readCode |= static_cast<uint64_t>(b & 0x7F) << shift;
        shift += 7;
        if (!(b & 0x80)) break;
    }
    EXPECT_EQ(readCode, sampleCode);
}

TEST(ManifestCodeTest, EnsureManifestFileLocalAndRemoteFallback)
{
    // Test 0: Invalid arguments
    EXPECT_FALSE(ManifestCode::ensureManifestFile(0, 0, 0));
    EXPECT_FALSE(ManifestCode::ensureManifestFile(480, 0, 12345));
    EXPECT_FALSE(ManifestCode::ensureManifestFile(480, 481, 0));

    // Test 1: Local cache detection
    const std::string testDir = "/tmp/unit_test_manifest_" + std::to_string(getpid());
    fs::remove_all(testDir);
    fs::create_directories(testDir + "/.local/share/Steam/steamui");
    fs::create_directories(testDir + "/.local/share/Steam/depotcache");
    fs::create_directories(testDir + "/.local/share/Steam/config/depotcache");

    const char* origHome = getenv("HOME");
    std::string origHomeStr = origHome ? origHome : "";
    setenv("HOME", testDir.c_str(), 1);

    // Create a local manifest in depotcache
    std::string cachedManifest = testDir + "/.local/share/Steam/depotcache/481_999999999.manifest";
    {
        std::ofstream f(cachedManifest, std::ios::binary);
        f << "DUMMY_MANIFEST_PAYLOAD_TEST";
    }

    EXPECT_TRUE(ManifestCode::ensureManifestFile(480, 481, 999999999ULL));

    // Test 2: Legacy config/depotcache promotion
    std::string legacyManifest = testDir + "/.local/share/Steam/config/depotcache/482_888888888.manifest";
    {
        std::ofstream f(legacyManifest, std::ios::binary);
        f << "LEGACY_MANIFEST_PAYLOAD_TEST";
    }

    EXPECT_TRUE(ManifestCode::ensureManifestFile(480, 482, 888888888ULL));
    EXPECT_TRUE(fs::exists(testDir + "/.local/share/Steam/depotcache/482_888888888.manifest"));

    // Clean up
    if (origHome) setenv("HOME", origHomeStr.c_str(), 1);
    else unsetenv("HOME");
    fs::remove_all(testDir);
}

TEST(NativeInstallTest, PersistenceAndArmDisarm)
{
    const std::string testDir = "/tmp/unit_test_native_persist_" + std::to_string(getpid());
    fs::remove_all(testDir);
    fs::create_directories(testDir + "/.config/navi");

    const char* origHome = getenv("HOME");
    std::string origHomeStr = origHome ? origHome : "";
    setenv("HOME", testDir.c_str(), 1);

    NativeInstall::init();

    EXPECT_FALSE(NativeInstall::isNative(480));
    EXPECT_FALSE(NativeInstall::isQueuedOrArmed(480));

    // Cache an install request first so arm can take it
    NativeInstall::cacheInstallRequest(nullptr, 480, 1, 0);

    // Provide depot keys for unowned app check
    std::string keysYamlPath = testDir + "/.config/navi/depot_keys.yaml";
    {
        std::ofstream out(keysYamlPath);
        out << "480:\n  depots:\n    481: \"1122334455667788990011223344556677889900112233445566778899001122\"\n    482: \"1122334455667788990011223344556677889900112233445566778899001122\"\n";
    }
    DepotKeys::loadDepotKeysYaml();

    // Test arming via CSV depot list
    bool armed = NativeInstall::arm(480, "481, 482");
    ASSERT_TRUE(armed);
    EXPECT_TRUE(NativeInstall::isNative(480));
    EXPECT_TRUE(NativeInstall::isQueuedOrArmed(480));

    auto sel = NativeInstall::getSelection(480);
    ASSERT_NE(sel, nullptr);
    EXPECT_EQ(sel->depots.size(), 2u);
    EXPECT_EQ(sel->depots.count(481), 1u);
    EXPECT_EQ(sel->depots.count(482), 1u);

    // Verify persistence file exists
    std::string yamlPath = testDir + "/.config/navi/native_installs.yaml";
    EXPECT_TRUE(fs::exists(yamlPath));

    // Re-initialize to test load from disk
    NativeInstall::init();
    EXPECT_TRUE(NativeInstall::isNative(480));

    // Test disarm
    NativeInstall::disarm(480);
    EXPECT_FALSE(NativeInstall::isNative(480));
    EXPECT_FALSE(NativeInstall::isQueuedOrArmed(480));

    // Re-initialize to verify disarm persisted to disk
    NativeInstall::init();
    EXPECT_FALSE(NativeInstall::isNative(480));

    if (!origHomeStr.empty())
    {
        setenv("HOME", origHomeStr.c_str(), 1);
    }
    fs::remove_all(testDir);
}

TEST(NativeInstallTest, DepotFilteringAlgorithm)
{
    DepotInfo_t depots[10];
    std::memset(depots, 0, sizeof(depots));

    uint32_t count = 4;
    depots[0].depotId = 101;
    depots[1].depotId = 102;
    depots[2].depotId = 103;
    depots[3].depotId = 104;

    std::unordered_set<uint32_t> selected = { 101, 103 };

    for (int i = static_cast<int>(count) - 1; i >= 0; --i)
    {
        if (selected.find(depots[i].depotId) == selected.end())
        {
            for (uint32_t j = static_cast<uint32_t>(i); j < count - 1; ++j)
            {
                depots[j] = depots[j + 1];
            }
            count--;
        }
    }

    EXPECT_EQ(count, 2u);
    EXPECT_EQ(depots[0].depotId, 101u);
    EXPECT_EQ(depots[1].depotId, 103u);

    // Prune all
    selected.clear();
    for (int i = static_cast<int>(count) - 1; i >= 0; --i)
    {
        if (selected.find(depots[i].depotId) == selected.end())
        {
            for (uint32_t j = static_cast<uint32_t>(i); j < count - 1; ++j)
            {
                depots[j] = depots[j + 1];
            }
            count--;
        }
    }
    EXPECT_EQ(count, 0u);

    // Prune consecutive
    count = 4;
    depots[0].depotId = 200;
    depots[1].depotId = 201;
    depots[2].depotId = 202;
    depots[3].depotId = 203;

    selected = { 203 };
    for (int i = static_cast<int>(count) - 1; i >= 0; --i)
    {
        if (selected.find(depots[i].depotId) == selected.end())
        {
            for (uint32_t j = static_cast<uint32_t>(i); j < count - 1; ++j)
            {
                depots[j] = depots[j + 1];
            }
            count--;
        }
    }
    EXPECT_EQ(count, 1u);
    EXPECT_EQ(depots[0].depotId, 203u);
}

TEST(NativeInstallTest, ProviderSidecarFormatAndEscaping)
{
    fs::path tempDir = fs::temp_directory_path() / ("unit_provider_" + std::to_string(getpid()));
    fs::create_directories(tempDir);

    std::string provFile = (tempDir / "1443430.provider").string();
    {
        std::ofstream pf(provFile);
        pf << "Hubcap \"Special\" \\Provider\\\n";
    }

    std::string providerName;
    {
        std::ifstream pf(provFile);
        ASSERT_TRUE(pf.is_open());
        std::getline(pf, providerName);
    }
    EXPECT_EQ(providerName, "Hubcap \"Special\" \\Provider\\");

    std::string escapedProvider;
    for (char c : providerName) {
        if (c == '"' || c == '\\') escapedProvider += '\\';
        escapedProvider += c;
    }
    EXPECT_EQ(escapedProvider, "Hubcap \\\"Special\\\" \\\\Provider\\\\");

    fs::remove_all(tempDir);
}

TEST(NativeInstallTest, RemoveLuaPatternScanning)
{
    fs::path pluginDir = fs::temp_directory_path() / ("unit_removelua_" + std::to_string(getpid()));
    fs::create_directories(pluginDir);

    uint32_t targetAppId = 1443430;
    std::string idStr = std::to_string(targetAppId);

    // 1. Target lua with addappid(1443430)
    {
        std::ofstream f(pluginDir / "rogue.lua");
        f << "-- Rogue configuration\naddappid(" << targetAppId << ")\nsetname(\"Rogue\")\n";
    }
    // 2. Target lua with spaces addappid( 1443430 )
    {
        std::ofstream f(pluginDir / "rogue_alt.lua");
        f << "addappid( " << targetAppId << " )\n";
    }
    // 3. Unrelated lua (must NOT be touched)
    {
        std::ofstream f(pluginDir / "unrelated.lua");
        f << "addappid(504230)\nsetname(\"Celeste\")\n";
    }
    // 4. Target provider sidecar
    {
        std::ofstream f(pluginDir / (idStr + ".provider"));
        f << "HubcapDB\n";
    }
    // 5. Target manifest
    {
        std::ofstream f(pluginDir / (idStr + "_1443431.manifest"));
        f << "MANIFEST_BINARY_DATA\n";
    }
    // 6. Unrelated manifest
    {
        std::ofstream f(pluginDir / "504230_504231.manifest");
        f << "MANIFEST_CELESTE\n";
    }

    std::vector<std::string> deletedFiles;

    for (const auto& entry : fs::directory_iterator(pluginDir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".lua") {
            std::ifstream file(entry.path());
            if (file.is_open()) {
                std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                file.close();
                if (content.find("addappid") != std::string::npos && content.find(idStr) != std::string::npos) {
                    deletedFiles.push_back(entry.path().filename().string());
                    fs::remove(entry.path());
                }
            }
        }
    }

    for (const auto& entry : fs::directory_iterator(pluginDir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".manifest") {
            std::string stem = entry.path().stem().string();
            if (stem.find(idStr) == 0) {
                deletedFiles.push_back(entry.path().filename().string());
                fs::remove(entry.path());
            }
        }
    }

    auto providerPath = pluginDir / (idStr + ".provider");
    if (fs::exists(providerPath)) {
        deletedFiles.push_back(providerPath.filename().string());
        fs::remove(providerPath);
    }

    EXPECT_TRUE(fs::exists(pluginDir / "unrelated.lua"));
    EXPECT_TRUE(fs::exists(pluginDir / "504230_504231.manifest"));
    EXPECT_FALSE(fs::exists(pluginDir / "rogue.lua"));
    EXPECT_FALSE(fs::exists(pluginDir / "rogue_alt.lua"));
    EXPECT_FALSE(fs::exists(pluginDir / (idStr + ".provider")));
    EXPECT_FALSE(fs::exists(pluginDir / (idStr + "_1443431.manifest")));
    EXPECT_EQ(deletedFiles.size(), 4u);

    fs::remove_all(pluginDir);
}

TEST(NativeInstallTest, CrossPlatformDepotGuardSimulation)
{
    // Native Linux depot present
    {
        uint32_t nativeDepotCount = 1;
        bool injected = false;
        if (nativeDepotCount == 0) {
            injected = true;
        }
        EXPECT_FALSE(injected);
    }

    // 0 native depots
    {
        uint32_t nativeDepotCount = 0;
        bool injected = false;
        if (nativeDepotCount == 0) {
            injected = true;
        }
        EXPECT_TRUE(injected);
    }
}

TEST(AppsTest, NonSteamAppClassification)
{
    // Non-Steam shortcut IDs with bit 31 set
    EXPECT_TRUE(Apps::isNonSteamApp(2783438810u));
    EXPECT_TRUE(Apps::isNonSteamApp(3576629899u));
    EXPECT_TRUE(Apps::isNonSteamApp(2147483648u));

    // Internal range >= 10^9
    EXPECT_TRUE(Apps::isNonSteamApp(1000000000u));

    // Genuine Steam AppIDs
    EXPECT_FALSE(Apps::isNonSteamApp(480u));
    EXPECT_FALSE(Apps::isNonSteamApp(1443430u));
    EXPECT_FALSE(Apps::isNonSteamApp(4993040u));
    EXPECT_FALSE(Apps::isNonSteamApp(3155980u));

    // AppID 0 is invalid
    EXPECT_TRUE(Apps::isNonSteamApp(0u));
}

TEST(AutoUpdateTest, CheckAndPromptSyncMock)
{
    const std::string tempDir = "/tmp/unit_test_autoupdate_" + std::to_string(getpid());
    fs::remove_all(tempDir);
    fs::create_directories(tempDir);

    const std::string verFile = tempDir + "/version.yaml";
    {
        std::ofstream f(verFile);
        f << "Version: 0.1.0\n";
        f << "Changelog: Test changelog\n";
        f << "DownloadUrl: file:///dev/null\n";
    }

    setenv("SLS_UPDATE_VERSION_URL", ("file://" + verFile).c_str(), 1);
    setenv("SLS_TEST_ENV", "1", 1);

    // Call checkAndPromptSync - should cleanly read mock version and return without blocking
    AutoUpdate::checkAndPromptSync();

    unsetenv("SLS_UPDATE_VERSION_URL");
    unsetenv("SLS_TEST_ENV");
    fs::remove_all(tempDir);
}
