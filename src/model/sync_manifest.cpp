#include "sync_manifest.hpp"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <openssl/evp.h>
#include <yaml-cpp/yaml.h>

namespace Sync
{
    // ── String conversions ────────────────────────────────────────────────────

    std::string syncRoleToString(ESyncRole role)
    {
        switch (role)
        {
            case ESyncRole::k_Disabled:          return "disabled";
            case ESyncRole::k_Overrider:         return "overrider";
            case ESyncRole::k_Receiver:          return "receiver";
            case ESyncRole::k_BidirectionalPeer: return "bidirectional_peer";
        }
        return "disabled";
    }

    ESyncRole parseSyncRole(std::string_view str)
    {
        if (str == "overrider" || str == "Overrider")
            return ESyncRole::k_Overrider;
        if (str == "receiver" || str == "Receiver")
            return ESyncRole::k_Receiver;
        if (str == "bidirectional_peer" || str == "bidirectional" || str == "peer" ||
            str == "BidirectionalPeer" || str == "Bidirectional")
            return ESyncRole::k_BidirectionalPeer;
        return ESyncRole::k_Disabled;
    }

    std::string cloudProviderToString(ECloudProviderType provider)
    {
        switch (provider)
        {
            case ECloudProviderType::k_None:        return "none";
            case ECloudProviderType::k_GoogleDrive: return "googledrive";
            case ECloudProviderType::k_WebDav:      return "webdav";
        }
        return "none";
    }

    ECloudProviderType parseCloudProvider(std::string_view str)
    {
        if (str == "googledrive" || str == "google_drive" || str == "gdrive" ||
            str == "GoogleDrive" || str == "Google_Drive")
            return ECloudProviderType::k_GoogleDrive;
        if (str == "webdav" || str == "WebDav" || str == "WEBDAV")
            return ECloudProviderType::k_WebDav;
        return ECloudProviderType::k_None;
    }

    std::string syncTopologyToString(ESyncTopology topology)
    {
        switch (topology)
        {
            case ESyncTopology::k_CloudOnly:   return "cloud";
            case ESyncTopology::k_P2PAndCloud: return "p2p_and_cloud";
        }
        return "cloud";
    }

    ESyncTopology parseSyncTopology(std::string_view str)
    {
        if (str == "p2p_and_cloud" || str == "p2p" || str == "p2pandcloud" ||
            str == "P2PAndCloud" || str == "P2P")
            return ESyncTopology::k_P2PAndCloud;
        return ESyncTopology::k_CloudOnly;
    }

    std::string syncCategoryToString(ESyncCategory cat)
    {
        switch (cat)
        {
            case ESyncCategory::k_ManifestsAndLua: return "manifests_and_lua";
            case ESyncCategory::k_Configs:         return "configs";
            case ESyncCategory::k_CloudSaves:      return "cloud_saves";
            case ESyncCategory::k_Achievements:    return "achievements";
            case ESyncCategory::k_ApiKeys:         return "api_keys";
        }
        return "unknown";
    }

    ESyncCategory parseSyncCategory(std::string_view str)
    {
        if (str == "manifests_and_lua" || str == "manifests" || str == "manifestsandlua" ||
            str == "ManifestsAndLua" || str == "Manifests")
            return ESyncCategory::k_ManifestsAndLua;
        if (str == "configs" || str == "config" || str == "Configs" || str == "Config")
            return ESyncCategory::k_Configs;
        if (str == "cloud_saves" || str == "cloudsaves" || str == "saves" ||
            str == "CloudSaves" || str == "Saves")
            return ESyncCategory::k_CloudSaves;
        if (str == "achievements" || str == "achievement" ||
            str == "Achievements" || str == "Achievement")
            return ESyncCategory::k_Achievements;
        if (str == "api_keys" || str == "apikeys" || str == "keys" ||
            str == "ApiKeys" || str == "Keys")
            return ESyncCategory::k_ApiKeys;
        return ESyncCategory::k_ManifestsAndLua;
    }

