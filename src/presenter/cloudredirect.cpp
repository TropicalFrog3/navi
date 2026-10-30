#include "cloudredirect.hpp"
#include "atomic_file.hpp"
#include "log.hpp"
#include "paths.hpp"

#include "yaml-cpp/yaml.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <regex>
#include <sstream>
#include <system_error>
#include <vector>

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
            std::regex disableCloudRegex(R"(^\s*DisableCloud\s*:.*$)");
            if (std::regex_match(line, disableCloudRegex))
            {
                continue;
            }

            // Check if top-level AdditionalApps:
            std::regex addAppsRegex(R"(^\s*AdditionalApps\s*:.*$)");
            if (std::regex_match(line, addAppsRegex))
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

        const std::string minimalConfig = "{\n  \"provider\": \"local-only\"\n}\n";
        return AtomicFile::write(paths.crConfigFile.string(), minimalConfig);
    }

    std::filesystem::path CCloudRedirectBridge::GetTokenPath(const std::string& provider) const
    {
        const auto& paths = GetPaths();
        return paths.crConfigDir / ("tokens_" + provider + ".json");
    }
} // namespace Navi::CloudRedirect
