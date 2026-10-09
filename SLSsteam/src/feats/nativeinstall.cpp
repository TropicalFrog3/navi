#include "nativeinstall.hpp"
#include "manifestcode.hpp"
#include "depotkeys.hpp"
#include "../log.hpp"
#include "../config.hpp"
#include "../atomic_file.hpp"
#include "../CppAccela/acceladownload.hpp"
#include "../CppAccela/accelaluaparser.hpp"
#include "../CppAccela/accelapath.hpp"

#include <yaml-cpp/yaml.h>
#include <filesystem>
#include <sstream>
#include <algorithm>

namespace NativeInstall
{
    static std::unordered_map<uint32_t, Selection> s_selections;
    static std::vector<QueuedInstall> s_queuedInstalls;
    static std::mutex s_mutex;
    static bool s_initialized = false;

    static std::string getPersistPath()
    {
        return g_config.getDir() + "/native_installs.yaml";
    }

    void load()
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        std::string path = getPersistPath();
        if (!std::filesystem::exists(path)) return;

        try
        {
            YAML::Node root = YAML::LoadFile(path);
            if (!root || !root.IsMap()) return;

            s_selections.clear();
            for (const auto& it : root)
            {
                uint32_t appId = it.first.as<uint32_t>();
                YAML::Node node = it.second;
                Selection sel;

                if (node["depots"] && node["depots"].IsSequence())
                {
                    for (const auto& d : node["depots"])
                    {
                        sel.depots.insert(d.as<uint32_t>());
                    }
                }

                if (node["gid"] && node["gid"].IsMap())
                {
                    for (const auto& g : node["gid"])
                    {
                        sel.gid[g.first.as<uint32_t>()] = g.second.as<uint64_t>();
                    }
                }

                if (node["size"] && node["size"].IsMap())
                {
                    for (const auto& s : node["size"])
                    {
                        sel.size[s.first.as<uint32_t>()] = s.second.as<uint64_t>();
                    }
                }

                s_selections[appId] = std::move(sel);
                LOG_INFO("NativeInstall: Loaded persisted selection for appId=%u (%zu depots)\n",
                         appId, s_selections[appId].depots.size());
            }
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("NativeInstall: Failed to load %s: %s\n", path.c_str(), e.what());
        }
    }

    void save()
    {
        std::string path = getPersistPath();
        YAML::Emitter out;
        out << YAML::BeginMap;

        {
            std::lock_guard<std::mutex> lock(s_mutex);
            for (const auto& [appId, sel] : s_selections)
            {
                out << YAML::Key << appId << YAML::Value << YAML::BeginMap;

                out << YAML::Key << "depots" << YAML::Value << YAML::BeginSeq;
                for (uint32_t d : sel.depots)
                {
                    out << d;
                }
                out << YAML::EndSeq;

                out << YAML::Key << "gid" << YAML::Value << YAML::BeginMap;
                for (const auto& [depId, gid] : sel.gid)
                {
                    out << YAML::Key << depId << YAML::Value << gid;
                }
                out << YAML::EndMap;

                out << YAML::Key << "size" << YAML::Value << YAML::BeginMap;
                for (const auto& [depId, sz] : sel.size)
                {
                    out << YAML::Key << depId << YAML::Value << sz;
                }
                out << YAML::EndMap;

                out << YAML::EndMap;
            }
        }

        out << YAML::EndMap;

        if (!AtomicFile::write(path, out.c_str(), true))
        {
            LOG_ERROR("NativeInstall: Failed to write %s\n", path.c_str());
        }
    }

    void init()
    {
        if (s_initialized) return;
        load();
        s_initialized = true;
    }

    bool arm(uint32_t appId, const std::string& csvDepots)
    {
        init();

        CppAccela::Download::CachedRequest cached;
        if (!CppAccela::Download::takeCachedInstall(appId, cached))
        {
            LOG_WARN("NativeInstall: arm failed, no cached install request for appId=%u\n", appId);
            return false;
        }

        Selection sel;

        // Parse user-selected depots from comma-separated string
        std::stringstream ss(csvDepots);
        std::string item;
        while (std::getline(ss, item, ','))
        {
            while (!item.empty() && isspace(static_cast<unsigned char>(item.front()))) item.erase(item.begin());
            while (!item.empty() && isspace(static_cast<unsigned char>(item.back()))) item.pop_back();
            if (!item.empty())
            {
                try
                {
                    sel.depots.insert(std::stoul(item));
                }
                catch (...) {}
            }
        }

        LOG_INFO("NativeInstall: arming appId=%u with %zu depots: %s\n",
                 appId, sel.depots.size(), csvDepots.c_str());

        // Parse Lua plugin to collect manifest GIDs and sizes
        std::string luaPath = CppAccela::Path::stplugDir() + "/" + std::to_string(appId) + ".lua";
        if (std::filesystem::exists(luaPath))
        {
            auto res = CppAccela::LuaParser::parseFile(luaPath);
            for (const auto& [depotStr, gidStr] : res.manifests)
            {
                try
                {
                    sel.gid[std::stoul(depotStr)] = std::stoull(gidStr);
                }
                catch (...) {}
            }
            for (const auto& [depotStr, szStr] : res.manifestSizes)
            {
                try
                {
                    sel.size[std::stoul(depotStr)] = std::stoull(szStr);
                }
                catch (...) {}
            }
        }

        // Rescan depot keys from Lua files
        DepotKeys::scanLuaPluginsForDepotKeys();

        // Phase 0: Pre-cache local manifests
        for (uint32_t depotId : sel.depots)
        {
            auto it = sel.gid.find(depotId);
            if (it != sel.gid.end() && it->second != 0)
            {
                ManifestCode::ensureManifestFile(appId, depotId, it->second);
            }
        }

        {
            std::lock_guard<std::mutex> lock(s_mutex);
            s_selections[appId] = sel;
            s_queuedInstalls.push_back({ cached.pClientAppManager, appId, cached.library, cached.a4 });
        }

        save();
        return true;
    }

    void disarm(uint32_t appId)
    {
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            s_selections.erase(appId);
            std::erase_if(s_queuedInstalls, [appId](const QueuedInstall& q) {
                return q.appId == appId;
            });
        }
        save();
        LOG_INFO("NativeInstall: disarmed and removed persisted selection for appId=%u\n", appId);
    }

    void runFrame(InstallAppFn original)
    {
        init();

        std::vector<QueuedInstall> batch;
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            if (!s_queuedInstalls.empty())
            {
                batch.swap(s_queuedInstalls);
            }
        }

        for (const auto& q : batch)
        {
            LOG_INFO("NativeInstall: Running original InstallApp on main thread for appId=%u (pMgr=%p, lib=%u, a4=%u)\n",
                     q.appId, (void*)q.pClientAppManager, q.library, q.a4);
            if (original)
            {
                original(q.pClientAppManager, q.appId, q.library, q.a4);
            }
        }
    }

    std::optional<Selection> getSelection(uint32_t appId)
    {
        init();
        std::lock_guard<std::mutex> lock(s_mutex);
        auto it = s_selections.find(appId);
        if (it != s_selections.end())
        {
            return it->second;
        }
        return std::nullopt;
    }

    bool isNative(uint32_t appId)
    {
        init();
        std::lock_guard<std::mutex> lock(s_mutex);
        return s_selections.contains(appId);
    }

    bool isQueuedOrArmed(uint32_t appId)
    {
        init();
        std::lock_guard<std::mutex> lock(s_mutex);
        if (s_selections.contains(appId)) return true;
        for (const auto& q : s_queuedInstalls)
        {
            if (q.appId == appId) return true;
        }
        return false;
    }
}