    // ── SyncSelectionFilter_t methods ─────────────────────────────────────────

    bool SyncSelectionFilter_t::isAppSelected(ESyncCategory cat, uint32_t appId) const
    {
        switch (cat)
        {
            case ESyncCategory::k_ManifestsAndLua:
                return allManifestsAndLua || manifestAppIds.contains(appId);
            case ESyncCategory::k_CloudSaves:
                return allCloudSaves || cloudSaveAppIds.contains(appId);
            case ESyncCategory::k_Achievements:
                return allAchievements || achievementAppIds.contains(appId);
            default:
                return false;
        }
    }

    bool SyncSelectionFilter_t::isConfigSelected(const std::string& key) const
    {
        return allConfigs || configKeys.contains(key);
    }

    bool SyncSelectionFilter_t::isProviderSelected(const std::string& provider) const
    {
        return allApiKeys || providerNames.contains(provider);
    }

    // ── Hashing & Canonical Manifest Root ─────────────────────────────────────

    std::string computeBufferSha256Hex(std::string_view data)
    {
        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        if (!ctx) return "";

        if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1 ||
            EVP_DigestUpdate(ctx, data.data(), data.size()) != 1)
        {
            EVP_MD_CTX_free(ctx);
            return "";
        }

        unsigned char hash[EVP_MAX_MD_SIZE];
        unsigned int len = 0;
        if (EVP_DigestFinal_ex(ctx, hash, &len) != 1)
        {
            EVP_MD_CTX_free(ctx);
            return "";
        }
        EVP_MD_CTX_free(ctx);

