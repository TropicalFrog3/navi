#include "power_monitor.hpp"
#include "storeinject_view.hpp"
#include "sync_crypto.hpp"
#include "sync_engine.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
#include <sys/stat.h>

namespace fs = std::filesystem;

class SyncIpcViewsTest : public ::testing::Test
{
protected:
    fs::path m_testDir;

    void SetUp() override
    {
        m_testDir = fs::temp_directory_path() / ("test_sync_ipc_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
        fs::create_directories(m_testDir);
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(m_testDir, ec);
    }
};

TEST_F(SyncIpcViewsTest, PowerMonitorClockJumpDetection)
{
    // Normal progression (no sleep jump)
    EXPECT_FALSE(Sync::PowerMonitor::hasClockJumped(1000, 1002, 5000, 5002, 5));
    EXPECT_FALSE(Sync::PowerMonitor::hasClockJumped(1000, 1004, 5000, 5008, 5)); // deltaReal(8) - deltaMono(4) = 4 < 5

    // Sleep jump >= threshold (e.g. system suspended for 5 or 60 seconds)
    EXPECT_TRUE(Sync::PowerMonitor::hasClockJumped(1000, 1002, 5000, 5007, 5));  // deltaReal(7) - deltaMono(2) = 5 >= 5
    EXPECT_TRUE(Sync::PowerMonitor::hasClockJumped(1000, 1002, 5000, 5062, 5));  // deltaReal(62) - deltaMono(2) = 60 >= 5

    // Negative monotonic delta (clock warp backwards) must be safely rejected
    EXPECT_FALSE(Sync::PowerMonitor::hasClockJumped(1000, 998, 5000, 5050, 5));

    // Custom threshold testing
    EXPECT_FALSE(Sync::PowerMonitor::hasClockJumped(100, 101, 200, 208, 10)); // diff = 7 < 10
    EXPECT_TRUE(Sync::PowerMonitor::hasClockJumped(100, 101, 200, 212, 10));  // diff = 11 >= 10
}

TEST_F(SyncIpcViewsTest, PowerMonitorNetworkRouteReadiness)
{
    fs::path nonexistent = m_testDir / "nonexistent_route";
    EXPECT_FALSE(Sync::PowerMonitor::isNetworkRouteReady(nonexistent.string()));

    fs::path emptyFile = m_testDir / "empty_route";
    {
        std::ofstream ofs(emptyFile);
    }
    EXPECT_FALSE(Sync::PowerMonitor::isNetworkRouteReady(emptyFile.string()));

    // Loopback-only route file
    fs::path loopbackOnly = m_testDir / "loopback_route";
    {
        std::ofstream ofs(loopbackOnly);
        ofs << "Iface\tDestination\tGateway\tFlags\tRefCnt\tUse\tMetric\tMask\tMTU\tWindow\tIRTT\n";
        ofs << "lo\t00000000\t00000000\t0001\t0\t0\t0\t00000000\t0\t0\t0\n";
    }
    EXPECT_FALSE(Sync::PowerMonitor::isNetworkRouteReady(loopbackOnly.string()));

    // Active default route on wlan0 with RTF_UP (Flags 0003)
    fs::path activeWlan = m_testDir / "active_wlan_route";
    {
        std::ofstream ofs(activeWlan);
        ofs << "Iface\tDestination\tGateway\tFlags\tRefCnt\tUse\tMetric\tMask\tMTU\tWindow\tIRTT\n";
        ofs << "lo\t00000000\t00000000\t0001\t0\t0\t0\t00000000\t0\t0\t0\n";
        ofs << "wlan0\t00000000\t0100000A\t0003\t0\t0\t600\t00000000\t0\t0\t0\n";
    }
    EXPECT_TRUE(Sync::PowerMonitor::isNetworkRouteReady(activeWlan.string()));

    // Active eth0 subnet route with RTF_UP (Flags 0001)
    fs::path activeEth = m_testDir / "active_eth_route";
    {
        std::ofstream ofs(activeEth);
        ofs << "Iface\tDestination\tGateway\tFlags\tRefCnt\tUse\tMetric\tMask\tMTU\tWindow\tIRTT\n";
        ofs << "eth0\t0000000A\t00000000\t0001\t0\t0\t100\t00FFFFFF\t0\t0\t0\n";
    }
    EXPECT_TRUE(Sync::PowerMonitor::isNetworkRouteReady(activeEth.string()));

    // Interface route without RTF_UP (Flags 0000)
    fs::path downEth = m_testDir / "down_eth_route";
    {
        std::ofstream ofs(downEth);
        ofs << "Iface\tDestination\tGateway\tFlags\tRefCnt\tUse\tMetric\tMask\tMTU\tWindow\tIRTT\n";
        ofs << "eth0\t0000000A\t00000000\t0000\t0\t0\t100\t00FFFFFF\t0\t0\t0\n";
    }
    EXPECT_FALSE(Sync::PowerMonitor::isNetworkRouteReady(downEth.string()));
}

TEST_F(SyncIpcViewsTest, PowerMonitorSimulateResumeAndLaunchDebounce)
{
    fs::path routeFile = m_testDir / "mock_route";
    {
        std::ofstream ofs(routeFile);
        ofs << "Iface\tDestination\tGateway\tFlags\tRefCnt\tUse\tMetric\tMask\tMTU\tWindow\tIRTT\n";
        ofs << "wlan0\t00000000\t0100000A\t0003\t0\t0\t600\t00000000\t0\t0\t0\n";
    }

    int triggerCount = 0;
    Sync::ESyncTriggerReason lastReason = Sync::ESyncTriggerReason::k_Launch;

    Sync::PowerMonitor monitor(
        [&](Sync::ESyncTriggerReason r) {
            triggerCount++;
            lastReason = r;
        },
        1000,
        5,
        routeFile.string());

    // 1. Initial simulated resume event
    monitor.simulateResumeEventForTest();
    EXPECT_EQ(triggerCount, 1);
    EXPECT_EQ(lastReason, Sync::ESyncTriggerReason::k_WakeFromSleep);

    // 2. Rapid duplicate wake event within 2s should be debounced
    monitor.simulateResumeEventForTest();
    EXPECT_EQ(triggerCount, 1);

    // 3. triggerLaunchCheckOnce should fire exactly once
    monitor.triggerLaunchCheckOnce();
    EXPECT_EQ(triggerCount, 2);
    EXPECT_EQ(lastReason, Sync::ESyncTriggerReason::k_Launch);

    monitor.triggerLaunchCheckOnce(); // Should not fire second time
    EXPECT_EQ(triggerCount, 2);
}

TEST_F(SyncIpcViewsTest, RenderSyncStatusJsonSerialization)
{
    StoreInjectView::SyncStatusViewModel vm;
    vm.role = "overrider";
    vm.topology = "p2p_and_cloud";
    vm.cloudProvider = "gdrive";
    vm.cloudAuthenticated = true;
    vm.gdriveAuthenticated = true;
    vm.recoveryKeyMaskedOrFull = "ABCDE-FGHIJ-KLMNO-PQRST-UVWXY-Z2345-67ABC-DEFGH-JKLMN-OPQRS-T";
    vm.keyEpoch = 2;
    vm.hasRemoteBump = true;
    vm.lastTriggerReason = "wake_from_sleep";
    vm.overriderHostname = "steamdeck-oled";
    vm.remoteGeneration = 42;
    vm.localGeneration = 41;

    StoreInjectView::SyncDiffItemViewModel diff;
    diff.appId = 480;
    diff.title = "Spacewar";
    diff.category = "manifests";
    diff.logicalPath = "steamapps://appmanifest_480.acf";
    diff.sizeBytes = 1024;
    diff.generation = 42;
    diff.isGameRunning = false;
    vm.diffItems.push_back(diff);

    Sync::SaveConflict_t conflict;
    conflict.appId = 730;
    conflict.logicalPath = "userdata://12345/730/remote/save.dat";
    conflict.localSha256 = "1111222233334444555566667777888811112222333344445555666677778888";
    conflict.localMtimeUnixSec = 1700000000;
    conflict.localSizeBytes = 100;
    conflict.remoteSha256 = "aaaa9999888877776666555544443333aaaa9999888877776666555544443333";
    conflict.remoteMtimeUnixSec = 1700000050;
    conflict.remoteSizeBytes = 110;
    vm.conflicts.push_back(conflict);

    Sync::DiscoveredPeer_t peer;
    peer.deviceId = "dev-deck-01";
    peer.hostname = "deck-p2p";
    peer.role = Sync::ESyncRole::k_Receiver;
    peer.ipAddress = "192.168.1.100";
    peer.tlsPort = 9005;
    peer.globalGeneration = 42;
    peer.keyEpoch = 2;
    peer.sidHashHex = "0123456789abcdef";
    peer.lastSeenUnixMs = 1700000100000LL;
    vm.p2pPeers.push_back(peer);

    vm.stagedAcfRestartRequiredAppIds = {480, 730};

    std::string json = StoreInjectView::renderSyncStatusJson(vm);
    EXPECT_NE(json.find("\"role\": \"overrider\""), std::string::npos);
    EXPECT_NE(json.find("\"topology\": \"p2p_and_cloud\""), std::string::npos);
    EXPECT_NE(json.find("\"cloudProvider\": \"gdrive\""), std::string::npos);
    EXPECT_NE(json.find("\"cloudAuthenticated\": true"), std::string::npos);
    EXPECT_NE(json.find("\"gdriveAuthenticated\": true"), std::string::npos);
    EXPECT_NE(json.find("\"hasRemoteBump\": true"), std::string::npos);
    EXPECT_NE(json.find("\"lastTriggerReason\": \"wake_from_sleep\""), std::string::npos);
    EXPECT_NE(json.find("\"overriderHostname\": \"steamdeck-oled\""), std::string::npos);
    EXPECT_NE(json.find("\"remoteGeneration\": 42"), std::string::npos);
    EXPECT_NE(json.find("\"appId\": 480"), std::string::npos);
    EXPECT_NE(json.find("\"logicalPath\": \"userdata://12345/730/remote/save.dat\""), std::string::npos);
    EXPECT_NE(json.find("\"ipAddress\": \"192.168.1.100\""), std::string::npos);
    EXPECT_NE(json.find("480, 730"), std::string::npos);
}

TEST_F(SyncIpcViewsTest, RenderSyncApplyResultJsonSerialization)
{
    Sync::SyncApplyReport_t report;
    report.status = Sync::ESyncApplyStatus::k_Ok;
    report.snapshotId = "snap-20261024-123456";
    report.appliedLogicalPaths = {"steamapps://appmanifest_480.acf", "depotcache://depot_481.manifest"};
    report.deferredAppIds = {570};
    report.stagedAcfRestartRequiredAppIds = {480};

    std::string json = StoreInjectView::renderSyncApplyResultJson(report);
    EXPECT_NE(json.find("\"status\": 0"), std::string::npos);
    EXPECT_NE(json.find("\"success\": true"), std::string::npos);
    EXPECT_NE(json.find("\"snapshotId\": \"snap-20261024-123456\""), std::string::npos);
    EXPECT_NE(json.find("\"steamapps://appmanifest_480.acf\""), std::string::npos);
    EXPECT_NE(json.find("\"depotcache://depot_481.manifest\""), std::string::npos);
    EXPECT_NE(json.find("570"), std::string::npos);
    EXPECT_NE(json.find("480"), std::string::npos);
}

TEST_F(SyncIpcViewsTest, ParseSyncApplyRequestJsonGranularAndConflictChoices)
{
    std::string json = R"({
        "allManifestsAndLua": true,
        "manifestAppIds": [480, 570],
        "allConfigs": false,
        "configKeys": ["syncRole", "syncTopology"],
        "allCloudSaves": false,
        "cloudSaveAppIds": [730],
        "allAchievements": true,
        "achievementAppIds": [1234],
        "allApiKeys": true,
        "providerNames": ["ryuu", "depotbox"],
        "conflictChoices": {
            "userdata://1/730/save1.dat": "keep_local",
            "userdata://1/730/save2.dat": "keep_remote",
            "userdata://1/730/save3.dat": "backup_both"
        }
    })";

    StoreInjectView::SyncApplyRequest_t req;
    ASSERT_TRUE(StoreInjectView::parseSyncApplyRequestJson(json, req));

    EXPECT_TRUE(req.filter.allManifestsAndLua);
    EXPECT_EQ(req.filter.manifestAppIds.count(480), 1u);
    EXPECT_EQ(req.filter.manifestAppIds.count(570), 1u);

    EXPECT_FALSE(req.filter.allConfigs);
    EXPECT_EQ(req.filter.configKeys.count("syncRole"), 1u);
    EXPECT_EQ(req.filter.configKeys.count("syncTopology"), 1u);

    EXPECT_FALSE(req.filter.allCloudSaves);
    EXPECT_EQ(req.filter.cloudSaveAppIds.count(730), 1u);

    EXPECT_TRUE(req.filter.allAchievements);
    EXPECT_EQ(req.filter.achievementAppIds.count(1234), 1u);

    EXPECT_TRUE(req.filter.allApiKeys);
    EXPECT_EQ(req.filter.providerNames.count("ryuu"), 1u);
    EXPECT_EQ(req.filter.providerNames.count("depotbox"), 1u);

    EXPECT_EQ(req.conflictChoices["userdata://1/730/save1.dat"], Sync::EConflictChoice::k_KeepLocal);
    EXPECT_EQ(req.conflictChoices["userdata://1/730/save2.dat"], Sync::EConflictChoice::k_KeepRemote);
    EXPECT_EQ(req.conflictChoices["userdata://1/730/save3.dat"], Sync::EConflictChoice::k_BackupLocalAndUseRemote);

    // Empty input returns false
    StoreInjectView::SyncApplyRequest_t emptyReq;
    EXPECT_FALSE(StoreInjectView::parseSyncApplyRequestJson("", emptyReq));
}

