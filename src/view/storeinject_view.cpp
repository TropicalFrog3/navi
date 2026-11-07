#include "storeinject_view.hpp"
#include "atomic_file.hpp"
#include "log.hpp"
#include "depotkeys.hpp"
#include "config.hpp"
#include "cloudredirect.hpp"
#include <yaml-cpp/yaml.h>

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <sys/stat.h>
#include <filesystem>
#include <vector>
#include <map>
#include <set>
#include <unordered_set>





namespace StoreInjectView
{
    std::filesystem::path findSteamUIIndexHtml()
    {
        const char* home = getenv("HOME");
        if (!home) return {};

        std::filesystem::path candidates[] = {
            std::filesystem::path(home) / ".local" / "share" / "Steam" / "steamui" / "index.html",
            std::filesystem::path(home) / ".steam" / "steam" / "steamui" / "index.html",
            std::filesystem::path(home) / ".var" / "app" / "com.valvesoftware.Steam" / "data" / "Steam" / "steamui" / "index.html",
        };

        for (auto& path : candidates)
        {
            if (std::filesystem::exists(path)) return path;
        }
        return {};
    }
static std::string escapeJson(const std::string& s);
    void removeStoreInjectScript()
    {
        auto indexPath = findSteamUIIndexHtml();
        if (indexPath.empty()) return;

        std::ifstream inFile(indexPath);
        if (!inFile.is_open()) return;

        std::stringstream buffer;
        buffer << inFile.rdbuf();
        std::string content = buffer.str();
        inFile.close();

        auto beginPos = content.find(INJECT_MARKER_BEGIN);
        if (beginPos == std::string::npos) return;

        auto endPos = content.find(INJECT_MARKER_END);
        if (endPos == std::string::npos) return;

        auto removeEnd = endPos + strlen(INJECT_MARKER_END);
        if (removeEnd < content.size() && content[removeEnd] == '\n')
            removeEnd++;

        content.erase(beginPos, removeEnd - beginPos);

        if (AtomicFile::write(indexPath.string(), content))
        {
            LOG_INFO("StoreInject: Removed injected script from index.html\n");
        }
    }

    std::string renderSuccessJson(const std::string& message)
    {
        if (message.empty())
        {
            return "{\"success\":true}";
        }

        std::string escaped;
        for (char c : message)
        {
            if (c == '"' || c == '\\') escaped += '\\';
            escaped += c;
        }
        return "{\"success\":true,\"message\":\"" + escaped + "\"}";
    }

    std::string renderErrorJson(const std::string& message)
    {
        std::string escaped;
        for (char c : message)
        {
            if (c == '"' || c == '\\') escaped += '\\';
            escaped += c;
        }
        return "{\"success\":false,\"message\":\"" + escaped + "\"}";
    }

    std::string renderLuaAppsJson(const std::set<uint32_t>& appIds)
    {
        std::string json = "{\"appids\":[";
        bool first = true;
        for (uint32_t aid : appIds)
        {
            if (!first) json += ",";
            first = false;
            json += std::to_string(aid);
        }
        json += "]}";
        return json;
    }

    std::string renderApiKeysJson(const std::string& ryuuKey, const std::string& depotBoxKey, const std::string& hubcapKey)
    {
        auto escapeJson = [](const std::string& str) {
            std::string escaped;
            escaped.reserve(str.size());
            for (char c : str)
            {
                if (c == '"' || c == '\\') escaped += '\\';
                escaped += c;
            }
            return escaped;
        };

        return "{\"success\":true,\"ryuu\":\"" + escapeJson(ryuuKey) +
               "\",\"dpbx\":\"" + escapeJson(depotBoxKey) +
               "\",\"hubcap\":\"" + escapeJson(hubcapKey) + "\"}";
    }



std::string renderCheckJson(const CheckViewModel& info) {
    std::string escapedProvider;
    for (char c : info.provider) {
        if (c == '"' || c == '\\') escapedProvider += '\\';
        escapedProvider += c;
    }
    std::string escapedAutoCrackState = escapeJson(info.autoCrackState);
    std::string json = "{\"exists\":" + std::string(info.exists ? "true" : "false") +
                       ",\"hasLua\":" + std::string(info.hasLua ? "true" : "false") +
                       ",\"gameExists\":" + std::string(info.gameExists ? "true" : "false") +
                       ",\"isUnlocked\":" + std::string(info.isUnlocked ? "true" : "false") +
                       ",\"pending\":" + std::string(info.pending ? "true" : "false") +
                       ",\"downloading\":" + std::string(info.downloading ? "true" : "false") +
                       ",\"paused\":" + std::string(info.paused ? "true" : "false") +
                       ",\"onlineFixInstalled\":" + std::string(info.onlineFixInstalled ? "true" : "false") +
                       ",\"autoCrackInstalled\":" + std::string(info.autoCrackInstalled ? "true" : "false") +
                       ",\"autoCrackState\":\"" + escapedAutoCrackState + "\"" +
                       ",\"provider\":\"" + escapedProvider + "\"}";
    return json;
}




