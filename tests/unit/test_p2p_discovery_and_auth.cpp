#include "p2p_discovery.hpp"
#include "p2p_session.hpp"
#include "cloud_provider.hpp"
#include "sync_crypto.hpp"
#include "sync_manifest.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <thread>

namespace fs = std::filesystem;
using namespace Sync;

class MockCloudStorageProvider : public ICloudStorageProvider
{
public:
    ECloudProviderType providerType() const override { return ECloudProviderType::k_WebDav; }
    ECloudError refreshAuthTokenIfNeeded() override { return ECloudError::k_Ok; }
    ECloudError queryQuota(CloudQuotaInfo_t& outQuota) override
    {
        outQuota.limitBytes = 1000000000;
        outQuota.usedBytes = 100;
        return ECloudError::k_Ok;
    }
    ECloudError fetchRemoteManifest(uint64_t, SyncStateManifest_t&, std::string&) override
    {
        return ECloudError::k_Ok;
    }
    ECloudError pushEncryptedObject(uint64_t /*steamId64*/, const std::string& objectKey, const std::vector<uint8_t>& blob) override
    {
        pushedObjects[objectKey] = blob;
        return ECloudError::k_Ok;
    }
    ECloudError pullEncryptedObject(uint64_t, const std::string& objectKey, std::vector<uint8_t>& outBlob) override
    {
        auto it = pushedObjects.find(objectKey);
        if (it != pushedObjects.end())
        {
            outBlob = it->second;
            return ECloudError::k_Ok;
        }
        return ECloudError::k_NotFound;
    }
    ECloudError commitRemoteManifest(uint64_t /*steamId64*/, const SyncStateManifest_t& manifest, const std::string& /*expectedETag*/, std::string& outNewETag) override
    {
        committedManifest = manifest;
        commitCount++;
        outNewETag = "new-etag-rev-1";
        return ECloudError::k_Ok;
    }

    int commitCount = 0;
    SyncStateManifest_t committedManifest;
    std::unordered_map<std::string, std::vector<uint8_t>> pushedObjects;
};

class P2PDiscoveryAndAuthTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        signal(SIGPIPE, SIG_IGN);
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        m_testDir = fs::temp_directory_path() / ("navi_p2p_test_" + std::to_string(now));
        fs::create_directories(m_testDir);
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(m_testDir, ec);
    }

    fs::path m_testDir;
};

TEST_F(P2PDiscoveryAndAuthTest, SteamIdDiscoveryHashValidation)
{
    const uint64_t steamId = 76561198012345678ULL;
    const int64_t testUnixSec = 1700000000; // arbitrary fixed UTC timestamp

    std::string hash = computeSteamIdDiscoveryHash(steamId, testUnixSec);
    EXPECT_EQ(hash.size(), 16);

    // Matches same day
    EXPECT_TRUE(matchesSteamIdDiscoveryHash(steamId, hash, testUnixSec));

    // Matches +1 day (clock skew tolerance)
    EXPECT_TRUE(matchesSteamIdDiscoveryHash(steamId, hash, testUnixSec + 86400));

    // Matches -1 day (clock skew tolerance)
    EXPECT_TRUE(matchesSteamIdDiscoveryHash(steamId, hash, testUnixSec - 86400));

    // Fails on +2 days skew
    EXPECT_FALSE(matchesSteamIdDiscoveryHash(steamId, hash, testUnixSec + (2 * 86400)));

    // Fails on different steamId
    const uint64_t differentSteamId = 76561198099999999ULL;
    EXPECT_FALSE(matchesSteamIdDiscoveryHash(differentSteamId, hash, testUnixSec));

    // Fails closed on steamId == 0
    EXPECT_EQ(computeSteamIdDiscoveryHash(0, testUnixSec), "");
    EXPECT_FALSE(matchesSteamIdDiscoveryHash(0, hash, testUnixSec));
    EXPECT_FALSE(matchesSteamIdDiscoveryHash(steamId, "", testUnixSec));
    EXPECT_FALSE(matchesSteamIdDiscoveryHash(steamId, "tooshort", testUnixSec));
}

