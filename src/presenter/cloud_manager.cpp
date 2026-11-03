#include "cloud_manager.hpp"
#include "cloudredirect.hpp"
#include "paths.hpp"
#include "lua_parser.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <sys/poll.h>
#include <sys/wait.h>
#include <unistd.h>

namespace Navi::CloudRedirect
{

CCloudManager& CCloudManager::Instance()
{
    static CCloudManager instance;
    return instance;
}

CCloudManager::CCloudManager() = default;

void CCloudManager::SetCliPathForTest(const fs::path& path)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cliPath = path;
}

void CCloudManager::SetBackupDirForTest(const fs::path& path)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_backupDir = path;
}

void CCloudManager::SetStorageDirForTest(const fs::path& path)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_storageDir = path;
}

void CCloudManager::SetSteamAppsRootForTest(const fs::path& path)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_steamAppsRoot = path;
}

void CCloudManager::SetSteamRootForTest(const fs::path& path)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_steamRoot = path;
}

fs::path CCloudManager::GetSteamRoot() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_steamRoot.empty()) return m_steamRoot;
    if (!m_steamAppsRoot.empty())
    {
        return (m_steamAppsRoot.filename() == "common")
               ? m_steamAppsRoot.parent_path().parent_path()
               : m_steamAppsRoot.parent_path();
    }
    return CCloudRedirectBridge::Instance().GetPaths().steamRoot;
}

fs::path CCloudManager::GetCliPath() const
{
    if (!m_cliPath.empty())
    {
        return m_cliPath;
    }
    fs::path installedCli = CCloudRedirectBridge::Instance().GetPaths().cliBin;
    std::error_code ec;
    if (fs::exists(installedCli, ec))
    {
        return installedCli;
    }
    // Fallback to res/cloud_redirect_cli in repository root
    fs::path repoCli = "res/cloud_redirect_cli";
    if (fs::exists(repoCli, ec))
    {
        return repoCli;
    }
    return installedCli;
}

fs::path CCloudManager::GetBackupDir() const
{
    if (!m_backupDir.empty())
    {
        return m_backupDir;
    }
    return CCloudRedirectBridge::Instance().GetPaths().naviBackupDir;
}

fs::path CCloudManager::GetStorageDir() const
{
    if (!m_storageDir.empty())
    {
        return m_storageDir;
    }
    const auto& paths = CCloudRedirectBridge::Instance().GetPaths();
    fs::path crStorage = paths.crConfigDir / "storage";
    std::error_code ec;
    if (!fs::exists(crStorage, ec) && fs::exists(paths.storageDir, ec))
    {
        return paths.storageDir;
    }
    return crStorage;
}

static std::string ExtractJsonSubstring(const std::string& output)
{
    size_t start = output.find('{');
    if (start == std::string::npos) return "";

    int depth = 0;
    size_t end = std::string::npos;
    for (size_t i = start; i < output.size(); ++i)
    {
        if (output[i] == '{') depth++;
        else if (output[i] == '}')
        {
            depth--;
            if (depth == 0)
            {
                end = i;
                break;
            }
        }
    }

    if (end != std::string::npos)
    {
        return output.substr(start, end - start + 1);
    }
    return "";
}

static bool ExtractJsonBool(const std::string& json, const std::string& key, bool defaultVal)
{
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return defaultVal;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return defaultVal;
    ++pos;
    while (pos < json.size() && (std::isspace(json[pos]) || json[pos] == '"')) ++pos;
    if (json.compare(pos, 4, "true") == 0) return true;
    if (json.compare(pos, 5, "false") == 0) return false;
    return defaultVal;
}

static std::string ExtractJsonString(const std::string& json, const std::string& key)
{
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return "";
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return "";
    ++pos;
    size_t end = json.find('"', pos);
    if (end == std::string::npos) return "";
    return json.substr(pos, end - pos);
}

ProviderAuthStatus_t CCloudManager::CheckAuthStatus(const std::string& provider)
{
    ProviderAuthStatus_t status;
    status.provider = provider;
    status.authenticated = false;

    fs::path cli = GetCliPath();
    std::error_code ec;
    if (!fs::exists(cli, ec))
    {
        status.rawJson = "{\"authenticated\":false,\"error\":\"CLI binary not found\"}";
        return status;
    }

    int pipefd[2];
    if (pipe(pipefd) != 0)
    {
        status.rawJson = "{\"authenticated\":false,\"error\":\"Failed to create pipe\"}";
        return status;
    }

    pid_t pid = fork();
    if (pid == 0)
    {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        std::string cliStr = cli.string();
        const char* args[] = { cliStr.c_str(), "auth-status", provider.c_str(), nullptr };
        execv(args[0], const_cast<char**>(args));
        _exit(127);
    }
    else if (pid > 0)
    {
        close(pipefd[1]);

        std::string output;
        char buffer[1024];

        struct pollfd pfd{};
        pfd.fd = pipefd[0];
        pfd.events = POLLIN;

        auto start = std::chrono::steady_clock::now();
        while (true)
        {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
            if (elapsed > 10000) // 10-second timeout
            {
                kill(pid, SIGKILL);
                break;
            }

            int pollRet = poll(&pfd, 1, 500);
            if (pollRet > 0 && (pfd.revents & POLLIN))
            {
                ssize_t count = read(pipefd[0], buffer, sizeof(buffer) - 1);
                if (count <= 0) break;
                buffer[count] = '\0';
                output.append(buffer, count);
            }
            else if (pollRet > 0 && (pfd.revents & (POLLHUP | POLLERR)))
            {
                // Drain remainder
                ssize_t count;
                while ((count = read(pipefd[0], buffer, sizeof(buffer) - 1)) > 0)
                {
                    output.append(buffer, count);
                }
                break;
            }
        }
        close(pipefd[0]);

        int wstatus = 0;
        waitpid(pid, &wstatus, 0);

        std::string jsonStr = ExtractJsonSubstring(output);
        if (!jsonStr.empty())
        {
            status.rawJson = jsonStr;
            status.authenticated = ExtractJsonBool(jsonStr, "authenticated", false);
            status.accountEmail = ExtractJsonString(jsonStr, "account_email");
            if (status.accountEmail.empty())
            {
                status.accountEmail = ExtractJsonString(jsonStr, "email");
            }
        }
        else
        {
            status.rawJson = "{\"authenticated\":false,\"raw_output\":\"" + output + "\"}";
        }

        return status;
    }

    status.rawJson = "{\"authenticated\":false,\"error\":\"Fork failed\"}";
    return status;
}

std::vector<std::string> CCloudManager::ListBackups(AppId_t appId)
{
    std::vector<std::string> backups;
    fs::path backupDir = GetBackupDir() / std::to_string(appId);

    std::error_code ec;
    if (!fs::exists(backupDir, ec) || !fs::is_directory(backupDir, ec))
    {
        return backups;
    }

    for (const auto& entry : fs::directory_iterator(backupDir, ec))
    {
        if (entry.is_regular_file(ec))
        {
            std::string filename = entry.path().filename().string();
            if (filename.ends_with(".tar.gz"))
            {
                backups.push_back(filename);
            }
        }
    }

    // Sort descending (newest first by timestamp in filename)
    std::sort(backups.begin(), backups.end(), std::greater<std::string>());
    return backups;
}

uint64_t CCloudManager::GetStorageUsageBytes(AppId_t appId)
{
    uint64_t totalBytes = 0;
    std::error_code ec;
    std::string appIdStr = std::to_string(appId);

    // 1. Redirect storage root: <storageDir>/*/<appId>/
    fs::path storageRoot = GetStorageDir();
    if (fs::exists(storageRoot, ec) && fs::is_directory(storageRoot, ec))
    {
        for (const auto& acctEntry : fs::directory_iterator(storageRoot, ec))
        {
            if (acctEntry.is_directory(ec))
            {
                fs::path appDir = acctEntry.path() / appIdStr;
                if (fs::exists(appDir, ec) && fs::is_directory(appDir, ec))
                {
                    for (const auto& fileEntry : fs::recursive_directory_iterator(appDir, ec))
                    {
                        if (fileEntry.is_regular_file(ec))
                        {
                            totalBytes += fileEntry.file_size(ec);
                        }
                    }
                }
            }
        }
    }

    // 2. Steam userdata: <SteamRoot>/userdata/*/<appId>/remote/
    fs::path steamRoot = CCloudRedirectBridge::Instance().GetPaths().steamRoot;
    fs::path userdataRoot = steamRoot / "userdata";
    if (fs::exists(userdataRoot, ec) && fs::is_directory(userdataRoot, ec))
    {
        for (const auto& acctEntry : fs::directory_iterator(userdataRoot, ec))
        {
            if (acctEntry.is_directory(ec))
            {
                fs::path remoteDir = acctEntry.path() / appIdStr / "remote";
                if (fs::exists(remoteDir, ec) && fs::is_directory(remoteDir, ec))
                {
                    for (const auto& fileEntry : fs::recursive_directory_iterator(remoteDir, ec))
                    {
                        if (fileEntry.is_regular_file(ec))
                        {
                            totalBytes += fileEntry.file_size(ec);
                        }
                    }
                }
            }
        }
    }

    // 3. Goldberg emulator save paths (games using goldberg_emulator bypass the
    //    CUserRemoteStorage vtable hook entirely; their saves land elsewhere).
    for (const auto& goldbergDir : ResolveGoldbergSavePaths(appId))
    {
        if (fs::exists(goldbergDir, ec) && fs::is_directory(goldbergDir, ec))
        {
            for (const auto& fileEntry : fs::recursive_directory_iterator(goldbergDir, ec))
            {
                if (fileEntry.is_regular_file(ec))
                {
                    totalBytes += fileEntry.file_size(ec);
                }
            }
        }
    }

    return totalBytes;
}

