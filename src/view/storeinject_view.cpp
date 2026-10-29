#include "storeinject_view.hpp"
#include "atomic_file.hpp"
#include "log.hpp"
#include "depotkeys.hpp"
#include "config.hpp"
#include <yaml-cpp/yaml.h>

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <sys/stat.h>

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

    std::string renderCheckJson(const CheckViewModel& info)
    {
        std::string escapedProvider;
        for (char c : info.provider)
        {
            if (c == '"' || c == '\\') escapedProvider += '\\';
            escapedProvider += c;
        }

        std::string json = "{\"exists\":" + std::string(info.exists ? "true" : "false") + 
                           ",\"hasLua\":" + std::string(info.hasLua ? "true" : "false") +
                           ",\"gameExists\":" + std::string(info.gameExists ? "true" : "false") +
                           ",\"isUnlocked\":" + std::string(info.isUnlocked ? "true" : "false") +
                           ",\"pending\":" + std::string(info.pending ? "true" : "false") +
                           ",\"downloading\":" + std::string(info.downloading ? "true" : "false") +
                           ",\"paused\":" + std::string(info.paused ? "true" : "false") +
                           ",\"onlineFixInstalled\":" + std::string(info.onlineFixInstalled ? "true" : "false") +
                           ",\"autoCrackInstalled\":" + std::string(info.autoCrackInstalled ? "true" : "false") +
                           ",\"provider\":\"" + escapedProvider + "\"}";
        return json;
    }

    std::string renderCheckJson(bool exists, bool hasLua, bool gameExists, bool isUnlocked,
                                bool pending, bool downloading, bool paused,
                                bool onlineFixInstalled, bool autoCrackInstalled,
                                const std::string& provider)
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

    static std::string getJsonStringField(const std::string& json, const std::string& key)
    {
        std::string needle = "\"" + key + "\"";
        size_t pos = json.find(needle);
        if (pos == std::string::npos) return "";

        pos = json.find(':', pos + needle.size());
        if (pos == std::string::npos) return "";

        pos = json.find('"', pos + 1);
        if (pos == std::string::npos) return "";
        pos++;

        size_t end = pos;
        while (end < json.size())
        {
            if (json[end] == '\\')
            {
                end += 2;
                continue;
            }
            if (json[end] == '"')
            {
                break;
            }
            end++;
        }
        if (end > json.size()) return "";

        std::string raw = json.substr(pos, end - pos);
        std::string unescaped;
        unescaped.reserve(raw.size());
        for (size_t i = 0; i < raw.size(); ++i)
        {
            if (raw[i] == '\\' && i + 1 < raw.size())
            {
                i++;
                switch (raw[i])
                {
                    case '"': unescaped += '"'; break;
                    case '\\': unescaped += '\\'; break;
                    case '/': unescaped += '/'; break;
                    case 'b': unescaped += '\b'; break;
                    case 'f': unescaped += '\f'; break;
                    case 'n': unescaped += '\n'; break;
                    case 'r': unescaped += '\r'; break;
                    case 't': unescaped += '\t'; break;
                    default: unescaped += raw[i]; break;
                }
            }
            else
            {
                unescaped += raw[i];
            }
        }
        return unescaped;
    }

    static bool getJsonBoolField(const std::string& json, const std::string& key, bool defaultVal = false)
    {
        std::string needle = "\"" + key + "\"";
        size_t pos = json.find(needle);
        if (pos == std::string::npos) return defaultVal;

        pos = json.find(':', pos + needle.size());
        if (pos == std::string::npos) return defaultVal;

        while (pos < json.size() && (std::isspace(static_cast<unsigned char>(json[pos])) || json[pos] == ':'))
        {
            pos++;
        }

        if (json.compare(pos, 4, "true") == 0) return true;
        if (json.compare(pos, 5, "false") == 0) return false;
        if (pos < json.size() && json[pos] == '1') return true;
        if (pos < json.size() && json[pos] == '0') return false;
        return defaultVal;
    }

    static std::vector<uint32_t> getJsonUint32ArrayField(const std::string& json, const std::string& key)
    {
        std::vector<uint32_t> result;
        std::string needle = "\"" + key + "\"";
        size_t pos = json.find(needle);
        if (pos == std::string::npos) return result;

        pos = json.find(':', pos + needle.size());
        if (pos == std::string::npos) return result;

        pos = json.find('[', pos);
        if (pos == std::string::npos) return result;
        pos++;

        size_t end = json.find(']', pos);
        if (end == std::string::npos) return result;

        std::string inside = json.substr(pos, end - pos);
        std::istringstream iss(inside);
        std::string token;
        while (std::getline(iss, token, ','))
        {
            size_t s = token.find_first_not_of(" \t\r\n");
            if (s == std::string::npos) continue;
            size_t e = token.find_last_not_of(" \t\r\n");
            std::string numStr = token.substr(s, e - s + 1);
            try
            {
                result.push_back(static_cast<uint32_t>(std::stoul(numStr)));
            }
            catch (...) {}
        }
        return result;
    }

    static std::vector<std::string> getJsonStringArrayField(const std::string& json, const std::string& key)
    {
        std::vector<std::string> result;
        std::string needle = "\"" + key + "\"";
        size_t pos = json.find(needle);
        if (pos == std::string::npos) return result;

        pos = json.find(':', pos + needle.size());
        if (pos == std::string::npos) return result;

        pos = json.find('[', pos);
        if (pos == std::string::npos) return result;
        pos++;

        size_t end = json.find(']', pos);
        if (end == std::string::npos) return result;

        size_t p = pos;
        while (p < end)
        {
            size_t q1 = json.find('"', p);
            if (q1 == std::string::npos || q1 >= end) break;
            size_t q2 = q1 + 1;
            while (q2 < end)
            {
                if (json[q2] == '\\') { q2 += 2; continue; }
                if (json[q2] == '"') break;
                q2++;
            }
            if (q2 >= end) break;
            result.push_back(json.substr(q1 + 1, q2 - (q1 + 1)));
            p = q2 + 1;
        }
        return result;
    }

    static std::unordered_map<std::string, std::string> getJsonObjectField(const std::string& json, const std::string& key)
    {
        std::unordered_map<std::string, std::string> result;
        std::string needle = "\"" + key + "\"";
        size_t pos = json.find(needle);
        if (pos == std::string::npos) return result;

        pos = json.find(':', pos + needle.size());
        if (pos == std::string::npos) return result;

        pos = json.find('{', pos);
        if (pos == std::string::npos) return result;

        size_t depth = 1;
        size_t end = pos + 1;
        while (end < json.size() && depth > 0)
        {
            if (json[end] == '{') depth++;
            else if (json[end] == '}') depth--;
            end++;
        }
        if (depth != 0) return result;

        std::string inside = json.substr(pos + 1, end - pos - 2);
        size_t p = 0;
        while (p < inside.size())
        {
            size_t kStart = inside.find('"', p);
            if (kStart == std::string::npos) break;
            size_t kEnd = inside.find('"', kStart + 1);
            if (kEnd == std::string::npos) break;
            std::string mapKey = inside.substr(kStart + 1, kEnd - kStart - 1);

            size_t colon = inside.find(':', kEnd + 1);
            if (colon == std::string::npos) break;

            size_t valStart = inside.find_first_not_of(" \t\r\n", colon + 1);
            if (valStart == std::string::npos) break;

            std::string mapVal;
            if (inside[valStart] == '"')
            {
                size_t valEnd = inside.find('"', valStart + 1);
                if (valEnd == std::string::npos) break;
                mapVal = inside.substr(valStart + 1, valEnd - valStart - 1);
                p = valEnd + 1;
            }
            else
            {
                size_t valEnd = inside.find_first_of(",}\r\n", valStart);
                if (valEnd == std::string::npos) valEnd = inside.size();
                mapVal = inside.substr(valStart, valEnd - valStart);
                p = valEnd;
            }

            result[mapKey] = mapVal;
            size_t comma = inside.find(',', p);
            if (comma == std::string::npos) break;
            p = comma + 1;
        }
        return result;
    }

    std::string renderSyncStatusJson(const SyncStatusViewModel& vm)
    {
        std::ostringstream ss;
        ss << "{\n";
        ss << "  \"role\": \"" << escapeJson(vm.role) << "\",\n";
        ss << "  \"topology\": \"" << escapeJson(vm.topology) << "\",\n";
        ss << "  \"cloudProvider\": \"" << escapeJson(vm.cloudProvider) << "\",\n";
        ss << "  \"cloudAuthenticated\": " << (vm.cloudAuthenticated ? "true" : "false") << ",\n";
        ss << "  \"gdriveAuthenticated\": " << (vm.gdriveAuthenticated ? "true" : "false") << ",\n";
        ss << "  \"recoveryKey\": \"" << escapeJson(vm.recoveryKeyMaskedOrFull) << "\",\n";
        ss << "  \"recoveryKeyMaskedOrFull\": \"" << escapeJson(vm.recoveryKeyMaskedOrFull) << "\",\n";
        ss << "  \"keyEpoch\": " << vm.keyEpoch << ",\n";
        ss << "  \"hasRemoteBump\": " << (vm.hasRemoteBump ? "true" : "false") << ",\n";
        ss << "  \"lastTriggerReason\": \"" << escapeJson(vm.lastTriggerReason) << "\",\n";
        ss << "  \"overriderHostname\": \"" << escapeJson(vm.overriderHostname) << "\",\n";
        ss << "  \"remoteGeneration\": " << vm.remoteGeneration << ",\n";
        ss << "  \"localGeneration\": " << vm.localGeneration << ",\n";

        // diffItems
        ss << "  \"diffItems\": [";
        for (size_t i = 0; i < vm.diffItems.size(); ++i)
        {
            const auto& it = vm.diffItems[i];
            if (i > 0) ss << ",";
            ss << "\n    {";
            ss << "\"appId\": " << it.appId << ", ";
            ss << "\"title\": \"" << escapeJson(it.title) << "\", ";
            ss << "\"category\": \"" << escapeJson(it.category) << "\", ";
            ss << "\"logicalPath\": \"" << escapeJson(it.logicalPath) << "\", ";
            ss << "\"sizeBytes\": " << it.sizeBytes << ", ";
            ss << "\"generation\": " << it.generation << ", ";
            ss << "\"isGameRunning\": " << (it.isGameRunning ? "true" : "false") << "}";
        }
        if (!vm.diffItems.empty()) ss << "\n  ";
        ss << "],\n";

        // conflicts
        ss << "  \"conflicts\": [";
        for (size_t i = 0; i < vm.conflicts.size(); ++i)
        {
            const auto& c = vm.conflicts[i];
            if (i > 0) ss << ",";
            ss << "\n    {";
            ss << "\"appId\": " << c.appId << ", ";
            ss << "\"logicalPath\": \"" << escapeJson(c.logicalPath) << "\", ";
            ss << "\"localSha256\": \"" << escapeJson(c.localSha256) << "\", ";
            ss << "\"localMtimeUnixSec\": " << c.localMtimeUnixSec << ", ";
            ss << "\"localSizeBytes\": " << c.localSizeBytes << ", ";
            ss << "\"remoteSha256\": \"" << escapeJson(c.remoteSha256) << "\", ";
            ss << "\"remoteMtimeUnixSec\": " << c.remoteMtimeUnixSec << ", ";
            ss << "\"remoteSizeBytes\": " << c.remoteSizeBytes << "}";
        }
        if (!vm.conflicts.empty()) ss << "\n  ";
        ss << "],\n";

        // p2pPeers
        ss << "  \"p2pPeers\": [";
        for (size_t i = 0; i < vm.p2pPeers.size(); ++i)
        {
            const auto& p = vm.p2pPeers[i];
            if (i > 0) ss << ",";
            ss << "\n    {";
            ss << "\"deviceId\": \"" << escapeJson(p.deviceId) << "\", ";
            ss << "\"hostname\": \"" << escapeJson(p.hostname) << "\", ";
            ss << "\"role\": " << static_cast<uint32_t>(p.role) << ", ";
            ss << "\"ipAddress\": \"" << escapeJson(p.ipAddress) << "\", ";
            ss << "\"tlsPort\": " << p.tlsPort << ", ";
            ss << "\"globalGeneration\": " << p.globalGeneration << ", ";
            ss << "\"keyEpoch\": " << p.keyEpoch << ", ";
            ss << "\"sidHashHex\": \"" << escapeJson(p.sidHashHex) << "\", ";
            ss << "\"lastSeenUnixMs\": " << p.lastSeenUnixMs << "}";
        }
        if (!vm.p2pPeers.empty()) ss << "\n  ";
        ss << "],\n";

        // stagedAcfRestartRequiredAppIds
        ss << "  \"stagedAcfRestartRequiredAppIds\": [";
        for (size_t i = 0; i < vm.stagedAcfRestartRequiredAppIds.size(); ++i)
        {
            if (i > 0) ss << ", ";
            ss << vm.stagedAcfRestartRequiredAppIds[i];
        }
        ss << "]\n";
        ss << "}";
        return ss.str();
    }

    std::string renderSyncApplyResultJson(const Sync::SyncApplyReport_t& report)
    {
        std::ostringstream ss;
        ss << "{\n";
        ss << "  \"status\": " << static_cast<int>(report.status) << ",\n";
        ss << "  \"success\": " << (report.status == Sync::ESyncApplyStatus::k_Ok ? "true" : "false") << ",\n";
        ss << "  \"snapshotId\": \"" << escapeJson(report.snapshotId) << "\",\n";

        // appliedLogicalPaths
        ss << "  \"appliedLogicalPaths\": [";
        for (size_t i = 0; i < report.appliedLogicalPaths.size(); ++i)
        {
            if (i > 0) ss << ", ";
            ss << "\"" << escapeJson(report.appliedLogicalPaths[i]) << "\"";
        }
        ss << "],\n";

        // deferredAppIds
        ss << "  \"deferredAppIds\": [";
        for (size_t i = 0; i < report.deferredAppIds.size(); ++i)
        {
            if (i > 0) ss << ", ";
            ss << report.deferredAppIds[i];
        }
        ss << "],\n";

        // conflicts
        ss << "  \"conflicts\": [";
        for (size_t i = 0; i < report.conflicts.size(); ++i)
        {
            const auto& c = report.conflicts[i];
            if (i > 0) ss << ",";
            ss << "\n    {";
            ss << "\"appId\": " << c.appId << ", ";
            ss << "\"logicalPath\": \"" << escapeJson(c.logicalPath) << "\", ";
            ss << "\"localSha256\": \"" << escapeJson(c.localSha256) << "\", ";
            ss << "\"localMtimeUnixSec\": " << c.localMtimeUnixSec << ", ";
            ss << "\"localSizeBytes\": " << c.localSizeBytes << ", ";
            ss << "\"remoteSha256\": \"" << escapeJson(c.remoteSha256) << "\", ";
            ss << "\"remoteMtimeUnixSec\": " << c.remoteMtimeUnixSec << ", ";
            ss << "\"remoteSizeBytes\": " << c.remoteSizeBytes << "}";
        }
        if (!report.conflicts.empty()) ss << "\n  ";
        ss << "],\n";

        // stagedAcfRestartRequiredAppIds
        ss << "  \"stagedAcfRestartRequiredAppIds\": [";
        for (size_t i = 0; i < report.stagedAcfRestartRequiredAppIds.size(); ++i)
        {
            if (i > 0) ss << ", ";
            ss << report.stagedAcfRestartRequiredAppIds[i];
        }
        ss << "]\n";
        ss << "}";
        return ss.str();
    }

    bool parseSyncApplyRequestJson(const std::string& jsonBody, SyncApplyRequest_t& outReq)
    {
        if (jsonBody.empty() || jsonBody.find('{') == std::string::npos)
        {
            return false;
        }

        std::string filterBody = jsonBody;
        size_t fpos = jsonBody.find("\"filter\"");
        if (fpos != std::string::npos)
        {
            size_t ob = jsonBody.find('{', fpos);
            if (ob != std::string::npos)
            {
                size_t d = 1, e = ob + 1;
                while (e < jsonBody.size() && d > 0)
                {
                    if (jsonBody[e] == '{') d++;
                    else if (jsonBody[e] == '}') d--;
                    e++;
                }
                if (d == 0)
                {
                    filterBody = jsonBody.substr(ob, e - ob);
                }
            }
        }

        outReq.filter.allManifestsAndLua = getJsonBoolField(filterBody, "allManifestsAndLua", getJsonBoolField(jsonBody, "allManifestsAndLua", false));
        for (uint32_t id : getJsonUint32ArrayField(filterBody, "manifestAppIds"))
        {
            outReq.filter.manifestAppIds.insert(id);
        }
        for (uint32_t id : getJsonUint32ArrayField(jsonBody, "manifestAppIds"))
        {
            outReq.filter.manifestAppIds.insert(id);
        }

        outReq.filter.allConfigs = getJsonBoolField(filterBody, "allConfigs", getJsonBoolField(jsonBody, "allConfigs", false));
        for (const auto& k : getJsonStringArrayField(filterBody, "configKeys"))
        {
            outReq.filter.configKeys.insert(k);
        }
        for (const auto& k : getJsonStringArrayField(jsonBody, "configKeys"))
        {
            outReq.filter.configKeys.insert(k);
        }

        outReq.filter.allCloudSaves = getJsonBoolField(filterBody, "allCloudSaves", getJsonBoolField(jsonBody, "allCloudSaves", false));
        for (uint32_t id : getJsonUint32ArrayField(filterBody, "cloudSaveAppIds"))
        {
            outReq.filter.cloudSaveAppIds.insert(id);
        }
        for (uint32_t id : getJsonUint32ArrayField(jsonBody, "cloudSaveAppIds"))
        {
            outReq.filter.cloudSaveAppIds.insert(id);
        }

        outReq.filter.allAchievements = getJsonBoolField(filterBody, "allAchievements", getJsonBoolField(jsonBody, "allAchievements", false));
        for (uint32_t id : getJsonUint32ArrayField(filterBody, "achievementAppIds"))
        {
            outReq.filter.achievementAppIds.insert(id);
        }
        for (uint32_t id : getJsonUint32ArrayField(jsonBody, "achievementAppIds"))
        {
            outReq.filter.achievementAppIds.insert(id);
        }

        outReq.filter.allApiKeys = getJsonBoolField(filterBody, "allApiKeys", getJsonBoolField(jsonBody, "allApiKeys", false));
        for (const auto& prov : getJsonStringArrayField(filterBody, "providerNames"))
        {
            outReq.filter.providerNames.insert(prov);
        }
        for (const auto& prov : getJsonStringArrayField(jsonBody, "providerNames"))
        {
            outReq.filter.providerNames.insert(prov);
        }

        auto rawChoices = getJsonObjectField(jsonBody, "conflictChoices");
        for (const auto& [path, choiceStr] : rawChoices)
        {
            if (choiceStr == "keep_local" || choiceStr == "1")
            {
                outReq.conflictChoices[path] = Sync::EConflictChoice::k_KeepLocal;
            }
            else if (choiceStr == "keep_remote" || choiceStr == "2")
            {
                outReq.conflictChoices[path] = Sync::EConflictChoice::k_KeepRemote;
            }
            else if (choiceStr == "backup_both" || choiceStr == "backup_local_and_use_remote" || choiceStr == "3")
            {
                outReq.conflictChoices[path] = Sync::EConflictChoice::k_BackupLocalAndUseRemote;
            }
            else
            {
                outReq.conflictChoices[path] = Sync::EConflictChoice::k_PendingUserChoice;
            }
        }

        return true;
    }

    bool parseSyncConfigUpdateJson(const std::string& jsonBody, SyncConfigUpdateRequest_t& outReq)
    {
        if (jsonBody.empty() || jsonBody.find('{') == std::string::npos)
        {
            return false;
        }

        outReq.role = getJsonStringField(jsonBody, "role");
        outReq.topology = getJsonStringField(jsonBody, "topology");
        outReq.cloudProvider = getJsonStringField(jsonBody, "cloudProvider");
        outReq.webDavUrl = getJsonStringField(jsonBody, "webDavUrl");
        if (outReq.webDavUrl.empty()) outReq.webDavUrl = getJsonStringField(jsonBody, "webdavUrl");
        outReq.webDavUser = getJsonStringField(jsonBody, "webDavUser");
        if (outReq.webDavUser.empty()) outReq.webDavUser = getJsonStringField(jsonBody, "webdavUser");
        outReq.webDavPassword = getJsonStringField(jsonBody, "webDavPassword");
        if (outReq.webDavPassword.empty()) outReq.webDavPassword = getJsonStringField(jsonBody, "webdavPassword");

        return true;
    }

    bool validateAndParseRecoveryKeyJson(
        const std::string& jsonBody,
        Sync::MasterKey_t& outKey,
        bool& outRotateRequested,
        std::string& outErrorMsg)
    {
        outRotateRequested = false;
        outErrorMsg.clear();

        if (jsonBody.empty() || jsonBody.find('{') == std::string::npos)
        {
            outErrorMsg = "Empty request body";
            return false;
        }

        std::string action = getJsonStringField(jsonBody, "action");
        if (action == "rotate")
        {
            outRotateRequested = true;
            return true;
        }

        std::string keyStr = getJsonStringField(jsonBody, "recoveryKey");
        if (keyStr.empty())
        {
            outErrorMsg = "Missing recoveryKey or action";
            return false;
        }

        if (!Sync::decodeRecoveryKey(keyStr, outKey))
        {
            outErrorMsg = "Invalid Recovery Key or CRC-16 checksum mismatch";
            return false;
        }

        return true;
    }

    bool loadCloudCredentials(const std::string& configDir, CloudCredentials_t& outCreds)
    {
        std::filesystem::path filePath = std::filesystem::path(configDir) / "sync_keys" / "cloud_creds.yaml";
        if (!std::filesystem::exists(filePath))
        {
            return false;
        }

        try
        {
            YAML::Node node = YAML::LoadFile(filePath.string());
            if (!node || !node.IsMap()) return false;

            if (node["GDriveClientId"] && node["GDriveClientId"].IsScalar()) outCreds.gdriveClientId = node["GDriveClientId"].Scalar();
            if (node["GDriveClientSecret"] && node["GDriveClientSecret"].IsScalar()) outCreds.gdriveClientSecret = node["GDriveClientSecret"].Scalar();
            if (node["GDriveAccessToken"] && node["GDriveAccessToken"].IsScalar()) outCreds.gdriveAccessToken = node["GDriveAccessToken"].Scalar();
            if (node["GDriveRefreshToken"] && node["GDriveRefreshToken"].IsScalar()) outCreds.gdriveRefreshToken = node["GDriveRefreshToken"].Scalar();
            if (node["GDriveTokenExpiresAt"] && node["GDriveTokenExpiresAt"].IsScalar())
            {
                std::string s = node["GDriveTokenExpiresAt"].Scalar();
                outCreds.gdriveTokenExpiresAt = static_cast<int64_t>(std::strtoll(s.c_str(), nullptr, 10));
            }
            if (node["GDriveDeviceAuthEndpoint"] && node["GDriveDeviceAuthEndpoint"].IsScalar() && !node["GDriveDeviceAuthEndpoint"].Scalar().empty())
                outCreds.gdriveDeviceAuthEndpoint = node["GDriveDeviceAuthEndpoint"].Scalar();
            if (node["GDriveTokenEndpoint"] && node["GDriveTokenEndpoint"].IsScalar() && !node["GDriveTokenEndpoint"].Scalar().empty())
                outCreds.gdriveTokenEndpoint = node["GDriveTokenEndpoint"].Scalar();
            if (node["GDriveApiBaseUrl"] && node["GDriveApiBaseUrl"].IsScalar() && !node["GDriveApiBaseUrl"].Scalar().empty())
                outCreds.gdriveApiBaseUrl = node["GDriveApiBaseUrl"].Scalar();
            if (node["GDriveUploadBaseUrl"] && node["GDriveUploadBaseUrl"].IsScalar() && !node["GDriveUploadBaseUrl"].Scalar().empty())
                outCreds.gdriveUploadBaseUrl = node["GDriveUploadBaseUrl"].Scalar();
            if (node["WebDavPassword"] && node["WebDavPassword"].IsScalar()) outCreds.webdavPassword = node["WebDavPassword"].Scalar();

            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool saveCloudCredentials(const std::string& configDir, const CloudCredentials_t& creds)
    {
        std::filesystem::path dir = std::filesystem::path(configDir) / "sync_keys";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);

        std::filesystem::path filePath = dir / "cloud_creds.yaml";

        YAML::Node node(YAML::NodeType::Map);
        node["GDriveClientId"] = creds.gdriveClientId;
        node["GDriveClientSecret"] = creds.gdriveClientSecret;
        node["GDriveAccessToken"] = creds.gdriveAccessToken;
        node["GDriveRefreshToken"] = creds.gdriveRefreshToken;
        node["GDriveTokenExpiresAt"] = creds.gdriveTokenExpiresAt;
        node["GDriveDeviceAuthEndpoint"] = creds.gdriveDeviceAuthEndpoint.empty() ? "https://oauth2.googleapis.com/device/code" : creds.gdriveDeviceAuthEndpoint;
        node["GDriveTokenEndpoint"] = creds.gdriveTokenEndpoint.empty() ? "https://oauth2.googleapis.com/token" : creds.gdriveTokenEndpoint;
        node["GDriveApiBaseUrl"] = creds.gdriveApiBaseUrl.empty() ? "https://www.googleapis.com/drive/v3" : creds.gdriveApiBaseUrl;
        node["GDriveUploadBaseUrl"] = creds.gdriveUploadBaseUrl.empty() ? "https://www.googleapis.com/upload/drive/v3" : creds.gdriveUploadBaseUrl;
        node["WebDavPassword"] = creds.webdavPassword;

        std::stringstream ss;
        ss << node;

        if (!AtomicFile::write(filePath.string(), ss.str()))
        {
            return false;
        }

        chmod(filePath.c_str(), 0600);
        return true;
    }

    bool parseGDriveDeviceStartJson(const std::string& jsonBody, std::string& outClientId, std::string& outClientSecret)
    {
        outClientId.clear();
        outClientSecret.clear();
        if (jsonBody.empty()) return true;

        try
        {
            YAML::Node node = YAML::Load(jsonBody);
            if (!node || !node.IsMap()) return false;

            if (node["clientId"]) outClientId = node["clientId"].as<std::string>();
            else if (node["SyncGDriveClientId"]) outClientId = node["SyncGDriveClientId"].as<std::string>();

            if (node["clientSecret"]) outClientSecret = node["clientSecret"].as<std::string>();
            else if (node["SyncGDriveClientSecret"]) outClientSecret = node["SyncGDriveClientSecret"].as<std::string>();

            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    std::string renderGDriveDevicePromptJson(const Sync::DeviceCodePrompt_t& prompt)
    {
        std::ostringstream ss;
        ss << "{\n"
           << "  \"success\": true,\n"
           << "  \"deviceCode\": \"" << escapeJson(prompt.deviceCode) << "\",\n"
           << "  \"userCode\": \"" << escapeJson(prompt.userCode) << "\",\n"
           << "  \"verificationUrl\": \"" << escapeJson(prompt.verificationUrl) << "\",\n"
           << "  \"expiresInSec\": " << prompt.expiresInSec << ",\n"
           << "  \"intervalSec\": " << prompt.intervalSec << "\n"
           << "}";
        return ss.str();
    }

    bool parseGDriveDevicePollJson(const std::string& jsonBody, std::string& outDeviceCode)
    {
        outDeviceCode.clear();
        if (jsonBody.empty()) return false;

        try
        {
            YAML::Node node = YAML::Load(jsonBody);
            if (!node || !node.IsMap()) return false;

            if (node["deviceCode"]) outDeviceCode = node["deviceCode"].as<std::string>();
            else if (node["device_code"]) outDeviceCode = node["device_code"].as<std::string>();

            return !outDeviceCode.empty();
        }
        catch (...)
        {
            return false;
        }
    }

    std::string renderSyncPushResultJson(uint64_t generation, size_t pushedFiles, const std::string& etag)
    {
        std::ostringstream ss;
        ss << "{\n"
           << "  \"success\": true,\n"
           << "  \"generation\": " << generation << ",\n"
           << "  \"pushedFiles\": " << pushedFiles << ",\n"
           << "  \"etag\": \"" << escapeJson(etag) << "\"\n"
           << "}";
        return ss.str();
    }

    static std::string urlDecode(const std::string& str)
    {
        std::string ret;
        for (size_t i = 0; i < str.length(); ++i)
        {
            if (str[i] == '%')
            {
                if (i + 2 < str.length())
                {
                    int value = 0;
                    std::istringstream is(str.substr(i + 1, 2));
                    if (is >> std::hex >> value)
                    {
                        ret += static_cast<char>(value);
                        i += 2;
                    }
                    else
                    {
                        ret += '%';
                    }
                }
                else
                {
                    ret += '%';
                }
            }
            else if (str[i] == '+')
            {
                ret += ' ';
            }
            else
            {
                ret += str[i];
            }
        }
        return ret;
    }

    static std::string escapeHtml(const std::string& str)
    {
        std::string out;
        out.reserve(str.size());
        for (char c : str)
        {
            switch (c)
            {
                case '&':  out += "&amp;"; break;
                case '<':  out += "&lt;"; break;
                case '>':  out += "&gt;"; break;
                case '"':  out += "&quot;"; break;
                case '\'': out += "&#39;"; break;
                default:   out += c; break;
            }
        }
        return out;
    }

    bool parseOAuthCallbackQuery(const std::string& uriOrQuery, OAuthCallbackParams_t& outParams)
    {
        outParams = {};
        if (uriOrQuery.empty()) return false;

        std::string q = uriOrQuery;
        auto qmark = q.find('?');
        if (qmark != std::string::npos)
        {
            q = q.substr(qmark + 1);
        }

        auto space = q.find(' ');
        if (space != std::string::npos)
        {
            q = q.substr(0, space);
        }
        auto hash = q.find('#');
        if (hash != std::string::npos)
        {
            q = q.substr(0, hash);
        }

        if (q.empty()) return false;

        std::stringstream ss(q);
        std::string item;
        while (std::getline(ss, item, '&'))
        {
            if (item.empty()) continue;
            auto eq = item.find('=');
            std::string key = (eq == std::string::npos) ? item : item.substr(0, eq);
            std::string val = (eq == std::string::npos) ? "" : item.substr(eq + 1);

            key = urlDecode(key);
            val = urlDecode(val);

            if (key == "code") outParams.code = val;
            else if (key == "state") outParams.state = val;
            else if (key == "error") outParams.error = val;
            else if (key == "error_description") outParams.errorDescription = val;
        }

        return (!outParams.code.empty() || !outParams.error.empty());
    }

    std::string renderOAuthCallbackHtml(bool success, const std::string& message)
    {
        std::ostringstream ss;
        std::string escapedMsg = escapeHtml(message);
        std::string title = success ? "Google Drive Connected — Navi" : "Authentication Failed — Navi";
        std::string cardBorderColor = success ? "#2ecc71" : "#e74c3c";
        std::string badgeBg = success ? "rgba(46, 204, 113, 0.15)" : "rgba(231, 76, 60, 0.15)";
        std::string badgeColor = success ? "#2ecc71" : "#e74c3c";
        std::string badgeText = success ? "AUTHENTICATED ✓" : "SIGN-IN FAILED";

        std::string iconSvg = success
            ? "<svg width=\"48\" height=\"48\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#2ecc71\" stroke-width=\"2.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\"><path d=\"M20 6L9 17l-5-5\"/></svg>"
            : "<svg width=\"48\" height=\"48\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#e74c3c\" stroke-width=\"2.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\"><circle cx=\"12\" cy=\"12\" r=\"10\"/><line x1=\"15\" y1=\"9\" x2=\"9\" y2=\"15\"/><line x1=\"9\" y1=\"9\" x2=\"15\" y2=\"15\"/></svg>";

        ss << "<!DOCTYPE html>\n"
           << "<html lang=\"en\">\n"
           << "<head>\n"
           << "  <meta charset=\"UTF-8\">\n"
           << "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
           << "  <title>" << title << "</title>\n"
           << "  <style>\n"
           << "    body {\n"
           << "      margin: 0;\n"
           << "      padding: 0;\n"
           << "      background-color: #171a21;\n"
           << "      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif;\n"
           << "      color: #c6d4df;\n"
           << "      display: flex;\n"
           << "      align-items: center;\n"
           << "      justify-content: center;\n"
           << "      min-height: 100vh;\n"
           << "    }\n"
           << "    .auth-card {\n"
           << "      background: linear-gradient(135deg, #1e2837 0%, #171a21 100%);\n"
           << "      border: 1px solid " << cardBorderColor << ";\n"
           << "      border-radius: 12px;\n"
           << "      padding: 36px 32px;\n"
           << "      max-width: 460px;\n"
           << "      width: 90%;\n"
           << "      text-align: center;\n"
           << "      box-shadow: 0 16px 32px rgba(0,0,0,0.5);\n"
           << "    }\n"
           << "    .icon-container {\n"
           << "      display: flex;\n"
           << "      justify-content: center;\n"
           << "      margin-bottom: 20px;\n"
           << "    }\n"
           << "    h1 {\n"
           << "      font-size: 22px;\n"
           << "      font-weight: 700;\n"
           << "      color: #ffffff;\n"
           << "      margin: 0 0 12px 0;\n"
           << "    }\n"
           << "    .badge {\n"
           << "      display: inline-block;\n"
           << "      background: " << badgeBg << ";\n"
           << "      color: " << badgeColor << ";\n"
           << "      font-size: 11px;\n"
           << "      font-weight: 800;\n"
           << "      letter-spacing: 1px;\n"
           << "      padding: 4px 10px;\n"
           << "      border-radius: 4px;\n"
           << "      margin-bottom: 18px;\n"
           << "    }\n"
           << "    p.desc {\n"
           << "      font-size: 14px;\n"
           << "      line-height: 1.6;\n"
           << "      color: #8f98a0;\n"
           << "      margin: 0 0 24px 0;\n"
           << "    }\n"
           << "    .btn-close {\n"
           << "      background: #2a475e;\n"
           << "      color: #ffffff;\n"
           << "      border: none;\n"
           << "      padding: 10px 20px;\n"
           << "      border-radius: 6px;\n"
           << "      font-size: 13px;\n"
           << "      font-weight: 600;\n"
           << "      cursor: pointer;\n"
           << "      transition: background 0.2s;\n"
           << "    }\n"
           << "    .btn-close:hover { background: #3a6282; }\n"
           << "    .auto-close {\n"
           << "      font-size: 11px;\n"
           << "      color: #66c0f4;\n"
           << "      margin-top: 16px;\n"
           << "    }\n"
           << "  </style>\n"
           << "</head>\n"
           << "<body>\n"
           << "  <div class=\"auth-card\">\n"
           << "    <div class=\"icon-container\">" << iconSvg << "</div>\n"
           << "    <div class=\"badge\">" << badgeText << "</div>\n"
           << "    <h1>" << (success ? "Connected to Google Drive" : "Authentication Unsuccessful") << "</h1>\n"
           << "    <p class=\"desc\">"
           << (escapedMsg.empty() ? (success ? "Navi Cross-Device Sync is now securely authorized. You may close this tab and return to Steam." : "Sign-in could not be completed.") : escapedMsg)
           << "</p>\n"
           << "    <button class=\"btn-close\" onclick=\"window.open('','_self',''); window.close();\">Close Window</button>\n";
        if (success)
        {
            ss << "    <div class=\"auto-close\">This window will attempt to close automatically...</div>\n"
               << "    <script>setTimeout(function() { window.open('','_self',''); window.close(); }, 2500);</script>\n";
        }
        ss << "  </div>\n"
           << "</body>\n"
           << "</html>\n";

        return ss.str();
    }

    std::string renderGDriveBrowserStartJson(const std::string& authUrl, const std::string& state)
    {
        std::ostringstream ss;
        ss << "{\n"
           << "  \"success\": true,\n"
           << "  \"authUrl\": \"" << escapeJson(authUrl) << "\",\n"
           << "  \"state\": \"" << escapeJson(state) << "\"\n"
           << "}";
        return ss.str();
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

        // Cross-Device Sync Settings
        std::string syncRole = getString("SyncRole", g_config.syncRole.get());
        std::string syncTopology = getString("SyncTopology", g_config.syncTopology.get());
        std::string syncCloudProvider = getString("SyncCloudProvider", g_config.syncCloudProvider.get());
        std::string syncWebDavUrl = getString("SyncWebDavUrl", g_config.syncWebDavUrl.get());
        std::string syncWebDavUser = getString("SyncWebDavUser", g_config.syncWebDavUser.get());

        std::string configDir = std::filesystem::path(configYamlPath).parent_path().string();
        if (configDir.empty()) configDir = ".";
        CloudCredentials_t creds;
        loadCloudCredentials(configDir, creds);

        Sync::SyncSelectionFilter_t filter = g_config.syncFilter.get();
        if (fileLoaded && node["SyncFilter"] && node["SyncFilter"].IsMap())
        {
            const auto& fn = node["SyncFilter"];
            try { if (fn["AllManifestsAndLua"]) filter.allManifestsAndLua = fn["AllManifestsAndLua"].as<bool>(); } catch (...) {}
            if (fn["ManifestAppIds"] && fn["ManifestAppIds"].IsSequence())
            {
                filter.manifestAppIds.clear();
                for (const auto& it : fn["ManifestAppIds"])
                {
                    try { filter.manifestAppIds.insert(it.as<uint32_t>()); } catch (...) {}
                }
            }
            try { if (fn["AllConfigs"]) filter.allConfigs = fn["AllConfigs"].as<bool>(); } catch (...) {}
            if (fn["ConfigKeys"] && fn["ConfigKeys"].IsSequence())
            {
                filter.configKeys.clear();
                for (const auto& it : fn["ConfigKeys"])
                {
                    try { filter.configKeys.insert(it.as<std::string>()); } catch (...) {}
                }
            }
            try { if (fn["AllCloudSaves"]) filter.allCloudSaves = fn["AllCloudSaves"].as<bool>(); } catch (...) {}
            if (fn["CloudSaveAppIds"] && fn["CloudSaveAppIds"].IsSequence())
            {
                filter.cloudSaveAppIds.clear();
                for (const auto& it : fn["CloudSaveAppIds"])
                {
                    try { filter.cloudSaveAppIds.insert(it.as<uint32_t>()); } catch (...) {}
                }
            }
            try { if (fn["AllAchievements"]) filter.allAchievements = fn["AllAchievements"].as<bool>(); } catch (...) {}
            if (fn["AchievementAppIds"] && fn["AchievementAppIds"].IsSequence())
            {
                filter.achievementAppIds.clear();
                for (const auto& it : fn["AchievementAppIds"])
                {
                    try { filter.achievementAppIds.insert(it.as<uint32_t>()); } catch (...) {}
                }
            }
            try { if (fn["AllApiKeys"]) filter.allApiKeys = fn["AllApiKeys"].as<bool>(); } catch (...) {}
            if (fn["ProviderNames"] && fn["ProviderNames"].IsSequence())
            {
                filter.providerNames.clear();
                for (const auto& it : fn["ProviderNames"])
                {
                    try { filter.providerNames.insert(it.as<std::string>()); } catch (...) {}
                }
            }
        }

        std::vector<uint32_t> manifestAppIds(filter.manifestAppIds.begin(), filter.manifestAppIds.end());
        std::sort(manifestAppIds.begin(), manifestAppIds.end());
        std::vector<std::string> configKeys(filter.configKeys.begin(), filter.configKeys.end());
        std::sort(configKeys.begin(), configKeys.end());
        std::vector<uint32_t> cloudSaveAppIds(filter.cloudSaveAppIds.begin(), filter.cloudSaveAppIds.end());
        std::sort(cloudSaveAppIds.begin(), cloudSaveAppIds.end());
        std::vector<uint32_t> achievementAppIds(filter.achievementAppIds.begin(), filter.achievementAppIds.end());
        std::sort(achievementAppIds.begin(), achievementAppIds.end());
        std::vector<std::string> providerNames(filter.providerNames.begin(), filter.providerNames.end());
        std::sort(providerNames.begin(), providerNames.end());

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

        auto formatStringArray = [](const std::vector<std::string>& vec) -> std::string {
            std::string s = "[";
            for (size_t i = 0; i < vec.size(); ++i)
            {
                if (i > 0) s += ", ";
                s += "\"" + escapeJson(vec[i]) + "\"";
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
        ss << "  \"SyncRole\": \"" << escapeJson(syncRole) << "\",\n";
        ss << "  \"SyncTopology\": \"" << escapeJson(syncTopology) << "\",\n";
        ss << "  \"SyncCloudProvider\": \"" << escapeJson(syncCloudProvider) << "\",\n";
        ss << "  \"SyncWebDavUrl\": \"" << escapeJson(syncWebDavUrl) << "\",\n";
        ss << "  \"SyncWebDavUser\": \"" << escapeJson(syncWebDavUser) << "\",\n";
        ss << "  \"SyncGDriveClientId\": \"" << escapeJson(creds.gdriveClientId) << "\",\n";
        ss << "  \"SyncGDriveClientSecret\": \"" << escapeJson(creds.gdriveClientSecret) << "\",\n";
        ss << "  \"SyncWebDavPass\": \"" << escapeJson(creds.webdavPassword) << "\",\n";
        ss << "  \"GDriveAuthenticated\": " << ((!creds.gdriveRefreshToken.empty() || !creds.gdriveAccessToken.empty()) ? "true" : "false") << ",\n";
        ss << "  \"SyncFilter\": {\n";
        ss << "    \"AllManifestsAndLua\": " << (filter.allManifestsAndLua ? "true" : "false") << ",\n";
        ss << "    \"ManifestAppIds\": " << formatUint32Array(manifestAppIds) << ",\n";
        ss << "    \"AllConfigs\": " << (filter.allConfigs ? "true" : "false") << ",\n";
        ss << "    \"ConfigKeys\": " << formatStringArray(configKeys) << ",\n";
        ss << "    \"AllCloudSaves\": " << (filter.allCloudSaves ? "true" : "false") << ",\n";
        ss << "    \"CloudSaveAppIds\": " << formatUint32Array(cloudSaveAppIds) << ",\n";
        ss << "    \"AllAchievements\": " << (filter.allAchievements ? "true" : "false") << ",\n";
        ss << "    \"AchievementAppIds\": " << formatUint32Array(achievementAppIds) << ",\n";
        ss << "    \"AllApiKeys\": " << (filter.allApiKeys ? "true" : "false") << ",\n";
        ss << "    \"ProviderNames\": " << formatStringArray(providerNames) << "\n";
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

        for (const char* sKey : {"FakeName", "FakeEmail", "RyuuKey", "DepotBoxKey", "HubcapKey",
                                 "SyncRole", "SyncTopology", "SyncCloudProvider", "SyncWebDavUrl", "SyncWebDavUser"})
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

        // Cross-Device Sync Filter
        if (parsed["SyncFilter"] && parsed["SyncFilter"].IsMap())
        {
            YAML::Node fn(YAML::NodeType::Map);
            const auto& sf = parsed["SyncFilter"];
            try { if (sf["AllManifestsAndLua"]) fn["AllManifestsAndLua"] = sf["AllManifestsAndLua"].as<bool>(); } catch (...) {}

            YAML::Node mfSeq(YAML::NodeType::Sequence);
            if (sf["ManifestAppIds"] && sf["ManifestAppIds"].IsSequence())
            {
                for (const auto& item : sf["ManifestAppIds"])
                {
                    try { mfSeq.push_back(item.as<uint32_t>()); } catch (...) {}
                }
            }
            fn["ManifestAppIds"] = mfSeq;

            try { if (sf["AllConfigs"]) fn["AllConfigs"] = sf["AllConfigs"].as<bool>(); } catch (...) {}
            YAML::Node cfgSeq(YAML::NodeType::Sequence);
            if (sf["ConfigKeys"] && sf["ConfigKeys"].IsSequence())
            {
                for (const auto& item : sf["ConfigKeys"])
                {
                    try { cfgSeq.push_back(item.as<std::string>()); } catch (...) {}
                }
            }
            fn["ConfigKeys"] = cfgSeq;

            try { if (sf["AllCloudSaves"]) fn["AllCloudSaves"] = sf["AllCloudSaves"].as<bool>(); } catch (...) {}
            YAML::Node csSeq(YAML::NodeType::Sequence);
            if (sf["CloudSaveAppIds"] && sf["CloudSaveAppIds"].IsSequence())
            {
                for (const auto& item : sf["CloudSaveAppIds"])
                {
                    try { csSeq.push_back(item.as<uint32_t>()); } catch (...) {}
                }
            }
            fn["CloudSaveAppIds"] = csSeq;

            try { if (sf["AllAchievements"]) fn["AllAchievements"] = sf["AllAchievements"].as<bool>(); } catch (...) {}
            YAML::Node achSeq(YAML::NodeType::Sequence);
            if (sf["AchievementAppIds"] && sf["AchievementAppIds"].IsSequence())
            {
                for (const auto& item : sf["AchievementAppIds"])
                {
                    try { achSeq.push_back(item.as<uint32_t>()); } catch (...) {}
                }
            }
            fn["AchievementAppIds"] = achSeq;

            try { if (sf["AllApiKeys"]) fn["AllApiKeys"] = sf["AllApiKeys"].as<bool>(); } catch (...) {}
            YAML::Node provSeq(YAML::NodeType::Sequence);
            if (sf["ProviderNames"] && sf["ProviderNames"].IsSequence())
            {
                for (const auto& item : sf["ProviderNames"])
                {
                    try { provSeq.push_back(item.as<std::string>()); } catch (...) {}
                }
            }
            fn["ProviderNames"] = provSeq;

            rootNode["SyncFilter"] = fn;
        }

        // Sensitive Cloud Credentials (saved to 0600 sync_keys/cloud_creds.yaml)
        std::string configDir = std::filesystem::path(configYamlPath).parent_path().string();
        if (configDir.empty()) configDir = ".";
        CloudCredentials_t creds;
        loadCloudCredentials(configDir, creds);
        bool credsModified = false;

        if (parsed["SyncGDriveClientId"])
        {
            creds.gdriveClientId = parsed["SyncGDriveClientId"].as<std::string>();
            credsModified = true;
        }
        if (parsed["SyncGDriveClientSecret"])
        {
            creds.gdriveClientSecret = parsed["SyncGDriveClientSecret"].as<std::string>();
            credsModified = true;
        }
        if (parsed["SyncWebDavPass"])
        {
            creds.webdavPassword = parsed["SyncWebDavPass"].as<std::string>();
            credsModified = true;
        }

        if (credsModified)
        {
            saveCloudCredentials(configDir, creds);
        }

        // Clean out sensitive cloud credentials from plaintext config.yaml
        rootNode.remove("SyncGDriveClientId");
        rootNode.remove("SyncGDriveClientSecret");
        rootNode.remove("SyncWebDavPass");

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
        return true;
    }
}

