#include "cloudredirect.hpp"
#include "atomic_file.hpp"
#include "log.hpp"
#include "paths.hpp"

#include "yaml-cpp/yaml.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fstream>
#include <sstream>
#include <string_view>
#include <system_error>
#include <vector>

#include "libmem/libmem.h"

extern "C" lm_bool_t LM_FindModule(lm_string_t name, lm_module_t *module_out) __attribute__((weak));

namespace Navi::CloudRedirect
{
    static std::string StripSentinelsAndShimKeys(const std::string& input)
    {
        std::istringstream stream(input);
        std::string line;
        std::ostringstream out;
        bool inSentinelBlock = false;
        bool inAdditionalAppsBlock = false;

        while (std::getline(stream, line))
        {
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }

            if (line.find("# >>> NAVI CLOUD SHIM") != std::string::npos)
            {
                inSentinelBlock = true;
                continue;
            }
            if (inSentinelBlock)
            {
                if (line.find("# <<< NAVI CLOUD SHIM") != std::string::npos)
                {
                    inSentinelBlock = false;
                }
                continue;
            }

            // Check if top-level DisableCloud:
            size_t firstNonWs = line.find_first_not_of(" \t");
            std::string_view trimmed = (firstNonWs != std::string::npos) ? std::string_view(line.data() + firstNonWs, line.size() - firstNonWs) : std::string_view();

            if (trimmed.starts_with("DisableCloud:") || trimmed.starts_with("DisableCloud "))
            {
                continue;
            }

            // Check if top-level AdditionalApps:
            if (trimmed.starts_with("AdditionalApps:") || trimmed.starts_with("AdditionalApps "))
            {
                if (line.find('[') != std::string::npos)
                {
                    // Inline sequence e.g. AdditionalApps: [] or [1, 2]
                    continue;
                }
                inAdditionalAppsBlock = true;
                continue;
            }

            if (inAdditionalAppsBlock)
            {
                // Indented line or empty line is part of AdditionalApps sequence
                if (line.empty() || line[0] == ' ' || line[0] == '\t')
                {
                    continue;
                }
                else
                {
                    inAdditionalAppsBlock = false;
                    // fall through to process new top-level line
                }
            }

            out << line << "\n";
        }

