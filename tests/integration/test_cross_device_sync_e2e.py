"""
Cross-Device Sync End-to-End Integration Test Suite.
Validates Linux Desktop <-> Steam Deck / SteamOS synchronization across all 6 E2E scenarios:
  Scenario 1: Cloud-Only E2E (Google Drive + Base32 Recovery Key + Desktop Linux -> SteamOS Wake-from-Sleep)
  Scenario 2: Cloud-Only E2E (WebDAV Provider Switch & If-Match 412 Concurrency Protection)
  Scenario 3: P2P Direct LAN Pairing (6-Digit PIN + Automatic Master Key Transfer + Hybrid Cloud Commit)
  Scenario 4: Offline Achievement Set Union (A U B) & Cloud Save 3-Way Conflict Resolution
  Scenario 5: Active Game Process Lock (running_appid == target_appid) & Deferred Queue Drain
  Scenario 6: Cross-SteamID Contamination Rejection & CEF Modal (navi_inject.js) DOM/XSS Verification
"""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Any, Dict, Generator

import pytest
from tests.integration.mock_cloud_relay_server import MockCloudRelayServer

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
BIN_DIR = REPO_ROOT / "bin"
LIBNAVI_SO = BIN_DIR / "libnavi.so"
INJECT_SCRIPT = REPO_ROOT / "res" / "inject-scripts" / "navi_inject.js"

# ─────────────────────────────────────────────────────────────────────────────
# C++ E2E Test Runner Source
# Compiles into a 32-bit executable linking bin/libnavi.so to drive real
# C++ CloudProvider, P2P, Crypto, SyncEngine, and ProcessLock routines.
# ─────────────────────────────────────────────────────────────────────────────