TEST_F(P2PDiscoveryAndAuthTest, DiscoveryServiceLoopbackAndAccountFilter)
{
    const uint64_t steamIdA = 76561198011111111ULL;
    const uint64_t steamIdOther = 76561198022222222ULL;

    DiscoveredPeer_t peerInfoA;
    peerInfoA.deviceId = "device-A";
    peerInfoA.hostname = "SteamDeck";
    peerInfoA.role = ESyncRole::k_Receiver;
    peerInfoA.tlsPort = 18881;
    peerInfoA.globalGeneration = 10;
    peerInfoA.keyEpoch = 1;

    DiscoveredPeer_t peerInfoB;
    peerInfoB.deviceId = "device-B";
    peerInfoB.hostname = "DesktopPC";
    peerInfoB.role = ESyncRole::k_Overrider;
    peerInfoB.tlsPort = 18882;
    peerInfoB.globalGeneration = 12;
    peerInfoB.keyEpoch = 1;

    DiscoveredPeer_t peerInfoOther;
    peerInfoOther.deviceId = "device-Other";
    peerInfoOther.hostname = "StrangerDevice";
    peerInfoOther.role = ESyncRole::k_Overrider;
    peerInfoOther.tlsPort = 18883;
    peerInfoOther.globalGeneration = 5;
    peerInfoOther.keyEpoch = 1;

    // Use port 0 for ephemeral OS allocation
    P2PDiscoveryService serviceA(steamIdA, peerInfoA, 0, "", 100);
    P2PDiscoveryService serviceB(steamIdA, peerInfoB, 0, "", 100);
    P2PDiscoveryService serviceOther(steamIdOther, peerInfoOther, 0, "", 100);

    ASSERT_TRUE(serviceA.start());
    ASSERT_TRUE(serviceB.start());
    ASSERT_TRUE(serviceOther.start());

    EXPECT_GT(serviceA.boundUdpPort(), 0);
    EXPECT_GT(serviceB.boundUdpPort(), 0);
    EXPECT_GT(serviceOther.boundUdpPort(), 0);

    // Send direct loopback beacons to each other
    serviceB.sendBeaconTo("127.0.0.1", serviceA.boundUdpPort());
    serviceA.sendBeaconTo("127.0.0.1", serviceB.boundUdpPort());
    serviceOther.sendBeaconTo("127.0.0.1", serviceA.boundUdpPort()); // Mismatched steamId

    // Allow worker threads to receive and process
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    auto peersSeenByA = serviceA.getDiscoveredPeers();
    auto peersSeenByB = serviceB.getDiscoveredPeers();

    // A should see B, but NOT Other
    ASSERT_EQ(peersSeenByA.size(), 1u);
    EXPECT_EQ(peersSeenByA[0].deviceId, "device-B");
    EXPECT_EQ(peersSeenByA[0].hostname, "DesktopPC");
    EXPECT_EQ(peersSeenByA[0].tlsPort, 18882);
    EXPECT_EQ(peersSeenByA[0].role, ESyncRole::k_Overrider);

    // B should see A
    ASSERT_EQ(peersSeenByB.size(), 1u);
    EXPECT_EQ(peersSeenByB[0].deviceId, "device-A");
    EXPECT_EQ(peersSeenByB[0].hostname, "SteamDeck");
    EXPECT_EQ(peersSeenByB[0].tlsPort, 18881);

    serviceA.stop();
    serviceB.stop();
    serviceOther.stop();
}