// ---------------------------------------------------------------------------
// Goldberg SteamEmu save path discovery
// ---------------------------------------------------------------------------
// Goldberg replaces steam_api.so with its own ISteamRemoteStorage implementation,
// which means our CUserRemoteStorage vtable hook never fires for those games.
// Their saves therefore land in Goldberg-configured directories, not in
// userdata/<accId>/<appId>/remote/.  We discover those paths dynamically from
// environment variables and well-known relative locations — zero hardcoded paths.
//
// Priority order (highest to lowest):
//   A) $STEAM_COMPAT_DATA_PATH Proton prefix path (games in Proton+Goldberg)
//   B) $XDG_DATA_HOME/Goldberg SteamEmu Saves/<appId>/remote/   (native Linux)
//   C) $HOME/Goldberg SteamEmu Saves/<appId>/remote/            (fallback)
//   D) steamapps/common/**/steam_settings/save_path.txt          (custom path)
//
// All returned paths may or may not exist yet; callers guard with fs::exists().
std::vector<fs::path> CCloudManager::ResolveGoldbergSavePaths(AppId_t appId) const
{
    std::vector<fs::path> result;
    if (appId == 0) return result;

    std::error_code ec;
    std::string appIdStr = std::to_string(appId);

    // Helper: add a candidate only if it doesn't escape via symlinks/traversal.
    auto addCandidate = [&](fs::path p)
    {
        // Normalise the path; reject if it contains ".." components after
        // resolution (defence in depth — callers still check fs::exists()).
        fs::path canon = fs::weakly_canonical(p, ec);
        if (ec) { ec.clear(); canon = p.lexically_normal(); }
        std::string s = canon.string();
        if (s.find("/../") != std::string::npos || s.ends_with("/..")) return;
        result.push_back(std::move(canon));
    };

    // ----- A) Proton prefix via STEAM_COMPAT_DATA_PATH & compatdata/<appId> -
    // Layout: <compat>/pfx/drive_c/users/<user>/AppData/{Roaming,Local,LocalLow}/<...>/<appId>/remote/
    static const char* kEmuFolders[] = {"GSE Saves", "Goldberg SteamEmu Saves"};
    static const char* kAppDataCats[] = {"Roaming", "Local", "LocalLow"};

    const char* compatData = std::getenv("STEAM_COMPAT_DATA_PATH");
    if (compatData && std::strlen(compatData) > 0)
    {
        for (const char* emuFolder : kEmuFolders)
        {
            addCandidate(fs::path(compatData)
                / "pfx" / "drive_c" / "users" / "steamuser"
                / "AppData" / "Roaming"
                / emuFolder / appIdStr / "remote");
        }
    }

    {
        fs::path steamAppsDir;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_steamAppsRoot.empty())
            {
                steamAppsDir = (m_steamAppsRoot.filename() == "common")
                                   ? m_steamAppsRoot.parent_path()
                                   : m_steamAppsRoot;
            }
        }
        if (steamAppsDir.empty())
        {
            steamAppsDir = CCloudRedirectBridge::Instance().GetPaths().steamRoot / "steamapps";
        }
        const fs::path usersDir = steamAppsDir / "compatdata" / appIdStr / "pfx" / "drive_c" / "users";
        if (fs::is_directory(usersDir, ec) && !ec)
        {
            for (const auto& userEntry : fs::directory_iterator(usersDir, fs::directory_options::skip_permission_denied, ec))
            {
                std::error_code uEc;
                if (!userEntry.is_directory(uEc) || uEc) continue;
                for (const char* cat : kAppDataCats)
                {
                    const fs::path catRoot = userEntry.path() / "AppData" / cat;
                    if (!fs::is_directory(catRoot, uEc) || uEc) continue;
                    for (const auto& child : fs::directory_iterator(catRoot, fs::directory_options::skip_permission_denied, uEc))
                    {
                        std::error_code cEc;
                        if (!child.is_directory(cEc) || cEc) continue;
                        const fs::path appBucket = child.path() / appIdStr / "remote";
                        if (fs::is_directory(appBucket, cEc) && !cEc)
                        {
                            addCandidate(appBucket);
                        }
                    }
                }
            }
        }
    }

    // ----- B/C) Native Linux Goldberg default location ----------------------
    // Goldberg uses $XDG_DATA_HOME/{GSE Saves,Goldberg SteamEmu Saves}/<appId>/remote/ or
    // falls back to $HOME/{GSE Saves,Goldberg SteamEmu Saves}/<appId>/remote/.
    const char* xdgData = std::getenv("XDG_DATA_HOME");
    const char* homeDir  = std::getenv("HOME");

    if (xdgData && std::strlen(xdgData) > 0)
    {
        for (const char* emuFolder : kEmuFolders)
        {
            addCandidate(fs::path(xdgData) / emuFolder / appIdStr / "remote");
        }
    }
    if (homeDir && std::strlen(homeDir) > 0)
    {
        // Always include $HOME variant regardless of XDG_DATA_HOME because some
        // Goldberg builds hardcode the $HOME path.
        for (const char* emuFolder : kEmuFolders)
        {
            addCandidate(fs::path(homeDir) / emuFolder / appIdStr / "remote");
        }
    }

    // ----- D) Custom save_path.txt under steamapps/common ------------------
    // Goldberg supports a per-game override:  steam_settings/save_path.txt
    // whose content (first line) is the absolute or $HOME-relative save root.
    // We scan steamapps/common/* for any directory that:
    //   1. Contains steam_settings/save_path.txt, AND
    //   2. The appId can be verified via steam_appid.txt in the same game dir.
    //
    // We limit the scan depth to 2 levels (game dir → steam_settings/) to
    // avoid walking deep into large game libraries.
    {
        // Determine steamapps root: test override first, then auto-resolve.
        fs::path steamAppsRoot;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            steamAppsRoot = m_steamAppsRoot;
        }
        if (steamAppsRoot.empty())
        {
            fs::path steamRoot = CCloudRedirectBridge::Instance().GetPaths().steamRoot;
            steamAppsRoot = steamRoot / "steamapps" / "common";
        }

        if (fs::exists(steamAppsRoot, ec) && fs::is_directory(steamAppsRoot, ec))
        {
            for (const auto& gameEntry : fs::directory_iterator(steamAppsRoot, ec))
            {
                if (!gameEntry.is_directory(ec)) continue;

                // Verify appId via steam_appid.txt (optional; if absent we still check)
                fs::path appIdFile = gameEntry.path() / "steam_appid.txt";
                if (fs::exists(appIdFile, ec))
                {
                    std::ifstream af(appIdFile);
                    if (af.is_open())
                    {
                        std::string line;
                        std::getline(af, line);
                        // Trim whitespace
                        line.erase(0, line.find_first_not_of(" \t\r\n"));
                        line.erase(line.find_last_not_of(" \t\r\n") + 1);
                        if (!line.empty() && line != appIdStr) continue;
                    }
                }

                fs::path savePathFile = gameEntry.path() / "steam_settings" / "save_path.txt";
                if (!fs::exists(savePathFile, ec)) continue;

                std::ifstream spf(savePathFile);
                if (!spf.is_open()) continue;

                std::string saveLine;
                std::getline(spf, saveLine);
                // Trim whitespace / CR
                saveLine.erase(0, saveLine.find_first_not_of(" \t\r\n"));
                saveLine.erase(saveLine.find_last_not_of(" \t\r\n") + 1);
                if (saveLine.empty()) continue;

                // Expand leading $HOME or ~ if present
                if (homeDir && std::strlen(homeDir) > 0)
                {
                    if (saveLine.rfind("$HOME", 0) == 0)
                        saveLine = std::string(homeDir) + saveLine.substr(5);
                    else if (saveLine.rfind("~/", 0) == 0)
                        saveLine = std::string(homeDir) + saveLine.substr(1);
                    else if (saveLine == "~")
                        saveLine = homeDir;
                }

                fs::path customRoot(saveLine);
                // Append /<appId>/remote/ under the custom root
                addCandidate(customRoot / appIdStr / "remote");
                // Also try the custom root directly (some configs point to the
                // save dir itself without a further appId subdirectory).
                addCandidate(customRoot);
            }
        }
    }

    // Deduplicate while preserving order
    std::vector<fs::path> deduped;
    for (auto& p : result)
    {
        bool dup = false;
        for (const auto& q : deduped)
        {
            if (p == q) { dup = true; break; }
        }
        if (!dup) deduped.push_back(std::move(p));
    }
    return deduped;
}

// ---------------------------------------------------------------------------
// Steam Cloud File & App Inventory (View Steam Cloud UX)
// ---------------------------------------------------------------------------

static bool IsDigitsOnly(const std::string& s)
{
    if (s.empty()) return false;
    for (unsigned char c : s)
    {
        if (!std::isdigit(c)) return false;
    }
    return true;
}

static bool IsHexHashSegment(const std::string& s)
{
    if (s.size() != 40 && s.size() != 64) return false;
    for (unsigned char c : s)
    {
        if (!std::isxdigit(c)) return false;
    }
    return true;
}

namespace
{
    static bool IsPlaceholderAppTitle(const std::string& title, AppId_t appId = 0)
    {
        if (title.empty()) return true;
        if (title.rfind("App ", 0) == 0 || title.rfind("app ", 0) == 0 ||
            title.rfind("App_", 0) == 0 || title.rfind("app_", 0) == 0)
        {
            size_t start = 4;
            if (start < title.size() && IsDigitsOnly(title.substr(start)))
            {
                return true;
            }
        }
        if (appId != 0)
        {
            const std::string sId = std::to_string(appId);
            if (title == ("App " + sId) || title == ("app " + sId) ||
                title == ("App_" + sId) || title == ("app_" + sId))
            {
                return true;
            }
        }
        return false;
    }

    struct DepotOwner_t
    {
        AppId_t appId = 0;
        std::string title;
    };

    /**
     * Find the owning parent game of a depot ID by scanning installed app manifests
     * (appmanifest_*.acf) across Steam library folders. Each game manifest records
     * its child depots in the "InstalledDepots" block.
     */
    DepotOwner_t FindDepotOwnerInAppManifests(AppId_t depotId, const std::vector<fs::path>& steamAppsDirs)
    {
        DepotOwner_t owner;
        if (depotId == 0) return owner;
        const std::string depotIdStr = std::to_string(depotId);
        std::error_code ec;

        for (const auto& saDir : steamAppsDirs)
        {
            if (!fs::is_directory(saDir, ec)) continue;
            for (const auto& entry : fs::directory_iterator(saDir, fs::directory_options::skip_permission_denied, ec))
            {
                if (ec) break;
                if (!entry.is_regular_file(ec)) continue;
                const fs::path& p = entry.path();
                const std::string fn = p.filename().string();
                if (fn.rfind("appmanifest_", 0) != 0 || !fn.ends_with(".acf")) continue;

                // Extract parent appId from filename appmanifest_<appId>.acf
                std::string parentIdStr = fn.substr(12, fn.length() - 12 - 4);
                if (!IsDigitsOnly(parentIdStr) || parentIdStr == depotIdStr) continue;

                std::ifstream mf(p);
                if (!mf.is_open()) continue;

                std::string line;
                bool inInstalledDepots = false;
                int depotsDepth = 0;
                bool foundDepot = false;
                std::string appName;

                while (std::getline(mf, line))
                {
                    if (appName.empty())
                    {
                        size_t nameKey = line.find("\"name\"");
                        if (nameKey != std::string::npos)
                        {
                            size_t q1 = line.find('"', nameKey + 6);
                            size_t q2 = (q1 != std::string::npos) ? line.find('"', q1 + 1) : std::string::npos;
                            if (q1 != std::string::npos && q2 != std::string::npos && q2 > q1 + 1)
                            {
                                std::string n = line.substr(q1 + 1, q2 - q1 - 1);
                                if (!IsPlaceholderAppTitle(n))
                                {
                                    appName = n;
                                }
                            }
                        }
                    }

                    if (!inInstalledDepots)
                    {
                        if (line.find("\"InstalledDepots\"") != std::string::npos)
                        {
                            inInstalledDepots = true;
                            depotsDepth = 0;
                        }
                    }
                    else
                    {
                        for (char c : line)
                        {
                            if (c == '{') depotsDepth++;
                            else if (c == '}')
                            {
                                depotsDepth--;
                                if (depotsDepth <= 0)
                                {
                                    inInstalledDepots = false;
                                    break;
                                }
                            }
                        }
                        if (inInstalledDepots && depotsDepth == 1)
                        {
                            size_t q1 = line.find('"');
                            if (q1 != std::string::npos)
                            {
                                size_t q2 = line.find('"', q1 + 1);
                                if (q2 != std::string::npos)
                                {
                                    std::string key = line.substr(q1 + 1, q2 - q1 - 1);
                                    if (key == depotIdStr)
                                    {
                                        foundDepot = true;
                                    }
                                }
                            }
                        }
                    }

                    if (foundDepot && !appName.empty())
                    {
                        owner.appId = static_cast<AppId_t>(std::strtoul(parentIdStr.c_str(), nullptr, 10));
                        owner.title = appName;
                        return owner;
                    }
                }

                if (foundDepot)
                {
                    owner.appId = static_cast<AppId_t>(std::strtoul(parentIdStr.c_str(), nullptr, 10));
                    return owner;
                }
            }
        }
        return owner;
    }