TEST_F(SyncIpcViewsTest, ParseSyncConfigUpdateJsonProviders)
{
    // Test WebDAV config
    std::string webdavJson = R"({
        "role": "receiver",
        "topology": "p2p_and_cloud",
        "cloudProvider": "webdav",
        "webDavUrl": "https://dav.example.com/sync",
        "webDavUser": "alice",
        "webDavPassword": "secretpassword"
    })";

    StoreInjectView::SyncConfigUpdateRequest_t req;
    ASSERT_TRUE(StoreInjectView::parseSyncConfigUpdateJson(webdavJson, req));
    EXPECT_EQ(req.role, "receiver");
    EXPECT_EQ(req.topology, "p2p_and_cloud");
    EXPECT_EQ(req.cloudProvider, "webdav");
    EXPECT_EQ(req.webDavUrl, "https://dav.example.com/sync");
    EXPECT_EQ(req.webDavUser, "alice");
    EXPECT_EQ(req.webDavPassword, "secretpassword");

    // Test Google Drive config
    std::string gdriveJson = R"({
        "role": "overrider",
        "topology": "cloud",
        "cloudProvider": "gdrive"
    })";
    StoreInjectView::SyncConfigUpdateRequest_t gReq;
    ASSERT_TRUE(StoreInjectView::parseSyncConfigUpdateJson(gdriveJson, gReq));
    EXPECT_EQ(gReq.role, "overrider");
    EXPECT_EQ(gReq.cloudProvider, "gdrive");
}

