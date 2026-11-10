/**
 * @file  paths.cpp
 * @brief Steam filesystem directory layout detection and path resolution
 */
#include "paths.hpp"
#include "vdf.hpp"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <regex>
#include <system_error>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

namespace Paths
{
    // ── Internal helpers ──────────────────────────────────────────────────────

    static const char* homeDir()
    {
        const char* home = getenv("HOME");
        return home ? home : "/tmp";
    }

    static void clearDirectoryEntries(const fs::path& dir, const std::string& excludeSubdir = "")
    {
        std::error_code ec;
        if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec))
            return;

        fs::directory_iterator it(dir, ec);
        fs::directory_iterator end;
        while (!ec && it != end)
        {
            if (excludeSubdir.empty() || it->path().filename().string() != excludeSubdir)
            {
                std::error_code rmEc;
                fs::remove_all(it->path(), rmEc);
            }
            it.increment(ec);
        }
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

    bool clearAppCache(uint32_t appId)
    {
        const char* home = homeDir();
        const std::string idStr = std::to_string(appId);
        const char* xdgConfig = getenv("XDG_CONFIG_HOME");

        std::vector<fs::path> pathsToRemove = {
            fs::path(home) / ".cache" / "navi" / "downloads" / (idStr + ".zip"),
            fs::path(home) / ".cache" / "navi" / "downloads" / (idStr + "_extracted"),
            fs::path(home) / ".config" / "navi" / "cache" / ("ticket_" + idStr + ".yaml"),
            fs::path(home) / ".config" / "navi" / "cache" / ("encryptedTicket_" + idStr + ".yaml"),
            fs::path(home) / ".local" / "share" / "navi" / "cache" / (idStr + ".zip"),
            fs::path(home) / ".local" / "share" / "navi" / "cache" / ("ticket_" + idStr + ".yaml"),
            fs::path(home) / ".local" / "share" / "navi" / "cache" / ("encryptedTicket_" + idStr + ".yaml"),
            fs::path("/tmp") / ("sls_lua_" + idStr),
            fs::path("/tmp") / ("sls_manual_dir_" + idStr)
        };

        if (xdgConfig && *xdgConfig)
        {
            pathsToRemove.push_back(fs::path(xdgConfig) / "navi" / "cache" / ("ticket_" + idStr + ".yaml"));
            pathsToRemove.push_back(fs::path(xdgConfig) / "navi" / "cache" / ("encryptedTicket_" + idStr + ".yaml"));
        }

        std::error_code ec;
        for (const auto& p : pathsToRemove)
        {
            fs::remove_all(p, ec);
        }

        return true;
    }

    bool clearAllCache()
    {
        const char* home = homeDir();
        const char* xdgConfig = getenv("XDG_CONFIG_HOME");

        // 1. All entries inside $HOME/.cache/navi/downloads/
        clearDirectoryEntries(fs::path(home) / ".cache" / "navi" / "downloads");

        // 2. All entries inside $HOME/.config/navi/cache/ (and $XDG_CONFIG_HOME/navi/cache/ if set)
        clearDirectoryEntries(fs::path(home) / ".config" / "navi" / "cache");
        if (xdgConfig && *xdgConfig)
        {
            fs::path xdgDir = fs::path(xdgConfig) / "navi" / "cache";
            if (xdgDir != fs::path(home) / ".config" / "navi" / "cache")
            {
                clearDirectoryEntries(xdgDir);
            }
        }

        // 3. All entries inside $HOME/.local/share/navi/cache/ EXCEPT manifests
        clearDirectoryEntries(fs::path(home) / ".local" / "share" / "navi" / "cache", "manifests");

        // 4. Any /tmp/sls_lua_* and /tmp/sls_manual_dir_* entries in /tmp
        std::error_code ec;
        fs::directory_iterator it("/tmp", ec);
        fs::directory_iterator end;
        while (!ec && it != end)
        {
            const std::string name = it->path().filename().string();
            if (name.rfind("sls_lua_", 0) == 0 || name.rfind("sls_manual_dir_", 0) == 0)
            {
                std::error_code rmEc;
                fs::remove_all(it->path(), rmEc);
            }
            it.increment(ec);
        }

        return true;
    }

    std::vector<std::string> findLibraryFolders(const std::string& steamRootOverride)
    {
        std::string root = !steamRootOverride.empty() ? steamRootOverride : findSteamRoot();
        if (root.empty()) return {};

        root = fs::path(root).lexically_normal().string();
        std::vector<std::string> folders;
        std::unordered_set<std::string> seen;

        folders.push_back(root);
        seen.insert(root);

        const fs::path vdfCandidates[] = {
            fs::path(root) / "steamapps" / "libraryfolders.vdf",
            fs::path(root) / "config" / "libraryfolders.vdf"
        };

        for (const auto& vdfPath : vdfCandidates)
        {
            std::error_code ec;
            if (!fs::exists(vdfPath, ec)) continue;

            VDF::Node rootNode;
            if (VDF::parseFile(vdfPath.string(), rootNode))
            {
                std::function<void(const VDF::Node&)> extractPaths = [&](const VDF::Node& n)
                {
                    if (strcasecmp(n.key.c_str(), "path") == 0 && !n.value.empty())
                    {
                        std::string p = fs::path(n.value).lexically_normal().string();
                        if (!seen.contains(p))
                        {
                            seen.insert(p);
                            folders.push_back(p);
                        }
                    }
                    for (const auto& child : n.children)
                    {
                        extractPaths(child);
                    }
                };
                extractPaths(rootNode);
            }

            // Fallback line regex scan for standard VDF "path" entries
            std::ifstream vdfFile(vdfPath);
            if (vdfFile.is_open())
            {
                std::string line;
                std::regex pathRegex(R"x(\"path\"\s+\"([^\"]+)\")x", std::regex::icase);
                while (std::getline(vdfFile, line))
                {
                    std::smatch m;
                    if (std::regex_search(line, m, pathRegex) && m.size() > 1)
                    {
                        std::string p = fs::path(m[1].str()).lexically_normal().string();
                        if (!seen.contains(p))
                        {
                            seen.insert(p);
                            folders.push_back(p);
                        }
                    }
                }
            }
        }

        return folders;
    }

    std::string findAppLibraryFolder(uint32_t appId, const std::string& steamRootOverride)
    {
        const auto folders = findLibraryFolders(steamRootOverride);
        if (folders.empty())
        {
            return !steamRootOverride.empty() ? steamRootOverride : findSteamRoot();
        }

        for (const auto& folder : folders)
        {
            std::error_code ec;
            fs::path acf = fs::path(folder) / "steamapps" / ("appmanifest_" + std::to_string(appId) + ".acf");
            if (fs::exists(acf, ec))
            {
                return folder;
            }
        }

        return folders.front();
    }
} // namespace Paths