    /**
     * Find the owning game of a depot ID by scanning the numeric Lua plugins in
     * config/stplug-in. Depot IDs are listed inside their parent game's
     * <appId>.lua via addappid()/setManifestid(), so this lets unresolved depot
     * IDs reported as cloud apps inherit the real game title.
     */
    DepotOwner_t FindDepotOwnerInLuaPlugins(AppId_t depotId, const fs::path& stplugDir)
    {
        DepotOwner_t owner;
        std::error_code ec;
        if (!fs::is_directory(stplugDir, ec)) return owner;

        const std::string depotIdStr = std::to_string(depotId);
        for (const auto& entry : fs::directory_iterator(stplugDir, fs::directory_options::skip_permission_denied, ec))
        {
            if (ec) break;
            if (!entry.is_regular_file(ec)) continue;

            const fs::path& path = entry.path();
            if (path.extension() != ".lua") continue;

            const std::string stem = path.stem().string();
            if (!IsDigitsOnly(stem) || stem == depotIdStr) continue;

            std::ifstream lf(path);
            if (!lf.is_open()) continue;

            std::string content((std::istreambuf_iterator<char>(lf)),
                                 std::istreambuf_iterator<char>());
            if (content.find(depotIdStr) == std::string::npos) continue;

            const LuaParser::ParseResult parsed = LuaParser::parse(content);
            const bool referencesDepot = parsed.depots.find(depotIdStr) != parsed.depots.end() ||
                                         parsed.manifests.find(depotIdStr) != parsed.manifests.end() ||
                                         parsed.dlcs.find(depotIdStr) != parsed.dlcs.end();
            if (!referencesDepot) continue;

            std::string parentIdStr = (parsed.valid && !parsed.appId.empty() && IsDigitsOnly(parsed.appId)) ? parsed.appId : stem;
            if (parentIdStr == depotIdStr) continue;

            const bool hasUsableTitle = !parsed.gameName.empty() && !IsPlaceholderAppTitle(parsed.gameName);
            if (hasUsableTitle)
            {
                owner.appId = static_cast<AppId_t>(std::strtoul(parentIdStr.c_str(), nullptr, 10));
                owner.title = parsed.gameName;
                return owner;
            }

            if (owner.appId == 0)
            {
                owner.appId = static_cast<AppId_t>(std::strtoul(parentIdStr.c_str(), nullptr, 10));
            }
        }
        return owner;
    }
} // namespace

static bool IsInternalMetadataRelPath(const std::string& rel)
{
    if (rel.empty()) return true;
    if (rel.find("..") != std::string::npos) return true;
    if (rel.rfind(".cloudredirect/", 0) == 0 || rel == ".cloudredirect") return true;
    if (rel.rfind("blobs/", 0) == 0 || rel == "blobs") return true;
    if (rel == "cn.cloudredirect" || rel == "cn.dat" ||
        rel == "state.cloudredirect" || rel == "state.dat" ||
        rel == "manifest.cloudredirect" || rel == "manifest.dat" ||
        rel == "root_token.cloudredirect" || rel == "root_token.dat" ||
        rel == "file_tokens.cloudredirect" || rel == "file_tokens.dat" ||
        rel == "steam_autocloud.vdf" || rel.ends_with("/steam_autocloud.vdf") ||
        rel == "remotecache.vdf" || rel.ends_with("/remotecache.vdf"))
    {
        return true;
    }
    if (rel.ends_with(".cloudredirect"))
    {
        return true;
    }
    return false;
}

static std::string NormalizeFolderToken(std::string token, const std::string& relPath)
{
    while (!token.empty() && (std::isspace(static_cast<unsigned char>(token.front())) || token.front() == '%'))
    {
        token.erase(0, 1);
    }
    while (!token.empty() && (std::isspace(static_cast<unsigned char>(token.back())) || token.back() == '%'))
    {
        token.pop_back();
    }
    if (!token.empty())
    {
        return token;
    }
    if (relPath.rfind("GSE Saves/", 0) == 0 || relPath.rfind("Goldberg SteamEmu Saves/", 0) == 0)
    {
        return "WinAppDataRoaming";
    }
    if (relPath.rfind("My Games/", 0) == 0)
    {
        return "SteamCloudDocuments";
    }
    if (relPath.rfind("stplug-in/", 0) == 0 || relPath.ends_with(".lua") || relPath.ends_with(".provider"))
    {
        return "stplug-in";
    }
    if (relPath.rfind("depotcache/", 0) == 0 || relPath.ends_with(".manifest"))
    {
        return "depotcache";
    }
    return "SteamCloudRemote";
}

static uint64_t ExtractJsonUint64(const std::string& json, const std::string& key, uint64_t defaultVal = 0)
{
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return defaultVal;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return defaultVal;
    ++pos;
    while (pos < json.size() && (std::isspace(static_cast<unsigned char>(json[pos])) || json[pos] == '"')) ++pos;
    std::string digits;
    while (pos < json.size() && std::isdigit(static_cast<unsigned char>(json[pos])))
    {
        digits.push_back(json[pos]);
        ++pos;
    }
    if (digits.empty()) return defaultVal;
    try
    {
        return std::stoull(digits);
    }
    catch (...)
    {
        return defaultVal;
    }
}

std::string CCloudManager::RunCliCommand(const std::vector<std::string>& args, int timeoutMs) const
{
    fs::path cli = GetCliPath();
    std::error_code ec;
    if (!fs::exists(cli, ec))
    {
        return "";
    }

    int pipefd[2];
    if (pipe(pipefd) != 0)
    {
        return "";
    }

    pid_t pid = fork();
    if (pid == 0)
    {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        std::string cliStr = cli.string();
        std::vector<const char*> argv;
        argv.reserve(args.size() + 2);
        argv.push_back(cliStr.c_str());
        for (const auto& a : args)
        {
            argv.push_back(a.c_str());
        }
        argv.push_back(nullptr);
        execv(argv[0], const_cast<char* const*>(argv.data()));
        _exit(127);
    }
    else if (pid > 0)
    {
        close(pipefd[1]);
        std::string output;
        char buffer[2048];

        struct pollfd pfd{};
        pfd.fd = pipefd[0];
        pfd.events = POLLIN;

        auto start = std::chrono::steady_clock::now();
        while (true)
        {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
            if (elapsed > timeoutMs)
            {
                kill(pid, SIGKILL);
                break;
            }

            int pollRet = poll(&pfd, 1, 250);
            if (pollRet > 0 && (pfd.revents & POLLIN))
            {
                ssize_t count = read(pipefd[0], buffer, sizeof(buffer) - 1);
                if (count <= 0) break;
                output.append(buffer, static_cast<size_t>(count));
            }
            else if (pollRet > 0 && (pfd.revents & (POLLHUP | POLLERR)))
            {
                ssize_t count;
                while ((count = read(pipefd[0], buffer, sizeof(buffer) - 1)) > 0)
                {
                    output.append(buffer, static_cast<size_t>(count));
                }
                break;
            }
        }
        close(pipefd[0]);
        int wstatus = 0;
        waitpid(pid, &wstatus, 0);
        return output;
    }

    close(pipefd[0]);
    close(pipefd[1]);
    return "";
}

std::vector<uint32_t> CCloudManager::ResolveCandidateAccountIds(uint32_t preferredAccountId) const
{
    std::vector<uint32_t> accounts;
    auto addAcct = [&](uint32_t id) {
        if (id == 0) return;
        if (std::find(accounts.begin(), accounts.end(), id) == accounts.end())
        {
            accounts.push_back(id);
        }
    };

    addAcct(preferredAccountId);

    std::error_code ec;
    fs::path storageRoot = GetStorageDir();
    if (fs::exists(storageRoot, ec) && fs::is_directory(storageRoot, ec))
    {
        for (const auto& entry : fs::directory_iterator(storageRoot, ec))
        {
            if (!entry.is_directory(ec)) continue;
            std::string name = entry.path().filename().string();
            if (IsDigitsOnly(name))
            {
                uint32_t id = static_cast<uint32_t>(std::strtoul(name.c_str(), nullptr, 10));
                addAcct(id);
            }
        }
    }

    // Only scan Steam userdata as a fallback when neither an active Steam ID nor a local storage account exists
    if (accounts.empty())
    {
        fs::path userdataRoot = CCloudRedirectBridge::Instance().GetPaths().steamRoot / "userdata";
        if (fs::exists(userdataRoot, ec) && fs::is_directory(userdataRoot, ec))
        {
            for (const auto& entry : fs::directory_iterator(userdataRoot, ec))
            {
                if (!entry.is_directory(ec)) continue;
                std::string name = entry.path().filename().string();
                if (IsDigitsOnly(name))
                {
                    uint32_t id = static_cast<uint32_t>(std::strtoul(name.c_str(), nullptr, 10));
                    addAcct(id);
                }
            }
        }
    }

    return accounts;
}

static std::vector<fs::path> GetSteamAppsDirs(const fs::path& steamRoot)
{
    std::vector<fs::path> steamAppsDirs;
    std::error_code ec;
    fs::path primarySteamApps = steamRoot / "steamapps";
    if (fs::is_directory(primarySteamApps, ec))
    {
        steamAppsDirs.push_back(primarySteamApps);
    }

    // Also parse libraryfolders.vdf for additional Steam library paths
    fs::path libVdf = primarySteamApps / "libraryfolders.vdf";
    if (fs::is_regular_file(libVdf, ec))
    {
        std::ifstream lf(libVdf);
        std::string line;
        while (std::getline(lf, line))
        {
            size_t pathKey = line.find("\"path\"");
            if (pathKey != std::string::npos)
            {
                size_t q1 = line.find('"', pathKey + 6);
                size_t q2 = (q1 != std::string::npos) ? line.find('"', q1 + 1) : std::string::npos;
                if (q1 != std::string::npos && q2 != std::string::npos && q2 > q1 + 1)
                {
                    fs::path extraSteamApps = fs::path(line.substr(q1 + 1, q2 - q1 - 1)) / "steamapps";
                    if (fs::is_directory(extraSteamApps, ec))
                    {
                        if (std::find(steamAppsDirs.begin(), steamAppsDirs.end(), extraSteamApps) == steamAppsDirs.end())
                        {
                            steamAppsDirs.push_back(extraSteamApps);
                        }
                    }
                }
            }
        }
    }
    return steamAppsDirs;
}

std::string CCloudManager::ResolveAppTitleFromManifests(AppId_t appId) const
{
    return ResolveAppTitleFromManifestsInternal(appId, 0);
}