TEST_F(SyncIpcViewsTest, ValidateAndParseRecoveryKeyJsonCrc16AndRotate)
{
    Sync::MasterKey_t originalKey{};
    ASSERT_TRUE(Sync::generateMasterKey(originalKey));
    std::string validKeyStr = Sync::encodeRecoveryKey(originalKey);

    // 1. Valid recovery key
    std::string validBody = "{\"recoveryKey\": \"" + validKeyStr + "\"}";
    Sync::MasterKey_t decodedKey{};
    bool rotateRequested = false;
    std::string errorMsg;
    EXPECT_TRUE(StoreInjectView::validateAndParseRecoveryKeyJson(validBody, decodedKey, rotateRequested, errorMsg));
    EXPECT_FALSE(rotateRequested);
    EXPECT_EQ(decodedKey, originalKey);
    EXPECT_TRUE(errorMsg.empty());

    // 2. Key with 1-character typo (CRC-16 mismatch)
    std::string corruptedKeyStr = validKeyStr;
    corruptedKeyStr[10] = (corruptedKeyStr[10] == 'A') ? 'B' : 'A';
    std::string corruptedBody = "{\"recoveryKey\": \"" + corruptedKeyStr + "\"}";
    EXPECT_FALSE(StoreInjectView::validateAndParseRecoveryKeyJson(corruptedBody, decodedKey, rotateRequested, errorMsg));
    EXPECT_NE(errorMsg.find("Invalid Recovery Key or CRC-16 checksum mismatch"), std::string::npos);

    // 3. Action rotate requested
    std::string rotateBody = "{\"action\": \"rotate\"}";
    EXPECT_TRUE(StoreInjectView::validateAndParseRecoveryKeyJson(rotateBody, decodedKey, rotateRequested, errorMsg));
    EXPECT_TRUE(rotateRequested);

    // 4. Missing recoveryKey and action
    std::string malformedBody = "{\"foo\": \"bar\"}";
    EXPECT_FALSE(StoreInjectView::validateAndParseRecoveryKeyJson(malformedBody, decodedKey, rotateRequested, errorMsg));
    EXPECT_NE(errorMsg.find("Missing recoveryKey or action"), std::string::npos);

    // 5. Empty input
    EXPECT_FALSE(StoreInjectView::validateAndParseRecoveryKeyJson("", decodedKey, rotateRequested, errorMsg));
}