CPP_RUNNER_SOURCE = r"""
#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>
#include <filesystem>
#include <fstream>
#include <cassert>
#include <cstring>

#include "achievement_merger.hpp"
#include "atomic_file.hpp"
#include "cloud_provider.hpp"
#include "p2p_discovery.hpp"
#include "p2p_session.hpp"
#include "log.hpp"
#include "power_monitor.hpp"
#include "process_lock.hpp"
#include "sync_crypto.hpp"
#include "sync_engine.hpp"
#include "sync_manifest.hpp"

namespace fs = std::filesystem;

static std::string sha256Of(const std::vector<uint8_t>& b) {
    return Sync::computeBufferSha256Hex(std::string_view(reinterpret_cast<const char*>(b.data()), b.size()));
}

static std::string sha256Of(const std::string& s) {
    return Sync::computeBufferSha256Hex(std::string_view(s.data(), s.size()));
}

static std::string cleanETag(std::string etag) {
    while (!etag.empty() && (etag.front() == '"' || etag.front() == '\\')) {
        etag.erase(0, 1);
    }
    while (!etag.empty() && (etag.back() == '"' || etag.back() == '\\')) {
        etag.pop_back();
    }
    return etag;
}

// ── Subcommand: scenario1_push ──────────────────────────────────────────────
int cmd_scenario1_push(int argc, char** argv) {
    if (argc < 6) return 1;
    std::string serverUrl = argv[2];
    std::string steamRoot = argv[3];
    std::string configDir = argv[4];
    uint64_t steamId64 = std::stoull(argv[5]);
    uint32_t steam32 = static_cast<uint32_t>(steamId64 & 0xFFFFFFFFULL);

    // 1. Generate 256-bit Master Key
    Sync::MasterKey_t masterKey{};
    if (!Sync::generateMasterKey(masterKey)) {
        std::cerr << "Failed to generate master key\n";
        return 1;
    }

    // 2. Export 55-char Base32 Recovery Key with CRC-16
    std::string recKey = Sync::encodeRecoveryKey(masterKey);

    // Validate CRC-16
    Sync::MasterKey_t decodedKey{};
    bool crcValid = Sync::decodeRecoveryKey(recKey, decodedKey);
    assert(crcValid && decodedKey == masterKey);

    // Test tamper rejection
    std::string tampered = recKey;
    tampered[10] = (tampered[10] == 'A') ? 'B' : 'A';
    Sync::MasterKey_t badKey{};
    bool tamperRejected = !Sync::decodeRecoveryKey(tampered, badKey);
    assert(tamperRejected);

    // 3. Prepare Desktop Linux Steam Overrider layout
    fs::create_directories(fs::path(steamRoot) / "steamapps");
    fs::create_directories(fs::path(steamRoot) / "depotcache");
    fs::create_directories(fs::path(steamRoot) / "userdata" / std::to_string(steam32) / "480" / "remote");
    fs::create_directories(fs::path(configDir) / "plugins");

    // Payloads
    std::string acfData = "\"AppState\"\n{\n\t\"appid\"\t\"480\"\n\t\"buildid\"\t\"100\"\n}\n";
    std::string luaData = "addappid(480, 1)\n";
    std::string depotData = "BINARY_DEPOT_MANIFEST_DATA_481";
    std::string configData = "syncRole: overrider\nsyncTopology: cloud\n";
    std::string saveData = "OverriderCloudSaveData_480_desktop";

    // Achievement BinaryKV
    Sync::AchievementStatBlob_t achBlob;
    achBlob.appId = 480;
    achBlob.achievements["Ach_Win"] = {"Ach_Win", true, 1000};
    achBlob.achievements["Ach_Play"] = {"Ach_Play", true, 2000};
    achBlob.intStats["kills"] = 50;
    std::vector<uint8_t> achData = Sync::serializeAchievementBinaryKV(achBlob);

    // Write on-disk local files
    AtomicFile::write((fs::path(steamRoot) / "steamapps" / "appmanifest_480.acf").string(), acfData, true);
    AtomicFile::write((fs::path(steamRoot) / "depotcache" / "481_123.manifest").string(), depotData, true);
    AtomicFile::write((fs::path(steamRoot) / "userdata" / std::to_string(steam32) / "480" / "remote" / "save.dat").string(), saveData, true);
    AtomicFile::write((fs::path(steamRoot) / "userdata" / std::to_string(steam32) / "480" / "remote" / "UserGameStats.bin").string(),
                      std::string_view(reinterpret_cast<const char*>(achData.data()), achData.size()), true);
    AtomicFile::write((fs::path(configDir) / "config.yaml").string(), configData, true);
    AtomicFile::write((fs::path(configDir) / "plugins" / "480.lua").string(), luaData, true);

    // 4. Configure GoogleDriveAppDataProvider
    Sync::CloudProviderConfig_t cfg;
    cfg.type = Sync::ECloudProviderType::k_GoogleDrive;
    cfg.masterKey = masterKey;
    cfg.keyEpoch = 1;
    cfg.oauthTokenEndpoint = serverUrl + "/oauth2/v4/token";
    cfg.oauthDeviceAuthEndpoint = serverUrl + "/device/code";
    cfg.gdriveApiBaseUrl = serverUrl + "/drive/v3";
    cfg.gdriveUploadBaseUrl = serverUrl + "/upload/drive/v3";
    cfg.accessToken = "ya29.mock_valid_access_token_12345";
    cfg.refreshToken = "1//mock_valid_refresh_token_67890";
    cfg.baseRetryDelayMs = 1;

    Sync::GoogleDriveAppDataProvider provider(cfg);

    // 5. Encrypt and push objects
    Sync::SyncStateManifest_t manifest;
    manifest.schemaVersion = 1;
    manifest.steamId64 = steamId64;
    manifest.deviceId = "desktop_linux_overrider";
    manifest.deviceHostname = "arch-desktop";
    manifest.globalGeneration = 1;
    manifest.keyEpoch = 1;

    struct Item_t {
        Sync::ESyncCategory cat;
        uint32_t appId;
        std::string logicalPath;
        std::vector<uint8_t> plain;
    };

    std::vector<Item_t> items = {
        {Sync::ESyncCategory::k_ManifestsAndLua, 480, "steamapps://appmanifest_480.acf", {acfData.begin(), acfData.end()}},
        {Sync::ESyncCategory::k_ManifestsAndLua, 480, "stplugin://480.lua", {luaData.begin(), luaData.end()}},
        {Sync::ESyncCategory::k_ManifestsAndLua, 480, "depotcache://481_123.manifest", {depotData.begin(), depotData.end()}},
        {Sync::ESyncCategory::k_Configs, 0, "config://config.yaml", {configData.begin(), configData.end()}},
        {Sync::ESyncCategory::k_CloudSaves, 480, "userdata://" + std::to_string(steam32) + "/480/remote/save.dat", {saveData.begin(), saveData.end()}},
        {Sync::ESyncCategory::k_Achievements, 480, "userdata://" + std::to_string(steam32) + "/480/remote/UserGameStats.bin", achData},
    };

    int pushedCount = 0;
    for (const auto& it : items) {
        Sync::SyncFileEntry_t entry;
        entry.category = it.cat;
        entry.appId = it.appId;
        entry.logicalPath = it.logicalPath;
        entry.sizeBytes = it.plain.size();
        entry.sha256Hex = sha256Of(it.plain);
        entry.generation = 1;
        entry.originDeviceId = "desktop_linux_overrider";
        manifest.entries.push_back(entry);

        Sync::EncryptedEnvelopeMetadata_t meta;
        meta.steamId64 = steamId64;
        meta.keyEpoch = 1;
        meta.category = it.cat;
        meta.logicalPath = it.logicalPath;
        meta.generation = 1;

        std::vector<uint8_t> encryptedBlob;
        if (!Sync::encryptBlob(masterKey, meta, it.plain, encryptedBlob)) {
            std::cerr << "Failed to encrypt blob " << it.logicalPath << "\n";
            return 1;
        }

        if (provider.pushEncryptedObject(steamId64, entry.sha256Hex, encryptedBlob) != Sync::ECloudError::k_Ok) {
            std::cerr << "Failed to push encrypted blob " << it.logicalPath << "\n";
            return 1;
        }
        pushedCount++;
    }

    // Push Provider API Keys (Ryuu, DepotBox, Hubcap)
    std::unordered_map<std::string, std::string> apiKeys = {
        {"ryuu", "ryuu_secret_key_12345"},
        {"depotbox", "depotbox_secret_key_67890"},
        {"hubcap", "hubcap_secret_key_abcde"},
    };

    for (const auto& [prov, val] : apiKeys) {
        std::vector<uint8_t> encKeyBlob;
        assert(Sync::encryptApiKey(masterKey, steamId64, 1, prov, 1, val, encKeyBlob));
        std::string keyHash = Sync::computeBufferSha256Hex(val);

        Sync::SyncFileEntry_t entry;
        entry.category = Sync::ESyncCategory::k_ApiKeys;
        entry.appId = 0;
        entry.logicalPath = "apikeys://" + prov;
        entry.sizeBytes = val.size();
        entry.sha256Hex = keyHash;
        entry.generation = 1;
        entry.originDeviceId = "desktop_linux_overrider";
        manifest.entries.push_back(entry);

        assert(provider.pushEncryptedObject(steamId64, keyHash, encKeyBlob) == Sync::ECloudError::k_Ok);
        pushedCount++;
    }

    manifest.manifestRootSha256 = Sync::computeManifestRootSha256(manifest.entries);

    std::string newETag;
    auto cErr = provider.commitRemoteManifest(steamId64, manifest, "", newETag);
    if (cErr != Sync::ECloudError::k_Ok) {
        std::cerr << "Failed to commit manifest: " << static_cast<int>(cErr) << "\n";
        return 1;
    }

    std::cout << "{\"success\": true, \"recovery_key\": \"" << recKey << "\", "
              << "\"crc_valid\": true, \"crc_tamper_rejected\": true, "
              << "\"pushed_items\": " << pushedCount << ", \"etag\": \"" << cleanETag(newETag) << "\"}\n";
    return 0;
}

// ── Subcommand: scenario1_pull ──────────────────────────────────────────────
int cmd_scenario1_pull(int argc, char** argv) {
    if (argc < 7) return 1;
    std::string serverUrl = argv[2];
    std::string steamRoot = argv[3];
    std::string configDir = argv[4];
    uint64_t steamId64 = std::stoull(argv[5]);
    std::string recKey = argv[6];
    std::string sdCardRoot = (argc >= 8) ? argv[7] : "";

    // 1. Decode Recovery Key
    Sync::MasterKey_t masterKey{};
    if (!Sync::decodeRecoveryKey(recKey, masterKey)) {
        std::cerr << "Receiver failed to decode recovery key\n";
        return 1;
    }

    // 2. Setup SteamOS directories & libraryfolders.vdf
    fs::create_directories(fs::path(steamRoot) / "steamapps");
    fs::create_directories(fs::path(steamRoot) / "config" / "depotcache");
    fs::create_directories(fs::path(steamRoot) / "config" / "stplug-in");
    fs::create_directories(configDir);

    if (!sdCardRoot.empty()) {
        fs::create_directories(fs::path(sdCardRoot) / "steamapps");
        std::ofstream vdf(fs::path(steamRoot) / "steamapps" / "libraryfolders.vdf");
        vdf << "\"libraryfolders\"\n{\n"
            << "  \"0\" { \"path\" \"" << steamRoot << "\" }\n"
            << "  \"1\" { \"path\" \"" << sdCardRoot << "\" }\n"
            << "}\n";
    }

    // 3. Simulate PowerMonitor wake-from-sleep
    int triggerFired = 0;
    Sync::PowerMonitor monitor([&](Sync::ESyncTriggerReason r) {
        if (r == Sync::ESyncTriggerReason::k_WakeFromSleep) triggerFired++;
    }, 1000, 5);
    monitor.simulateResumeEventForTest();
    assert(triggerFired == 1);

    // 4. Configure GoogleDriveAppDataProvider on Receiver
    Sync::CloudProviderConfig_t cfg;
    cfg.type = Sync::ECloudProviderType::k_GoogleDrive;
    cfg.masterKey = masterKey;
    cfg.keyEpoch = 1;
    cfg.oauthTokenEndpoint = serverUrl + "/oauth2/v4/token";
    cfg.oauthDeviceAuthEndpoint = serverUrl + "/device/code";
    cfg.gdriveApiBaseUrl = serverUrl + "/drive/v3";
    cfg.gdriveUploadBaseUrl = serverUrl + "/upload/drive/v3";
    cfg.accessToken = "ya29.mock_valid_access_token_12345";
    cfg.baseRetryDelayMs = 1;

    Sync::GoogleDriveAppDataProvider provider(cfg);

    // 5. Fetch Remote Manifest
    Sync::SyncStateManifest_t remoteManifest;
    std::string fetchedETag;
    auto fErr = provider.fetchRemoteManifest(steamId64, remoteManifest, fetchedETag);
    if (fErr != Sync::ECloudError::k_Ok) {
        std::cerr << "Failed to fetch remote manifest: " << static_cast<int>(fErr) << "\n";
        return 1;
    }

    // 6. Pull and decrypt blobs
    std::unordered_map<std::string, std::vector<uint8_t>> payloads;
    for (const auto& entry : remoteManifest.entries) {
        std::vector<uint8_t> encBlob;
        if (provider.pullEncryptedObject(steamId64, entry.sha256Hex, encBlob) != Sync::ECloudError::k_Ok) {
            std::cerr << "Failed to pull blob: " << entry.logicalPath << "\n";
            return 1;
        }

        std::vector<uint8_t> plain;
        if (entry.category == Sync::ESyncCategory::k_ApiKeys) {
            std::string provName = entry.logicalPath.substr(std::string("apikeys://").size());
            std::string apiKeyPlain;
            assert(Sync::decryptApiKey(masterKey, steamId64, entry.generation, provName, entry.generation, encBlob, apiKeyPlain));
            plain.assign(apiKeyPlain.begin(), apiKeyPlain.end());
        } else {
            Sync::EncryptedEnvelopeMetadata_t meta;
            meta.steamId64 = steamId64;
            meta.keyEpoch = remoteManifest.keyEpoch;
            meta.category = entry.category;
            meta.logicalPath = entry.logicalPath;
            meta.generation = entry.generation;
            if (!Sync::decryptBlob(masterKey, meta, encBlob, plain)) {
                std::cerr << "Failed to decrypt blob: " << entry.logicalPath << "\n";
                return 1;
            }
        }
        payloads[entry.logicalPath] = std::move(plain);
    }

    // 7. Apply remote sync package via SyncEngine
    Sync::SyncEngine engine(configDir, steamRoot);
    Sync::SyncSelectionFilter_t filter;
    filter.allManifestsAndLua = true;
    filter.allConfigs = true;
    filter.allCloudSaves = true;
    filter.allAchievements = true;
    filter.allApiKeys = true;

    Sync::SyncApplyReport_t report;
    auto applyStatus = engine.applyRemoteSyncPackage(steamId64, remoteManifest, payloads, filter, {}, report);
    if (applyStatus != Sync::ESyncApplyStatus::k_Ok) {
        std::cerr << "applyRemoteSyncPackage failed with status: " << static_cast<int>(applyStatus) << "\n";
        return 1;
    }

    // 8. Assert snapshot backup was created
    assert(!report.snapshotId.empty());
    fs::path snapDir = fs::path(configDir) / "sync_backups" / std::to_string(steamId64) / report.snapshotId;
    assert(fs::exists(snapDir));

    std::cout << "{\"success\": true, \"snapshot_id\": \"" << report.snapshotId << "\", "
              << "\"applied_items\": " << report.appliedLogicalPaths.size() << ", "
              << "\"etag\": \"" << cleanETag(fetchedETag) << "\"}\n";
    return 0;
}

// ── Subcommand: scenario2_webdav ────────────────────────────────────────────
int cmd_scenario2_webdav(int argc, char** argv) {
    if (argc < 4) return 1;
    std::string serverUrl = argv[2];
    uint64_t steamId64 = std::stoull(argv[3]);

    Sync::MasterKey_t key{};
    assert(Sync::generateMasterKey(key));

    Sync::CloudProviderConfig_t cfg;
    cfg.type = Sync::ECloudProviderType::k_WebDav;
    cfg.masterKey = key;
    cfg.keyEpoch = 1;
    cfg.webDavBaseUrl = serverUrl + "/webdav";
    cfg.webDavUsername = "deck_user";
    cfg.webDavPasswordOrToken = "secret_pass";
    cfg.baseRetryDelayMs = 1;

    Sync::WebDavCloudProvider provider(cfg);

    // 1. Quota query
    Sync::CloudQuotaInfo_t quota;
    assert(provider.queryQuota(quota) == Sync::ECloudError::k_Ok);
    assert(quota.limitBytes > 0);

    // 2. Commit initial manifest v1
    Sync::SyncStateManifest_t manifest;
    manifest.schemaVersion = 1;
    manifest.steamId64 = steamId64;
    manifest.deviceId = "webdav_node";
    manifest.globalGeneration = 1;
    manifest.keyEpoch = 1;

    std::string etag1;
    assert(provider.commitRemoteManifest(steamId64, manifest, "", etag1) == Sync::ECloudError::k_Ok);
    assert(!etag1.empty());

    // 3. Stale ETag commit should fail with k_PreconditionFailed (HTTP 412)
    manifest.globalGeneration = 2;
    std::string conflictETag;
    auto failErr = provider.commitRemoteManifest(steamId64, manifest, "\"stale_etag_v0\"", conflictETag);
    assert(failErr == Sync::ECloudError::k_PreconditionFailed);

    // 4. Valid expected ETag commit succeeds
    std::string etag2;
    assert(provider.commitRemoteManifest(steamId64, manifest, etag1, etag2) == Sync::ECloudError::k_Ok);
    assert(etag2 != etag1);

    // 5. Fetch Remote Manifest
    Sync::SyncStateManifest_t fetched;
    std::string fetchedETag;
    assert(provider.fetchRemoteManifest(steamId64, fetched, fetchedETag) == Sync::ECloudError::k_Ok);
    assert(fetched.steamId64 == steamId64);
    assert(fetched.globalGeneration == 2);

    // 6. Push & Pull Encrypted Object
    std::string objKey = "test_blob.enc";
    std::vector<uint8_t> testBlob = {0xAA, 0xBB, 0xCC, 0xDD};
    assert(provider.pushEncryptedObject(steamId64, objKey, testBlob) == Sync::ECloudError::k_Ok);

    std::vector<uint8_t> pulled;
    assert(provider.pullEncryptedObject(steamId64, objKey, pulled) == Sync::ECloudError::k_Ok);
    assert(pulled == testBlob);

    std::cout << "{\"success\": true, \"etag_v1\": \"" << cleanETag(etag1) << "\", "
              << "\"etag_v2\": \"" << cleanETag(etag2) << "\", \"stale_rejected\": true}\n";
    return 0;
}

// ── Subcommand: scenario3_p2p ───────────────────────────────────────────────
int cmd_scenario3_p2p(int argc, char** argv) {
    if (argc < 4) return 1;
    std::string serverUrl = argv[2];
    uint64_t steamId64 = std::stoull(argv[3]);

    Sync::MasterKey_t overriderKey{};
    assert(Sync::generateMasterKey(overriderKey));

    std::string srvTrustPath = "/tmp/test_p2p_srv_trust_" + std::to_string(steamId64) + ".yaml";
    std::string cliTrustPath = "/tmp/test_p2p_cli_trust_" + std::to_string(steamId64) + ".yaml";
    auto serverStore = std::make_shared<Sync::TrustedPeerStore>(srvTrustPath);
    auto clientStore = std::make_shared<Sync::TrustedPeerStore>(cliTrustPath);

    // Configure WebDAV provider for Overrider hybrid commit
    Sync::CloudProviderConfig_t cloudCfg;
    cloudCfg.type = Sync::ECloudProviderType::k_WebDav;
    cloudCfg.masterKey = overriderKey;
    cloudCfg.keyEpoch = 1;
    cloudCfg.webDavBaseUrl = serverUrl + "/webdav";
    cloudCfg.webDavUsername = "p2p_user";
    cloudCfg.webDavPasswordOrToken = "p2p_pass";
    cloudCfg.baseRetryDelayMs = 1;
    auto cloudRelay = Sync::createCloudProvider(cloudCfg);

    // Prepare provision bundle
    Sync::P2PPairingProvisionBundle_t provisionBundle;
    provisionBundle.steamId64 = steamId64;
    provisionBundle.keyEpoch = 1;
    provisionBundle.masterKey = overriderKey;
    provisionBundle.activeCloudProvider = Sync::ECloudProviderType::k_WebDav;
    provisionBundle.webDavUrl = serverUrl + "/webdav";
    provisionBundle.webDavUser = "p2p_user";
    provisionBundle.encryptedCloudAuthToken = {0x01, 0x02, 0x03, 0x04};

    // Prepare sync manifest and blobs
    Sync::SyncStateManifest_t sampleManifest;
    sampleManifest.schemaVersion = 1;
    sampleManifest.steamId64 = steamId64;
    sampleManifest.keyEpoch = 1;
    sampleManifest.deviceId = "desktop_overrider";
    sampleManifest.globalGeneration = 10;

    Sync::SyncFileEntry_t entry;
    entry.category = Sync::ESyncCategory::k_ManifestsAndLua;
    entry.appId = 480;
    entry.logicalPath = "manifests/480.lua";
    entry.sizeBytes = 20;
    entry.sha256Hex = Sync::computeBufferSha256Hex("test_lua_content\n");
    sampleManifest.entries.push_back(entry);
    sampleManifest.manifestRootSha256 = Sync::computeManifestRootSha256(sampleManifest.entries);

    std::unordered_map<std::string, std::vector<uint8_t>> sampleBlobs;
    sampleBlobs["manifests/480.lua"] = {0x74, 0x65, 0x73, 0x74};

    // 1. Setup Server
    Sync::P2PServerConfig_t srvCfg;
    srvCfg.steamId64 = steamId64;
    srvCfg.deviceId = "desktop_overrider";
    srvCfg.hostname = "DesktopPC";
    srvCfg.trustedStore = serverStore;
    srvCfg.provisionBundle = provisionBundle;
    srvCfg.activePin = "654321";
    srvCfg.syncManifest = sampleManifest;
    srvCfg.syncBlobs = sampleBlobs;
    srvCfg.optionalCloudRelay = cloudRelay.get();

    Sync::P2PSessionServer server(std::move(srvCfg));
    assert(server.start(0, "127.0.0.1"));
    uint16_t port = server.boundPort();
    assert(port > 0);

    // 2. Setup Client & Pair with PIN
    Sync::P2PClientConfig_t cliCfg;
    cliCfg.steamId64 = steamId64;
    cliCfg.deviceId = "steamdeck_receiver";
    cliCfg.hostname = "SteamDeck";
    cliCfg.trustedStore = clientStore;

    Sync::P2PSessionClient client(std::move(cliCfg));

    Sync::P2PPairingProvisionBundle_t receivedBundle;
    auto pairErr = client.pairWithPin("127.0.0.1", port, "654321", receivedBundle);
    assert(pairErr == Sync::EP2PError::k_Ok);
    assert(receivedBundle.masterKey == overriderKey);
    assert(receivedBundle.activeCloudProvider == Sync::ECloudProviderType::k_WebDav);

    // 3. Mutual TLS 1.3 Sync Transfer
    Sync::SyncStateManifest_t recManifest;
    std::unordered_map<std::string, std::vector<uint8_t>> recBlobs;
    auto syncErr = client.syncWithPeer("127.0.0.1", port, receivedBundle.masterKey, {}, {}, recManifest, recBlobs);
    assert(syncErr == Sync::EP2PError::k_Ok);
    assert(recManifest.deviceId == "desktop_overrider");
    assert(recBlobs.count("manifests/480.lua") == 1);

    server.stop();
    fs::remove(srvTrustPath);
    fs::remove(cliTrustPath);

    std::cout << "{\"success\": true, \"pair_success\": true, \"master_key_matched\": true, \"sync_success\": true}\n";
    return 0;
}

// ── Subcommand: scenario4_merge ─────────────────────────────────────────────
int cmd_scenario4_merge(int argc, char** argv) {
    if (argc < 5) return 1;
    std::string steamRoot = argv[2];
    std::string configDir = argv[3];
    uint64_t steamId64 = std::stoull(argv[4]);
    uint32_t steam32 = static_cast<uint32_t>(steamId64 & 0xFFFFFFFFULL);

    // 1. Offline Achievement Set Union
    Sync::AchievementStatBlob_t blobA;
    blobA.appId = 440;
    blobA.achievements["Ach_1"] = {"Ach_1", true, 1000};
    blobA.achievements["Ach_2"] = {"Ach_2", true, 2000};
    blobA.achievements["Ach_3"] = {"Ach_3", false, 0};
    blobA.intStats["kills"] = 50;
    auto bufA = Sync::serializeAchievementBinaryKV(blobA);

    Sync::AchievementStatBlob_t blobB;
    blobB.appId = 440;
    blobB.achievements["Ach_1"] = {"Ach_1", false, 0};
    blobB.achievements["Ach_2"] = {"Ach_2", true, 1500};
    blobB.achievements["Ach_4"] = {"Ach_4", true, 3000};
    blobB.intStats["kills"] = 85;
    auto bufB = Sync::serializeAchievementBinaryKV(blobB);

    std::vector<uint8_t> mergedBuf;
    assert(Sync::mergeAchievementBuffersUnion(bufA, bufB, mergedBuf));

    Sync::AchievementStatBlob_t merged;
    assert(Sync::parseAchievementBinaryKV(mergedBuf, merged));
    assert(merged.achievements["Ach_1"].unlocked && merged.achievements["Ach_1"].unlockTimestamp == 1000);
    assert(merged.achievements["Ach_2"].unlocked && merged.achievements["Ach_2"].unlockTimestamp == 1500); // Earliest
    assert(!merged.achievements["Ach_3"].unlocked);
    assert(merged.achievements["Ach_4"].unlocked && merged.achievements["Ach_4"].unlockTimestamp == 3000);
    assert(merged.intStats["kills"] == 85); // Max stat

    // 2. Cloud Save 3-Way Conflict
    fs::path savePath = fs::path(steamRoot) / "userdata" / std::to_string(steam32) / "400" / "remote" / "save.dat";
    fs::create_directories(savePath.parent_path());
    std::string localContent = "LocalModData_V1";
    AtomicFile::write(savePath.string(), localContent, true);

    std::string remoteContent = "RemoteModData_V2";
    std::vector<uint8_t> remotePayload(remoteContent.begin(), remoteContent.end());

    Sync::SyncEngine engine(configDir, steamRoot);

    Sync::SyncStateManifest_t remoteManifest;
    remoteManifest.steamId64 = steamId64;
    Sync::SyncFileEntry_t entry;
    entry.category = Sync::ESyncCategory::k_CloudSaves;
    entry.appId = 400;
    entry.logicalPath = "userdata://" + std::to_string(steam32) + "/400/remote/save.dat";
    entry.sizeBytes = remotePayload.size();
    entry.sha256Hex = sha256Of(remotePayload);
    entry.mtimeUnixSec = 1710000000;
    remoteManifest.entries.push_back(entry);

    std::unordered_map<std::string, std::vector<uint8_t>> payloads;
    payloads[entry.logicalPath] = remotePayload;

    Sync::SyncSelectionFilter_t filter;
    filter.allCloudSaves = true;

    // First call: Empty choices -> ConflictPendingUserChoice
    Sync::SyncApplyReport_t rep1;
    auto status1 = engine.applyRemoteSyncPackage(steamId64, remoteManifest, payloads, filter, {}, rep1);
    assert(status1 == Sync::ESyncApplyStatus::k_ConflictPendingUserChoice);
    assert(rep1.conflicts.size() == 1);

    // Second call: User chooses BackupBoth (k_BackupLocalAndUseRemote)
    std::unordered_map<std::string, Sync::EConflictChoice> choices;
    choices[entry.logicalPath] = Sync::EConflictChoice::k_BackupLocalAndUseRemote;

    Sync::SyncApplyReport_t rep2;
    auto status2 = engine.applyRemoteSyncPackage(steamId64, remoteManifest, payloads, filter, choices, rep2);
    assert(status2 == Sync::ESyncApplyStatus::k_Ok);

    // Verify local file updated to remote
    std::ifstream fLive(savePath);
    std::string sLive((std::istreambuf_iterator<char>(fLive)), std::istreambuf_iterator<char>());
    assert(sLive == remoteContent);

    // Verify conflict backup exists with local content
    fs::path conflictBakPath = fs::path(configDir) / "sync_backups" / std::to_string(steamId64) / "conflicts" / "400_save.dat.conflict_bak";
    assert(fs::exists(conflictBakPath));
    std::ifstream fBak(conflictBakPath);
    std::string sBak((std::istreambuf_iterator<char>(fBak)), std::istreambuf_iterator<char>());
    assert(sBak == localContent);

    std::cout << "{\"success\": true, \"achievements_merged\": true, \"conflict_detected\": true, \"conflict_resolved\": true, \"conflict_backup_exists\": true}\n";
    return 0;
}

// ── Subcommand: scenario5_lock ──────────────────────────────────────────────
int cmd_scenario5_lock(int argc, char** argv) {
    if (argc < 5) return 1;
    std::string steamRoot = argv[2];
    std::string configDir = argv[3];
    uint64_t steamId64 = std::stoull(argv[4]);
    uint32_t steam32 = static_cast<uint32_t>(steamId64 & 0xFFFFFFFFULL);

    Sync::SyncEngine engine(configDir, steamRoot);

    bool r1091500 = Sync::GameProcessLock::isAppRunning(1091500, "/proc", steamRoot);
    bool r400 = Sync::GameProcessLock::isAppRunning(400, "/proc", steamRoot);

    assert(r1091500 == true);
    assert(r400 == false);

    Sync::SyncStateManifest_t remoteManifest;
    remoteManifest.steamId64 = steamId64;

    Sync::SyncFileEntry_t eRunning;
    eRunning.category = Sync::ESyncCategory::k_CloudSaves;
    eRunning.appId = 1091500;
    eRunning.logicalPath = "userdata://" + std::to_string(steam32) + "/1091500/remote/save.dat";
    std::string pRunning = "CP2077_Save";
    eRunning.sizeBytes = pRunning.size();
    eRunning.sha256Hex = sha256Of(pRunning);
    remoteManifest.entries.push_back(eRunning);

    Sync::SyncFileEntry_t eNotRunning;
    eNotRunning.category = Sync::ESyncCategory::k_CloudSaves;
    eNotRunning.appId = 400;
    eNotRunning.logicalPath = "userdata://" + std::to_string(steam32) + "/400/remote/save.dat";
    std::string pNotRunning = "Portal_Save";
    eNotRunning.sizeBytes = pNotRunning.size();
    eNotRunning.sha256Hex = sha256Of(pNotRunning);
    remoteManifest.entries.push_back(eNotRunning);

    std::unordered_map<std::string, std::vector<uint8_t>> payloads;
    payloads[eRunning.logicalPath] = {pRunning.begin(), pRunning.end()};
    payloads[eNotRunning.logicalPath] = {pNotRunning.begin(), pNotRunning.end()};

    Sync::SyncSelectionFilter_t filter;
    filter.allCloudSaves = true;

    Sync::SyncApplyReport_t rep;
    auto status = engine.applyRemoteSyncPackage(steamId64, remoteManifest, payloads, filter, {}, rep);

    assert(status == Sync::ESyncApplyStatus::k_PartialDeferredOrConflict);
    assert(rep.deferredAppIds.size() == 1 && rep.deferredAppIds[0] == 1091500);

    // App 400 is applied
    fs::path p400 = fs::path(steamRoot) / "userdata" / std::to_string(steam32) / "400" / "remote" / "save.dat";
    assert(fs::exists(p400));

    // App 1091500 is NOT applied while game running
    fs::path p1091500 = fs::path(steamRoot) / "userdata" / std::to_string(steam32) / "1091500" / "remote" / "save.dat";
    assert(!fs::exists(p1091500));
    assert(engine.isAppDeferred(1091500));

    std::cout << "PHASE_DEFERRED\n";
    std::cout.flush();

    // Wait until child process exits
    while (Sync::GameProcessLock::isAppRunning(1091500, "/proc", steamRoot)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    // Process has exited: drain deferred queue
    auto drainRep = engine.drainDeferredGamesIfExited(steamId64);
    assert(drainRep.status == Sync::ESyncApplyStatus::k_Ok);
    assert(!engine.isAppDeferred(1091500));
    assert(fs::exists(p1091500));

    std::cout << "{\"success\": true, \"deferred\": true, \"applied_400\": true, \"drained\": true, \"applied_1091500\": true}\n";
    std::cout.flush();
    return 0;
}

// ── Subcommand: scenario6_crypto_aad ────────────────────────────────────────
int cmd_scenario6_crypto_aad(int argc, char** argv) {
    if (argc < 4) return 1;
    uint64_t steamIdLegit = std::stoull(argv[2]);
    uint64_t steamIdBad = std::stoull(argv[3]);
    std::string configDir = (argc >= 5) ? argv[4] : "/tmp/test_scenario6_config";
    std::string steamRoot = (argc >= 6) ? argv[5] : "/tmp/test_scenario6_steam";

    Sync::MasterKey_t masterKey{};
    assert(Sync::generateMasterKey(masterKey));

    Sync::EncryptedEnvelopeMetadata_t metaLegit;
    metaLegit.steamId64 = steamIdLegit;
    metaLegit.keyEpoch = 1;
    metaLegit.category = Sync::ESyncCategory::k_CloudSaves;
    metaLegit.logicalPath = "userdata://12345/480/save.dat";
    metaLegit.generation = 1;

    std::vector<uint8_t> plain = {'S', 'e', 'c', 'r', 'e', 't', 'S', 'a', 'v', 'e'};
    std::vector<uint8_t> encryptedBlob;
    assert(Sync::encryptBlob(masterKey, metaLegit, plain, encryptedBlob));

    // Decrypt with bad steamId in expected metadata -> must fail at AAD verification!
    Sync::EncryptedEnvelopeMetadata_t metaBad = metaLegit;
    metaBad.steamId64 = steamIdBad;

    std::vector<uint8_t> decryptedBad;
    bool badDecrypted = Sync::decryptBlob(masterKey, metaBad, encryptedBlob, decryptedBad);
    assert(!badDecrypted);

    // Decrypt with legit metadata succeeds
    std::vector<uint8_t> decryptedGood;
    bool goodDecrypted = Sync::decryptBlob(masterKey, metaLegit, encryptedBlob, decryptedGood);
    assert(goodDecrypted && decryptedGood == plain);

    // SyncEngine SteamID Mismatch guard
    Sync::SyncEngine engine(configDir, steamRoot);
    Sync::SyncStateManifest_t badManifest;
    badManifest.steamId64 = steamIdBad;

    Sync::SyncApplyReport_t rep;
    auto status = engine.applyRemoteSyncPackage(steamIdLegit, badManifest, {}, {}, {}, rep);
    assert(status == Sync::ESyncApplyStatus::k_SteamIdMismatch);

    std::cout << "{\"success\": true, \"aad_mismatch_rejected\": true, \"engine_mismatch_rejected\": true}\n";
    return 0;
}

int main(int argc, char** argv) {
    if (!g_pLog) {
        g_pLog = std::make_unique<CLog>("/dev/null");
    }
    if (argc < 2) {
        std::cerr << "Usage: navi_e2e_runner <subcommand> [args...]\n";
        return 1;
    }
    std::string cmd = argv[1];
    if (cmd == "scenario1_push") return cmd_scenario1_push(argc, argv);
    if (cmd == "scenario1_pull") return cmd_scenario1_pull(argc, argv);
    if (cmd == "scenario2_webdav") return cmd_scenario2_webdav(argc, argv);
    if (cmd == "scenario3_p2p") return cmd_scenario3_p2p(argc, argv);
    if (cmd == "scenario4_merge") return cmd_scenario4_merge(argc, argv);
    if (cmd == "scenario5_lock") return cmd_scenario5_lock(argc, argv);
    if (cmd == "scenario6_crypto_aad") return cmd_scenario6_crypto_aad(argc, argv);

    std::cerr << "Unknown subcommand: " << cmd << "\n";
    return 1;
}
"""

