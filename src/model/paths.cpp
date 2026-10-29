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

    std::string resolveLogicalSyncPath(const std::string& logicalUri, uint64_t steamId64, uint32_t appId, const std::string& steamRootOverride)
    {
        if (logicalUri.empty() || logicalUri.find('\0') != std::string::npos)
            return "";

        const size_t schemePos = logicalUri.find("://");
        if (schemePos == std::string::npos)
            return "";

        const std::string scheme = logicalUri.substr(0, schemePos);
        const std::string rest = logicalUri.substr(schemePos + 3);
        if (rest.empty() || rest.front() == '/')
            return "";

        // Check for path traversal or absolute path injection
        fs::path relPath(rest);
        if (relPath.is_absolute())
            return "";

        for (const auto& part : relPath)
        {
            if (part == ".." || part == "/")
                return "";
        }

        fs::path normRel = relPath.lexically_normal();
        for (const auto& part : normRel)
        {
            if (part == "..")
                return "";
        }

        std::string steamRoot = !steamRootOverride.empty() ? steamRootOverride : findSteamRoot();
        if (steamRoot.empty())
            return "";
        steamRoot = fs::path(steamRoot).lexically_normal().string();

        fs::path baseRoot;
        fs::path targetRel;

        if (scheme == "steamapps")
        {
            uint32_t resolvedAppId = appId;
            if (resolvedAppId == 0)
            {
                std::smatch m;
                std::regex re(R"x(appmanifest_(\d+)\.acf)x");
                if (std::regex_search(rest, m, re))
                {
                    try { resolvedAppId = std::stoul(m[1].str()); } catch (...) {}
                }
            }

            std::string lib = findAppLibraryFolder(resolvedAppId, steamRoot);
            baseRoot = fs::path(lib) / "steamapps";
            targetRel = relPath;
        }
        else if (scheme == "depotcache")
        {
            baseRoot = fs::path(steamRoot) / "config" / "depotcache";
            targetRel = relPath;
        }
        else if (scheme == "stplugin")
        {
            baseRoot = fs::path(steamRoot) / "config" / "stplug-in";
            targetRel = relPath;
        }
        else if (scheme == "userdata")
        {
            // Format: userdata://{steam32}/{appid}/remote/{relpath}
            const size_t s1 = rest.find('/');
            if (s1 == std::string::npos) return "";
            const std::string steam32Seg = rest.substr(0, s1);

            const size_t s2 = rest.find('/', s1 + 1);
            if (s2 == std::string::npos) return "";
            const std::string appIdSeg = rest.substr(s1 + 1, s2 - s1 - 1);

            const size_t s3 = rest.find('/', s2 + 1);
            const std::string remoteSeg = (s3 == std::string::npos)
                ? rest.substr(s2 + 1)
                : rest.substr(s2 + 1, s3 - s2 - 1);

            if (remoteSeg != "remote" || s3 == std::string::npos)
                return "";

            const std::string fileRel = rest.substr(s3 + 1);
            if (fileRel.empty())
                return "";

            const uint32_t expectedSteam32 = static_cast<uint32_t>(steamId64 & 0xFFFFFFFFULL);
            if (expectedSteam32 == 0)
                return "";

            std::string finalSteam32;
            if (steam32Seg == "{steam32}")
            {
                finalSteam32 = std::to_string(expectedSteam32);
            }
            else
            {
                uint32_t parsed = 0;
                try { parsed = static_cast<uint32_t>(std::stoul(steam32Seg)); } catch (...) { return ""; }
                if (parsed != expectedSteam32)
                    return "";
                finalSteam32 = std::to_string(parsed);
            }

            std::string finalAppId;
            if (appIdSeg == "{appid}")
            {
                if (appId == 0) return "";
                finalAppId = std::to_string(appId);
            }
            else
            {
                uint32_t parsedApp = 0;
                try { parsedApp = static_cast<uint32_t>(std::stoul(appIdSeg)); } catch (...) { return ""; }
                finalAppId = std::to_string(parsedApp);
            }

            baseRoot = fs::path(steamRoot) / "userdata" / finalSteam32 / finalAppId / "remote";
            targetRel = fs::path(fileRel);
        }
        else if (scheme == "protonprefix")
        {
            // Format: protonprefix://{appid}/drive_c/users/steamuser/{relpath}
            const size_t s1 = rest.find('/');
            if (s1 == std::string::npos) return "";
            const std::string appIdSeg = rest.substr(0, s1);
            const std::string sub = rest.substr(s1 + 1);

            const std::string prefixMarker = "drive_c/users/steamuser/";
            if (sub.rfind(prefixMarker, 0) != 0)
                return "";

            const std::string fileRel = sub.substr(prefixMarker.size());
            if (fileRel.empty())
                return "";

            uint32_t targetAppId = appId;
            if (appIdSeg != "{appid}")
            {
                try { targetAppId = static_cast<uint32_t>(std::stoul(appIdSeg)); } catch (...) { return ""; }
            }
            if (targetAppId == 0)
                return "";

            std::string lib = findAppLibraryFolder(targetAppId, steamRoot);
            baseRoot = fs::path(lib) / "steamapps" / "compatdata" / std::to_string(targetAppId) / "pfx" / "drive_c" / "users" / "steamuser";
            targetRel = fs::path(fileRel);
        }
        else
        {
            return "";
        }

        const fs::path resolved = (baseRoot / targetRel).lexically_normal();
        const fs::path normBase = baseRoot.lexically_normal();

        std::string resStr = resolved.string();
        std::string baseStr = normBase.string();
        if (baseStr.empty() || resStr.empty())
            return "";
        if (baseStr.back() == '/')
            baseStr.pop_back();

        // Lexical boundary check
        if (resStr != baseStr && (resStr.size() <= baseStr.size() || resStr[baseStr.size()] != '/' || resStr.compare(0, baseStr.size(), baseStr) != 0))
        {
            return "";
        }

        // Symlink escape check
        std::error_code ec;
        const fs::path canonicalBase = fs::weakly_canonical(normBase, ec);
        const fs::path canonicalResolved = fs::weakly_canonical(resolved, ec);

        std::string canBaseStr = canonicalBase.string();
        std::string canResStr = canonicalResolved.string();
        if (canBaseStr.back() == '/')
            canBaseStr.pop_back();

        if (canResStr != canBaseStr && (canResStr.size() <= canBaseStr.size() || canResStr[canBaseStr.size()] != '/' || canResStr.compare(0, canBaseStr.size(), canBaseStr) != 0))
        {
            return "";
        }

        return resStr;
    }

    std::string toLogicalSyncUri(Sync::ESyncCategory category, uint32_t appId, const std::string& relativeSubPath, uint64_t steamId64)
    {
        fs::path p(relativeSubPath);
        std::string filename = p.filename().string();

        switch (category)
        {
            case Sync::ESyncCategory::k_ManifestsAndLua:
            {
                if (relativeSubPath.find("stplug-in") != std::string::npos || p.extension() == ".lua")
                {
                    return "stplugin://" + filename;
                }
                if (relativeSubPath.find("depotcache") != std::string::npos || p.extension() == ".manifest")
                {
                    return "depotcache://" + filename;
                }
                if (p.extension() == ".acf" || filename.rfind("appmanifest_", 0) == 0)
                {
                    return "steamapps://" + filename;
                }
                return "steamapps://appmanifest_" + std::to_string(appId) + ".acf";
            }
            case Sync::ESyncCategory::k_CloudSaves:
            {
                const std::string pfxMarker = "drive_c/users/steamuser/";
                size_t pfxPos = relativeSubPath.find(pfxMarker);
                if (pfxPos != std::string::npos)
                {
                    std::string rel = relativeSubPath.substr(pfxPos + pfxMarker.size());
                    return "protonprefix://" + std::to_string(appId) + "/drive_c/users/steamuser/" + rel;
                }
                uint32_t steam32 = static_cast<uint32_t>(steamId64 & 0xFFFFFFFFULL);
                size_t remPos = relativeSubPath.find("remote/");
                std::string rel = (remPos != std::string::npos) ? relativeSubPath.substr(remPos + 7) : relativeSubPath;
                return "userdata://" + std::to_string(steam32) + "/" + std::to_string(appId) + "/remote/" + rel;
            }
            case Sync::ESyncCategory::k_Configs:
                return "config://" + relativeSubPath;
            case Sync::ESyncCategory::k_Achievements:
                return "achievements://" + std::to_string(appId) + "/" + relativeSubPath;
            case Sync::ESyncCategory::k_ApiKeys:
                return "apikeys://" + relativeSubPath;
        }
        return "";
    }
} // namespace Paths