TEST_F(SyncIpcViewsTest, FullConfigRenderAndApplyRoundTrip)
{
    fs::path tempYaml = m_testDir / "config.yaml";
    std::string err;

    std::string jsonInput = "{\n"
        "  \"DisableFamilyShareLock\": false,\n"
        "  \"UseWhitelist\": true,\n"
        "  \"SafeMode\": true,\n"
        "  \"WarnHashMissmatch\": true,\n"
        "  \"NotifyInit\": false,\n"
        "  \"API\": false,\n"
        "  \"DisableCloud\": false,\n"
        "  \"DisableUpdates\": false,\n"
        "  \"DumpClientInterfaces\": true,\n"
        "  \"ExtendedLogging\": true,\n"
        "  \"MaxSchemaTries\": 42,\n"
        "  \"FakeWalletBalance\": 9999,\n"
        "  \"LogLevels\": 165,\n"
        "  \"FakeName\": \"Gordon Freeman\",\n"
        "  \"FakeEmail\": \"gordon@blackmesa.gov\",\n"
        "  \"RyuuKey\": \"ryuu-sec-123\",\n"
        "  \"DepotBoxKey\": \"dpbx-sec-456\",\n"
        "  \"HubcapKey\": \"hub-sec-789\",\n"
        "  \"IdleStatus\": {\"AppId\": 480, \"Title\": \"Spacewar In Space\"},\n"
        "  \"UnownedStatus\": {\"AppId\": 730, \"Title\": \"CS Test\"},\n"
        "  \"AppIds\": [440, 730],\n"
        "  \"AdditionalApps\": [10, 20],\n"
        "  \"FakeOffline\": [480],\n"
        "  \"DepotBlacklist\": [999],\n"
        "  \"FakeAppIds\": {\"440\": \"480\"},\n"
        "  \"ManifestIds\": {\"440\": \"123456789012345678\"},\n"
        "  \"AppTokens\": {\"730\": \"987654321098765432\"},\n"
        "  \"CDKeys\": {\"440\": \"ABCD-1234\"},\n"
        "  \"GameTitles\": {\"440\": \"Team Fortress 2 Deluxe\"},\n"
        "  \"SubscriptionTimestamps\": {\"440\": \"1600000000\"},\n"
        "  \"SteamIdOverride\": {\"440\": \"76561198000000001\"},\n"
        "  \"DlcDataYaml\": \"440:\\n  441: \\\"TF2 DLC 1\\\"\\n\",\n"
        "  \"DenuvoGamesYaml\": \"76561198000000001:\\n  - 440\\n  - 730\\n\",\n"
        "  \"SyncRole\": \"overrider\",\n"
        "  \"SyncTopology\": \"p2p_and_cloud\",\n"
        "  \"SyncCloudProvider\": \"webdav\",\n"
        "  \"SyncWebDavUrl\": \"https://webdav.example.com/sync\",\n"
        "  \"SyncWebDavUser\": \"testuser\",\n"
        "  \"SyncFilter\": {\n"
        "    \"AllManifestsAndLua\": true,\n"
        "    \"ManifestAppIds\": [440, 730],\n"
        "    \"AllConfigs\": false,\n"
        "    \"ConfigKeys\": [\"SafeMode\", \"RyuuKey\"],\n"
        "    \"AllCloudSaves\": true,\n"
        "    \"CloudSaveAppIds\": [480],\n"
        "    \"AllAchievements\": false,\n"
        "    \"AchievementAppIds\": [730],\n"
        "    \"AllApiKeys\": true,\n"
        "    \"ProviderNames\": [\"ryuu\", \"hubcap\"]\n"
        "  }\n"
        "}";

    // 1. Apply configuration JSON
    bool applied = StoreInjectView::applyFullConfigJson(tempYaml.string(), jsonInput, err);
    EXPECT_TRUE(applied) << "applyFullConfigJson failed with: " << err;
    EXPECT_TRUE(err.empty());
    EXPECT_TRUE(fs::exists(tempYaml));

    // 2. Render configuration JSON
    std::string renderedJson = StoreInjectView::renderFullConfigJson(tempYaml.string());
    EXPECT_FALSE(renderedJson.empty());

    // Verify scalar and boolean values
    EXPECT_NE(renderedJson.find("\"DisableFamilyShareLock\": false"), std::string::npos);
    EXPECT_NE(renderedJson.find("\"UseWhitelist\": true"), std::string::npos);
    EXPECT_NE(renderedJson.find("\"SafeMode\": true"), std::string::npos);
    EXPECT_NE(renderedJson.find("\"WarnHashMissmatch\": true"), std::string::npos);
    EXPECT_NE(renderedJson.find("\"NotifyInit\": false"), std::string::npos);
    EXPECT_NE(renderedJson.find("\"API\": false"), std::string::npos);
    EXPECT_NE(renderedJson.find("\"DisableCloud\": false"), std::string::npos);
    EXPECT_NE(renderedJson.find("\"DisableUpdates\": false"), std::string::npos);
    EXPECT_NE(renderedJson.find("\"DumpClientInterfaces\": true"), std::string::npos);
    EXPECT_NE(renderedJson.find("\"ExtendedLogging\": true"), std::string::npos);
    EXPECT_NE(renderedJson.find("\"MaxSchemaTries\": 42"), std::string::npos);
    EXPECT_NE(renderedJson.find("\"FakeWalletBalance\": 9999"), std::string::npos);
    EXPECT_NE(renderedJson.find("\"LogLevels\": 165"), std::string::npos);

    // Verify strings
    EXPECT_NE(renderedJson.find("\"FakeName\": \"Gordon Freeman\""), std::string::npos);
    EXPECT_NE(renderedJson.find("\"FakeEmail\": \"gordon@blackmesa.gov\""), std::string::npos);
    EXPECT_NE(renderedJson.find("\"RyuuKey\": \"ryuu-sec-123\""), std::string::npos);
    EXPECT_NE(renderedJson.find("\"DepotBoxKey\": \"dpbx-sec-456\""), std::string::npos);
    EXPECT_NE(renderedJson.find("\"HubcapKey\": \"hub-sec-789\""), std::string::npos);

    // Verify status structs
    EXPECT_NE(renderedJson.find("\"IdleStatus\": {\"AppId\": 480, \"Title\": \"Spacewar In Space\"}"), std::string::npos);
    EXPECT_NE(renderedJson.find("\"UnownedStatus\": {\"AppId\": 730, \"Title\": \"CS Test\"}"), std::string::npos);

    // Verify arrays and maps
    EXPECT_NE(renderedJson.find("\"AppIds\": [440, 730]"), std::string::npos);
    EXPECT_NE(renderedJson.find("\"AdditionalApps\": [10, 20]"), std::string::npos);
    EXPECT_NE(renderedJson.find("\"FakeOffline\": [480]"), std::string::npos);
    EXPECT_NE(renderedJson.find("\"DepotBlacklist\": [999]"), std::string::npos);
    EXPECT_NE(renderedJson.find("\"ABCD-1234\""), std::string::npos);
    EXPECT_NE(renderedJson.find("\"Team Fortress 2 Deluxe\""), std::string::npos);
    EXPECT_NE(renderedJson.find("\"123456789012345678\""), std::string::npos);

    // Verify complex YAML nodes
    EXPECT_NE(renderedJson.find("TF2 DLC 1"), std::string::npos);

    // Verify sync settings
    EXPECT_NE(renderedJson.find("\"SyncRole\": \"overrider\""), std::string::npos);
    EXPECT_NE(renderedJson.find("\"SyncTopology\": \"p2p_and_cloud\""), std::string::npos);
    EXPECT_NE(renderedJson.find("\"SyncCloudProvider\": \"webdav\""), std::string::npos);
    EXPECT_NE(renderedJson.find("\"SyncWebDavUrl\": \"https://webdav.example.com/sync\""), std::string::npos);
    EXPECT_NE(renderedJson.find("\"SyncWebDavUser\": \"testuser\""), std::string::npos);
    EXPECT_NE(renderedJson.find("\"AllManifestsAndLua\": true"), std::string::npos);
    EXPECT_NE(renderedJson.find("\"SafeMode\""), std::string::npos);
    EXPECT_NE(renderedJson.find("\"ryuu\""), std::string::npos);
    EXPECT_NE(renderedJson.find("\"hubcap\""), std::string::npos);

    // 3. Round trip: apply rendered JSON to a second file and re-render
    fs::path tempYaml2 = m_testDir / "config2.yaml";
    std::string err2;
    EXPECT_TRUE(StoreInjectView::applyFullConfigJson(tempYaml2.string(), renderedJson, err2)) << err2;
    std::string renderedJson2 = StoreInjectView::renderFullConfigJson(tempYaml2.string());
    EXPECT_EQ(renderedJson, renderedJson2);
}