std::string CCloudManager::ResolveAppTitleFromManifestsInternal(AppId_t appId, int depth) const
{
    if (appId == 0 || depth > 3) return "";
    if (appId == 7) return "Steam Client";
    if (appId == 241100) return "Steam Input Configs";
    if (appId == 250820) return "SteamVR";
    std::error_code ec;
    const fs::path steamRoot = GetSteamRoot();
    const std::vector<fs::path> steamAppsDirs = GetSteamAppsDirs(steamRoot);

    const std::string manifestName = "appmanifest_" + std::to_string(appId) + ".acf";
    for (const auto& saDir : steamAppsDirs)
    {
        fs::path manifestPath = saDir / manifestName;
        if (!fs::is_regular_file(manifestPath, ec)) continue;
        std::ifstream mf(manifestPath);
        std::string line;
        while (std::getline(mf, line))
        {
            size_t nameKey = line.find("\"name\"");
            if (nameKey != std::string::npos)
            {
                size_t q1 = line.find('"', nameKey + 6);
                size_t q2 = (q1 != std::string::npos) ? line.find('"', q1 + 1) : std::string::npos;
                if (q1 != std::string::npos && q2 != std::string::npos && q2 > q1 + 1)
                {
                    std::string rawName = line.substr(q1 + 1, q2 - q1 - 1);
                    if (!IsPlaceholderAppTitle(rawName, appId))
                    {
                        return rawName;
                    }
                }
            }
        }
    }

    // Fallback for uninstalled games: resolve from <SteamRoot>/appcache/appinfo.vdf (cached in memory)
    static std::mutex s_appInfoMutex;
    static std::unordered_map<AppId_t, std::string> s_appInfoTitles;
    static uint64_t s_appInfoFileSize = 0;

    fs::path appInfoPath = steamRoot / "appcache" / "appinfo.vdf";
    if (fs::is_regular_file(appInfoPath, ec))
    {
        uint64_t fsize = static_cast<uint64_t>(fs::file_size(appInfoPath, ec));
        std::lock_guard<std::mutex> lock(s_appInfoMutex);
        if (fsize > 24 && fsize != s_appInfoFileSize)
        {
            s_appInfoTitles.clear();
            s_appInfoFileSize = fsize;
            std::ifstream af(appInfoPath, std::ios::binary);
            if (af.is_open())
            {
                std::vector<uint8_t> buf(static_cast<size_t>(fsize));
                if (af.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(fsize)))
                {
                    uint32_t magic = 0;
                    std::memcpy(&magic, buf.data(), 4);
                    // Steam appinfo.vdf v29 (0x07564429) uses a string table at offset [8..16]
                    if (magic == 0x07564429 && buf.size() >= 16)
                    {
                        uint64_t strTableOff = 0;
                        std::memcpy(&strTableOff, buf.data() + 8, 8);
                        if (strTableOff + 4 <= buf.size())
                        {
                            uint32_t strCount = 0;
                            std::memcpy(&strCount, buf.data() + strTableOff, 4);
                            uint32_t commonIdx = UINT32_MAX;
                            uint32_t nameIdx = UINT32_MAX;
                            size_t pos = static_cast<size_t>(strTableOff) + 4;
                            for (uint32_t i = 0; i < strCount && pos < buf.size(); ++i)
                            {
                                const void* nul = std::memchr(buf.data() + pos, 0, buf.size() - pos);
                                if (!nul) break;
                                size_t slen = static_cast<const uint8_t*>(nul) - (buf.data() + pos);
                                if (slen == 6 && std::memcmp(buf.data() + pos, "common", 6) == 0) commonIdx = i;
                                else if (slen == 4 && std::memcmp(buf.data() + pos, "name", 4) == 0) nameIdx = i;
                                pos += slen + 1;
                                if (commonIdx != UINT32_MAX && nameIdx != UINT32_MAX) break;
                            }

                            if (commonIdx != UINT32_MAX && nameIdx != UINT32_MAX)
                            {
                                uint8_t commonPat[5] = {0x00, 0, 0, 0, 0};
                                uint8_t namePat[5]   = {0x01, 0, 0, 0, 0};
                                std::memcpy(commonPat + 1, &commonIdx, 4);
                                std::memcpy(namePat + 1, &nameIdx, 4);

                                size_t off = 16;
                                while (off + 68 <= strTableOff && off + 8 <= buf.size())
                                {
                                    uint32_t entryAppId = 0;
                                    uint32_t entrySize = 0;
                                    std::memcpy(&entryAppId, buf.data() + off, 4);
                                    std::memcpy(&entrySize, buf.data() + off + 4, 4);
                                    if (entryAppId == 0 || entrySize < 60) break;
                                    size_t kvStart = off + 68;
                                    size_t kvEnd = off + 8 + static_cast<size_t>(entrySize);
                                    if (kvEnd > strTableOff || kvEnd > buf.size()) break;

                                    const uint8_t* base = buf.data() + kvStart;
                                    size_t kvLen = kvEnd - kvStart;
                                    const uint8_t* cMatch = std::search(base, base + kvLen, commonPat, commonPat + 5);
                                    if (cMatch != base + kvLen)
                                    {
                                        const uint8_t* afterCommon = cMatch + 5;
                                        const uint8_t* nMatch = std::search(afterCommon, base + kvLen, namePat, namePat + 5);
                                        if (nMatch != base + kvLen)
                                        {
                                            const uint8_t* strStart = nMatch + 5;
                                            const void* strEnd = std::memchr(strStart, 0, (base + kvLen) - strStart);
                                            if (strEnd && strEnd > strStart)
                                            {
                                                s_appInfoTitles[entryAppId] = std::string(
                                                    reinterpret_cast<const char*>(strStart),
                                                    static_cast<const uint8_t*>(strEnd) - strStart);
                                            }
                                        }
                                    }
                                    off = kvEnd;
                                }
                            }
                        }
                    }
                }
            }
        }
        auto it = s_appInfoTitles.find(appId);
        if (it != s_appInfoTitles.end())
        {
            return it->second;
        }
    }

    // Fallback 3: Extract game title from Lua plugin trailing comment
    const fs::path luaPlugin = steamRoot / "config" / "stplug-in" / (std::to_string(appId) + ".lua");
    if (fs::is_regular_file(luaPlugin, ec))
    {
        std::ifstream lf(luaPlugin);
        std::string line;
        while (std::getline(lf, line))
        {
            size_t addApp = line.find("addappid(");
            if (addApp != std::string::npos)
            {
                size_t comment = line.find("--", addApp);
                if (comment != std::string::npos)
                {
                    std::string title = line.substr(comment + 2);
                    title.erase(0, title.find_first_not_of(" \t"));
                    title.erase(title.find_last_not_of(" \t\r\n") + 1);
                    if (!title.empty())
                    {
                        return title;
                    }
                }
            }
        }
    }

    // Fallback 4: Derive human-readable title from Proton Auto-Cloud directory parent folder
    const fs::path compatPfx = steamRoot / "steamapps" / "compatdata" / std::to_string(appId) / "pfx" / "drive_c" / "users" / "steamuser";
    if (fs::is_directory(compatPfx, ec))
    {
        for (const char* relCat : {"AppData/LocalLow", "AppData/Local", "AppData/Roaming", "Documents", "Saved Games"})
        {
            const fs::path catPath = compatPfx / relCat;
            if (!fs::is_directory(catPath, ec)) continue;
            for (auto it = fs::recursive_directory_iterator(catPath, fs::directory_options::skip_permission_denied, ec);
                 it != fs::recursive_directory_iterator(); it.increment(ec))
            {
                std::error_code fEc;
                if (it->is_symlink(fEc))
                {
                    if (it->is_directory(fEc)) it.disable_recursion_pending();
                    continue;
                }
                if (it->is_regular_file(fEc) && it->path().filename() == "steam_autocloud.vdf")
                {
                    // Only derive a title from shallow save layouts; deeply nested paths
                    // are usually per-user/profile folders, not the game name.
                    std::error_code relEc;
                    const fs::path relPath = fs::relative(it->path().parent_path(), catPath, relEc);
                    if (relEc || relPath.empty()) continue;
                    size_t depth = 0;
                    for (const auto& part : relPath) { (void)part; ++depth; }
                    if (depth > 3) continue;

                    fs::path cur = it->path().parent_path();
                    while (!cur.empty() && cur != catPath)
                    {
                        std::string fn = cur.filename().string();
                        std::string fnLower = fn;
                        for (auto& c : fnLower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                        if (fnLower != "save" && fnLower != "saves" && fnLower != "saved" &&
                            fnLower != "savegames" && fnLower != "savedgames" && fnLower != "config" &&
                            fnLower != "userdata" && fnLower != "profile" && fnLower != "backup" &&
                            fnLower != "temp")
                        {
                            for (auto& c : fn) { if (c == '_') c = ' '; }
                            return fn;
                        }
                        cur = cur.parent_path();
                    }
                }
            }
        }
    }

    // Fallback 5: Depots referenced by another game's manifest (InstalledDepots) or
    // Lua plugin belong to that game. Steam can report a depot ID (e.g. 1030303 or 2937911)
    // as a standalone cloud app, so map it back to the owning game instead of leaving
    // the caller with "App <depotId>".
    if (depth < 3)
    {
        const DepotOwner_t manifestOwner = FindDepotOwnerInAppManifests(appId, steamAppsDirs);
        if (!manifestOwner.title.empty())
        {
            return manifestOwner.title;
        }
        if (manifestOwner.appId != 0 && manifestOwner.appId != appId)
        {
            const std::string ownerTitle = ResolveAppTitleFromManifestsInternal(manifestOwner.appId, depth + 1);
            if (!ownerTitle.empty())
            {
                return ownerTitle;
            }
        }

        const DepotOwner_t luaOwner = FindDepotOwnerInLuaPlugins(appId, steamRoot / "config" / "stplug-in");
        if (!luaOwner.title.empty())
        {
            return luaOwner.title;
        }
        if (luaOwner.appId != 0 && luaOwner.appId != appId)
        {
            const std::string ownerTitle = ResolveAppTitleFromManifestsInternal(luaOwner.appId, depth + 1);
            if (!ownerTitle.empty())
            {
                return ownerTitle;
            }
        }
    }

    return "";
}

AppId_t CCloudManager::GetParentAppIdForDepot(AppId_t depotId) const
{
    if (depotId == 0) return 0;
    // Shared Steamworks Common Redistributables
    if (depotId >= 228980 && depotId <= 229007) return 0;

    // In unit test mode where storageDir is set but steamRoot is not, do not leak host steamRoot
    if (!m_storageDir.empty() && m_steamRoot.empty()) return 0;

    const fs::path steamRoot = GetSteamRoot();
    if (steamRoot.empty()) return 0;
    const std::vector<fs::path> steamAppsDirs = GetSteamAppsDirs(steamRoot);

    // 1. Check if an installed appmanifest_*.acf lists this depot in InstalledDepots
    const DepotOwner_t manifestOwner = FindDepotOwnerInAppManifests(depotId, steamAppsDirs);
    if (manifestOwner.appId != 0 && manifestOwner.appId != depotId)
    {
        return manifestOwner.appId;
    }

    // 2. Check if a Lua plugin lists this depot in depots, dlcs, or manifests
    const DepotOwner_t luaOwner = FindDepotOwnerInLuaPlugins(depotId, steamRoot / "config" / "stplug-in");
    if (luaOwner.appId != 0 && luaOwner.appId != depotId)
    {
        return luaOwner.appId;
    }

    return 0;
}