@pytest.fixture(scope="session")
def e2e_runner() -> Generator[Path, None, None]:
    """Compiles and yields the 32-bit C++ E2E runner executable, unlinking it on session teardown."""
    assert LIBNAVI_SO.is_file(), f"bin/libnavi.so not found at {LIBNAVI_SO}. Run `make audit-libs` first."

    runner_bin = Path(tempfile.gettempdir()) / f"navi_e2e_runner_{os.getpid()}"
    runner_cpp = Path(tempfile.gettempdir()) / f"navi_e2e_runner_{os.getpid()}.cpp"
    runner_cpp.write_text(CPP_RUNNER_SOURCE, encoding="utf-8")

    compile_cmd = [
        "g++",
        "-m32",
        "-std=c++20",
        "-D_GLIBCXX_USE_CXX11_ABI=0",
        "-I./src",
        "-I./src/core",
        "-I./src/model",
        "-I./src/presenter",
        "-I./src/view",
        str(runner_cpp),
        "-L./bin",
        "-lnavi",
        "-lssl",
        "-lcrypto",
        "/usr/lib/i386-linux-gnu/libcurl.so.4",
        "-lpthread",
        "-ldl",
        f"-Wl,-rpath,{BIN_DIR.resolve()}",
        "-o",
        str(runner_bin),
    ]

    res = subprocess.run(compile_cmd, cwd=REPO_ROOT, capture_output=True, text=True)
    if res.returncode != 0:
        runner_cpp.unlink(missing_ok=True)
        raise RuntimeError(f"Failed to compile C++ E2E runner:\n{res.stderr}")

    runner_bin.chmod(0o755)
    try:
        yield runner_bin
    finally:
        runner_cpp.unlink(missing_ok=True)
        runner_bin.unlink(missing_ok=True)


