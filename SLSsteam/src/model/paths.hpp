#pragma once

#include <string>

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
} // namespace Paths