std::vector<AppId_t> CCloudManager::GetChildDepotIdsForApp(AppId_t parentAppId) const
{
    std::vector<AppId_t> children;
    if (parentAppId == 0) return children;

    // In unit test mode where storageDir is set but steamRoot is not, do not leak host steamRoot
    if (!m_storageDir.empty() && m_steamRoot.empty()) return children;

    std::unordered_set<AppId_t> seen;
    auto addChild = [&](AppId_t id) {
        if (id == 0 || id == parentAppId) return;
        // Skip shared Steamworks Common Redistributables
        if (id >= 228980 && id <= 229007) return;
        if (seen.insert(id).second)
        {
            children.push_back(id);
        }
    };

    const fs::path steamRoot = GetSteamRoot();
    if (steamRoot.empty()) return children;
    const std::vector<fs::path> steamAppsDirs = GetSteamAppsDirs(steamRoot);
    std::error_code ec;

    // 1. Scan InstalledDepots in appmanifest_<parentAppId>.acf across library folders
    for (const auto& saDir : steamAppsDirs)
    {
        fs::path p = saDir / ("appmanifest_" + std::to_string(parentAppId) + ".acf");
        if (!fs::is_regular_file(p, ec)) continue;

        std::ifstream mf(p);
        if (!mf.is_open()) continue;

        std::string line;
        bool inInstalledDepots = false;
        int depotsDepth = 0;

        while (std::getline(mf, line))
        {
            if (!inInstalledDepots)
            {
                if (line.find("\"InstalledDepots\"") != std::string::npos)
                {
                    inInstalledDepots = true;
                    depotsDepth = 0;
                }
            }
            else
            {
                for (char c : line)
                {
                    if (c == '{') depotsDepth++;
                    else if (c == '}')
                    {
                        depotsDepth--;
                        if (depotsDepth <= 0)
                        {
                            inInstalledDepots = false;
                            break;
                        }
                    }
                }
                if (inInstalledDepots && depotsDepth == 1)
                {
                    size_t q1 = line.find('"');
                    if (q1 != std::string::npos)
                    {
                        size_t q2 = line.find('"', q1 + 1);
                        if (q2 != std::string::npos)
                        {
                            std::string key = line.substr(q1 + 1, q2 - q1 - 1);
                            if (IsDigitsOnly(key))
                            {
                                try
                                {
                                    addChild(static_cast<AppId_t>(std::stoul(key)));
                                }
                                catch (...) {}
                            }
                        }
                    }
                }
            }
        }
    }

    // 2. Scan Lua plugin config/stplug-in/<parentAppId>.lua
    const fs::path stplugDir = steamRoot / "config" / "stplug-in";
    const fs::path parentLua = stplugDir / (std::to_string(parentAppId) + ".lua");
    if (fs::is_regular_file(parentLua, ec))
    {
        std::ifstream lf(parentLua);
        if (lf.is_open())
        {
            std::string content((std::istreambuf_iterator<char>(lf)),
                                 std::istreambuf_iterator<char>());
            auto parsed = LuaParser::parse(content);
            for (const auto& [depotId, _] : parsed.depots)
            {
                if (IsDigitsOnly(depotId))
                {
                    try { addChild(static_cast<AppId_t>(std::stoul(depotId))); } catch (...) {}
                }
            }
            for (const auto& [dlcId, _] : parsed.dlcs)
            {
                if (IsDigitsOnly(dlcId))
                {
                    try { addChild(static_cast<AppId_t>(std::stoul(dlcId))); } catch (...) {}
                }
            }
            for (const auto& [manifestId, _] : parsed.manifests)
            {
                if (IsDigitsOnly(manifestId))
                {
                    try { addChild(static_cast<AppId_t>(std::stoul(manifestId))); } catch (...) {}
                }
            }
        }
    }

    // 3. Scan all other .lua files in stplugDir where primary addappid matches parentAppId
    if (fs::is_directory(stplugDir, ec))
    {
        const std::string parentStr = std::to_string(parentAppId);
        for (const auto& entry : fs::directory_iterator(stplugDir, fs::directory_options::skip_permission_denied, ec))
        {
            if (ec) break;
            if (!entry.is_regular_file(ec)) continue;
            if (entry.path().extension() != ".lua") continue;
            if (entry.path() == parentLua) continue;

            std::ifstream lf(entry.path());
            if (!lf.is_open()) continue;
            std::string content((std::istreambuf_iterator<char>(lf)),
                                 std::istreambuf_iterator<char>());
            auto parsed = LuaParser::parse(content);
            if (parsed.valid && parsed.appId == parentStr)
            {
                for (const auto& [depotId, _] : parsed.depots)
                {
                    if (IsDigitsOnly(depotId))
                    {
                        try { addChild(static_cast<AppId_t>(std::stoul(depotId))); } catch (...) {}
                    }
                }
                for (const auto& [dlcId, _] : parsed.dlcs)
                {
                    if (IsDigitsOnly(dlcId))
                    {
                        try { addChild(static_cast<AppId_t>(std::stoul(dlcId))); } catch (...) {}
                    }
                }
                for (const auto& [manifestId, _] : parsed.manifests)
                {
                    if (IsDigitsOnly(manifestId))
                    {
                        try { addChild(static_cast<AppId_t>(std::stoul(manifestId))); } catch (...) {}
                    }
                }
            }
        }
    }

    return children;
}

