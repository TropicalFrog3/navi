/**
 * @file  cloudredirect.hpp
 * @brief Declarations for CloudRedirect lifecycle and provider routing
 */
#pragma once

#include "sdk/sdk.hpp"

#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_set>

namespace Navi::CloudRedirect
{
    enum class PlatformTier
    {
        Tier1_NativeDesktop,
        Tier1_SteamOS,
        Tier2_Flatpak
    };

    struct ResolvedPaths_t
    {
        PlatformTier tier = PlatformTier::Tier1_NativeDesktop;
        std::filesystem::path configHome;
        std::filesystem::path dataHome;
        std::filesystem::path slsSteamDir;
        std::filesystem::path slsSteamConfig;
        std::filesystem::path crConfigDir;
        std::filesystem::path crConfigFile;
        std::filesystem::path crKillswitchFile;
        std::filesystem::path crPendingOpsFile;
        std::filesystem::path naviConfigDir;
        std::filesystem::path naviEnabledFlag;
        std::filesystem::path naviBackupDir;
        std::filesystem::path steamRoot;
        std::filesystem::path storageDir;
        std::filesystem::path binDir;
        std::filesystem::path engineSo;
        std::filesystem::path cliBin;
    };

    struct EngineConfig_t
    {
        std::string provider{"local"};
        bool notificationsEnabled{true};
        bool statsSyncEnabled{false};
        bool syncAchievements{true};
        bool syncPlaytime{true};
    };

    class CCloudRedirectBridge
    {
    public:
        static CCloudRedirectBridge& Instance();

        PlatformTier DetectPlatformTier() const;
        const ResolvedPaths_t& GetPaths() const;
        bool IsEnabled() const;
        bool LoadEngine();
        bool DetectStandaloneInstallation() const;
        bool SetEnabledState(bool enable);
        bool SyncSlsSteamConfig(const std::unordered_set<AppId_t>& managedApps);
        bool RevertSlsSteamConfig(const std::unordered_set<AppId_t>& managedApps);
        bool EnsureLocalOnlyConfig();
        bool SyncEngineConfig(const EngineConfig_t& cfg);
        EngineConfig_t ReadEngineConfig() const;
        bool SyncEngineConfigFromGlobal();
        std::filesystem::path GetTokenPath(const std::string& provider) const;

    private:
        CCloudRedirectBridge() = default;
        ~CCloudRedirectBridge() = default;
        CCloudRedirectBridge(const CCloudRedirectBridge&) = delete;
        CCloudRedirectBridge& operator=(const CCloudRedirectBridge&) = delete;

        ResolvedPaths_t ResolvePaths() const;

        mutable std::mutex m_pathsMutex;
        mutable ResolvedPaths_t m_cachedPaths;
    };
} // namespace Navi::CloudRedirect