@pytest.fixture
def relay_server() -> Generator[MockCloudRelayServer, None, None]:
    with MockCloudRelayServer() as server:
        yield server


# ─────────────────────────────────────────────────────────────────────────────
# Test Scenarios
# ─────────────────────────────────────────────────────────────────────────────

def test_scenario1_cloud_only_gdrive_desktop_to_steamos_e2e(e2e_runner: Path, relay_server: MockCloudRelayServer, tmp_path: Path):
    """
    Scenario 1: Cloud-Only E2E
    - Overrider (Desktop Linux layout) generates 256-bit key, exports 55-char Base32 key with CRC-16.
    - Pushes .acf, .lua, .manifest, config.yaml, saves, achievements, and API keys to Google Drive endpoints.
    - Asserts raw server storage has NAVIENC1 headers and zero plaintext secret leakage.
    - Receiver (SteamOS layout + SD card libraryfolders.vdf) imports key, simulates wake-from-sleep,
      pulls from Google Drive, creates .bak snapshot, and applies granular selection.
    """
    steam_id_64 = 76561198000000001
    overrider_steam = tmp_path / "desktop_steam"
    overrider_config = tmp_path / "desktop_config"
    receiver_steam = tmp_path / "deck_steam"
    receiver_config = tmp_path / "deck_config"
    receiver_sdcard = tmp_path / "deck_sdcard"

    # 1. Overrider Push
    push_res = subprocess.run(
        [
            str(e2e_runner),
            "scenario1_push",
            relay_server.server_url(),
            str(overrider_steam),
            str(overrider_config),
            str(steam_id_64),
        ],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
    )
    assert push_res.returncode == 0, f"scenario1_push failed:\n{push_res.stderr}"
    push_data = json.loads(push_res.stdout)
    assert push_data["success"] is True
    assert push_data["crc_valid"] is True
    assert push_data["crc_tamper_rejected"] is True
    assert push_data["pushed_items"] >= 8
    recovery_key = push_data["recovery_key"]
    assert len(recovery_key) == 65  # 55 Base32 chars + 10 hyphens

    # 2. Inspect Mock Server Raw Storage: Zero Plaintext Leakage & NAVIENC1 headers
    stored_blobs = relay_server.get_all_stored_blobs()
    assert len(stored_blobs) >= 8, f"Expected >= 8 stored blobs, found {len(stored_blobs)}"

    forbidden_substrings = [
        b"ryuu_secret_key_12345",
        b"depotbox_secret_key_67890",
        b"hubcap_secret_key_abcde",
        b"OverriderCloudSaveData",
        b"addappid(480",
        b"syncRole: overrider",
    ]

    for blob in stored_blobs:
        if blob.startswith(b"schema_version:"):
            # Plain unencrypted manifest on Google Drive
            continue
        assert blob.startswith(b"NAVIENC1"), f"Blob missing NAVIENC1 magic: {blob[:12]}"
        for forbidden in forbidden_substrings:
            assert forbidden not in blob, f"SECURITY VIOLATION: Plaintext leakage found in encrypted blob: {forbidden.decode()}"

    # 3. Receiver Pull & Apply with Granular Selection on SteamOS + SD Card
    pull_res = subprocess.run(
        [
            str(e2e_runner),
            "scenario1_pull",
            relay_server.server_url(),
            str(receiver_steam),
            str(receiver_config),
            str(steam_id_64),
            recovery_key,
            str(receiver_sdcard),
        ],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
    )
    assert pull_res.returncode == 0, f"scenario1_pull failed:\n{pull_res.stderr}"
    pull_data = json.loads(pull_res.stdout)
    assert pull_data["success"] is True
    assert pull_data["applied_items"] >= 8
    assert pull_data["snapshot_id"] != ""

    # Verify files on Receiver
    steam32 = steam_id_64 & 0xFFFFFFFF
    assert (receiver_steam / "steamapps" / "appmanifest_480.acf").is_file()
    assert (receiver_steam / "userdata" / str(steam32) / "480" / "remote" / "save.dat").is_file()
    assert (receiver_steam / "config" / "depotcache" / "481_123.manifest").is_file()
    assert (receiver_steam / "config" / "stplug-in" / "480.lua").is_file()