        std::string res = out.str();
        while (!res.empty() && (res.back() == '\n' || res.back() == ' ' || res.back() == '\t' || res.back() == '\r'))
        {
            res.pop_back();
        }
        return res;
    }

    CCloudRedirectBridge& CCloudRedirectBridge::Instance()
    {
        static CCloudRedirectBridge instance;
        return instance;
    }

    PlatformTier CCloudRedirectBridge::DetectPlatformTier() const
    {
        std::error_code ec;
        if (std::filesystem::exists("/.flatpak-info", ec) || std::getenv("FLATPAK_ID") != nullptr)
        {
            return PlatformTier::Tier2_Flatpak;
        }

        if (std::filesystem::exists("/etc/os-release", ec))
        {
            std::ifstream osRelease("/etc/os-release");
            if (osRelease.is_open())
            {
                std::string line;
                while (std::getline(osRelease, line))
                {
                    if (line.find("steamos") != std::string::npos || line.find("SteamOS") != std::string::npos)
                    {
                        return PlatformTier::Tier1_SteamOS;
                    }
                }
            }
        }

        return PlatformTier::Tier1_NativeDesktop;
    }

    ResolvedPaths_t CCloudRedirectBridge::ResolvePaths() const
    {
        ResolvedPaths_t paths;
        paths.tier = DetectPlatformTier();

        const char* homeEnv = std::getenv("HOME");
        std::filesystem::path home = (homeEnv && strlen(homeEnv) > 0) ? homeEnv : "/tmp";

        const char* xdgConfigEnv = std::getenv("XDG_CONFIG_HOME");
        const char* xdgDataEnv = std::getenv("XDG_DATA_HOME");

        if (paths.tier == PlatformTier::Tier2_Flatpak)
        {
            std::error_code ec;
            if (std::filesystem::exists("/.flatpak-info", ec))
            {
                paths.configHome = (xdgConfigEnv && strlen(xdgConfigEnv) > 0) ? std::filesystem::path(xdgConfigEnv) : (home / ".config");
                paths.dataHome = (xdgDataEnv && strlen(xdgDataEnv) > 0) ? std::filesystem::path(xdgDataEnv) : (home / ".local" / "share");
            }
            else
            {
                paths.configHome = home / ".var" / "app" / "com.valvesoftware.Steam" / ".config";
                paths.dataHome = home / ".var" / "app" / "com.valvesoftware.Steam" / ".local" / "share";
            }
        }
        else
        {
            paths.configHome = (xdgConfigEnv && strlen(xdgConfigEnv) > 0) ? std::filesystem::path(xdgConfigEnv) : (home / ".config");
            paths.dataHome = (xdgDataEnv && strlen(xdgDataEnv) > 0) ? std::filesystem::path(xdgDataEnv) : (home / ".local" / "share");
        }

        paths.slsSteamDir = paths.configHome / "SLSsteam";
        paths.slsSteamConfig = paths.slsSteamDir / "config.yaml";
        paths.crConfigDir = paths.configHome / "CloudRedirect";
        paths.crConfigFile = paths.crConfigDir / "config.json";
        paths.crKillswitchFile = paths.crConfigDir / "disable";
        paths.crPendingOpsFile = paths.crConfigDir / "cloud_pending_ops.json";
        paths.naviConfigDir = paths.configHome / "navi";
        paths.naviEnabledFlag = paths.naviConfigDir / "cloud_enabled";
        paths.naviBackupDir = paths.naviConfigDir / "cloud_backups";
        paths.binDir = paths.dataHome / "navi" / "cloudredirect";
        paths.engineSo = paths.binDir / "cloud_redirect.so";
        paths.cliBin = paths.binDir / "cloud_redirect_cli";

        std::string root = Paths::findSteamRoot();
        if (!root.empty())
        {
            paths.steamRoot = root;
        }
        else
        {
            paths.steamRoot = paths.dataHome / "Steam";
        }
        paths.storageDir = paths.steamRoot / "cloud_redirect" / "storage";

        return paths;
    }

    const ResolvedPaths_t& CCloudRedirectBridge::GetPaths() const
    {
        std::lock_guard<std::mutex> lock(m_pathsMutex);
        m_cachedPaths = ResolvePaths();
        return m_cachedPaths;
    }

    bool CCloudRedirectBridge::IsEnabled() const
    {
        const auto& paths = GetPaths();
        std::error_code ec;
        return std::filesystem::exists(paths.naviEnabledFlag, ec) && !std::filesystem::exists(paths.crKillswitchFile, ec);
    }

    bool CCloudRedirectBridge::LoadEngine()
    {
        if (!IsEnabled())
        {
            return false;
        }

        const auto paths = GetPaths();
        std::error_code ec;
        if (!std::filesystem::exists(paths.engineSo, ec))
        {
            LOG_WARN("CloudRedirect engine not found: %s\n", paths.engineSo.c_str());
            return false;
        }

        lm_module_t mod{};
        if (LM_FindModule != nullptr && LM_FindModule("cloud_redirect.so", &mod))
        {
            return true;
        }

        void* handle = dlopen(paths.engineSo.c_str(), RTLD_NOW | RTLD_GLOBAL);
        if (!handle)
        {
            LOG_WARN("Failed to dlopen CloudRedirect engine: %s\n", dlerror());
            return false;
        }

        LOG_INFO("CloudRedirect engine loaded successfully at %p\n", handle);
        return true;
    }

    bool CCloudRedirectBridge::DetectStandaloneInstallation() const
    {
        const auto& paths = GetPaths();
        std::error_code ec;

        std::ifstream maps("/proc/self/maps");
        if (maps.is_open())
        {
            std::string line;
            while (std::getline(maps, line))
            {
                if (line.find("cloud_redirect.so") != std::string::npos)
                {
                    if (line.find("/.local/share/navi/") == std::string::npos)
                    {
                        return true;
                    }
                }
            }
        }

        if (std::filesystem::exists(paths.crConfigDir / "cloud_redirect.db", ec))
        {
            return true;
        }

        if (std::filesystem::exists(paths.crConfigFile, ec) && !std::filesystem::exists(paths.naviEnabledFlag, ec))
        {
            try
            {
                if (std::filesystem::file_size(paths.crConfigFile, ec) > 2)
                {
                    return true;
                }
            }
            catch (...) {}
        }

        return false;
    }

    bool CCloudRedirectBridge::SetEnabledState(bool enable)
    {
        const auto& paths = GetPaths();
        std::error_code ec;

        if (enable)
        {
            if (std::filesystem::exists(paths.crKillswitchFile, ec))
            {
                std::filesystem::remove(paths.crKillswitchFile, ec);
                if (ec)
                {
                    LOG_WARN("Failed to remove CloudRedirect killswitch: %s\n", ec.message().c_str());
                    return false;
                }
            }

            if (!std::filesystem::exists(paths.naviConfigDir, ec))
            {
                std::filesystem::create_directories(paths.naviConfigDir, ec);
                if (ec)
                {
                    LOG_WARN("Failed to create navi config dir: %s\n", ec.message().c_str());
                    return false;
                }
            }

            {
                std::ofstream flag(paths.naviEnabledFlag, std::ios::out | std::ios::app);
                if (!flag.is_open())
                {
                    LOG_WARN("Failed to touch navi cloud_enabled flag\n");
                    return false;
                }
            }
            return true;
        }
        else
        {
            if (!std::filesystem::exists(paths.crConfigDir, ec))
            {
                std::filesystem::create_directories(paths.crConfigDir, ec);
                if (ec)
                {
                    LOG_WARN("Failed to create CloudRedirect config dir: %s\n", ec.message().c_str());
                    return false;
                }
            }

            {
                std::ofstream disableFile(paths.crKillswitchFile, std::ios::out | std::ios::app);
                if (!disableFile.is_open())
                {
                    LOG_WARN("Failed to touch CloudRedirect disable killswitch\n");
                    return false;
                }
            }

            if (std::filesystem::exists(paths.naviEnabledFlag, ec))
            {
                std::filesystem::remove(paths.naviEnabledFlag, ec);
                if (ec)
                {
                    LOG_WARN("Failed to remove navi cloud_enabled flag: %s\n", ec.message().c_str());
                    return false;
                }
            }
            return true;
        }
    }

    bool CCloudRedirectBridge::SyncSlsSteamConfig(const std::unordered_set<AppId_t>& managedApps)
    {
        const auto& paths = GetPaths();
        std::error_code ec;

        if (!std::filesystem::exists(paths.slsSteamDir, ec))
        {
            std::filesystem::create_directories(paths.slsSteamDir, ec);
            if (ec)
            {
                LOG_WARN("Failed to create SLSsteam dir: %s\n", ec.message().c_str());
                return false;
            }
        }

        std::unordered_set<AppId_t> mergedApps = managedApps;
        std::string userRemaining;

        if (std::filesystem::exists(paths.slsSteamConfig, ec))
        {
            std::ifstream ifs(paths.slsSteamConfig);
            if (ifs.is_open())
            {
                std::stringstream ss;
                ss << ifs.rdbuf();
                std::string content = ss.str();
                ifs.close();

                try
                {
                    YAML::Node doc = YAML::Load(content);
                    if (doc && doc.IsMap() && doc["AdditionalApps"])
                    {
                        const auto& appsNode = doc["AdditionalApps"];
                        if (appsNode.IsSequence())
                        {
                            for (const auto& item : appsNode)
                            {
                                try
                                {
                                    mergedApps.insert(item.as<AppId_t>());
                                }
                                catch (...) {}
                            }
                        }
                    }
                }
                catch (const YAML::Exception& ye)
                {
                    LOG_WARN("YAML parse warning reading SLSsteam config: %s\n", ye.what());
                }

                userRemaining = StripSentinelsAndShimKeys(content);
            }
        }

        std::ostringstream out;
        if (!userRemaining.empty())
        {
            out << userRemaining << "\n\n";
        }

        out << "# >>> NAVI CLOUD SHIM (DO NOT EDIT MANUALLY)\n";
        out << "DisableCloud: no\n";
        if (mergedApps.empty())
        {
            out << "AdditionalApps: []\n";
        }
        else
        {
            out << "AdditionalApps:\n";
            std::vector<AppId_t> sortedApps(mergedApps.begin(), mergedApps.end());
            std::sort(sortedApps.begin(), sortedApps.end());
            for (AppId_t id : sortedApps)
            {
                out << "  - " << id << "\n";
            }
        }
        out << "# <<< NAVI CLOUD SHIM\n";

        return AtomicFile::write(paths.slsSteamConfig.string(), out.str());
    }

    bool CCloudRedirectBridge::RevertSlsSteamConfig(const std::unordered_set<AppId_t>& managedApps)
    {
        const auto& paths = GetPaths();
        std::error_code ec;

        if (!std::filesystem::exists(paths.slsSteamConfig, ec))
        {
            return true;
        }

        std::ifstream ifs(paths.slsSteamConfig);
        if (!ifs.is_open())
        {
            return false;
        }
        std::stringstream ss;
        ss << ifs.rdbuf();
        std::string content = ss.str();
        ifs.close();

        std::unordered_set<AppId_t> allApps;
        try
        {
            YAML::Node doc = YAML::Load(content);
            if (doc && doc.IsMap() && doc["AdditionalApps"])
            {
                const auto& appsNode = doc["AdditionalApps"];
                if (appsNode.IsSequence())
                {
                    for (const auto& item : appsNode)
                    {
                        try
                        {
                            allApps.insert(item.as<AppId_t>());
                        }
                        catch (...) {}
                    }
                }
            }
        }
        catch (const YAML::Exception& ye)
        {
            LOG_WARN("YAML parse warning reverting SLSsteam config: %s\n", ye.what());
        }

        std::unordered_set<AppId_t> remainingApps;
        for (AppId_t id : allApps)
        {
            if (!managedApps.contains(id))
            {
                remainingApps.insert(id);
            }
        }

        std::string userRemaining = StripSentinelsAndShimKeys(content);

        bool hasOtherUserKeys = false;
        if (!userRemaining.empty())
        {
            try
            {
                YAML::Node userDoc = YAML::Load(userRemaining);
                if (userDoc && userDoc.IsMap() && userDoc.size() > 0)
                {
                    hasOtherUserKeys = true;
                }
            }
            catch (...) {}
        }

        if (!hasOtherUserKeys && remainingApps.empty())
        {
            std::filesystem::remove(paths.slsSteamConfig, ec);
            return !ec;
        }

        std::ostringstream out;
        if (!userRemaining.empty())
        {
            out << userRemaining;
        }

        if (!remainingApps.empty())
        {
            if (!userRemaining.empty())
            {
                out << "\n\n";
            }
            out << "AdditionalApps:\n";
            std::vector<AppId_t> sortedApps(remainingApps.begin(), remainingApps.end());
            std::sort(sortedApps.begin(), sortedApps.end());
            for (AppId_t id : sortedApps)
            {
                out << "  - " << id << "\n";
            }
        }
        else
        {
            out << "\n";
        }

        return AtomicFile::write(paths.slsSteamConfig.string(), out.str());
    }

    static std::string EscapeJsonStr(const std::string& s)
    {
        std::string out;
        out.reserve(s.size() + 4);
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
                default:   out += c; break;
            }
        }
        return out;
    }

    static bool IsNumericScalar(const std::string& s)
    {
        if (s.empty()) return false;
        size_t i = (s[0] == '-') ? 1 : 0;
        if (i >= s.size()) return false;
        bool hasDot = false;
        for (; i < s.size(); ++i)
        {
            if (s[i] == '.')
            {
                if (hasDot) return false;
                hasDot = true;
            }
            else if (!std::isdigit(static_cast<unsigned char>(s[i])))
            {
                return false;
            }
        }
        return true;
    }

    static void EmitYamlNodeAsJson(std::ostream& os, const YAML::Node& node)
    {
        if (!node || node.IsNull())
        {
            os << "null";
            return;
        }
        if (node.IsScalar())
        {
            const std::string s = node.Scalar();
            if (node.Tag() != "!" && (s == "true" || s == "false"))
            {
                os << s;
            }
            else if (node.Tag() != "!" && IsNumericScalar(s))
            {
                os << s;
            }
            else
            {
                os << "\"" << EscapeJsonStr(s) << "\"";
            }
            return;
        }
        if (node.IsSequence())
        {
            os << "[";
            for (size_t i = 0; i < node.size(); ++i)
            {
                if (i > 0) os << ", ";
                EmitYamlNodeAsJson(os, node[i]);
            }
            os << "]";
            return;
        }
        if (node.IsMap())
        {
            os << "{";
            bool first = true;
            for (auto it = node.begin(); it != node.end(); ++it)
            {
                if (!first) os << ", ";
                first = false;
                os << "\"" << EscapeJsonStr(it->first.Scalar()) << "\": ";
                EmitYamlNodeAsJson(os, it->second);
            }
            os << "}";
            return;
        }
        os << "null";
    }

    static bool ParseYamlBoolScalar(const YAML::Node& node, bool defVal)
    {
        if (!node || !node.IsScalar()) return defVal;
        const std::string s = node.Scalar();
        if (s == "1" || s == "true" || s == "True" || s == "TRUE" ||
            s == "yes" || s == "Yes" || s == "YES" || s == "on" || s == "On" || s == "ON")
        {
            return true;
        }
        if (s == "0" || s == "false" || s == "False" || s == "FALSE" ||
            s == "no" || s == "No" || s == "NO" || s == "off" || s == "Off" || s == "OFF")
        {
            return false;
        }
        try { return node.as<bool>(); } catch (...) {}
        return defVal;
    }

    bool CCloudRedirectBridge::EnsureLocalOnlyConfig()
    {
        const auto& paths = GetPaths();
        std::error_code ec;

        if (std::filesystem::exists(paths.crConfigFile, ec))
        {
            return true;
        }

        if (!std::filesystem::exists(paths.crConfigDir, ec))
        {
            std::filesystem::create_directories(paths.crConfigDir, ec);
            if (ec)
            {
                LOG_WARN("Failed to create CloudRedirect config dir: %s\n", ec.message().c_str());
                return false;
            }
        }

        const std::string minimalConfig = "{\n  \"provider\": \"local\"\n}\n";
        return AtomicFile::write(paths.crConfigFile.string(), minimalConfig);
    }

    bool CCloudRedirectBridge::SyncEngineConfig(const EngineConfig_t& cfg)
    {
        const auto& paths = GetPaths();
        std::error_code ec;

        if (!std::filesystem::exists(paths.crConfigDir, ec))
        {
            std::filesystem::create_directories(paths.crConfigDir, ec);
            if (ec)
            {
                LOG_WARN("Failed to create CloudRedirect config dir: %s\n", ec.message().c_str());
                return false;
            }
        }

        std::string normProv = (cfg.provider.empty() || cfg.provider == "local-only") ? "local" : cfg.provider;

        YAML::Node existingDoc;
        bool hasExistingMap = false;
        if (std::filesystem::exists(paths.crConfigFile, ec))
        {
            try
            {
                existingDoc = YAML::LoadFile(paths.crConfigFile.string());
                if (existingDoc && existingDoc.IsMap())
                {
                    hasExistingMap = true;
                }
            }
            catch (...) {}
        }

        std::ostringstream out;
        out << "{\n"
            << "  \"provider\": \"" << EscapeJsonStr(normProv) << "\",\n"
            << "  \"notifications_enabled\": " << (cfg.notificationsEnabled ? "true" : "false") << ",\n"
            << "  \"stats_sync_enabled\": " << (cfg.statsSyncEnabled ? "true" : "false") << ",\n"
            << "  \"sync_achievements\": " << (cfg.syncAchievements ? "true" : "false") << ",\n"
            << "  \"sync_playtime\": " << (cfg.syncPlaytime ? "true" : "false");

        if (hasExistingMap)
        {
            for (auto it = existingDoc.begin(); it != existingDoc.end(); ++it)
            {
                if (!it->first.IsScalar()) continue;
                const std::string k = it->first.Scalar();
                if (k == "provider" ||
                    k == "notifications_enabled" ||
                    k == "stats_sync_enabled" ||
                    k == "sync_achievements" ||
                    k == "sync_playtime")
                {
                    continue;
                }
                out << ",\n  \"" << EscapeJsonStr(k) << "\": ";
                EmitYamlNodeAsJson(out, it->second);
            }
        }

        out << "\n}\n";
        return AtomicFile::write(paths.crConfigFile.string(), out.str());
    }

    EngineConfig_t CCloudRedirectBridge::ReadEngineConfig() const
    {
        EngineConfig_t cfg;
        const auto& paths = GetPaths();
        std::error_code ec;

        if (!std::filesystem::exists(paths.crConfigFile, ec))
        {
            return cfg;
        }

        try
        {
            YAML::Node doc = YAML::LoadFile(paths.crConfigFile.string());
            if (doc && doc.IsMap())
            {
                if (doc["provider"] && doc["provider"].IsScalar())
                {
                    std::string p = doc["provider"].as<std::string>();
                    cfg.provider = (p.empty() || p == "local-only") ? "local" : p;
                }
                if (doc["notifications_enabled"])
                {
                    cfg.notificationsEnabled = ParseYamlBoolScalar(doc["notifications_enabled"], true);
                }
                if (doc["stats_sync_enabled"])
                {
                    cfg.statsSyncEnabled = ParseYamlBoolScalar(doc["stats_sync_enabled"], false);
                }
                if (doc["sync_achievements"])
                {
                    cfg.syncAchievements = ParseYamlBoolScalar(doc["sync_achievements"], true);
                }
                if (doc["sync_playtime"])
                {
                    cfg.syncPlaytime = ParseYamlBoolScalar(doc["sync_playtime"], true);
                }
            }
        }
        catch (...) {}

        return cfg;
    }

    std::filesystem::path CCloudRedirectBridge::GetTokenPath(const std::string& provider) const
    {
        const auto& paths = GetPaths();
        return paths.crConfigDir / ("tokens_" + provider + ".json");
    }
} // namespace Navi::CloudRedirect