    std::string renderCheckJson(bool exists, bool hasLua, bool gameExists, bool isUnlocked,
                                bool pending, bool downloading, bool paused,
                                bool onlineFixInstalled, bool autoCrackInstalled,
                                const std::string& provider,
                                const std::string& autoCrackState)
    {
        CheckViewModel model;
        model.exists = exists;
        model.hasLua = hasLua;
        model.gameExists = gameExists;
        model.isUnlocked = isUnlocked;
        model.pending = pending;
        model.downloading = downloading;
        model.paused = paused;
        model.onlineFixInstalled = onlineFixInstalled;
        model.autoCrackInstalled = autoCrackInstalled;
        model.autoCrackState = autoCrackState;
        model.provider = provider;
        return renderCheckJson(model);
    }

    std::string renderDepotsJson(const std::vector<DepotEntryViewModel>& depots,
                                 bool locallyOwned,
                                 size_t validKeysCount)
    {
        if (!locallyOwned && validKeysCount == 0)
        {
            return "{\"success\":false,\"message\":\"No AES decryption keys available for this app. Cannot download.\"}";
        }

        std::string depotsJson = "[";
        bool first = true;
        for (const auto& d : depots)
        {
            if (!first) depotsJson += ",";
            depotsJson += "{\"id\":\"" + d.id + "\",\"name\":\"" + d.name + "\",\"size\":\"" + d.size + "\",\"os\":\"" + d.os + "\",\"hasKey\":" + (d.hasKey ? "true" : "false") + "}";
            first = false;
        }
        depotsJson += "]";

        return "{\"success\":true,\"depots\":" + depotsJson + "}";
    }

    std::string renderDepotsJson(const std::map<std::string, LuaParser::DepotInfo>& allDepots,
                                 size_t validKeysCount,
                                 bool locallyOwned)
    {
        if (!locallyOwned && validKeysCount == 0)
        {
            return "{\"success\":false,\"message\":\"No AES decryption keys available for this app. Cannot download.\"}";
        }

        std::string depotsJson = "[";
        bool first = true;
        for (const auto& [depotId, info] : allDepots)
        {
            uint32_t dId = 0;
            try { dId = std::stoul(depotId); } catch (...) {}
            std::string key = info.key;
            if (key.empty() && dId > 0)
            {
                key = DepotKeys::getDepotKey(dId);
            }
            bool hasKey = (key.size() == 64);

            std::string desc = info.description.empty() ? ("Depot " + depotId) : info.description;
            std::string sz = info.sizeBytes;
            std::string depotOs = "";

            if (!first) depotsJson += ",";
            depotsJson += "{\"id\":\"" + depotId + "\",\"name\":\"" + desc + "\",\"size\":\"" + sz + "\",\"os\":\"" + depotOs + "\",\"hasKey\":" + (hasKey ? "true" : "false") + "}";
            first = false;
        }
        depotsJson += "]";

        return "{\"success\":true,\"depots\":" + depotsJson + "}";
    }

