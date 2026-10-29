#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>

namespace Sync
{
    class GameProcessLock
    {
    public:
        /**
         * Thread-safe tracking of running AppIDs reported by hooks.
         */
        static void setAppRunningFromHook(uint32_t appId, bool running);

        /**
         * Clears all hooked running apps.
         */
        static void clearAllHookedRunningApps();

        /**
         * Checks whether targetAppId is currently running.
         * 1. Returns true if targetAppId != 0 and marked running via hook.
         * 2. Scans <procRootOverride>/[0-9]* owned by geteuid() (skipping getpid() if /proc):
         *    - Checks <pid>/environ for SteamAppId=<targetAppId> or SteamGameId=<targetAppId>.
         *    - Checks <pid>/cmdline for AppId=<targetAppId>.
         *    - If steamRootOverride is non-empty, checks <pid>/fd entries (symlinks) for compatdata or remote paths.
         */
        static bool isAppRunning(
            uint32_t targetAppId,
            const std::string& procRootOverride = "/proc",
            const std::string& steamRootOverride = "");

        /**
         * Collects all running AppIDs across hooks and proc scan.
         */
        static std::unordered_set<uint32_t> getAllRunningAppIds(
            const std::string& procRootOverride = "/proc");
    };
} // namespace Sync
