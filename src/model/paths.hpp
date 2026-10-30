#pragma once

#include <cstdint>
#include <string>
#include <vector>

/**
 * Paths
 *
 * Steam installation path detection and well-known sub-directory helpers.
 */
namespace Paths
{
    /**
     * Return the Steam root directory (the folder that contains "steamapps/"),
     * or an empty string if Steam could not be found.
     *
     * Search order:
     *   1. ~/.local/share/Steam
     *   2. ~/.steam/steam
     *   3. ~/.var/app/com.valvesoftware.Steam/data/Steam  (Flatpak)
     */
    std::string findSteamRoot();

    /**
     * Return the path to Steam's Lua plugin directory:
     *   <steam_root>/config/stplug-in
     * The directory is created if it does not exist.
     * Returns an empty string when the Steam root cannot be resolved.
     */
    std::string stplugDir();

    /**
     * Return the path to Steam's depot manifest cache:
     *   <steam_root>/config/depotcache
     * The directory is created if it does not exist.
     * Returns an empty string when the Steam root cannot be resolved.
     */
    std::string depotcacheDir();

    /**
     * Clear download and session caches for a specific AppID.
     * Never touches .lua files, .provider files, or manifests/ / depotcache/.
     */
    bool clearAppCache(uint32_t appId);

    /**
     * Clear all download and session caches across all apps.
     * Preserves manifests/ subdirectory and installed .lua files.
     */
    bool clearAllCache();

    /**
     * Parses <steam_root>/steamapps/libraryfolders.vdf or <steam_root>/config/libraryfolders.vdf
     * for all "path" entries. Always includes <steam_root> first if valid.
     * Handles SteamOS SD card paths like /run/media/mmcblk0p1.
     */
    std::vector<std::string> findLibraryFolders(const std::string& steamRootOverride = "");

    /**
     * Checks which library folder contains steamapps/appmanifest_<appId>.acf,
     * falling back to the primary <steam_root>.
     */
    std::string findAppLibraryFolder(uint32_t appId, const std::string& steamRootOverride = "");
} // namespace Paths

