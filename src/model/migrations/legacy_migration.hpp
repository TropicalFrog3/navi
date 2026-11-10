/**
 * @file  legacy_migration.hpp
 * @brief Interface for converting obsolete config formats
 */
#pragma once

#include <string>

class LegacyMigration {
public:
    struct Paths {
        std::string legacyConfigDir;        // e.g. ~/.config/SLSsteam
        std::string targetConfigDir;        // e.g. ~/.config/navi
        std::string legacyCacheDir;         // e.g. ~/.local/share/SLSsteam/cache
        std::string targetCacheDir;         // e.g. ~/.local/share/navi/cache
        std::string targetShareDir;         // e.g. ~/.local/share/navi
        std::string legacyManifestsDir;     // e.g. ~/.local/share/ACCELA/morrenus_manifests
        std::string targetManifestsDir;     // e.g. ~/.local/share/navi/cache/manifests
        std::string targetManifestsAltDir;  // e.g. ~/.local/share/navi/manifests
    };

    // Standard entry point using real environment ($HOME, $XDG_CONFIG_HOME, $XDG_DATA_HOME)
    static bool migrateIfNeeded();

    // Explicit path migration for unit testing and custom environments
    static bool migrate(const Paths& paths);

    // Convenience overload for testing with a sandbox home directory
    static bool migrate(const std::string& customHome);

    // Build Paths struct for a given home directory
    static Paths buildPaths(const std::string& homeDir);

    // Marker filename constant
    static inline const char* const MARKER_FILE = ".migrated_from_slssteam";
};