TEST_F(P2PDiscoveryAndAuthTest, FirstTimePinPairingAndMasterKeyProvision)
{
    const uint64_t steamId64 = 76561198012345678ULL;

    MasterKey_t overriderKey{};
    ASSERT_TRUE(generateMasterKey(overriderKey));

    auto serverStore = std::make_shared<TrustedPeerStore>((m_testDir / "server_trusted.yaml").string());
    auto clientStore = std::make_shared<TrustedPeerStore>((m_testDir / "client_trusted.yaml").string());

    P2PPairingProvisionBundle_t provisionBundle;
    provisionBundle.steamId64 = steamId64;
    provisionBundle.keyEpoch = 1;
    provisionBundle.masterKey = overriderKey;
    provisionBundle.activeCloudProvider = ECloudProviderType::k_WebDav;
    provisionBundle.webDavUrl = "https://dav.example.com/sync";
    provisionBundle.webDavUser = "overrider_user";
    provisionBundle.encryptedCloudAuthToken = { 0xAA, 0xBB, 0xCC, 0xDD };

    P2PServerConfig_t serverCfg;
    serverCfg.steamId64 = steamId64;
    serverCfg.deviceId = "overrider-pc";
    serverCfg.hostname = "OverriderDesktop";
    serverCfg.trustedStore = serverStore;
    serverCfg.provisionBundle = provisionBundle;
    serverCfg.activePin = "654321";

    P2PSessionServer server(std::move(serverCfg));
    ASSERT_TRUE(server.start(0, "127.0.0.1"));
    const uint16_t serverPort = server.boundPort();
    ASSERT_GT(serverPort, 0);

    P2PClientConfig_t clientCfg;
    clientCfg.steamId64 = steamId64;
    clientCfg.deviceId = "receiver-deck";
    clientCfg.hostname = "ReceiverDeck";
    clientCfg.trustedStore = clientStore;

    P2PSessionClient client(std::move(clientCfg));

    P2PPairingProvisionBundle_t receivedBundle;
    EP2PError err = client.pairWithPin("127.0.0.1", serverPort, "654321", receivedBundle);
    EXPECT_EQ(err, EP2PError::k_Ok);

    // Verify bundle received by Receiver matches Overrider
    EXPECT_EQ(receivedBundle.steamId64, steamId64);
    EXPECT_EQ(receivedBundle.keyEpoch, 1u);
    EXPECT_EQ(receivedBundle.masterKey, overriderKey);
    EXPECT_EQ(receivedBundle.activeCloudProvider, ECloudProviderType::k_WebDav);
    EXPECT_EQ(receivedBundle.webDavUrl, "https://dav.example.com/sync");
    EXPECT_EQ(receivedBundle.webDavUser, "overrider_user");
    EXPECT_EQ(receivedBundle.encryptedCloudAuthToken, provisionBundle.encryptedCloudAuthToken);
    EXPECT_EQ(receivedBundle.overriderDeviceId, "overrider-pc");

    // Receiver should immediately be able to decrypt a blob encrypted by Overrider
    EncryptedEnvelopeMetadata_t meta;
    meta.steamId64 = steamId64;
    meta.keyEpoch = 1;
    meta.category = ESyncCategory::k_ManifestsAndLua;
    meta.logicalPath = "configs/synced_config.yaml";
    meta.generation = 1;

    const std::vector<uint8_t> plaintext = { 'H', 'e', 'l', 'l', 'o', ' ', 'S', 'y', 'n', 'c', '!' };
    std::vector<uint8_t> encryptedBlob;
    ASSERT_TRUE(encryptBlob(overriderKey, meta, plaintext, encryptedBlob));

    std::vector<uint8_t> decryptedPlaintext;
    ASSERT_TRUE(decryptBlob(receivedBundle.masterKey, meta, encryptedBlob, decryptedPlaintext));
    EXPECT_EQ(decryptedPlaintext, plaintext);

    // Verify both peers are saved in each other's TrustedPeerStore
    EXPECT_TRUE(serverStore->isPeerTrusted(steamId64, "receiver-deck", client.certSha256Hex()));
    EXPECT_TRUE(clientStore->isPeerTrusted(steamId64, "overrider-pc", server.certSha256Hex()));

    server.stop();
}

TEST_F(P2PDiscoveryAndAuthTest, WrongPinRejectionAndLockoutRateLimiting)
{
    const uint64_t steamId64 = 76561198012345678ULL;

    auto serverStore = std::make_shared<TrustedPeerStore>((m_testDir / "server_trusted2.yaml").string());
    auto clientStore = std::make_shared<TrustedPeerStore>((m_testDir / "client_trusted2.yaml").string());

    P2PServerConfig_t serverCfg;
    serverCfg.steamId64 = steamId64;
    serverCfg.deviceId = "server-node";
    serverCfg.hostname = "ServerNode";
    serverCfg.trustedStore = serverStore;
    serverCfg.activePin = "112233";

    P2PSessionServer server(std::move(serverCfg));
    ASSERT_TRUE(server.start(0, "127.0.0.1"));
    const uint16_t serverPort = server.boundPort();

    P2PClientConfig_t clientCfg;
    clientCfg.steamId64 = steamId64;
    clientCfg.deviceId = "client-node";
    clientCfg.hostname = "ClientNode";
    clientCfg.trustedStore = clientStore;

    P2PSessionClient client(std::move(clientCfg));

    P2PPairingProvisionBundle_t dummyBundle;

    // Strike 1
    EP2PError err1 = client.pairWithPin("127.0.0.1", serverPort, "000000", dummyBundle);
    EXPECT_EQ(err1, EP2PError::k_PinInvalid);
    EXPECT_EQ(server.failedAttemptsForIp("127.0.0.1"), 1);

    // Strike 2
    EP2PError err2 = client.pairWithPin("127.0.0.1", serverPort, "000001", dummyBundle);
    EXPECT_EQ(err2, EP2PError::k_PinInvalid);
    EXPECT_EQ(server.failedAttemptsForIp("127.0.0.1"), 2);

    const std::string originalPin = server.activePin();

    // Strike 3
    EP2PError err3 = client.pairWithPin("127.0.0.1", serverPort, "000002", dummyBundle);
    EXPECT_EQ(err3, EP2PError::k_PinInvalid);
    EXPECT_EQ(server.failedAttemptsForIp("127.0.0.1"), 3);
    EXPECT_TRUE(server.isIpLockedOut("127.0.0.1"));

    // Active PIN must have been invalidated & regenerated upon 3rd failure
    EXPECT_NE(server.activePin(), originalPin);

    // Strike 4: Even with the original pin, server rejects with k_LockedOut
    EP2PError err4 = client.pairWithPin("127.0.0.1", serverPort, originalPin, dummyBundle);
    EXPECT_EQ(err4, EP2PError::k_LockedOut);

    server.stop();
}