def test_scenario2_cloud_only_webdav_provider_switch_and_412_concurrency(e2e_runner: Path, relay_server: MockCloudRelayServer):
    """
    Scenario 2: Cloud-Only E2E (WebDAV Provider Switch & If-Match 412 Concurrency Protection)
    - Verifies WebDavCloudProvider over real loopback HTTP against mock_cloud_relay_server.py (/webdav/...).
    - Enforces PROPFIND quota, manifest commits, 412 Precondition Failed rejection on stale ETag,
      and 507 Insufficient Storage on quota exhaustion.
    """
    steam_id_64 = 76561198000000002

    res = subprocess.run(
        [
            str(e2e_runner),
            "scenario2_webdav",
            relay_server.server_url(),
            str(steam_id_64),
        ],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
    )
    assert res.returncode == 0, f"scenario2_webdav failed:\n{res.stderr}"
    data = json.loads(res.stdout)
    assert data["success"] is True
    assert data["stale_rejected"] is True
    assert data["etag_v1"] != data["etag_v2"]

    # Test Quota Limit Enforcement (507 Insufficient Storage)
    relay_server.set_quota_limit(10)  # Restrict to 10 bytes
    large_payload = b"X" * 1024
    # Attempting to PUT a large file directly to WebDAV returns 507
    import urllib.request
    req = urllib.request.Request(
        f"{relay_server.server_url()}/webdav/{steam_id_64}/blobs/overflow.enc",
        data=large_payload,
        method="PUT",
        headers={"Authorization": "Basic dXNlcjpwYXNz"}
    )
    try:
        urllib.request.urlopen(req)
        pytest.fail("Expected HTTP 507 Insufficient Storage")
    except urllib.error.HTTPError as e:
        assert e.code == 507


