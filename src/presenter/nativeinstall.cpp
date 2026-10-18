#include "nativeinstall.hpp"
#include "manifestcode.hpp"
#include "depotkeys.hpp"
#include "luagenerator.hpp"
#include "log.hpp"
#include "config.hpp"
#include "atomic_file.hpp"
#include "lua_parser.hpp"
#include "paths.hpp"

#include <yaml-cpp/yaml.h>
#include <filesystem>
#include <sstream>
#include <algorithm>

namespace NativeInstall
{
    static std::unordered_map<uint32_t, std::shared_ptr<Selection>> s_selections;
    static std::vector<QueuedInstall> s_queuedInstalls;
    static std::unordered_map<uint32_t, QueuedInstall> s_cachedRequests;
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

                s_selections[appId] = std::make_shared<Selection>(std::move(sel));
                LOG_INFO("NativeInstall: Loaded persisted selection for appId=%u (%zu depots)\n",
                         appId, s_selections[appId]->depots.size());
            }
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("NativeInstall: Failed to load %s: %s\n", path.c_str(), e.what());
        }
    }

    void save()
    {
        try
        {
            std::string path = getPersistPath();
            YAML::Emitter out;
            out << YAML::BeginMap;

            {
                std::lock_guard<std::mutex> lock(s_mutex);
                for (const auto& [appId, pSel] : s_selections)
                {
                    out << YAML::Key << appId << YAML::Value << YAML::BeginMap;

                    out << YAML::Key << "depots" << YAML::Value << YAML::BeginSeq;
                    for (uint32_t d : pSel->depots)
                    {
                        out << d;
                    }
                    out << YAML::EndSeq;

                    out << YAML::Key << "gid" << YAML::Value << YAML::BeginMap;
                    for (const auto& [depId, gid] : pSel->gid)
                    {
                        out << YAML::Key << depId << YAML::Value << gid;
                    }
                    out << YAML::EndMap;

                    out << YAML::Key << "size" << YAML::Value << YAML::BeginMap;
                    for (const auto& [depId, sz] : pSel->size)
                    {
                        out << YAML::Key << depId << YAML::Value << sz;
                    }
                    out << YAML::EndMap;

                    out << YAML::EndMap;
                }
            }

            out << YAML::EndMap;

            if (out.good())
            {
                if (!AtomicFile::write(path, out.c_str(), true))
                {
                    LOG_ERROR("NativeInstall: Failed to write %s\n", path.c_str());
                }
            }
            else
            {
                LOG_ERROR("NativeInstall: YAML::Emitter error: %s\n", out.GetLastError().c_str());
            }
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("NativeInstall: save failed with exception: %s\n", e.what());
        }
        catch (...)
        {
            LOG_ERROR("NativeInstall: save failed with unknown exception\n");
        }
    }

    void init()
    {
        if (s_initialized) return;
        load();
        s_initialized = true;
    }

    void cacheInstallRequest(IClientAppManager* pMgr, uint32_t appId, uint32_t library, uint8_t a4)
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_cachedRequests[appId] = { pMgr, appId, library, a4 };
        LOG_INFO("NativeInstall: cached install request for appid=%u\n", appId);
    }

    void discardCachedInstall(uint32_t appId)
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_cachedRequests.erase(appId);
    }

    bool takeCachedInstall(uint32_t appId, QueuedInstall& out)
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        auto it = s_cachedRequests.find(appId);
        if (it == s_cachedRequests.end())
        {
            return false;
        }
        out = it->second;
        s_cachedRequests.erase(it);
        return true;
    }

    bool arm(uint32_t appId, const std::string& csvDepots)
    {
        try
        {
            init();

            QueuedInstall cached;
            if (!takeCachedInstall(appId, cached))
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
                    char* pEnd = nullptr;
                    unsigned long val = std::strtoul(item.c_str(), &pEnd, 10);
                    if (pEnd != item.c_str() && val > 0)
                    {
                        sel.depots.insert(static_cast<uint32_t>(val));
                    }
                }
            }

            LOG_INFO("NativeInstall: arming appId=%u with %zu depots: %s\n",
                     appId, sel.depots.size(), csvDepots.c_str());

            // If app has keys in depot_keys.yaml, ensure Lua plugin is generated/fresh
            if (DepotKeys::hasKeysForApp(appId))
            {
                LuaGenerator::generateForApp(appId);
            }

            // Parse Lua plugin to collect manifest GIDs and sizes
            std::string luaPath = Paths::stplugDir() + "/" + std::to_string(appId) + ".lua";
            if (std::filesystem::exists(luaPath))
            {
                try
                {
                    auto res = LuaParser::parseFile(luaPath);
                    for (const auto& [depotStr, gidStr] : res.manifests)
                    {
                        if (depotStr.empty() || gidStr.empty()) continue;
                        char* pEndD = nullptr;
                        unsigned long depId = std::strtoul(depotStr.c_str(), &pEndD, 10);
                        char* pEndG = nullptr;
                        unsigned long long gid = std::strtoull(gidStr.c_str(), &pEndG, 10);
                        if (pEndD != depotStr.c_str() && pEndG != gidStr.c_str() && depId > 0 && gid > 0)
                        {
                            sel.gid[static_cast<uint32_t>(depId)] = gid;
                        }
                    }
                    for (const auto& [depotStr, szStr] : res.manifestSizes)
                    {
                        if (depotStr.empty() || szStr.empty()) continue;
                        char* pEndD = nullptr;
                        unsigned long depId = std::strtoul(depotStr.c_str(), &pEndD, 10);
                        char* pEndS = nullptr;
                        unsigned long long sz = std::strtoull(szStr.c_str(), &pEndS, 10);
                        if (pEndD != depotStr.c_str() && pEndS != szStr.c_str() && depId > 0 && sz > 0)
                        {
                            sel.size[static_cast<uint32_t>(depId)] = sz;
                        }
                    }
                }
                catch (const std::exception& e)
                {
                    LOG_WARN("NativeInstall: Error parsing lua %s: %s\n", luaPath.c_str(), e.what());
                }
                catch (...)
                {
                    LOG_WARN("NativeInstall: Unknown error parsing lua %s\n", luaPath.c_str());
                }
            }

            // Rescan depot keys from Lua files
            DepotKeys::scanLuaPluginsForDepotKeys();

            // Guard: For unowned apps, bail if any selected depot has no decryption key
            bool locallyOwned = g_pSteamEngine && g_pSteamEngine->getUser(0) && g_pSteamEngine->getUser(0)->isSubscribed(appId);
            if (!locallyOwned)
            {
                std::unordered_set<uint32_t> validDepots;
                for (uint32_t depotId : sel.depots)
                {
                    std::string key = DepotKeys::getDepotKey(depotId);
                    if (!key.empty())
                    {
                        validDepots.insert(depotId);
                    }
                    else
                    {
                        LOG_WARN("NativeInstall: Depot %u for app %u has no decryption key (skipping)\n", depotId, appId);
                    }
                }
                sel.depots = std::move(validDepots);

                if (sel.depots.empty())
                {
                    LOG_WARN("NativeInstall: BAIL! No depots with decryption keys available for unowned app %u, aborting install\n", appId);
                    return false;
                }
            }

            // Phase 0: Pre-cache local manifests
            for (uint32_t depotId : sel.depots)
            {
                auto it = sel.gid.find(depotId);
                if (it != sel.gid.end() && it->second != 0)
                {
                    try
                    {
                        ManifestCode::ensureManifestFile(appId, depotId, it->second);
                    }
                    catch (...) {}
                }
            }

            {
                std::lock_guard<std::mutex> lock(s_mutex);
                s_selections[appId] = std::make_shared<Selection>(std::move(sel));
                s_queuedInstalls.push_back({ cached.pClientAppManager, appId, cached.library, cached.a4 });
            }

            save();
            return true;
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("NativeInstall: arm exception: %s\n", e.what());
            return false;
        }
        catch (...)
        {
            LOG_ERROR("NativeInstall: arm unknown exception\n");
            return false;
        }
    }

    void disarm(uint32_t appId)
    {
        try
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
        catch (...) {}
    }

    void runFrame(InstallAppFn original)
    {
        try
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
        catch (const std::exception& e)
        {
            LOG_ERROR("NativeInstall: runFrame exception: %s\n", e.what());
        }
        catch (...)
        {
            LOG_ERROR("NativeInstall: runFrame unknown exception\n");
        }
    }

    std::shared_ptr<const Selection> getSelection(uint32_t appId)
    {
        init();
        std::lock_guard<std::mutex> lock(s_mutex);
        auto it = s_selections.find(appId);
        if (it != s_selections.end())
        {
            return it->second;
        }
        return nullptr;
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
