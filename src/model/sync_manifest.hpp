#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Sync
{
    enum class ESyncRole : uint8_t
    {
        k_Disabled = 0,
        k_Overrider = 1,
        k_Receiver = 2,
        k_BidirectionalPeer = 3
    };

    enum class ECloudProviderType : uint8_t
    {
        k_None = 0,
        k_GoogleDrive = 1,
        k_WebDav = 2
    };

    enum class ESyncTopology : uint8_t
    {
        k_CloudOnly = 1,
        k_P2PAndCloud = 2
    };

    enum class ESyncCategory : uint8_t
    {
        k_ManifestsAndLua = 1 << 0,
        k_Configs         = 1 << 1,
        k_CloudSaves      = 1 << 2,
        k_Achievements    = 1 << 3,
        k_ApiKeys         = 1 << 4
    };

    // String conversion helpers
    std::string syncRoleToString(ESyncRole role);
    ESyncRole parseSyncRole(std::string_view str);

    std::string cloudProviderToString(ECloudProviderType provider);
    ECloudProviderType parseCloudProvider(std::string_view str);

    std::string syncTopologyToString(ESyncTopology topology);
    ESyncTopology parseSyncTopology(std::string_view str);

    std::string syncCategoryToString(ESyncCategory cat);
    ESyncCategory parseSyncCategory(std::string_view str);

    struct SyncSelectionFilter_t
    {
        bool allManifestsAndLua = false;
        std::unordered_set<uint32_t> manifestAppIds;

        bool allConfigs = false;
        std::unordered_set<std::string> configKeys;

        bool allCloudSaves = false;
        std::unordered_set<uint32_t> cloudSaveAppIds;

        bool allAchievements = false;
        std::unordered_set<uint32_t> achievementAppIds;

        bool allApiKeys = false;
        std::unordered_set<std::string> providerNames;

        bool isAppSelected(ESyncCategory cat, uint32_t appId) const;
        bool isConfigSelected(const std::string& key) const;
        bool isProviderSelected(const std::string& provider) const;

        bool operator==(const SyncSelectionFilter_t&) const = default;
    };

    struct SyncFileEntry_t
    {
        ESyncCategory category = ESyncCategory::k_ManifestsAndLua;
        uint32_t appId = 0;
        std::string logicalPath;
        std::string sha256Hex;
        uint64_t sizeBytes = 0;
        int64_t mtimeUnixSec = 0;
        uint64_t generation = 0;
        std::string originDeviceId;

        bool operator==(const SyncFileEntry_t&) const = default;
    };

    struct SyncStateManifest_t
    {
        uint32_t schemaVersion = 1;
        uint64_t steamId64 = 0;
        uint32_t keyEpoch = 1;
        std::string deviceId;
        std::string deviceHostname;
        uint64_t globalGeneration = 0;
        int64_t generatedAtUnixSec = 0;
        std::unordered_map<std::string, uint64_t> vectorClock;
        std::vector<SyncFileEntry_t> entries;
        std::string manifestRootSha256;

        bool operator==(const SyncStateManifest_t&) const = default;
    };

    std::string computeBufferSha256Hex(std::string_view data);
    std::string computeManifestRootSha256(const std::vector<SyncFileEntry_t>& entries);
    std::string serializeManifestToYaml(const SyncStateManifest_t& manifest);
    bool deserializeManifestFromYaml(const std::string& yamlStr, SyncStateManifest_t& outManifest);
    void bumpManifestGeneration(SyncStateManifest_t& manifest, const std::string& localDeviceId);
} // namespace Sync