TEST_F(SyncIpcViewsTest, CloudCredentialsLoadSaveAndFilePermissions)
{
    StoreInjectView::CloudCredentials_t creds;
    creds.gdriveClientId = "test_client_id.apps.googleusercontent.com";
    creds.gdriveClientSecret = "GOCSPX-secret123";
    creds.gdriveAccessToken = "ya29.test_access_token";
    creds.gdriveRefreshToken = "1//test_refresh_token";
    creds.gdriveTokenExpiresAt = 1700000000;
    creds.gdriveDeviceAuthEndpoint = "https://oauth2.googleapis.com/device/code";
    creds.gdriveTokenEndpoint = "https://oauth2.googleapis.com/token";
    creds.gdriveApiBaseUrl = "https://www.googleapis.com/drive/v3";
    creds.gdriveUploadBaseUrl = "https://www.googleapis.com/upload/drive/v3";
    creds.webdavPassword = "secret_webdav_password";

    EXPECT_TRUE(StoreInjectView::saveCloudCredentials(m_testDir.string(), creds));

    fs::path credsFile = m_testDir / "sync_keys" / "cloud_creds.yaml";
    ASSERT_TRUE(fs::exists(credsFile));

    // Verify 0600 permissions
    struct stat st{};
    ASSERT_EQ(stat(credsFile.c_str(), &st), 0);
    EXPECT_EQ(st.st_mode & 0777, 0600);

    StoreInjectView::CloudCredentials_t loaded;
    EXPECT_TRUE(StoreInjectView::loadCloudCredentials(m_testDir.string(), loaded));

    EXPECT_EQ(loaded.gdriveClientId, creds.gdriveClientId);
    EXPECT_EQ(loaded.gdriveClientSecret, creds.gdriveClientSecret);
    EXPECT_EQ(loaded.gdriveAccessToken, creds.gdriveAccessToken);
    EXPECT_EQ(loaded.gdriveRefreshToken, creds.gdriveRefreshToken);
    EXPECT_EQ(loaded.gdriveTokenExpiresAt, creds.gdriveTokenExpiresAt);
    EXPECT_EQ(loaded.gdriveDeviceAuthEndpoint, creds.gdriveDeviceAuthEndpoint);
    EXPECT_EQ(loaded.gdriveTokenEndpoint, creds.gdriveTokenEndpoint);
    EXPECT_EQ(loaded.gdriveApiBaseUrl, creds.gdriveApiBaseUrl);
    EXPECT_EQ(loaded.gdriveUploadBaseUrl, creds.gdriveUploadBaseUrl);
    EXPECT_EQ(loaded.webdavPassword, creds.webdavPassword);
}