std::vector<CloudFileEntry_t> CCloudManager::ListAppCloudFilesDirect(AppId_t appId, const std::string& provider, uint32_t accountId) const
{
    std::vector<CloudFileEntry_t> results;
    if (appId == 0) return results;

    const std::string appIdStr = std::to_string(appId);
    const std::vector<uint32_t> accounts = ResolveCandidateAccountIds(accountId);
    const fs::path storageRoot = GetStorageDir();
    const bool isRemoteProvider = (!provider.empty() && provider != "local-only");
    std::error_code ec;

    // Map of logical fileName -> CloudFileEntry_t
    std::vector<std::pair<std::string, CloudFileEntry_t>> fileMap;
    auto upsertFile = [&](const std::string& logicalName, CloudFileEntry_t entry) {
        if (IsInternalMetadataRelPath(logicalName)) return;
        for (auto& kv : fileMap)
        {
            if (kv.first == logicalName)
            {
                if (entry.inRemoteCloud) kv.second.inRemoteCloud = true;
                if (entry.inLocalMirror) kv.second.inLocalMirror = true;
                if (!entry.remoteBlobPath.empty()) kv.second.remoteBlobPath = entry.remoteBlobPath;
                if (!entry.folder.empty() && (kv.second.folder.empty() || kv.second.folder == "SteamCloudRemote"))
                {
                    kv.second.folder = entry.folder;
                }
                if (entry.inRemoteCloud || entry.modifiedTime >= kv.second.modifiedTime || kv.second.sizeBytes == 0)
                {
                    if (entry.inRemoteCloud || entry.sizeBytes > 0 || kv.second.sizeBytes == 0)
                    {
                        kv.second.sizeBytes = entry.sizeBytes;
                    }
                    if (entry.modifiedTime > 0)
                    {
                        kv.second.modifiedTime = entry.modifiedTime;
                    }
                }
                return;
            }
        }
        fileMap.emplace_back(logicalName, std::move(entry));
    };

    // 1. Read local token metadata & local mirror files across candidate accounts
    std::vector<std::pair<std::string, std::string>> fileTokens;
    std::string rootToken;
    bool hasLocalSyncMarker = false;

    for (uint32_t acct : accounts)
    {
        const fs::path appDir = storageRoot / std::to_string(acct) / appIdStr;
        if (!fs::is_directory(appDir, ec)) continue;

        if (fs::is_regular_file(appDir / "cn.cloudredirect", ec) ||
            fs::is_regular_file(appDir / "manifest.cloudredirect", ec))
        {
            hasLocalSyncMarker = true;
        }

        // Read root_token.cloudredirect
        fs::path rtFile = appDir / "root_token.cloudredirect";
        if (fs::is_regular_file(rtFile, ec))
        {
            std::ifstream rf(rtFile);
            std::string line;
            if (std::getline(rf, line) && !line.empty())
            {
                rootToken = line;
            }
        }

        // Read file_tokens.cloudredirect (<relPath>\t<token>)
        fs::path ftFile = appDir / "file_tokens.cloudredirect";
        if (fs::is_regular_file(ftFile, ec))
        {
            std::ifstream ff(ftFile);
            std::string line;
            while (std::getline(ff, line))
            {
                size_t tab = line.find('\t');
                if (tab != std::string::npos)
                {
                    std::string fpath = line.substr(0, tab);
                    std::string ftok = line.substr(tab + 1);
                    fileTokens.emplace_back(fpath, ftok);
                }
            }
        }

        // Walk local files in <storageRoot>/<acct>/<appId>/
        for (auto it = fs::recursive_directory_iterator(appDir, fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec))
        {
            std::error_code fEc;
            if (!it->is_regular_file(fEc) || fEc) continue;
            fs::path rel = fs::relative(it->path(), appDir, fEc);
            if (fEc || rel.empty()) continue;
            std::string relStr = rel.generic_string();
            if (IsInternalMetadataRelPath(relStr)) continue;

            std::string matchedTok = rootToken;
            if (!relStr.empty() && relStr[0] == '%')
            {
                size_t slashPos = relStr.find('/');
                if (slashPos != std::string::npos && slashPos > 1 && relStr[slashPos - 1] == '%')
                {
                    matchedTok = relStr.substr(0, slashPos);
                    relStr = relStr.substr(slashPos + 1);
                }
            }
            for (const auto& [k, v] : fileTokens)
            {
                if (k == relStr)
                {
                    matchedTok = v;
                    break;
                }
            }

            uint64_t mtime = 0;
            auto ftime = fs::last_write_time(it->path(), fEc);
            if (!fEc)
            {
                auto sctp = std::chrono::clock_cast<std::chrono::system_clock>(ftime);
                mtime = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(sctp.time_since_epoch()).count());
            }

            CloudFileEntry_t entry;
            entry.fileName = relStr;
            entry.folder = NormalizeFolderToken(matchedTok, relStr);
            entry.sizeBytes = static_cast<uint64_t>(it->file_size(fEc));
            entry.modifiedTime = mtime;
            entry.inLocalMirror = true;
            if (hasLocalSyncMarker && isRemoteProvider)
            {
                entry.inRemoteCloud = true;
            }
            upsertFile(relStr, std::move(entry));
        }
    }

    // 2. Query remote files via cloud_redirect_cli only when the app isn't already materialized
    // in the local CloudRedirect mirror (with cn.cloudredirect / manifest.cloudredirect)
    if (isRemoteProvider && (!hasLocalSyncMarker || fileMap.empty()))
    {
        for (uint32_t acct : accounts)
        {
            const std::string acctStr = std::to_string(acct);
            const std::string out = RunCliCommand({"list-remote-app-files", provider, acctStr, appIdStr}, 15000);
            const std::string jsonStr = ExtractJsonSubstring(out);
            if (jsonStr.empty()) continue;

            size_t filesPos = jsonStr.find("\"files\"");
            if (filesPos == std::string::npos) continue;
            size_t arrStart = jsonStr.find('[', filesPos);
            if (arrStart == std::string::npos) continue;

            size_t pos = arrStart + 1;
            while (pos < jsonStr.size())
            {
                size_t objStart = jsonStr.find('{', pos);
                if (objStart == std::string::npos) break;
                size_t objEnd = jsonStr.find('}', objStart);
                if (objEnd == std::string::npos) break;

                std::string itemJson = jsonStr.substr(objStart, objEnd - objStart + 1);
                std::string rawPath = ExtractJsonString(itemJson, "path");
                uint64_t sz = ExtractJsonUint64(itemJson, "size", 0);
                uint64_t mtime = ExtractJsonUint64(itemJson, "modified_time", 0);

                // Strip leading "<acct>/<appId>/"
                std::string prefix = acctStr + "/" + appIdStr + "/";
                std::string rel = rawPath;
                if (rel.rfind(prefix, 0) == 0)
                {
                    rel = rel.substr(prefix.size());
                }

                // Remote user files live at "blobs/<logicalPath>/<hash>"
                if (rel.rfind("blobs/", 0) == 0)
                {
                    std::string afterBlobs = rel.substr(6);
                    size_t lastSlash = afterBlobs.rfind('/');
                    if (lastSlash != std::string::npos)
                    {
                        std::string tailSeg = afterBlobs.substr(lastSlash + 1);
                        if (IsHexHashSegment(tailSeg))
                        {
                            afterBlobs = afterBlobs.substr(0, lastSlash);
                        }
                    }
                    rel = afterBlobs;
                }

                if (!IsInternalMetadataRelPath(rel))
                {
                    std::string matchedTok = rootToken;
                    for (const auto& [k, v] : fileTokens)
                    {
                        if (k == rel)
                        {
                            matchedTok = v;
                            break;
                        }
                    }

                    CloudFileEntry_t entry;
                    entry.fileName = rel;
                    entry.folder = NormalizeFolderToken(matchedTok, rel);
                    entry.sizeBytes = sz;
                    entry.modifiedTime = mtime;
                    entry.remoteBlobPath = rawPath;
                    entry.inRemoteCloud = true;
                    upsertFile(rel, std::move(entry));
                }

                pos = objEnd + 1;
            }

            break; // First valid response from active account is authoritative
        }
    }

    // 3. When running in normal mode or when steamRoot is configured,
    // also include local saves in Steam userdata/<acct>/<appId>/remote, Goldberg paths,
    // Proton Auto-Cloud roots, and Lua/manifest files
    const fs::path steamRoot = GetSteamRoot();
    if ((m_storageDir.empty() || !m_steamRoot.empty()) && appId != 760 && !steamRoot.empty())
    {
        const fs::path userdataRoot = steamRoot / "userdata";

        // 3a. Steam userdata/<acct>/<appId>/remote/
        for (uint32_t acct : accounts)
        {
            const fs::path remoteDir = userdataRoot / std::to_string(acct) / appIdStr / "remote";
            if (!fs::is_directory(remoteDir, ec)) continue;
            for (auto it = fs::recursive_directory_iterator(remoteDir, fs::directory_options::skip_permission_denied, ec);
                 it != fs::recursive_directory_iterator(); it.increment(ec))
            {
                std::error_code fEc;
                if (!it->is_regular_file(fEc) || fEc) continue;
                fs::path rel = fs::relative(it->path(), remoteDir, fEc);
                if (fEc || rel.empty()) continue;
                std::string relStr = rel.generic_string();
                if (IsInternalMetadataRelPath(relStr)) continue;

                uint64_t mtime = 0;
                auto ftime = fs::last_write_time(it->path(), fEc);
                if (!fEc)
                {
                    auto sctp = std::chrono::clock_cast<std::chrono::system_clock>(ftime);
                    mtime = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(sctp.time_since_epoch()).count());
                }

                CloudFileEntry_t entry;
                entry.fileName = relStr;
                entry.folder = "SteamCloudRemote";
                entry.sizeBytes = static_cast<uint64_t>(it->file_size(fEc));
                entry.modifiedTime = mtime;
                entry.inLocalMirror = true;
                upsertFile(relStr, std::move(entry));
            }
        }

        // 3b. Proton Auto-Cloud directories containing steam_autocloud.vdf
        static const std::pair<const char*, const char*> kAutoCloudRoots[] = {
            {"AppData/LocalLow", "WinAppDataLocalLow"},
            {"AppData/Local", "WinAppDataLocal"},
            {"AppData/Roaming", "WinAppDataRoaming"},
            {"Documents", "WinMyDocuments"},
            {"Saved Games", "WinSavedGames"},
        };

        const fs::path steamuserDir = steamRoot / "steamapps" / "compatdata" / appIdStr / "pfx" / "drive_c" / "users" / "steamuser";
        if (fs::is_directory(steamuserDir, ec))
        {
            for (const auto& [relCat, catToken] : kAutoCloudRoots)
            {
                const fs::path catRoot = steamuserDir / relCat;
                if (!fs::is_directory(catRoot, ec)) continue;

                std::vector<fs::path> autoCloudDirs;
                for (auto it = fs::recursive_directory_iterator(catRoot, fs::directory_options::skip_permission_denied, ec);
                     it != fs::recursive_directory_iterator(); it.increment(ec))
                {
                    std::error_code dEc;
                    if (it->is_symlink(dEc))
                    {
                        if (it->is_directory(dEc)) it.disable_recursion_pending();
                        continue;
                    }
                    if (it->is_directory(dEc))
                    {
                        std::string dname = it->path().filename().string();
                        if (it.depth() >= 5 || dname == "Temp" || dname == "Microsoft" || dname == "Wine" ||
                            dname == "BACKUP" || dname == "Steam" || dname == "Unity" ||
                            dname == "UnrealEngine" || dname == "CrashReportClient" ||
                            dname == "D3DSCache" || dname == "fontconfig")
                        {
                            it.disable_recursion_pending();
                        }
                        continue;
                    }
                    if (it->is_regular_file(dEc) && it->path().filename() == "steam_autocloud.vdf")
                    {
                        autoCloudDirs.push_back(it->path().parent_path());
                    }
                }

                for (const auto& acDir : autoCloudDirs)
                {
                    for (auto it = fs::recursive_directory_iterator(acDir, fs::directory_options::skip_permission_denied, ec);
                         it != fs::recursive_directory_iterator(); it.increment(ec))
                    {
                        std::error_code fEc;
                        if (!it->is_regular_file(fEc) || fEc) continue;
                        fs::path rel = fs::relative(it->path(), catRoot, fEc);
                        if (fEc || rel.empty()) continue;
                        std::string relStr = rel.generic_string();
                        if (IsInternalMetadataRelPath(relStr)) continue;

                        uint64_t mtime = 0;
                        auto ftime = fs::last_write_time(it->path(), fEc);
                        if (!fEc)
                        {
                            auto sctp = std::chrono::clock_cast<std::chrono::system_clock>(ftime);
                            mtime = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(sctp.time_since_epoch()).count());
                        }

                        CloudFileEntry_t entry;
                        entry.fileName = relStr;
                        entry.folder = catToken;
                        entry.sizeBytes = static_cast<uint64_t>(it->file_size(fEc));
                        entry.modifiedTime = mtime;
                        entry.inLocalMirror = true;
                        upsertFile(relStr, std::move(entry));
                    }
                }
            }
        }

        // 3c. Goldberg save paths (only when not already materialized in CloudRedirect storage)
        if (fileMap.empty())
        {
            for (const auto& goldbergDir : ResolveGoldbergSavePaths(appId))
            {
                if (!fs::is_directory(goldbergDir, ec)) continue;
                for (auto it = fs::recursive_directory_iterator(goldbergDir, fs::directory_options::skip_permission_denied, ec);
                     it != fs::recursive_directory_iterator(); it.increment(ec))
                {
                    std::error_code fEc;
                    if (!it->is_regular_file(fEc) || fEc) continue;
                    fs::path rel = fs::relative(it->path(), goldbergDir, fEc);
                    if (fEc || rel.empty()) continue;
                    std::string relStr = rel.generic_string();
                    if (IsInternalMetadataRelPath(relStr)) continue;

                    uint64_t mtime = 0;
                    auto ftime = fs::last_write_time(it->path(), fEc);
                    if (!fEc)
                    {
                        auto sctp = std::chrono::clock_cast<std::chrono::system_clock>(ftime);
                        mtime = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(sctp.time_since_epoch()).count());
                    }

                    CloudFileEntry_t entry;
                    entry.fileName = relStr;
                    entry.folder = "WinAppDataRoaming";
                    entry.sizeBytes = static_cast<uint64_t>(it->file_size(fEc));
                    entry.modifiedTime = mtime;
                    entry.inLocalMirror = true;
                    upsertFile(relStr, std::move(entry));
                }
            }
        }

        // 3d. Steam Lua unlocker plugin and depot manifest files
        const fs::path stplugDir = steamRoot / "config" / "stplug-in";
        const fs::path depotcacheDir = steamRoot / "depotcache";
        const fs::path legacyDepotcacheDir = steamRoot / "config" / "depotcache";

        // Check for <appId>.lua in stplug-in
        const fs::path luaFile = stplugDir / (appIdStr + ".lua");
        if (fs::is_regular_file(luaFile, ec) && !ec)
        {
            uint64_t mtime = 0;
            auto ftime = fs::last_write_time(luaFile, ec);
            if (!ec)
            {
                auto sctp = std::chrono::clock_cast<std::chrono::system_clock>(ftime);
                mtime = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(sctp.time_since_epoch()).count());
            }

            std::string luaFn = "stplug-in/" + appIdStr + ".lua";
            CloudFileEntry_t entry;
            entry.fileName = luaFn;
            entry.folder = "stplug-in";
            entry.sizeBytes = static_cast<uint64_t>(fs::file_size(luaFile, ec));
            entry.modifiedTime = mtime;
            entry.inLocalMirror = true;
            upsertFile(luaFn, std::move(entry));

            // Check for sidecar provider file
            const fs::path provFile = stplugDir / (appIdStr + ".provider");
            if (fs::is_regular_file(provFile, ec) && !ec)
            {
                uint64_t pmtime = 0;
                auto pftime = fs::last_write_time(provFile, ec);
                if (!ec)
                {
                    auto sctp = std::chrono::clock_cast<std::chrono::system_clock>(pftime);
                    pmtime = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(sctp.time_since_epoch()).count());
                }
                std::string provFn = "stplug-in/" + appIdStr + ".provider";
                CloudFileEntry_t pEntry;
                pEntry.fileName = provFn;
                pEntry.folder = "stplug-in";
                pEntry.sizeBytes = static_cast<uint64_t>(fs::file_size(provFile, ec));
                pEntry.modifiedTime = pmtime;
                pEntry.inLocalMirror = true;
                upsertFile(provFn, std::move(pEntry));
            }

            // Parse Lua file to discover depot manifest files
            auto luaData = LuaParser::parseFile(luaFile.string());
            for (const auto& [depotId, manifestGid] : luaData.manifests)
            {
                if (manifestGid.empty() || manifestGid == "0") continue;
                std::string mfName = depotId + "_" + manifestGid + ".manifest";
                fs::path mfPath;
                if (fs::is_regular_file(depotcacheDir / mfName, ec))
                {
                    mfPath = depotcacheDir / mfName;
                }
                else if (!legacyDepotcacheDir.empty() && fs::is_regular_file(legacyDepotcacheDir / mfName, ec))
                {
                    mfPath = legacyDepotcacheDir / mfName;
                }
                else if (fs::is_regular_file(stplugDir / "manifests" / mfName, ec))
                {
                    mfPath = stplugDir / "manifests" / mfName;
                }
                else if (fs::is_regular_file(stplugDir / mfName, ec))
                {
                    mfPath = stplugDir / mfName;
                }

                if (!mfPath.empty())
                {
                    uint64_t mmtime = 0;
                    auto mftime = fs::last_write_time(mfPath, ec);
                    if (!ec)
                    {
                        auto sctp = std::chrono::clock_cast<std::chrono::system_clock>(mftime);
                        mmtime = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(sctp.time_since_epoch()).count());
                    }

                    std::string fullMfName = "depotcache/" + mfName;
                    CloudFileEntry_t mfEntry;
                    mfEntry.fileName = fullMfName;
                    mfEntry.folder = "depotcache";
                    mfEntry.sizeBytes = static_cast<uint64_t>(fs::file_size(mfPath, ec));
                    mfEntry.modifiedTime = mmtime;
                    mfEntry.inLocalMirror = true;
                    upsertFile(fullMfName, std::move(mfEntry));
                }
            }
        }

        // Also check for any <appId>_*.manifest or <appId>.manifest directly
        auto scanDirForAppManifests = [&](const fs::path& dir) {
            if (!fs::is_directory(dir, ec)) return;
            for (const auto& entry : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec))
            {
                if (ec) break;
                if (!entry.is_regular_file(ec)) continue;
                std::string fn = entry.path().filename().string();
                if (fn.ends_with(".manifest") && (fn.rfind(appIdStr + "_", 0) == 0 || fn == appIdStr + ".manifest"))
                {
                    uint64_t mmtime = 0;
                    auto mftime = fs::last_write_time(entry.path(), ec);
                    if (!ec)
                    {
                        auto sctp = std::chrono::clock_cast<std::chrono::system_clock>(mftime);
                        mmtime = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(sctp.time_since_epoch()).count());
                    }

                    std::string fullAppMfName = "depotcache/" + fn;
                    CloudFileEntry_t mfEntry;
                    mfEntry.fileName = fullAppMfName;
                    mfEntry.folder = "depotcache";
                    mfEntry.sizeBytes = static_cast<uint64_t>(fs::file_size(entry.path(), ec));
                    mfEntry.modifiedTime = mmtime;
                    mfEntry.inLocalMirror = true;
                    upsertFile(fullAppMfName, std::move(mfEntry));
                }
            }
        };

        scanDirForAppManifests(depotcacheDir);
        scanDirForAppManifests(legacyDepotcacheDir);
        scanDirForAppManifests(stplugDir);
        scanDirForAppManifests(stplugDir / "manifests");
    }

    results.reserve(fileMap.size());
    for (auto& kv : fileMap)
    {
        results.push_back(std::move(kv.second));
    }

    std::sort(results.begin(), results.end(), [](const CloudFileEntry_t& a, const CloudFileEntry_t& b) {
        return a.fileName < b.fileName;
    });

    return results;
}

