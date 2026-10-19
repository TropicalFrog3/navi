#include "storeinject_view.hpp"
#include "atomic_file.hpp"
#include "log.hpp"
#include "depotkeys.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <cstring>
#include <algorithm>

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
}