TEST_F(SyncIpcViewsTest, GDriveDeviceFlowJsonParsingAndRendering)
{
    // Start JSON parsing
    std::string startJson = R"({ "clientId": "my_client_id", "clientSecret": "my_client_secret" })";
    std::string cid, sec;
    EXPECT_TRUE(StoreInjectView::parseGDriveDeviceStartJson(startJson, cid, sec));
    EXPECT_EQ(cid, "my_client_id");
    EXPECT_EQ(sec, "my_client_secret");

    // Device code prompt rendering
    Sync::DeviceCodePrompt_t prompt;
    prompt.deviceCode = "dev_123456";
    prompt.userCode = "ABCD-EFGH";
    prompt.verificationUrl = "https://www.google.com/device";
    prompt.expiresInSec = 1800;
    prompt.intervalSec = 5;

    std::string promptJson = StoreInjectView::renderGDriveDevicePromptJson(prompt);
    EXPECT_NE(promptJson.find("\"deviceCode\": \"dev_123456\""), std::string::npos);
    EXPECT_NE(promptJson.find("\"userCode\": \"ABCD-EFGH\""), std::string::npos);
    EXPECT_NE(promptJson.find("\"verificationUrl\": \"https://www.google.com/device\""), std::string::npos);

    // Poll JSON parsing
    std::string pollJson = R"({ "deviceCode": "dev_123456" })";
    std::string polledCode;
    EXPECT_TRUE(StoreInjectView::parseGDriveDevicePollJson(pollJson, polledCode));
    EXPECT_EQ(polledCode, "dev_123456");

    // Render push result JSON
    std::string pushJson = StoreInjectView::renderSyncPushResultJson(42, 7, "etag_xyz123");
    EXPECT_NE(pushJson.find("\"generation\": 42"), std::string::npos);
    EXPECT_NE(pushJson.find("\"pushedFiles\": 7"), std::string::npos);
    EXPECT_NE(pushJson.find("\"etag\": \"etag_xyz123\""), std::string::npos);
}