TEST_F(P2PDiscoveryAndAuthTest, CrossAccountHandshakeRejection)
{
    const uint64_t steamIdServer = 76561198011111111ULL;
    const uint64_t steamIdClient = 76561198022222222ULL;

    auto serverStore = std::make_shared<TrustedPeerStore>((m_testDir / "server_trusted3.yaml").string());
    auto clientStore = std::make_shared<TrustedPeerStore>((m_testDir / "client_trusted3.yaml").string());

    P2PServerConfig_t serverCfg;
    serverCfg.steamId64 = steamIdServer;
    serverCfg.deviceId = "server-node";
    serverCfg.hostname = "ServerNode";
    serverCfg.trustedStore = serverStore;
    serverCfg.activePin = "777888";

    P2PSessionServer server(std::move(serverCfg));
    ASSERT_TRUE(server.start(0, "127.0.0.1"));
    const uint16_t serverPort = server.boundPort();

    P2PClientConfig_t clientCfg;
    clientCfg.steamId64 = steamIdClient; // Different account!
    clientCfg.deviceId = "client-node";
    clientCfg.hostname = "ClientNode";
    clientCfg.trustedStore = clientStore;

    P2PSessionClient client(std::move(clientCfg));

    P2PPairingProvisionBundle_t dummyBundle;
    EP2PError err = client.pairWithPin("127.0.0.1", serverPort, "777888", dummyBundle);
    EXPECT_EQ(err, EP2PError::k_SteamIdMismatch);

    server.stop();
}