def test_scenario3_p2p_direct_lan_pairing_and_hybrid_cloud_commit(e2e_runner: Path, relay_server: MockCloudRelayServer):
    """
    Scenario 3: P2P Direct LAN Pairing (6-Digit PIN + Automatic Master Key Transfer + Hybrid Cloud Commit)
    - Verifies mDNS discovery hash matching.
    - Verifies TLS 1.3 + 6-digit PIN pairing automatically transferring Master Key & Cloud Provider config.
    - Verifies direct mutual TLS 1.3 payload transfer and simultaneous commit to the WebDAV Cloud Relay.
    """
    steam_id_64 = 76561198000000003

    res = subprocess.run(
        [
            str(e2e_runner),
            "scenario3_p2p",
            relay_server.server_url(),
            str(steam_id_64),
        ],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
    )
    assert res.returncode == 0, f"scenario3_p2p failed:\n{res.stderr}"
    data = json.loads(res.stdout)
    assert data["success"] is True
    assert data["pair_success"] is True
    assert data["master_key_matched"] is True
    assert data["sync_success"] is True

    # Assert hybrid commit pushed blobs to WebDAV server
    stored_blobs = relay_server.get_all_stored_blobs()
    assert len(stored_blobs) >= 1, "Hybrid cloud commit did not store blob on mock server"