TEST_F(SyncIpcViewsTest, OAuthCallbackQueryParsingAndHtmlRendering)
{
    // Success query parsing
    std::string reqLine = "GET /sync/gdrive/callback?state=my_test_state_123&code=4%2F0Afge_test_code%2Bxyz HTTP/1.1";
    StoreInjectView::OAuthCallbackParams_t params;
    EXPECT_TRUE(StoreInjectView::parseOAuthCallbackQuery(reqLine, params));
    EXPECT_EQ(params.state, "my_test_state_123");
    EXPECT_EQ(params.code, "4/0Afge_test_code+xyz");
    EXPECT_TRUE(params.error.empty());

    // Error query parsing
    std::string errLine = "/sync/gdrive/callback?error=access_denied&error_description=User%20denied%20consent";
    StoreInjectView::OAuthCallbackParams_t errParams;
    EXPECT_TRUE(StoreInjectView::parseOAuthCallbackQuery(errLine, errParams));
    EXPECT_EQ(errParams.error, "access_denied");
    EXPECT_EQ(errParams.errorDescription, "User denied consent");
    EXPECT_TRUE(errParams.code.empty());

    // Empty or malformed query
    StoreInjectView::OAuthCallbackParams_t invalidParams;
    EXPECT_FALSE(StoreInjectView::parseOAuthCallbackQuery("", invalidParams));
    EXPECT_FALSE(StoreInjectView::parseOAuthCallbackQuery("/sync/gdrive/callback", invalidParams));

    // HTML rendering - success
    std::string successHtml = StoreInjectView::renderOAuthCallbackHtml(true, "Navi Cross-Device Sync is active!");
    EXPECT_NE(successHtml.find("<!DOCTYPE html>"), std::string::npos);
    EXPECT_NE(successHtml.find("AUTHENTICATED ✓"), std::string::npos);
    EXPECT_NE(successHtml.find("#2ecc71"), std::string::npos);
    EXPECT_NE(successHtml.find("Navi Cross-Device Sync is active!"), std::string::npos);
    EXPECT_NE(successHtml.find("window.close()"), std::string::npos);

    // HTML rendering - failure with escaping
    std::string failHtml = StoreInjectView::renderOAuthCallbackHtml(false, "Token exchange failed: <bad_state>&'test'");
    EXPECT_NE(failHtml.find("SIGN-IN FAILED"), std::string::npos);
    EXPECT_NE(failHtml.find("#e74c3c"), std::string::npos);
    EXPECT_NE(failHtml.find("&lt;bad_state&gt;&amp;&#39;test&#39;"), std::string::npos);

    // Browser start JSON rendering
    std::string startJson = StoreInjectView::renderGDriveBrowserStartJson("https://accounts.google.com/o/oauth2/v2/auth?test=1", "state_hex_789");
    EXPECT_NE(startJson.find("\"success\": true"), std::string::npos);
    EXPECT_NE(startJson.find("\"authUrl\": \"https://accounts.google.com/o/oauth2/v2/auth?test=1\""), std::string::npos);
    EXPECT_NE(startJson.find("\"state\": \"state_hex_789\""), std::string::npos);
}

// ── Test 13: Google Drive Disconnect Clears Tokens & Preserves 0600 File Mode ──

TEST_F(SyncIpcViewsTest, CloudCredentialsDisconnectClearsTokensPreserving0600)
{
    fs::path credsFile = m_testDir / "sync_keys" / "cloud_creds.yaml";

    StoreInjectView::CloudCredentials_t initialCreds;
    initialCreds.gdriveClientId = "custom_id.apps.googleusercontent.com";
    initialCreds.gdriveClientSecret = "custom_secret";
    initialCreds.gdriveAccessToken = "ya29.valid_access_token";
    initialCreds.gdriveRefreshToken = "1//valid_refresh_token";
    initialCreds.gdriveTokenExpiresAt = 1800000000;
    initialCreds.webdavPassword = "preserve_this_webdav_password";

    // 1. Save initial active credentials
    ASSERT_TRUE(StoreInjectView::saveCloudCredentials(m_testDir.string(), initialCreds));
    ASSERT_TRUE(fs::exists(credsFile));

    // Verify authenticated initially
    StoreInjectView::CloudCredentials_t loaded;
    ASSERT_TRUE(StoreInjectView::loadCloudCredentials(m_testDir.string(), loaded));
    EXPECT_TRUE(!loaded.gdriveRefreshToken.empty() || !loaded.gdriveAccessToken.empty());

    // 2. Disconnect: wipe tokens and expires timestamp
    loaded.gdriveAccessToken.clear();
    loaded.gdriveRefreshToken.clear();
    loaded.gdriveTokenExpiresAt = 0;
    EXPECT_TRUE(StoreInjectView::saveCloudCredentials(m_testDir.string(), loaded));

    // 3. Verify unauthenticated state after reload
    StoreInjectView::CloudCredentials_t reloaded;
    EXPECT_TRUE(StoreInjectView::loadCloudCredentials(m_testDir.string(), reloaded));
    EXPECT_TRUE(reloaded.gdriveAccessToken.empty());
    EXPECT_TRUE(reloaded.gdriveRefreshToken.empty());
    EXPECT_EQ(reloaded.gdriveTokenExpiresAt, 0);
    EXPECT_FALSE(!reloaded.gdriveRefreshToken.empty() || !reloaded.gdriveAccessToken.empty());

    // Custom clientId/secret and webdavPassword remain intact if configured
    EXPECT_EQ(reloaded.gdriveClientId, "custom_id.apps.googleusercontent.com");
    EXPECT_EQ(reloaded.gdriveClientSecret, "custom_secret");
    EXPECT_EQ(reloaded.webdavPassword, "preserve_this_webdav_password");

    // 4. Verify 0600 POSIX permissions preserved
    struct stat st{};
    ASSERT_EQ(stat(credsFile.c_str(), &st), 0);
    EXPECT_EQ(st.st_mode & 0777, 0600);
    EXPECT_EQ(st.st_mode & 0077, 0000);

    // 5. Verify disconnect success JSON rendering
    std::string disconnectJson = StoreInjectView::renderSuccessJson("Google Drive disconnected");
    EXPECT_NE(disconnectJson.find("\"success\":true"), std::string::npos);
    EXPECT_NE(disconnectJson.find("\"message\":\"Google Drive disconnected\""), std::string::npos);
}