std::vector<CloudFileEntry_t> CCloudManager::ListAppCloudFiles(AppId_t appId, const std::string& provider, uint32_t accountId)
{
    // 1. Files belonging directly to base game
    std::vector<CloudFileEntry_t> results = ListAppCloudFilesDirect(appId, provider, accountId);

    // 2. Discover and merge files for all child depots / addons
    const std::vector<AppId_t> childDepots = GetChildDepotIdsForApp(appId);
    for (AppId_t childId : childDepots)
    {
        std::vector<CloudFileEntry_t> childFiles = ListAppCloudFilesDirect(childId, provider, accountId);
        for (auto& cf : childFiles)
        {
            // Do not duplicate files already added by the base game (e.g. depot manifests referenced in Lua)
            bool alreadyExists = false;
            for (const auto& existing : results)
            {
                if (existing.fileName == cf.fileName)
                {
                    alreadyExists = true;
                    break;
                }
            }
            if (alreadyExists)
            {
                continue;
            }

            // Tag folder label: "Depot <childId> / <folder>"
            cf.folder = "Depot " + std::to_string(childId) + " / " + (cf.folder.empty() ? "SteamCloudRemote" : cf.folder);
            results.push_back(std::move(cf));
        }
    }

    std::sort(results.begin(), results.end(), [](const CloudFileEntry_t& a, const CloudFileEntry_t& b) {
        if (a.folder != b.folder) return a.folder < b.folder;
        return a.fileName < b.fileName;
    });

    return results;
}

std::vector<CloudAppSummary_t> CCloudManager::ListCloudApps(const std::string& provider, uint32_t accountId)
{
    std::vector<CloudAppSummary_t> summaries;
    const std::vector<uint32_t> accounts = ResolveCandidateAccountIds(accountId);
    const fs::path storageRoot = GetStorageDir();
    std::error_code ec;

    std::vector<AppId_t> candidateApps;
    std::vector<AppId_t> remoteConfirmedApps;
    std::vector<AppId_t> localConfirmedApps;

    auto addCandidate = [&](AppId_t id) {
        if (id == 0) return;
        if (std::find(candidateApps.begin(), candidateApps.end(), id) == candidateApps.end())
        {
            candidateApps.push_back(id);
        }
    };

    // 1. Scan remote cloud provider first using fast list-remote-app-ids (~0.7s vs ~4.7s for list-remote-apps)
    const bool isRemoteProvider = (!provider.empty() && provider != "local-only");
    if (isRemoteProvider)
    {
        for (uint32_t acct : accounts)
        {
            std::string out = RunCliCommand({"list-remote-app-ids", provider, std::to_string(acct)}, 15000);
            std::string jsonStr = ExtractJsonSubstring(out);
            if (jsonStr.empty())
            {
                out = RunCliCommand({"list-remote-apps", provider, std::to_string(acct)}, 15000);
                jsonStr = ExtractJsonSubstring(out);
            }
            if (jsonStr.empty()) continue;

            // Parse "app_ids":["250900","3856460"] array if present
            size_t idsKey = jsonStr.find("\"app_ids\"");
            if (idsKey != std::string::npos)
            {
                size_t arrStart = jsonStr.find('[', idsKey);
                size_t arrEnd = (arrStart != std::string::npos) ? jsonStr.find(']', arrStart) : std::string::npos;
                if (arrStart != std::string::npos && arrEnd != std::string::npos)
                {
                    size_t qPos = arrStart + 1;
                    while (qPos < arrEnd)
                    {
                        size_t q1 = jsonStr.find('"', qPos);
                        if (q1 == std::string::npos || q1 >= arrEnd) break;
                        size_t q2 = jsonStr.find('"', q1 + 1);
                        if (q2 == std::string::npos || q2 >= arrEnd) break;
                        std::string appIdStr = jsonStr.substr(q1 + 1, q2 - q1 - 1);
                        if (IsDigitsOnly(appIdStr))
                        {
                            AppId_t id = static_cast<AppId_t>(std::strtoul(appIdStr.c_str(), nullptr, 10));
                            addCandidate(id);
                            remoteConfirmedApps.push_back(id);
                        }
                        qPos = q2 + 1;
                    }
                }
            }

            // Also parse {"apps":[{"app_id":"250900"}]} format for fallback/mock compatibility
            size_t pos = 0;
            while (pos < jsonStr.size())
            {
                size_t keyPos = jsonStr.find("\"app_id\"", pos);
                if (keyPos == std::string::npos) break;
                size_t objStart = jsonStr.rfind('{', keyPos);
                size_t objEnd = jsonStr.find('}', keyPos);
                if (objStart == std::string::npos || objEnd == std::string::npos) break;

                std::string itemJson = jsonStr.substr(objStart, objEnd - objStart + 1);
                std::string appIdStr = ExtractJsonString(itemJson, "app_id");
                if (IsDigitsOnly(appIdStr))
                {
                    AppId_t id = static_cast<AppId_t>(std::strtoul(appIdStr.c_str(), nullptr, 10));
                    addCandidate(id);
                    if (std::find(remoteConfirmedApps.begin(), remoteConfirmedApps.end(), id) == remoteConfirmedApps.end())
                    {
                        remoteConfirmedApps.push_back(id);
                    }
                }
                pos = objEnd + 1;
            }

            break; // First valid response from active account is authoritative
        }
    }

    // 2. Also scan local CloudRedirect storage directories (<storageDir>/<acct>/<appId>)
    if (fs::exists(storageRoot, ec) && fs::is_directory(storageRoot, ec))
    {
        for (uint32_t acct : accounts)
        {
            fs::path acctDir = storageRoot / std::to_string(acct);
            if (!fs::is_directory(acctDir, ec)) continue;
            for (const auto& entry : fs::directory_iterator(acctDir, ec))
            {
                if (!entry.is_directory(ec)) continue;
                std::string name = entry.path().filename().string();
                if (IsDigitsOnly(name))
                {
                    AppId_t id = static_cast<AppId_t>(std::strtoul(name.c_str(), nullptr, 10));
                    addCandidate(id);
                    localConfirmedApps.push_back(id);
                }
            }
        }
    }

    // 2b. In production mode (or when steamRoot is configured), also discover apps with local saves in Steam userdata/<acct>/<appId>/remote,
    // Proton compatdata/<appId>/pfx/drive_c/users/steamuser, and games with Lua plugins or depot manifests
    const fs::path steamRoot = GetSteamRoot();
    if ((m_storageDir.empty() || !m_steamRoot.empty()) && !steamRoot.empty())
    {
        const fs::path userdataRoot = steamRoot / "userdata";
        if (fs::is_directory(userdataRoot, ec))
        {
            for (uint32_t acct : accounts)
            {
                const fs::path acctDir = userdataRoot / std::to_string(acct);
                if (!fs::is_directory(acctDir, ec)) continue;
                for (const auto& entry : fs::directory_iterator(acctDir, ec))
                {
                    if (!entry.is_directory(ec)) continue;
                    std::string name = entry.path().filename().string();
                    if (!IsDigitsOnly(name)) continue;
                    AppId_t id = static_cast<AppId_t>(std::strtoul(name.c_str(), nullptr, 10));
                    if (id == 0 || id == 760) continue;
                    if (fs::is_directory(entry.path() / "remote", ec))
                    {
                        addCandidate(id);
                        localConfirmedApps.push_back(id);
                    }
                }
            }
        }

        const fs::path compatRoot = steamRoot / "steamapps" / "compatdata";
        if (fs::is_directory(compatRoot, ec))
        {
            for (const auto& entry : fs::directory_iterator(compatRoot, ec))
            {
                if (!entry.is_directory(ec)) continue;
                std::string name = entry.path().filename().string();
                if (!IsDigitsOnly(name)) continue;
                AppId_t id = static_cast<AppId_t>(std::strtoul(name.c_str(), nullptr, 10));
                if (id == 0) continue;
                if (fs::is_directory(entry.path() / "pfx" / "drive_c" / "users" / "steamuser", ec))
                {
                    addCandidate(id);
                    localConfirmedApps.push_back(id);
                }
            }
        }

        // 2c. Discover apps with Lua plugins and depot manifests in stplug-in and depotcache
        std::unordered_set<AppId_t> knownDepotIds;
        const fs::path stplugDir = steamRoot / "config" / "stplug-in";
        if (fs::is_directory(stplugDir, ec))
        {
            for (const auto& entry : fs::directory_iterator(stplugDir, fs::directory_options::skip_permission_denied, ec))
            {
                if (ec) break;
                if (!entry.is_regular_file(ec)) continue;
                const auto& path = entry.path();
                if (path.extension() == ".lua")
                {
                    std::string stem = path.stem().string();
                    if (IsDigitsOnly(stem))
                    {
                        AppId_t id = static_cast<AppId_t>(std::strtoul(stem.c_str(), nullptr, 10));
                        if (id != 0)
                        {
                            addCandidate(id);
                            localConfirmedApps.push_back(id);
                        }
                    }
                    else
                    {
                        std::ifstream lf(path);
                        if (lf.is_open())
                        {
                            std::string content((std::istreambuf_iterator<char>(lf)),
                                                 std::istreambuf_iterator<char>());
                            size_t pos = 0;
                            while ((pos = content.find("addappid(", pos)) != std::string::npos)
                            {
                                pos += 9;
                                size_t endPos = pos;
                                while (endPos < content.length() && std::isdigit(static_cast<unsigned char>(content[endPos])))
                                {
                                    endPos++;
                                }
                                if (endPos > pos)
                                {
                                    try
                                    {
                                        AppId_t id = static_cast<AppId_t>(std::stoul(content.substr(pos, endPos - pos)));
                                        if (id != 0)
                                        {
                                            addCandidate(id);
                                            localConfirmedApps.push_back(id);
                                        }
                                    }
                                    catch (...) {}
                                }
                                pos = endPos;
                            }
                        }
                    }

                    // Parse Lua plugin to collect all child depot IDs
                    auto luaData = LuaParser::parseFile(path.string());
                    for (const auto& [depotIdStr, _] : luaData.manifests)
                    {
                        if (IsDigitsOnly(depotIdStr))
                        {
                            try { knownDepotIds.insert(static_cast<AppId_t>(std::stoul(depotIdStr))); } catch (...) {}
                        }
                    }
                }
                else if (path.extension() == ".manifest")
                {
                    std::string stem = path.stem().string();
                    size_t underscore = stem.find('_');
                    std::string appIdStr = (underscore != std::string::npos) ? stem.substr(0, underscore) : stem;
                    if (IsDigitsOnly(appIdStr))
                    {
                        AppId_t id = static_cast<AppId_t>(std::strtoul(appIdStr.c_str(), nullptr, 10));
                        if (id != 0 && !knownDepotIds.contains(id))
                        {
                            if (!ResolveAppTitleFromManifests(id).empty() ||
                                fs::exists(steamRoot / "steamapps" / ("appmanifest_" + appIdStr + ".acf"), ec))
                            {
                                addCandidate(id);
                                localConfirmedApps.push_back(id);
                            }
                        }
                    }
                }
            }
        }

        const fs::path depotcacheDir = steamRoot / "depotcache";
        if (fs::is_directory(depotcacheDir, ec))
        {
            for (const auto& entry : fs::directory_iterator(depotcacheDir, fs::directory_options::skip_permission_denied, ec))
            {
                if (ec) break;
                if (!entry.is_regular_file(ec)) continue;
                if (entry.path().extension() == ".manifest")
                {
                    std::string stem = entry.path().stem().string();
                    size_t underscore = stem.find('_');
                    std::string appIdStr = (underscore != std::string::npos) ? stem.substr(0, underscore) : stem;
                    if (IsDigitsOnly(appIdStr))
                    {
                        AppId_t id = static_cast<AppId_t>(std::strtoul(appIdStr.c_str(), nullptr, 10));
                        if (id != 0 && !knownDepotIds.contains(id))
                        {
                            if (!ResolveAppTitleFromManifests(id).empty() ||
                                fs::exists(steamRoot / "steamapps" / ("appmanifest_" + appIdStr + ".acf"), ec))
                            {
                                addCandidate(id);
                                localConfirmedApps.push_back(id);
                            }
                        }
                    }
                }
            }
        }
    }

    // 3. Group child depots / addons into their owning parent games.
    std::vector<AppId_t> topLevelApps;
    std::unordered_map<AppId_t, std::vector<AppId_t>> parentToChildren;

    for (AppId_t id : candidateApps)
    {
        AppId_t parentId = GetParentAppIdForDepot(id);
        if (parentId != 0 && parentId != id)
        {
            if (std::find(topLevelApps.begin(), topLevelApps.end(), parentId) == topLevelApps.end())
            {
                topLevelApps.push_back(parentId);
            }
            auto& cList = parentToChildren[parentId];
            if (std::find(cList.begin(), cList.end(), id) == cList.end())
            {
                cList.push_back(id);
            }
        }
        else
        {
            if (std::find(topLevelApps.begin(), topLevelApps.end(), id) == topLevelApps.end())
            {
                topLevelApps.push_back(id);
            }
        }
    }

    // Also collect any other known child depots for each top-level app
    for (AppId_t parentId : topLevelApps)
    {
        for (AppId_t cid : GetChildDepotIdsForApp(parentId))
        {
            auto& cList = parentToChildren[parentId];
            if (std::find(cList.begin(), cList.end(), cid) == cList.end())
            {
                cList.push_back(cid);
            }
        }
    }

    // 4. For each top-level app, compute exact user save file count & total size
    // across the main game and all its child depots/addons.
    for (AppId_t appId : topLevelApps)
    {
        const auto& children = parentToChildren[appId];

        bool isRemoteApp = (std::find(remoteConfirmedApps.begin(), remoteConfirmedApps.end(), appId) != remoteConfirmedApps.end());
        for (AppId_t cid : children)
        {
            if (std::find(remoteConfirmedApps.begin(), remoteConfirmedApps.end(), cid) != remoteConfirmedApps.end())
            {
                isRemoteApp = true;
                break;
            }
        }

        const std::string effectiveProvider = (isRemoteProvider && isRemoteApp) ? provider : "local-only";
        std::vector<CloudFileEntry_t> files = ListAppCloudFiles(appId, effectiveProvider, accountId);
        if (files.empty())
        {
            continue;
        }

        CloudAppSummary_t summary;
        summary.appId = appId;
        summary.childAppIds = children;
        summary.gameTitle = ResolveAppTitleFromManifests(appId);
        summary.fileCount = static_cast<uint32_t>(files.size());
        summary.inRemoteCloud = isRemoteApp;
        summary.inLocalMirror = (std::find(localConfirmedApps.begin(), localConfirmedApps.end(), appId) != localConfirmedApps.end());
        for (AppId_t cid : children)
        {
            if (std::find(localConfirmedApps.begin(), localConfirmedApps.end(), cid) != localConfirmedApps.end())
            {
                summary.inLocalMirror = true;
                break;
            }
        }

        uint64_t totalBytes = 0;
        for (const auto& f : files)
        {
            totalBytes += f.sizeBytes;
            if (f.inRemoteCloud) summary.inRemoteCloud = true;
            if (f.inLocalMirror) summary.inLocalMirror = true;
        }
        summary.totalSizeBytes = totalBytes;
        summaries.push_back(std::move(summary));
    }

    std::sort(summaries.begin(), summaries.end(), [](const CloudAppSummary_t& a, const CloudAppSummary_t& b) {
        if (!a.gameTitle.empty() && !b.gameTitle.empty() && a.gameTitle != b.gameTitle)
        {
            return a.gameTitle < b.gameTitle;
        }
        return a.appId < b.appId;
    });

    return summaries;
}