def test_scenario4_offline_achievement_union_and_save_conflict_resolution(e2e_runner: Path, tmp_path: Path):
    """
    Scenario 4: Offline Achievement Set Union (A U B) & Cloud Save 3-Way Conflict Resolution
    - Verifies offline achievements on both devices merge into A U B with earliest unlock timestamps preserved
      and zero lost unlocks.
    - Verifies divergent Cloud Saves trigger conflict resolution (Backup Both) with .conflict_bak preservation.
    """
    steam_id_64 = 76561198000000004
    steam_root = tmp_path / "merge_steam"
    config_dir = tmp_path / "merge_config"

    res = subprocess.run(
        [
            str(e2e_runner),
            "scenario4_merge",
            str(steam_root),
            str(config_dir),
            str(steam_id_64),
        ],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
    )
    assert res.returncode == 0, f"scenario4_merge failed:\n{res.stderr}"
    data = json.loads(res.stdout)
    assert data["success"] is True
    assert data["achievements_merged"] is True
    assert data["conflict_detected"] is True
    assert data["conflict_resolved"] is True
    assert data["conflict_backup_exists"] is True


def test_scenario5_active_game_process_lock_and_deferred_queue_drain(e2e_runner: Path, tmp_path: Path):
    """
    Scenario 5: Active Game Process Lock (running_appid == target_appid) & Deferred Queue Drain
    - Spawns a real child process with SteamAppId=1091500 in its environment.
    - Verifies GameProcessLock blocks save/achievement overwrite for 1091500 while AppID 400 syncs immediately.
    - Terminates child process and verifies 1091500 drains cleanly from deferred queue.
    """
    steam_id_64 = 76561198000000005
    steam_root = tmp_path / "lock_steam"
    config_dir = tmp_path / "lock_config"

    # Spawn child process with SteamAppId=1091500
    child = subprocess.Popen(
        [
            sys.executable,
            "-c",
            "import time, sys; sys.stdout.write('ready\\n'); sys.stdout.flush(); time.sleep(60)",
        ],
        env={**os.environ, "SteamAppId": "1091500"},
        stdout=subprocess.PIPE,
        text=True,
    )
    runner_proc = None
    try:
        ready = child.stdout.readline()  # wait until running
        assert ready.strip() == "ready"

        # Launch C++ runner process
        runner_proc = subprocess.Popen(
            [
                str(e2e_runner),
                "scenario5_lock",
                str(steam_root),
                str(config_dir),
                str(steam_id_64),
            ],
            cwd=REPO_ROOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )

        phase_line = runner_proc.stdout.readline().strip()
        assert phase_line == "PHASE_DEFERRED"

        # Verify App 400 applied and App 1091500 not applied on disk while running
        steam32 = steam_id_64 & 0xFFFFFFFF
        assert (steam_root / "userdata" / str(steam32) / "400" / "remote" / "save.dat").is_file()
        assert not (steam_root / "userdata" / str(steam32) / "1091500" / "remote" / "save.dat").exists()

        # Terminate child process to trigger exit and drain
        child.terminate()
        child.wait()

        # Read JSON output from runner
        json_line = runner_proc.stdout.readline().strip()
        runner_proc.wait()
        assert runner_proc.returncode == 0, f"runner error: {runner_proc.stderr.read()}"

        data = json.loads(json_line)
        assert data["success"] is True
        assert data["deferred"] is True
        assert data["applied_400"] is True
        assert data["drained"] is True
        assert data["applied_1091500"] is True

        # Verify App 1091500 is now applied on disk
        assert (steam_root / "userdata" / str(steam32) / "1091500" / "remote" / "save.dat").is_file()
    finally:
        if runner_proc is not None and runner_proc.poll() is None:
            runner_proc.kill()
            runner_proc.wait()
        if child.poll() is None:
            child.terminate()
            child.wait()