        std::ostringstream oss;
        for (unsigned int i = 0; i < len; ++i)
        {
            oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
        }
        return oss.str();
    }

    std::string computeManifestRootSha256(const std::vector<SyncFileEntry_t>& entries)
    {
        std::vector<SyncFileEntry_t> sortedEntries = entries;
        std::sort(sortedEntries.begin(), sortedEntries.end(),
            [](const SyncFileEntry_t& a, const SyncFileEntry_t& b)
            {
                if (a.logicalPath != b.logicalPath)
                    return a.logicalPath < b.logicalPath;
                if (static_cast<uint8_t>(a.category) != static_cast<uint8_t>(b.category))
                    return static_cast<uint8_t>(a.category) < static_cast<uint8_t>(b.category);
                if (a.appId != b.appId)
                    return a.appId < b.appId;
                if (a.sha256Hex != b.sha256Hex)
                    return a.sha256Hex < b.sha256Hex;
                if (a.generation != b.generation)
                    return a.generation < b.generation;
                return a.originDeviceId < b.originDeviceId;
            });

        std::ostringstream oss;
        for (const auto& e : sortedEntries)
        {
            oss << syncCategoryToString(e.category) << '|'
                << e.appId << '|'
                << e.logicalPath << '|'
                << e.sha256Hex << '|'
                << e.sizeBytes << '|'
                << e.mtimeUnixSec << '|'
                << e.generation << '|'
                << e.originDeviceId << '\n';
        }

        return computeBufferSha256Hex(oss.str());
    }

    // ── YAML Serialization & Deserialization ──────────────────────────────────

    std::string serializeManifestToYaml(const SyncStateManifest_t& manifest)
    {
        YAML::Emitter out;
        out << YAML::BeginMap;
        out << YAML::Key << "schema_version" << YAML::Value << manifest.schemaVersion;
        out << YAML::Key << "steam_id_64" << YAML::Value << std::to_string(manifest.steamId64);
        out << YAML::Key << "key_epoch" << YAML::Value << manifest.keyEpoch;
        out << YAML::Key << "device_id" << YAML::Value << manifest.deviceId;
        out << YAML::Key << "device_hostname" << YAML::Value << manifest.deviceHostname;
        out << YAML::Key << "global_generation" << YAML::Value << manifest.globalGeneration;
        out << YAML::Key << "generated_at_unix_sec" << YAML::Value << manifest.generatedAtUnixSec;

        out << YAML::Key << "vector_clock" << YAML::Value << YAML::BeginMap;
        for (const auto& [devId, gen] : manifest.vectorClock)
        {
            out << YAML::Key << devId << YAML::Value << gen;
        }
        out << YAML::EndMap;

        out << YAML::Key << "manifest_root_sha256" << YAML::Value << manifest.manifestRootSha256;

        out << YAML::Key << "entries" << YAML::Value << YAML::BeginSeq;
        for (const auto& e : manifest.entries)
        {
            out << YAML::BeginMap;
            out << YAML::Key << "category" << YAML::Value << syncCategoryToString(e.category);
            out << YAML::Key << "app_id" << YAML::Value << e.appId;
            out << YAML::Key << "logical_path" << YAML::Value << e.logicalPath;
            out << YAML::Key << "sha256_hex" << YAML::Value << e.sha256Hex;
            out << YAML::Key << "size_bytes" << YAML::Value << e.sizeBytes;
            out << YAML::Key << "mtime_unix_sec" << YAML::Value << e.mtimeUnixSec;
            out << YAML::Key << "generation" << YAML::Value << e.generation;
            out << YAML::Key << "origin_device_id" << YAML::Value << e.originDeviceId;
            out << YAML::EndMap;
        }
        out << YAML::EndSeq;

        out << YAML::EndMap;
        return std::string(out.c_str());
    }

    static YAML::Node findNode(const YAML::Node& node, const char* k1, const char* k2 = nullptr, const char* k3 = nullptr)
    {
        if (node[k1]) return node[k1];
        if (k2 && node[k2]) return node[k2];
        if (k3 && node[k3]) return node[k3];
        return YAML::Node();
    }

    static bool isUnsignedDigits(const std::string& s)
    {
        if (s.empty()) return false;
        for (unsigned char c : s)
        {
            if (!std::isdigit(c)) return false;
        }
        return true;
    }

    static uint32_t parseScalarU32(const YAML::Node& n, uint32_t fallback = 0)
    {
        if (!n || !n.IsScalar()) return fallback;
        const std::string& s = n.Scalar();
        if (s.empty()) return fallback;
        char* endPtr = nullptr;
        unsigned long val = std::strtoul(s.c_str(), &endPtr, 10);
        if (endPtr == s.c_str()) return fallback;
        return static_cast<uint32_t>(val);
    }

    static uint64_t parseScalarU64(const YAML::Node& n, uint64_t fallback = 0)
    {
        if (!n || !n.IsScalar()) return fallback;
        const std::string& s = n.Scalar();
        if (s.empty()) return fallback;
        char* endPtr = nullptr;
        unsigned long long val = std::strtoull(s.c_str(), &endPtr, 10);
        if (endPtr == s.c_str()) return fallback;
        return static_cast<uint64_t>(val);
    }

    static int64_t parseScalarI64(const YAML::Node& n, int64_t fallback = 0)
    {
        if (!n || !n.IsScalar()) return fallback;
        const std::string& s = n.Scalar();
        if (s.empty()) return fallback;
        char* endPtr = nullptr;
        long long val = std::strtoll(s.c_str(), &endPtr, 10);
        if (endPtr == s.c_str()) return fallback;
        return static_cast<int64_t>(val);
    }

    static std::string parseScalarStr(const YAML::Node& n)
    {
        if (!n || !n.IsScalar()) return {};
        return n.Scalar();
    }

    bool deserializeManifestFromYaml(const std::string& yamlStr, SyncStateManifest_t& outManifest)
    {
        try
        {
            YAML::Node root = YAML::Load(yamlStr);
            if (!root.IsMap()) return false;

            SyncStateManifest_t m;

            if (auto n = findNode(root, "schema_version", "schemaVersion", "SchemaVersion"))
                m.schemaVersion = parseScalarU32(n, m.schemaVersion);

            if (auto n = findNode(root, "steam_id_64", "steamId64", "SteamId64"))
                m.steamId64 = parseScalarU64(n, m.steamId64);

            if (auto n = findNode(root, "key_epoch", "keyEpoch", "KeyEpoch"))
                m.keyEpoch = parseScalarU32(n, m.keyEpoch);

            if (auto n = findNode(root, "device_id", "deviceId", "DeviceId"))
                m.deviceId = parseScalarStr(n);

            if (auto n = findNode(root, "device_hostname", "deviceHostname", "DeviceHostname"))
                m.deviceHostname = parseScalarStr(n);

            if (auto n = findNode(root, "global_generation", "globalGeneration", "GlobalGeneration"))
                m.globalGeneration = parseScalarU64(n, m.globalGeneration);

            if (auto n = findNode(root, "generated_at_unix_sec", "generatedAtUnixSec", "GeneratedAtUnixSec"))
                m.generatedAtUnixSec = parseScalarI64(n, m.generatedAtUnixSec);

            if (auto n = findNode(root, "manifest_root_sha256", "manifestRootSha256", "ManifestRootSha256"))
                m.manifestRootSha256 = parseScalarStr(n);

            if (auto n = findNode(root, "vector_clock", "vectorClock", "VectorClock"))
            {
                if (n.IsMap())
                {
                    for (auto it = n.begin(); it != n.end(); ++it)
                    {
                        m.vectorClock[parseScalarStr(it->first)] = parseScalarU64(it->second, 0);
                    }
                }
            }

            if (auto n = findNode(root, "entries", "Entries"))
            {
                if (n.IsSequence())
                {
                    for (const auto& item : n)
                    {
                        if (!item.IsMap()) continue;
                        SyncFileEntry_t e;

                        if (auto catNode = findNode(item, "category", "Category"))
                        {
                            if (catNode.IsScalar())
                            {
                                const std::string& catScalar = catNode.Scalar();
                                if (isUnsignedDigits(catScalar))
                                {
                                    e.category = static_cast<ESyncCategory>(parseScalarU32(catNode, 0));
                                }
                                else
                                {
                                    e.category = parseSyncCategory(catScalar);
                                }
                            }
                        }

                        if (auto fn = findNode(item, "app_id", "appId", "AppId"))
                            e.appId = parseScalarU32(fn, 0);
                        if (auto fn = findNode(item, "logical_path", "logicalPath", "LogicalPath"))
                            e.logicalPath = parseScalarStr(fn);
                        if (auto fn = findNode(item, "sha256_hex", "sha256Hex", "Sha256Hex"))
                            e.sha256Hex = parseScalarStr(fn);
                        if (auto fn = findNode(item, "size_bytes", "sizeBytes", "SizeBytes"))
                            e.sizeBytes = parseScalarU64(fn, 0);
                        if (auto fn = findNode(item, "mtime_unix_sec", "mtimeUnixSec", "MtimeUnixSec"))
                            e.mtimeUnixSec = parseScalarI64(fn, 0);
                        if (auto fn = findNode(item, "generation", "Generation"))
                            e.generation = parseScalarU64(fn, 0);
                        if (auto fn = findNode(item, "origin_device_id", "originDeviceId", "OriginDeviceId"))
                            e.originDeviceId = parseScalarStr(fn);

                        m.entries.push_back(std::move(e));
                    }
                }
            }

            outManifest = std::move(m);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    void bumpManifestGeneration(SyncStateManifest_t& manifest, const std::string& localDeviceId)
    {
        manifest.globalGeneration++;
        manifest.vectorClock[localDeviceId] = manifest.globalGeneration;
        manifest.generatedAtUnixSec = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        manifest.manifestRootSha256 = computeManifestRootSha256(manifest.entries);
    }
} // namespace Sync