bool CCloudManager::ReadAppCloudFileDirect(AppId_t appId,
                                           const std::string& relativeFileName,
                                           const std::string& provider,
                                           uint32_t accountId,
                                           std::string& outContent,
                                           std::string& outDownloadName,
                                     std::string& outError)
{
    outContent.clear();
    outDownloadName.clear();
    outError.clear();

    if (appId == 0 || relativeFileName.empty() || IsInternalMetadataRelPath(relativeFileName))
    {
        outError = "Invalid cloud file path";
        return false;
    }

    fs::path relPath = fs::path(relativeFileName).lexically_normal();
    std::string relNorm = relPath.generic_string();
    if (relNorm.empty() || relNorm[0] == '/' || relNorm.find("..") != std::string::npos)
    {
        outError = "Path traversal rejected";
        return false;
    }

    outDownloadName = relPath.filename().string();
    if (outDownloadName.empty())
    {
        outDownloadName = "cloud_save.dat";
    }

    const std::string appIdStr = std::to_string(appId);
    const std::vector<uint32_t> accounts = ResolveCandidateAccountIds(accountId);
    const fs::path storageRoot = GetStorageDir();
    std::error_code ec;

    auto tryReadFromDisk = [&](const fs::path& fullPath) -> bool {
        if (!fs::is_regular_file(fullPath, ec) || ec) return false;
        std::ifstream in(fullPath, std::ios::binary);
        if (!in.is_open()) return false;
        std::ostringstream ss;
        ss << in.rdbuf();
        outContent = ss.str();
        return true;
    };

    // 1. Check local CloudRedirect storage mirror (<storageDir>/<acct>/<appId>/<relPath>)
    for (uint32_t acct : accounts)
    {
        fs::path appDir = storageRoot / std::to_string(acct) / appIdStr;
        if (tryReadFromDisk(appDir / relPath))
        {
            return true;
        }
        if (fs::is_directory(appDir, ec))
        {
            for (const auto& entry : fs::directory_iterator(appDir, ec))
            {
                if (!entry.is_directory(ec)) continue;
                std::string dname = entry.path().filename().string();
                if (dname.size() > 2 && dname.front() == '%' && dname.back() == '%')
                {
                    if (tryReadFromDisk(entry.path() / relPath))
                    {
                        return true;
                    }
                }
            }
        }
    }

    // 2. If not materialized locally and a remote provider is active, sync remote app first
    if (!provider.empty() && provider != "local-only" && !accounts.empty())
    {
        fs::path cloudRoot = storageRoot.parent_path();
        for (uint32_t acct : accounts)
        {
            RunCliCommand({"sync-remote-app", provider, std::to_string(acct), appIdStr, cloudRoot.string()}, 20000);
            fs::path candidate = storageRoot / std::to_string(acct) / appIdStr / relPath;
            if (tryReadFromDisk(candidate))
            {
                return true;
            }
        }
    }

    // 3. Fallback: check Goldberg save directories, Steam userdata remote directory, and Proton Auto-Cloud roots
    for (const auto& goldbergDir : ResolveGoldbergSavePaths(appId))
    {
        if (tryReadFromDisk(goldbergDir / relPath) || tryReadFromDisk(goldbergDir / relPath.filename()))
        {
            return true;
        }
    }

    const fs::path steamRoot = GetSteamRoot();
    const fs::path userdataRoot = steamRoot / "userdata";
    for (uint32_t acct : accounts)
    {
        fs::path remoteCandidate = userdataRoot / std::to_string(acct) / appIdStr / "remote" / relPath;
        if (tryReadFromDisk(remoteCandidate))
        {
            return true;
        }
    }

    const fs::path steamuserDir = steamRoot / "steamapps" / "compatdata" / appIdStr / "pfx" / "drive_c" / "users" / "steamuser";
    if (fs::is_directory(steamuserDir, ec))
    {
        for (const char* relCat : {"AppData/LocalLow", "AppData/Local", "AppData/Roaming", "Documents", "Saved Games"})
        {
            if (tryReadFromDisk(steamuserDir / relCat / relPath))
            {
                return true;
            }
        }
    }

    const fs::path stplugDir = steamRoot / "config" / "stplug-in";
    if (tryReadFromDisk(stplugDir / relPath) || tryReadFromDisk(stplugDir / relPath.filename()))
    {
        return true;
    }
    const fs::path depotcacheDir = steamRoot / "depotcache";
    if (tryReadFromDisk(depotcacheDir / relPath) || tryReadFromDisk(depotcacheDir / relPath.filename()))
    {
        return true;
    }
    const fs::path legacyDepotcache = steamRoot / "config" / "depotcache";
    if (tryReadFromDisk(legacyDepotcache / relPath) || tryReadFromDisk(legacyDepotcache / relPath.filename()))
    {
        return true;
    }

    outError = "Cloud file not found: " + relativeFileName;
    return false;
}

bool CCloudManager::ReadAppCloudFile(AppId_t appId,
                                     const std::string& relativeFileName,
                                     const std::string& provider,
                                     uint32_t accountId,
                                     std::string& outContent,
                                     std::string& outDownloadName,
                                     std::string& outError)
{
    if (appId == 0 || relativeFileName.empty())
    {
        outError = "Invalid cloud file path";
        return false;
    }

    // 1. Try reading directly from the main game
    if (ReadAppCloudFileDirect(appId, relativeFileName, provider, accountId, outContent, outDownloadName, outError))
    {
        return true;
    }

    // 2. If relativeFileName has a depot prefix like "depots/<id>/", parse and check target
    std::string cleanName = relativeFileName;
    if (cleanName.rfind("depots/", 0) == 0)
    {
        size_t nextSlash = cleanName.find('/', 7);
        if (nextSlash != std::string::npos)
        {
            std::string idStr = cleanName.substr(7, nextSlash - 7);
            if (IsDigitsOnly(idStr))
            {
                AppId_t targetId = static_cast<AppId_t>(std::strtoul(idStr.c_str(), nullptr, 10));
                std::string subPath = cleanName.substr(nextSlash + 1);
                if (ReadAppCloudFileDirect(targetId, subPath, provider, accountId, outContent, outDownloadName, outError))
                {
                    return true;
                }
            }
        }
    }

    // 3. Check all child depots / addons of this game
    const std::vector<AppId_t> childDepots = GetChildDepotIdsForApp(appId);
    for (AppId_t childId : childDepots)
    {
        if (ReadAppCloudFileDirect(childId, relativeFileName, provider, accountId, outContent, outDownloadName, outError))
        {
            return true;
        }
    }

    outError = "Cloud file not found: " + relativeFileName;
    return false;
}

} // namespace Navi::CloudRedirect