// ── Test 14: Sync Status JSON Reflects Disconnected Cloud State ────────────────

TEST_F(SyncIpcViewsTest, SyncStatusReflectsDisconnectedCloudState)
{
    // 1. Initial State: Connected Google Drive
    StoreInjectView::CloudCredentials_t creds;
    creds.gdriveAccessToken = "ya29.valid_token_abc";
    creds.gdriveRefreshToken = "1//valid_refresh_xyz";
    creds.gdriveTokenExpiresAt = 1800000000;
    ASSERT_TRUE(StoreInjectView::saveCloudCredentials(m_testDir.string(), creds));

    StoreInjectView::CloudCredentials_t loadedBefore;
    ASSERT_TRUE(StoreInjectView::loadCloudCredentials(m_testDir.string(), loadedBefore));

    StoreInjectView::SyncStatusViewModel vmBefore;
    vmBefore.role = "overrider";
    vmBefore.cloudProvider = "gdrive";
    vmBefore.cloudAuthenticated = (!loadedBefore.gdriveRefreshToken.empty() || !loadedBefore.gdriveAccessToken.empty());
    EXPECT_TRUE(vmBefore.cloudAuthenticated);

    std::string jsonBefore = StoreInjectView::renderSyncStatusJson(vmBefore);
    EXPECT_NE(jsonBefore.find("\"cloudProvider\": \"gdrive\""), std::string::npos);
    EXPECT_NE(jsonBefore.find("\"cloudAuthenticated\": true"), std::string::npos);

    // 2. Action: Disconnect Google Drive
    loadedBefore.gdriveAccessToken.clear();
    loadedBefore.gdriveRefreshToken.clear();
    loadedBefore.gdriveTokenExpiresAt = 0;
    EXPECT_TRUE(StoreInjectView::saveCloudCredentials(m_testDir.string(), loadedBefore));

    // 3. Post-Disconnect State: Reload credentials and render status
    StoreInjectView::CloudCredentials_t loadedAfter;
    ASSERT_TRUE(StoreInjectView::loadCloudCredentials(m_testDir.string(), loadedAfter));

    StoreInjectView::SyncStatusViewModel vmAfter;
    vmAfter.role = "overrider";
    vmAfter.cloudProvider = "gdrive";
    vmAfter.cloudAuthenticated = (!loadedAfter.gdriveRefreshToken.empty() || !loadedAfter.gdriveAccessToken.empty());
    EXPECT_FALSE(vmAfter.cloudAuthenticated);

    std::string jsonAfter = StoreInjectView::renderSyncStatusJson(vmAfter);
    EXPECT_NE(jsonAfter.find("\"cloudProvider\": \"gdrive\""), std::string::npos);
    EXPECT_NE(jsonAfter.find("\"cloudAuthenticated\": false"), std::string::npos);
    EXPECT_EQ(jsonAfter.find("\"cloudAuthenticated\": true"), std::string::npos);
}

// ── Test 15: Google Drive Disconnect Handles Missing Credentials Gracefully ───

TEST_F(SyncIpcViewsTest, GDriveDisconnectWhenCredentialsFileMissingOrEmpty)
{
    fs::path credsFile = m_testDir / "sync_keys" / "cloud_creds.yaml";
    ASSERT_FALSE(fs::exists(credsFile));

    // Calling saveCloudCredentials with empty credentials creates a clean file with 0600 mode
    StoreInjectView::CloudCredentials_t emptyCreds;
    EXPECT_TRUE(StoreInjectView::saveCloudCredentials(m_testDir.string(), emptyCreds));
    ASSERT_TRUE(fs::exists(credsFile));

    struct stat st{};
    ASSERT_EQ(stat(credsFile.c_str(), &st), 0);
    EXPECT_EQ(st.st_mode & 0777, 0600);

    StoreInjectView::CloudCredentials_t loaded;
    EXPECT_TRUE(StoreInjectView::loadCloudCredentials(m_testDir.string(), loaded));
    EXPECT_TRUE(loaded.gdriveAccessToken.empty());
    EXPECT_TRUE(loaded.gdriveRefreshToken.empty());

    StoreInjectView::SyncStatusViewModel vm;
    vm.cloudProvider = "gdrive";
    vm.cloudAuthenticated = (!loaded.gdriveRefreshToken.empty() || !loaded.gdriveAccessToken.empty());
    EXPECT_FALSE(vm.cloudAuthenticated);

    std::string json = StoreInjectView::renderSyncStatusJson(vm);
    EXPECT_NE(json.find("\"cloudAuthenticated\": false"), std::string::npos);
}