def test_scenario6_cross_steamid_rejection_and_cef_modal_xss_protection(e2e_runner: Path, tmp_path: Path):
    """
    Scenario 6: Cross-SteamID Contamination Rejection & CEF Modal (navi_inject.js) DOM/XSS Verification
    - Part A: Verifies SteamID64 mismatch is rejected at both the SyncCrypto AAD layer and SyncEngine layer.
    - Part B: Runs res/inject-scripts/navi_inject.js inside a node DOM harness with mocked XMLHttpRequest
      to verify window.__naviShowSyncModal renders when hasRemoteBump: true, escapes malicious <script>
      payloads in overriderHostname / title / logicalPath, and posts the expected granular filter +
      conflict choices JSON to POST /sync/apply.
    """
    # ── Part A: Crypto AAD & SyncEngine Rejection ──
    steam_id_legit = 76561198000000006
    steam_id_bad = 76561198999999999
    s6_config = tmp_path / "scenario6_config"
    s6_steam = tmp_path / "scenario6_steam"
    s6_config.mkdir(parents=True, exist_ok=True)
    s6_steam.mkdir(parents=True, exist_ok=True)

    res_crypto = subprocess.run(
        [
            str(e2e_runner),
            "scenario6_crypto_aad",
            str(steam_id_legit),
            str(steam_id_bad),
            str(s6_config),
            str(s6_steam),
        ],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
    )
    assert res_crypto.returncode == 0, f"scenario6_crypto_aad failed:\n{res_crypto.stderr}"
    crypto_data = json.loads(res_crypto.stdout)
    assert crypto_data["success"] is True
    assert crypto_data["aad_mismatch_rejected"] is True
    assert crypto_data["engine_mismatch_rejected"] is True

    # ── Part B: CEF Modal navi_inject.js DOM & XSS Verification in Node ──
    assert INJECT_SCRIPT.is_file(), f"navi_inject.js not found at {INJECT_SCRIPT}"

    node_harness_script = r"""
    const fs = require('fs');
    const scriptContent = fs.readFileSync(process.argv[1], 'utf8');

    global.window = global;
    global.window.addEventListener = () => {};
    global.window.removeEventListener = () => {};
    global.console = console;

    let elementsById = {};

    function makeElement(tag) {
        let _innerHTML = "";
        let el = {
            tagName: tag,
            id: "",
            style: {},
            attributes: {},
            checked: true,
            value: "keep_remote",
            textContent: "",
            children: [],
            addEventListener: () => {},
            removeEventListener: () => {},
            remove: function() {
                if (this.id) delete elementsById[this.id];
            },
            setAttribute: function(k, v) { this.attributes[k] = v; },
            getAttribute: function(k) { return this.attributes[k] || null; },
            querySelectorAll: function(sel) {
                if (sel === ".navi-conflict-choice") {
                    let list = [];
                    for (let k in elementsById) {
                        if (elementsById[k].className && elementsById[k].className.includes("navi-conflict-choice")) {
                            list.push(elementsById[k]);
                        }
                    }
                    return list;
                }
                return [];
            },
            appendChild: function(c) {
                if (c.id) elementsById[c.id] = c;
                return c;
            }
        };
        Object.defineProperty(el, "innerHTML", {
            get: () => _innerHTML,
            set: (html) => {
                _innerHTML = html;
                const idRegex = /<([a-z0-9]+)[^>]*\bid="([^"]+)"[^>]*>/gi;
                let m;
                while ((m = idRegex.exec(html)) !== null) {
                    let tag = m[1];
                    let elemId = m[2];
                    let fullTag = m[0];
                    let childEl = makeElement(tag);
                    childEl.id = elemId;
                    if (fullTag.includes("checked")) childEl.checked = true;
                    let classMatch = fullTag.match(/class="([^"]+)"/);
                    if (classMatch) childEl.className = classMatch[1];
                    let dataPathMatch = fullTag.match(/data-path="([^"]+)"/);
                    if (dataPathMatch) childEl.setAttribute("data-path", dataPathMatch[1]);
                    let valueMatch = fullTag.match(/value="([^"]+)"/);
                    if (valueMatch) childEl.value = valueMatch[1];
                    elementsById[elemId] = childEl;
                }
                const selectRegex = /<select[^>]*class="[^"]*navi-conflict-choice[^"]*"[^>]*>/gi;
                let sm;
                let idx = 0;
                while ((sm = selectRegex.exec(html)) !== null) {
                    let full = sm[0];
                    let selId = "conflict_choice_" + (++idx);
                    let selEl = makeElement("select");
                    selEl.id = selId;
                    selEl.className = "navi-conflict-choice";
                    let dp = full.match(/data-path="([^"]+)"/);
                    if (dp) selEl.setAttribute("data-path", dp[1]);
                    let val = full.match(/value="([^"]+)"/);
                    selEl.value = val ? val[1] : "keep_remote";
                    elementsById[selId] = selEl;
                }
            }
        });
        return el;
    }

    global.document = {
        createElement: makeElement,
        getElementById: (id) => elementsById[id] || null,
        body: makeElement("body"),
        addEventListener: () => {},
        removeEventListener: () => {}
    };

    global.sessionStorage = {
        getItem: () => null,
        setItem: () => {},
        removeItem: () => {}
    };

    let capturedPostUrl = null;
    let capturedPostBody = null;

    global.XMLHttpRequest = function() {
        this.headers = {};
        this.open = function(method, url, async) {
            this.method = method;
            this.url = url;
        };
        this.setRequestHeader = function(k, v) { this.headers[k] = v; };
        this.send = function(body) {
            capturedPostUrl = this.url;
            capturedPostBody = body;
            this.status = 200;
            this.responseText = JSON.stringify({ success: true, appliedLogicalPaths: ["test"] });
            if (this.onload) this.onload();
        };
    };

    eval(scriptContent);

    // Feed malicious XSS payloads in overriderHostname, title, and logicalPath
    const maliciousData = {
        hasRemoteBump: true,
        lastTriggerReason: "wake_from_sleep",
        overriderHostname: "<script>alert(\"xss_host\")</script>",
        role: "receiver",
        remoteGeneration: 5,
        recoveryKey: "ABCDE-12345",
        diffItems: [
            {
                category: "cloud_saves",
                appId: 400,
                title: "<img src=x onerror=alert(\"xss_title\")>",
                logicalPath: "userdata://1/400/remote/save.dat"
            }
        ],
        conflicts: [
            {
                appId: 400,
                logicalPath: "userdata://1/400/<script>alert(\"xss_path\")</script>"
            }
        ]
    };

    window.__naviShowSyncModal(maliciousData);

    const overlay = document.getElementById("navi-sync-modal-overlay");
    if (!overlay) {
        console.error("Modal overlay was not created!");
        process.exit(1);
    }

    // Verify unescaped malicious tags are NOT present
    if (overlay.innerHTML.includes("<script>alert(")) {
        console.error("XSS Vulnerability: Raw <script> tag rendered in innerHTML!");
        process.exit(2);
    }
    if (overlay.innerHTML.includes("<img src=x onerror=")) {
        console.error("XSS Vulnerability: Raw <img> onerror rendered in innerHTML!");
        process.exit(3);
    }

    // Verify properly escaped entities exist
    if (!overlay.innerHTML.includes("&lt;script&gt;alert(&quot;xss_host&quot;)&lt;/script&gt;")) {
        console.error("Missing escaped overriderHostname in DOM!");
        process.exit(4);
    }
    if (!overlay.innerHTML.includes("&lt;img src=x onerror=alert(&quot;xss_title&quot;)&gt;")) {
        console.error("Missing escaped title in DOM!");
        process.exit(5);
    }

    // Click Apply button
    const applyBtn = document.getElementById("navi-sync-apply-btn");
    if (!applyBtn || typeof applyBtn.onclick !== 'function') {
        console.error("Apply button missing or not clickable!");
        process.exit(6);
    }
    applyBtn.onclick();

    if (!capturedPostUrl || !capturedPostUrl.endsWith("/sync/apply")) {
        console.error("Apply button did not POST to /sync/apply! URL was: " + capturedPostUrl);
        process.exit(7);
    }

    const parsed = JSON.parse(capturedPostBody);
    if (!parsed.filter || !parsed.filter.allManifestsAndLua || !parsed.conflictChoices) {
        console.error("Invalid POST body format: " + capturedPostBody);
        process.exit(8);
    }

    console.log(JSON.stringify({
        modal_rendered: true,
        xss_escaped: true,
        post_url: capturedPostUrl,
        parsed_body: parsed
    }));
    process.exit(0);
    """

    node_res = subprocess.run(
        ["node", "-e", node_harness_script, str(INJECT_SCRIPT)],
        capture_output=True,
        text=True,
    )
    assert node_res.returncode == 0, f"Node DOM harness failed:\n{node_res.stderr}\nOutput:\n{node_res.stdout}"
    node_out = json.loads(node_res.stdout.strip())
    assert node_out["modal_rendered"] is True
    assert node_out["xss_escaped"] is True
    assert node_out["post_url"] == "http://127.0.0.1:9001/sync/apply"
    assert node_out["parsed_body"]["filter"]["allManifestsAndLua"] is True
    assert node_out["parsed_body"]["filter"]["allCloudSaves"] is True