    static std::string escapeJson(const std::string& s)
    {
        std::string out;
        out.reserve(s.size() + 8);
        for (char c : s)
        {
            switch (c)
            {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\b': out += "\\b"; break;
                case '\f': out += "\\f"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20)
                    {
                        char buf[8];
                        snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                        out += buf;
                    }
                    else
                    {
                        out += c;
                    }
                    break;
            }
        }
        return out;
    }

    std::string renderFullConfigJson(const std::string& configYamlPath)
    {
        YAML::Node node;
        bool fileLoaded = false;
        try
        {
            if (std::filesystem::exists(configYamlPath))
            {
                node = YAML::LoadFile(configYamlPath);
                fileLoaded = true;
            }
        }
        catch (...)
        {
            fileLoaded = false;
        }

        auto getBool = [&](const char* key, bool fallback) -> bool {
            if (fileLoaded && node[key] && !node[key].IsNull())
            {
                try
                {
                    if (node[key].IsScalar())
                    {
                        const std::string s = node[key].Scalar();
                        if (s == "1" || s == "true" || s == "True" || s == "TRUE" ||
                            s == "yes" || s == "Yes" || s == "YES" ||
                            s == "on" || s == "On" || s == "ON") return true;
                        if (s == "0" || s == "false" || s == "False" || s == "FALSE" ||
                            s == "no" || s == "No" || s == "NO" ||
                            s == "off" || s == "Off" || s == "OFF") return false;
                    }
                    return node[key].as<bool>();
                }
                catch (...) {}
            }
            return fallback;
        };

        auto getString = [&](const char* key, const std::string& fallback) -> std::string {
            if (fileLoaded && node[key] && !node[key].IsNull())
            {
                try { return node[key].as<std::string>(); } catch (...) {}
            }
            return fallback;
        };

        auto getUint32 = [&](const char* key, uint32_t fallback) -> uint32_t {
            if (fileLoaded && node[key] && !node[key].IsNull())
            {
                try { return node[key].as<uint32_t>(); } catch (...) {}
            }
            return fallback;
        };

        auto getInt32 = [&](const char* key, int32_t fallback) -> int32_t {
            if (fileLoaded && node[key] && !node[key].IsNull())
            {
                try { return node[key].as<int32_t>(); } catch (...) {}
            }
            return fallback;
        };

        auto getUint32Array = [&](const char* key, const std::unordered_set<AppId_t>& fallback) -> std::vector<uint32_t> {
            std::vector<uint32_t> vec;
            if (fileLoaded && node[key] && node[key].IsSequence())
            {
                for (const auto& item : node[key])
                {
                    try { vec.push_back(item.as<uint32_t>()); } catch (...) {}
                }
            }
            else
            {
                vec.assign(fallback.begin(), fallback.end());
            }
            std::sort(vec.begin(), vec.end());
            return vec;
        };

        auto getMapKv = [&](const char* key, const auto& fallbackMap) -> std::map<std::string, std::string> {
            std::map<std::string, std::string> out;
            if (fileLoaded && node[key] && node[key].IsMap())
            {
                for (const auto& kv : node[key])
                {
                    try
                    {
                        std::string k = kv.first.as<std::string>();
                        std::string v = kv.second.as<std::string>();
                        out[k] = v;
                    }
                    catch (...) {}
                }
            }
            else
            {
                for (const auto& [k, v] : fallbackMap)
                {
                    if constexpr (std::is_same_v<std::decay_t<decltype(v)>, std::string>)
                    {
                        out[std::to_string(k)] = v;
                    }
                    else
                    {
                        out[std::to_string(k)] = std::to_string(v);
                    }
                }
            }
            return out;
        };

        // Booleans
        bool disableFamilyShareLock = getBool("DisableFamilyShareLock", g_config.disableFamilyLock.get());
        bool useWhitelist = getBool("UseWhitelist", g_config.useWhiteList.get());
        bool safeMode = getBool("SafeMode", g_config.safeMode.get());
        bool warnHashMissmatch = getBool("WarnHashMissmatch", g_config.warnHashMissmatch.get());
        bool notifyInit = getBool("NotifyInit", g_config.notifyInit.get());
        bool api = getBool("API", g_config.api.get());
        bool disableCloud = getBool("DisableCloud", g_config.disableCloud.get());
        bool disableUpdates = getBool("DisableUpdates", g_config.disableUpdates.get());
        bool dumpClientInterfaces = getBool("DumpClientInterfaces", g_config.dumpInterfaceMaps.get());
        bool extendedLogging = getBool("ExtendedLogging", g_config.extendedLogging.get());

        // Numbers
        uint32_t maxSchemaTries = getUint32("MaxSchemaTries", g_config.maxSchemaTries.get());
        int32_t fakeWalletBalance = getInt32("FakeWalletBalance", g_config.fakeWalletBalance.get());
        uint32_t logLevels = getUint32("LogLevels", g_config.logLevels.get());

        // Strings
        std::string fakeName = getString("FakeName", g_config.fakeName.get());
        std::string fakeEmail = getString("FakeEmail", g_config.fakeEmail.get());
        std::string ryuuKey = getString("RyuuKey", g_config.ryuuKey.get());
        std::string depotBoxKey = getString("DepotBoxKey", g_config.depotBoxKey.get());
        std::string hubcapKey = getString("HubcapKey", g_config.hubcapKey.get());

        // Nested status structs
        uint32_t idleAppId = 0;
        std::string idleTitle = "";
        if (fileLoaded && node["IdleStatus"] && node["IdleStatus"].IsMap())
        {
            try { idleAppId = node["IdleStatus"]["AppId"].as<uint32_t>(); } catch (...) {}
            try { idleTitle = node["IdleStatus"]["Title"].as<std::string>(); } catch (...) {}
        }
        else
        {
            idleAppId = g_config.idleStatus.get().appId;
            idleTitle = g_config.idleStatus.get().title;
        }

        uint32_t unownedAppId = 0;
        std::string unownedTitle = "";
        if (fileLoaded && node["UnownedStatus"] && node["UnownedStatus"].IsMap())
        {
            try { unownedAppId = node["UnownedStatus"]["AppId"].as<uint32_t>(); } catch (...) {}
            try { unownedTitle = node["UnownedStatus"]["Title"].as<std::string>(); } catch (...) {}
        }
        else
        {
            unownedAppId = g_config.unownedStatus.get().appId;
            unownedTitle = g_config.unownedStatus.get().title;
        }

        // Arrays of uint32
        std::vector<uint32_t> appIds = getUint32Array("AppIds", g_config.appIds.get());
        std::vector<uint32_t> additionalApps = getUint32Array("AdditionalApps", g_config.addedAppIds.get());
        std::vector<uint32_t> fakeOffline = getUint32Array("FakeOffline", g_config.fakeOffline.get());
        std::vector<uint32_t> depotBlacklist = getUint32Array("DepotBlacklist", g_config.depotBlacklist.get());

        // Key-Value Maps
        std::map<std::string, std::string> fakeAppIds = getMapKv("FakeAppIds", g_config.fakeAppIds.get());
        std::map<std::string, std::string> manifestIds = getMapKv("ManifestIds", g_config.manifestIds.get());
        std::map<std::string, std::string> appTokens = getMapKv("AppTokens", g_config.appTokens.get());
        std::map<std::string, std::string> cdKeys = getMapKv("CDKeys", g_config.cdKeys.get());
        std::map<std::string, std::string> gameTitles = getMapKv("GameTitles", g_config.gameTitles.get());
        std::map<std::string, std::string> subscriptionTimestamps = getMapKv("SubscriptionTimestamps", g_config.subscriptionTimestamps.get());
        std::map<std::string, std::string> steamIdOverride = getMapKv("SteamIdOverride", g_config.steamIdOverride.get());

        // Complex Nested Maps
        std::string dlcDataYaml = "";
        if (fileLoaded && node["DlcData"] && !node["DlcData"].IsNull())
        {
            YAML::Emitter emitter;
            emitter << node["DlcData"];
            dlcDataYaml = emitter.c_str();
        }
        else
        {
            const auto& dlcMap = g_config.dlcData.get();
            if (!dlcMap.empty())
            {
                YAML::Node dn(YAML::NodeType::Map);
                for (const auto& [parentId, data] : dlcMap)
                {
                    YAML::Node sub(YAML::NodeType::Map);
                    for (const auto& [dlcId, dlcName] : data.dlcIds)
                    {
                        sub[std::to_string(dlcId)] = dlcName;
                    }
                    dn[std::to_string(parentId)] = sub;
                }
                YAML::Emitter emitter;
                emitter << dn;
                dlcDataYaml = emitter.c_str();
            }
        }

        std::string denuvoGamesYaml = "";
        if (fileLoaded && node["DenuvoGames"] && !node["DenuvoGames"].IsNull())
        {
            YAML::Emitter emitter;
            emitter << node["DenuvoGames"];
            denuvoGamesYaml = emitter.c_str();
        }
        else
        {
            const auto& denuvoMap = g_config.denuvoGames.get();
            if (!denuvoMap.empty())
            {
                YAML::Node dn(YAML::NodeType::Map);
                for (const auto& [steamId, apps] : denuvoMap)
                {
                    YAML::Node seq(YAML::NodeType::Sequence);
                    for (auto a : apps) seq.push_back(a);
                    dn[std::to_string(steamId)] = seq;
                }
                YAML::Emitter emitter;
                emitter << dn;
                denuvoGamesYaml = emitter.c_str();
            }
        }

        // CloudSaves
        bool csEnabled = g_config.cloudSavesEnabled.get();
        std::string csProvider = g_config.cloudSavesProvider.get();
        bool csAutoBackup = g_config.cloudAutoBackupBeforeReset.get();
        uint32_t csRetention = g_config.cloudBackupRetentionCount.get();
        std::vector<uint32_t> csManagedApps;
        {
            auto s = g_config.cloudSavesManagedApps.get();
            csManagedApps.assign(s.begin(), s.end());
            std::sort(csManagedApps.begin(), csManagedApps.end());
        }

        bool psEnabled = g_config.cloudPrefixSyncEnabled.get();
        bool psStartup = g_config.cloudPrefixSyncAtStartUp.get();
        bool psLaunch = g_config.cloudPrefixSyncOnLaunch.get();
        bool psExit = g_config.cloudPrefixSyncOnExit.get();
        uint32_t psMaxMb = g_config.cloudPrefixSyncMaxFileMb.get();

        if (fileLoaded && node["CloudSaves"] && node["CloudSaves"].IsMap())
        {
            const auto csNode = node["CloudSaves"];
            if (csNode["Enabled"]) { try { csEnabled = csNode["Enabled"].as<bool>(); } catch (...) {} }
            if (csNode["Provider"]) { try { csProvider = csNode["Provider"].as<std::string>(); } catch (...) {} }
            if (csNode["AutoBackupBeforeReset"]) { try { csAutoBackup = csNode["AutoBackupBeforeReset"].as<bool>(); } catch (...) {} }
            if (csNode["BackupRetentionCount"]) { try { csRetention = csNode["BackupRetentionCount"].as<uint32_t>(); } catch (...) {} }
            if (csNode["ManagedApps"] && csNode["ManagedApps"].IsSequence()) {
                csManagedApps.clear();
                for (const auto& item : csNode["ManagedApps"]) {
                    try { csManagedApps.push_back(item.as<uint32_t>()); } catch (...) {}
                }
                std::sort(csManagedApps.begin(), csManagedApps.end());
            }

            if (csNode["PrefixSync"] && csNode["PrefixSync"].IsMap())
            {
                const auto psNode = csNode["PrefixSync"];
                auto getPsBool = [&](const char* k, bool def) {
                    if (psNode[k])
                    {
                        try {
                            if (psNode[k].IsScalar())
                            {
                                const std::string s = psNode[k].Scalar();
                                if (s == "1" || s == "true" || s == "True" || s == "TRUE" ||
                                    s == "yes" || s == "Yes" || s == "YES" ||
                                    s == "on" || s == "On" || s == "ON") return true;
                                if (s == "0" || s == "false" || s == "False" || s == "FALSE" ||
                                    s == "no" || s == "No" || s == "NO" ||
                                    s == "off" || s == "Off" || s == "OFF") return false;
                            }
                            return psNode[k].as<bool>();
                        } catch (...) {}
                    }
                    return def;
                };
                psEnabled = getPsBool("Enabled", psEnabled);
                psStartup = getPsBool("SyncAtStartup", psStartup);
                psLaunch = getPsBool("SyncOnLaunch", psLaunch);
                psExit = getPsBool("SyncOnExit", psExit);
                if (psNode["MaxFileMb"]) { try { psMaxMb = psNode["MaxFileMb"].as<uint32_t>(); } catch (...) {} }
            }
        }

        auto formatUint32Array = [](const std::vector<uint32_t>& vec) -> std::string {
            std::string s = "[";
            for (size_t i = 0; i < vec.size(); ++i)
            {
                if (i > 0) s += ", ";
                s += std::to_string(vec[i]);
            }
            s += "]";
            return s;
        };

        auto formatStringMap = [](const std::map<std::string, std::string>& m) -> std::string {
            std::string s = "{";
            bool first = true;
            for (const auto& [k, v] : m)
            {
                if (!first) s += ", ";
                first = false;
                s += "\"" + escapeJson(k) + "\": \"" + escapeJson(v) + "\"";
            }
            s += "}";
            return s;
        };

        std::ostringstream ss;
        ss << "{\n";
        ss << "  \"DisableFamilyShareLock\": " << (disableFamilyShareLock ? "true" : "false") << ",\n";
        ss << "  \"UseWhitelist\": " << (useWhitelist ? "true" : "false") << ",\n";
        ss << "  \"SafeMode\": " << (safeMode ? "true" : "false") << ",\n";
        ss << "  \"WarnHashMissmatch\": " << (warnHashMissmatch ? "true" : "false") << ",\n";
        ss << "  \"NotifyInit\": " << (notifyInit ? "true" : "false") << ",\n";
        ss << "  \"API\": " << (api ? "true" : "false") << ",\n";
        ss << "  \"DisableCloud\": " << (disableCloud ? "true" : "false") << ",\n";
        ss << "  \"DisableUpdates\": " << (disableUpdates ? "true" : "false") << ",\n";
        ss << "  \"DumpClientInterfaces\": " << (dumpClientInterfaces ? "true" : "false") << ",\n";
        ss << "  \"ExtendedLogging\": " << (extendedLogging ? "true" : "false") << ",\n";
        ss << "  \"MaxSchemaTries\": " << maxSchemaTries << ",\n";
        ss << "  \"FakeWalletBalance\": " << fakeWalletBalance << ",\n";
        ss << "  \"LogLevels\": " << logLevels << ",\n";
        ss << "  \"FakeName\": \"" << escapeJson(fakeName) << "\",\n";
        ss << "  \"FakeEmail\": \"" << escapeJson(fakeEmail) << "\",\n";
        ss << "  \"RyuuKey\": \"" << escapeJson(ryuuKey) << "\",\n";
        ss << "  \"DepotBoxKey\": \"" << escapeJson(depotBoxKey) << "\",\n";
        ss << "  \"HubcapKey\": \"" << escapeJson(hubcapKey) << "\",\n";
        ss << "  \"IdleStatus\": {\"AppId\": " << idleAppId << ", \"Title\": \"" << escapeJson(idleTitle) << "\"},\n";
        ss << "  \"UnownedStatus\": {\"AppId\": " << unownedAppId << ", \"Title\": \"" << escapeJson(unownedTitle) << "\"},\n";
        ss << "  \"AppIds\": " << formatUint32Array(appIds) << ",\n";
        ss << "  \"AdditionalApps\": " << formatUint32Array(additionalApps) << ",\n";
        ss << "  \"FakeOffline\": " << formatUint32Array(fakeOffline) << ",\n";
        ss << "  \"DepotBlacklist\": " << formatUint32Array(depotBlacklist) << ",\n";
        ss << "  \"FakeAppIds\": " << formatStringMap(fakeAppIds) << ",\n";
        ss << "  \"ManifestIds\": " << formatStringMap(manifestIds) << ",\n";
        ss << "  \"AppTokens\": " << formatStringMap(appTokens) << ",\n";
        ss << "  \"CDKeys\": " << formatStringMap(cdKeys) << ",\n";
        ss << "  \"GameTitles\": " << formatStringMap(gameTitles) << ",\n";
        ss << "  \"SubscriptionTimestamps\": " << formatStringMap(subscriptionTimestamps) << ",\n";
        ss << "  \"SteamIdOverride\": " << formatStringMap(steamIdOverride) << ",\n";
        ss << "  \"DlcDataYaml\": \"" << escapeJson(dlcDataYaml) << "\",\n";
        ss << "  \"DenuvoGamesYaml\": \"" << escapeJson(denuvoGamesYaml) << "\",\n";
        ss << "  \"CloudSaves\": {\n";
        ss << "    \"Enabled\": " << (csEnabled ? "true" : "false") << ",\n";
        ss << "    \"Provider\": \"" << escapeJson(csProvider) << "\",\n";
        ss << "    \"AutoBackupBeforeReset\": " << (csAutoBackup ? "true" : "false") << ",\n";
        ss << "    \"BackupRetentionCount\": " << csRetention << ",\n";
        ss << "    \"ManagedApps\": " << formatUint32Array(csManagedApps) << ",\n";
        ss << "    \"PrefixSync\": {\n";
        ss << "      \"Enabled\": " << (psEnabled ? "true" : "false") << ",\n";
        ss << "      \"SyncAtStartup\": " << (psStartup ? "true" : "false") << ",\n";
        ss << "      \"SyncOnLaunch\": " << (psLaunch ? "true" : "false") << ",\n";
        ss << "      \"SyncOnExit\": " << (psExit ? "true" : "false") << ",\n";
        ss << "      \"MaxFileMb\": " << psMaxMb << "\n";
        ss << "    }\n";
        ss << "  }\n";
        ss << "}";
        return ss.str();
    }

    bool applyFullConfigJson(const std::string& configYamlPath, const std::string& jsonBody, std::string& outErrorMsg)
    {
        outErrorMsg.clear();
        if (jsonBody.empty())
        {
            outErrorMsg = "Empty JSON body";
            return false;
        }

        YAML::Node parsed;
        try
        {
            parsed = YAML::Load(jsonBody);
        }
        catch (const std::exception& e)
        {
            outErrorMsg = "Failed to parse JSON body: " + std::string(e.what());
            return false;
        }

        if (!parsed || !parsed.IsMap())
        {
            outErrorMsg = "Invalid JSON body: expected a JSON object";
            return false;
        }

        YAML::Node rootNode;
        if (std::filesystem::exists(configYamlPath))
        {
            try
            {
                rootNode = YAML::LoadFile(configYamlPath);
            }
            catch (...)
            {
                rootNode = YAML::Node(YAML::NodeType::Map);
            }
        }
        if (!rootNode || !rootNode.IsMap())
        {
            rootNode = YAML::Node(YAML::NodeType::Map);
        }

        // Booleans
        auto updateBool = [&](const char* key) {
            if (parsed[key])
            {
                bool val = false;
                try
                {
                    if (parsed[key].IsScalar())
                    {
                        const std::string s = parsed[key].Scalar();
                        if (s == "1" || s == "true" || s == "True" || s == "TRUE" ||
                            s == "yes" || s == "Yes" || s == "YES" ||
                            s == "on" || s == "On" || s == "ON") val = true;
                        else if (s == "0" || s == "false" || s == "False" || s == "FALSE" ||
                                 s == "no" || s == "No" || s == "NO" ||
                                 s == "off" || s == "Off" || s == "OFF") val = false;
                        else val = parsed[key].as<bool>();
                    }
                    else
                    {
                        val = parsed[key].as<bool>();
                    }
                    rootNode[key] = val;
                }
                catch (...) {}
            }
        };

        for (const char* bKey : {"DisableFamilyShareLock", "UseWhitelist", "SafeMode", "WarnHashMissmatch",
                                 "NotifyInit", "API", "DisableCloud", "DisableUpdates",
                                 "DumpClientInterfaces", "ExtendedLogging"})
        {
            updateBool(bKey);
        }

        // Numbers
        if (parsed["MaxSchemaTries"])
        {
            try { rootNode["MaxSchemaTries"] = parsed["MaxSchemaTries"].as<uint32_t>(); } catch (...) {}
        }
        if (parsed["FakeWalletBalance"])
        {
            try { rootNode["FakeWalletBalance"] = parsed["FakeWalletBalance"].as<int32_t>(); } catch (...) {}
        }
        if (parsed["LogLevels"])
        {
            try { rootNode["LogLevels"] = parsed["LogLevels"].as<uint32_t>(); } catch (...) {}
        }

        // Strings
        auto updateString = [&](const char* key) {
            if (parsed[key])
            {
                try { rootNode[key] = parsed[key].as<std::string>(); } catch (...) {}
            }
        };

        for (const char* sKey : {"FakeName", "FakeEmail", "RyuuKey", "DepotBoxKey", "HubcapKey"})
        {
            updateString(sKey);
        }

        // Status structs
        if (parsed["IdleStatus"] && parsed["IdleStatus"].IsMap())
        {
            YAML::Node s(YAML::NodeType::Map);
            try { s["AppId"] = parsed["IdleStatus"]["AppId"].as<uint32_t>(); } catch (...) { s["AppId"] = 0; }
            try { s["Title"] = parsed["IdleStatus"]["Title"].as<std::string>(); } catch (...) { s["Title"] = ""; }
            rootNode["IdleStatus"] = s;
        }

        if (parsed["UnownedStatus"] && parsed["UnownedStatus"].IsMap())
        {
            YAML::Node s(YAML::NodeType::Map);
            try { s["AppId"] = parsed["UnownedStatus"]["AppId"].as<uint32_t>(); } catch (...) { s["AppId"] = 0; }
            try { s["Title"] = parsed["UnownedStatus"]["Title"].as<std::string>(); } catch (...) { s["Title"] = ""; }
            rootNode["UnownedStatus"] = s;
        }

        // Arrays of uint32
        for (const char* arrKey : {"AppIds", "AdditionalApps", "FakeOffline", "DepotBlacklist"})
        {
            if (parsed[arrKey])
            {
                YAML::Node seq(YAML::NodeType::Sequence);
                if (parsed[arrKey].IsSequence())
                {
                    for (const auto& item : parsed[arrKey])
                    {
                        try { seq.push_back(item.as<uint32_t>()); } catch (...) {}
                    }
                }
                rootNode[arrKey] = seq;
            }
        }

        // Key-Value Maps
        for (const char* mapKey : {"FakeAppIds", "ManifestIds", "AppTokens", "CDKeys", "GameTitles", "SubscriptionTimestamps", "SteamIdOverride"})
        {
            if (parsed[mapKey])
            {
                YAML::Node m(YAML::NodeType::Map);
                if (parsed[mapKey].IsMap())
                {
                    for (auto it = parsed[mapKey].begin(); it != parsed[mapKey].end(); ++it)
                    {
                        try
                        {
                            std::string k = it->first.as<std::string>();
                            if (std::string(mapKey) == "CDKeys" || std::string(mapKey) == "GameTitles")
                            {
                                m[k] = it->second.as<std::string>();
                            }
                            else if (std::string(mapKey) == "ManifestIds" || std::string(mapKey) == "AppTokens" || std::string(mapKey) == "SteamIdOverride")
                            {
                                uint64_t v = 0;
                                try { v = std::stoull(it->second.as<std::string>()); }
                                catch (...) { try { v = it->second.as<uint64_t>(); } catch (...) {} }
                                m[k] = v;
                            }
                            else
                            {
                                uint32_t v = 0;
                                try { v = std::stoul(it->second.as<std::string>()); }
                                catch (...) { try { v = it->second.as<uint32_t>(); } catch (...) {} }
                                m[k] = v;
                            }
                        }
                        catch (...) {}
                    }
                }
                rootNode[mapKey] = m;
            }
        }

        // Complex Nested Maps
        if (parsed["DlcDataYaml"])
        {
            std::string dlcStr = parsed["DlcDataYaml"].as<std::string>();
            if (!dlcStr.empty() && dlcStr != "{}" && dlcStr != "null")
            {
                try
                {
                    rootNode["DlcData"] = YAML::Load(dlcStr);
                }
                catch (const std::exception& e)
                {
                    outErrorMsg = "Failed to parse DlcDataYaml: " + std::string(e.what());
                    return false;
                }
            }
            else
            {
                rootNode["DlcData"] = YAML::Node(YAML::NodeType::Map);
            }
        }

        if (parsed["DenuvoGamesYaml"])
        {
            std::string denuvoStr = parsed["DenuvoGamesYaml"].as<std::string>();
            if (!denuvoStr.empty() && denuvoStr != "{}" && denuvoStr != "null")
            {
                try
                {
                    rootNode["DenuvoGames"] = YAML::Load(denuvoStr);
                }
                catch (const std::exception& e)
                {
                    outErrorMsg = "Failed to parse DenuvoGamesYaml: " + std::string(e.what());
                    return false;
                }
            }
            else
            {
                rootNode["DenuvoGames"] = YAML::Node(YAML::NodeType::Map);
            }
        }

        if (parsed["CloudSaves"] && parsed["CloudSaves"].IsMap())
        {
            const auto& cs = parsed["CloudSaves"];
            YAML::Node csNode;
            if (rootNode["CloudSaves"] && rootNode["CloudSaves"].IsMap())
            {
                csNode = rootNode["CloudSaves"];
            }
            else
            {
                csNode = YAML::Node(YAML::NodeType::Map);
            }

            if (cs["Enabled"])
            {
                try { csNode["Enabled"] = cs["Enabled"].as<bool>(); } catch (...) {}
            }
            if (cs["Provider"])
            {
                try {
                    std::string prov = cs["Provider"].as<std::string>();
                    if (prov == "local-only" || prov == "gdrive" || prov == "onedrive" || prov == "r2" || prov == "s3")
                    {
                        csNode["Provider"] = prov;
                    }
                } catch (...) {}
            }
            if (cs["AutoBackupBeforeReset"])
            {
                try { csNode["AutoBackupBeforeReset"] = cs["AutoBackupBeforeReset"].as<bool>(); } catch (...) {}
            }
            if (cs["BackupRetentionCount"])
            {
                try {
                    uint32_t ret = cs["BackupRetentionCount"].as<uint32_t>();
                    if (ret < 1) ret = 1;
                    if (ret > 100) ret = 100;
                    csNode["BackupRetentionCount"] = ret;
                } catch (...) {}
            }
            if (cs["ManagedApps"] && cs["ManagedApps"].IsSequence())
            {
                YAML::Node seq(YAML::NodeType::Sequence);
                for (const auto& item : cs["ManagedApps"])
                {
                    try { seq.push_back(item.as<uint32_t>()); } catch (...) {}
                }
                csNode["ManagedApps"] = seq;
            }
            if (cs["PrefixSync"] && cs["PrefixSync"].IsMap())
            {
                const auto& ps = cs["PrefixSync"];
                YAML::Node psNode;
                if (csNode["PrefixSync"] && csNode["PrefixSync"].IsMap())
                {
                    psNode = csNode["PrefixSync"];
                }
                else
                {
                    psNode = YAML::Node(YAML::NodeType::Map);
                }

                auto parseBoolVal = [](const YAML::Node& valNode, bool def) -> bool {
                    if (valNode.IsScalar())
                    {
                        const std::string s = valNode.Scalar();
                        if (s == "1" || s == "true" || s == "True" || s == "TRUE" ||
                            s == "yes" || s == "Yes" || s == "YES" ||
                            s == "on" || s == "On" || s == "ON") return true;
                        if (s == "0" || s == "false" || s == "False" || s == "FALSE" ||
                            s == "no" || s == "No" || s == "NO" ||
                            s == "off" || s == "Off" || s == "OFF") return false;
                    }
                    try { return valNode.as<bool>(); } catch (...) {}
                    return def;
                };

                if (ps["Enabled"])
                {
                    psNode["Enabled"] = parseBoolVal(ps["Enabled"], true);
                }
                if (ps["SyncAtStartup"])
                {
                    psNode["SyncAtStartup"] = parseBoolVal(ps["SyncAtStartup"], true);
                }
                if (ps["SyncOnLaunch"])
                {
                    psNode["SyncOnLaunch"] = parseBoolVal(ps["SyncOnLaunch"], true);
                }
                if (ps["SyncOnExit"])
                {
                    psNode["SyncOnExit"] = parseBoolVal(ps["SyncOnExit"], true);
                }
                if (ps["MaxFileMb"])
                {
                    try { psNode["MaxFileMb"] = ps["MaxFileMb"].as<uint32_t>(); } catch (...) {}
                }
                csNode["PrefixSync"] = psNode;
            }
            rootNode["CloudSaves"] = csNode;
        }

        std::filesystem::path p(configYamlPath);
        if (p.has_parent_path() && !std::filesystem::exists(p.parent_path()))
        {
            std::error_code ec;
            std::filesystem::create_directories(p.parent_path(), ec);
        }

        std::stringstream yss;
        yss << rootNode;
        if (!AtomicFile::write(configYamlPath, yss.str()))
        {
            outErrorMsg = "Failed to write config file to " + configYamlPath;
            return false;
        }

        g_config.loadSettings(false);
        Navi::CloudRedirect::CCloudRedirectBridge::Instance().SyncSlsSteamConfig(g_config.GetEffectiveCloudRedirectedApps());
        return true;
    }
}