TEST_F(P2PDiscoveryAndAuthTest, TrustedMutualSyncAndCloudRelayCommitAndMitmRejection)
{
    const uint64_t steamId64 = 76561198012345678ULL;

    MasterKey_t masterKey{};
    ASSERT_TRUE(generateMasterKey(masterKey));

    auto serverStore = std::make_shared<TrustedPeerStore>((m_testDir / "server_trusted4.yaml").string());
    auto clientStore = std::make_shared<TrustedPeerStore>((m_testDir / "client_trusted4.yaml").string());

    // Prepare mock cloud relay on Overrider
    MockCloudStorageProvider mockCloudRelay;

    // Prepare sample manifest and blobs
    SyncStateManifest_t sampleManifest;
    sampleManifest.schemaVersion = 1;
    sampleManifest.steamId64 = steamId64;
    sampleManifest.keyEpoch = 1;
    sampleManifest.deviceId = "overrider-pc";
    sampleManifest.globalGeneration = 42;

    SyncFileEntry_t entry1;
    entry1.category = ESyncCategory::k_ManifestsAndLua;
    entry1.appId = 480;
    entry1.logicalPath = "manifests/480.lua";
    entry1.sizeBytes = 14;
    entry1.sha256Hex = computeBufferSha256Hex("print('test')\n");
    sampleManifest.entries.push_back(entry1);
    sampleManifest.manifestRootSha256 = computeManifestRootSha256(sampleManifest.entries);

    std::unordered_map<std::string, std::vector<uint8_t>> sampleBlobs;
    sampleBlobs["manifests/480.lua"] = { 'p', 'r', 'i', 'n', 't', '(', '\'', 't', 'e', 's', 't', '\'', ')', '\n' };

    P2PServerConfig_t serverCfg;
    serverCfg.steamId64 = steamId64;
    serverCfg.deviceId = "overrider-pc";
    serverCfg.hostname = "OverriderDesktop";
    serverCfg.trustedStore = serverStore;
    serverCfg.provisionBundle.masterKey = masterKey;
    serverCfg.syncManifest = sampleManifest;
    serverCfg.syncBlobs = sampleBlobs;
    serverCfg.optionalCloudRelay = &mockCloudRelay;

    P2PSessionServer server(std::move(serverCfg));
    ASSERT_TRUE(server.start(0, "127.0.0.1"));
    const uint16_t serverPort = server.boundPort();

    P2PClientConfig_t clientCfg;
    clientCfg.steamId64 = steamId64;
    clientCfg.deviceId = "receiver-deck";
    clientCfg.hostname = "ReceiverDeck";
    clientCfg.trustedStore = clientStore;

    P2PSessionClient client(std::move(clientCfg));

    // Pre-pin certificates in both TrustedPeerStores (simulating already-paired nodes)
    TrustedPeerEntry_t clientEntry;
    clientEntry.deviceId = "receiver-deck";
    clientEntry.steamId64 = steamId64;
    clientEntry.certSha256FingerprintHex = client.certSha256Hex();
    serverStore->addTrustedPeer(clientEntry);

    TrustedPeerEntry_t serverEntry;
    serverEntry.deviceId = "overrider-pc";
    serverEntry.steamId64 = steamId64;
    serverEntry.certSha256FingerprintHex = server.certSha256Hex();
    clientStore->addTrustedPeer(serverEntry);

    // Zero-PIN direct sync transfer over TLS 1.3
    SyncStateManifest_t receivedManifest;
    std::unordered_map<std::string, std::vector<uint8_t>> receivedBlobs;

    EP2PError syncErr = client.syncWithPeer("127.0.0.1", serverPort, masterKey, {}, {}, receivedManifest, receivedBlobs);
    EXPECT_EQ(syncErr, EP2PError::k_Ok);

    // Verify manifest and blobs transferred accurately
    EXPECT_EQ(receivedManifest.deviceId, "overrider-pc");
    EXPECT_EQ(receivedManifest.globalGeneration, 42u);
    ASSERT_EQ(receivedManifest.entries.size(), 1u);
    EXPECT_EQ(receivedManifest.entries[0].logicalPath, "manifests/480.lua");

    ASSERT_EQ(receivedBlobs.size(), 1u);
    EXPECT_EQ(receivedBlobs["manifests/480.lua"], sampleBlobs["manifests/480.lua"]);

    // Verify Cloud Relay commit was triggered on Overrider
    EXPECT_EQ(mockCloudRelay.commitCount, 1);
    EXPECT_EQ(mockCloudRelay.committedManifest.deviceId, "overrider-pc");
    EXPECT_EQ(mockCloudRelay.committedManifest.globalGeneration, 42u);
    EXPECT_EQ(mockCloudRelay.pushedObjects["manifests/480.lua"], sampleBlobs["manifests/480.lua"]);

    // MITM Rejection Test: An unpinned peer attempting to sync with server
    auto mitmStore = std::make_shared<TrustedPeerStore>((m_testDir / "mitm_trusted.yaml").string());
    P2PClientConfig_t mitmCfg;
    mitmCfg.steamId64 = steamId64;
    mitmCfg.deviceId = "mitm-attacker";
    mitmCfg.hostname = "AttackerDevice";
    mitmCfg.trustedStore = mitmStore;

    P2PSessionClient mitmClient(std::move(mitmCfg));
    // Notice: Server does NOT have mitmClient's cert pinned, and mitmClient does NOT have server pinned!

    SyncStateManifest_t mitmManifest;
    std::unordered_map<std::string, std::vector<uint8_t>> mitmBlobs;

    // Case 1: MITM client connects to server without server in its store -> rejected at client side
    EP2PError mitmErr1 = mitmClient.syncWithPeer("127.0.0.1", serverPort, masterKey, {}, {}, mitmManifest, mitmBlobs);
    EXPECT_EQ(mitmErr1, EP2PError::k_UntrustedPeer);

    // Case 2: Even if MITM client blindly trusts server cert in its store, Server rejects MITM client cert!
    mitmStore->addTrustedPeer(serverEntry);
    EP2PError mitmErr2 = mitmClient.syncWithPeer("127.0.0.1", serverPort, masterKey, {}, {}, mitmManifest, mitmBlobs);
    EXPECT_EQ(mitmErr2, EP2PError::k_UntrustedPeer);

    server.stop();
}
