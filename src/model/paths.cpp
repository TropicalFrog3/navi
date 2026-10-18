#include "paths.hpp"

#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

namespace Paths
{
    // ── Internal helpers ──────────────────────────────────────────────────────

    static const char* homeDir()
    {
        const char* home = getenv("HOME");
        return home ? home : "/tmp";
    }

    // ── Public API ────────────────────────────────────────────────────────────

    std::string findSteamRoot()
    {
        const char* home = homeDir();

        const fs::path candidates[] = {
            fs::path(home) / ".local" / "share" / "Steam",
            fs::path(home) / ".steam" / "steam",
            fs::path(home) / ".var" / "app" / "com.valvesoftware.Steam" / "data" / "Steam",
        };

        for (const auto& path : candidates)
        {
            // steamui/ is a reliable marker for a valid Steam install tree
            if (fs::exists(path / "steamui"))
                return path.string();
        }
        return {};
    }

    std::string stplugDir()
    {
        const std::string root = findSteamRoot();
        if (root.empty()) return {};

        const fs::path dir = fs::path(root) / "config" / "stplug-in";
        fs::create_directories(dir);
        return dir.string();
    }

    std::string depotcacheDir()
    {
        const std::string root = findSteamRoot();
        if (root.empty()) return {};

        const fs::path dir = fs::path(root) / "depotcache";
        const fs::path legacyDir = fs::path(root) / "config" / "depotcache";
        fs::create_directories(dir);
        fs::create_directories(legacyDir);
        return dir.string();
    }
} // namespace Paths
