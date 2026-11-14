/**
 * @file  cloud_manager.cpp
 * @brief CloudRedirect CLI subprocess orchestration and cloud save manager
 */
#include "cloud_manager.hpp"
#include "cloudredirect.hpp"
#include "paths.hpp"
#include "lua_parser.hpp"
#include "config.hpp"
#include "atomic_file.hpp"
#include "log.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <atomic>
#include <sys/poll.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <yaml-cpp/yaml.h>

namespace
{
    // Test-only scan instrumentation (see ScanCounters in cloud_manager.hpp).
    // These must be bumped at every appmanifest/lua file the inventory path
    // opens so unit tests can pin scan-once behavior. They never gate logic.
    std::atomic<uint64_t> s_scanManifestFilesRead{0};
    std::atomic<uint64_t> s_scanLuaFilesParsed{0};
    std::atomic<uint64_t> s_scanFileUpsertProbes{0};
    std::atomic<uint64_t> s_scanFileDedupProbes{0};

    inline void NoteManifestFileRead()
    {
        s_scanManifestFilesRead.fetch_add(1, std::memory_order_relaxed);
    }
    inline void NoteLuaFileParsed()
    {
        s_scanLuaFilesParsed.fetch_add(1, std::memory_order_relaxed);
    }
    inline void NoteFileUpsertProbe()
    {
        s_scanFileUpsertProbes.fetch_add(1, std::memory_order_relaxed);
    }
    inline void NoteFileDedupProbe()
    {
        s_scanFileDedupProbes.fetch_add(1, std::memory_order_relaxed);
    }
} // namespace

namespace Navi::CloudRedirect
{

static std::vector<fs::path> GetSteamAppsDirs(const fs::path& steamRoot);

namespace
{
    void BuildScanIndexInto(const fs::path& steamRoot,
                            const std::vector<fs::path>& steamAppsDirs,
                            const fs::path& stplugDir,
                            ScanIndex& out);
}

CCloudManager& CCloudManager::Instance()
{
    static CCloudManager instance;
    return instance;
}

CCloudManager::CCloudManager() = default;

void CCloudManager::SetCliPathForTest(const fs::path& path)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_cliPath = path;
    }
    InvalidateCloudCatalogCache();
}

void CCloudManager::SetBackupDirForTest(const fs::path& path)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_backupDir = path;
}

void CCloudManager::SetStorageDirForTest(const fs::path& path)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_storageDir = path;
    }
    InvalidateCloudCatalogCache();
}

void CCloudManager::SetSteamAppsRootForTest(const fs::path& path)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_steamAppsRoot = path;
}

void CCloudManager::SetSteamRootForTest(const fs::path& path)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_steamRoot = path;
    }
    InvalidateCloudCatalogCache();
}

ScanCounters CCloudManager::GetScanCountersForTest() const
{
    ScanCounters c;
    c.manifestFilesRead = s_scanManifestFilesRead.load(std::memory_order_relaxed);
    c.luaFilesParsed = s_scanLuaFilesParsed.load(std::memory_order_relaxed);
    c.fileUpsertProbes = s_scanFileUpsertProbes.load(std::memory_order_relaxed);
    c.fileDedupProbes = s_scanFileDedupProbes.load(std::memory_order_relaxed);
    return c;
}

void CCloudManager::ResetScanCountersForTest()
{
    s_scanManifestFilesRead.store(0, std::memory_order_relaxed);
    s_scanLuaFilesParsed.store(0, std::memory_order_relaxed);
    s_scanFileUpsertProbes.store(0, std::memory_order_relaxed);
    s_scanFileDedupProbes.store(0, std::memory_order_relaxed);
}

std::shared_ptr<const ScanIndex> CCloudManager::ObtainScanIndex() const
{
    const fs::path steamRoot = GetSteamRoot();
    // In unit test mode where storageDir is set but steamRoot is not, do not
    // leak the host steamRoot into library lookups (legacy scan guard).
    const bool steamRootUsable = !steamRoot.empty() && (m_storageDir.empty() || !m_steamRoot.empty());
    if (!steamRootUsable)
    {
        auto empty = std::make_shared<ScanIndex>();
        empty->steamRootMissing = true;
        return empty;
    }

    const uint64_t stamp = ComputeScanIndexStamp();
    {
        std::lock_guard<std::mutex> lock(m_scanIndexMutex);
        if (m_scanIndexCache && m_scanIndexStamp == stamp)
        {
            return m_scanIndexCache;
        }
    }

    // Build outside the cache lock: one pass over appmanifests + lua plugins.
    auto index = std::make_shared<ScanIndex>();
    BuildScanIndexInto(steamRoot, GetSteamAppsDirs(steamRoot), steamRoot / "config" / "stplug-in", *index);

    std::lock_guard<std::mutex> lock(m_scanIndexMutex);
    if (m_scanIndexCache && m_scanIndexStamp == stamp)
    {
        return m_scanIndexCache; // another thread won the build race
    }
    m_scanIndexCache = std::move(index);
    m_scanIndexStamp = stamp;
    return m_scanIndexCache;
}

uint64_t CCloudManager::ComputeScanIndexStamp() const
{
    // FNV-1a over the library state the inventory depends on: directory lists,
    // every appmanifest/lua/depotcache file name and mtime. Adding, removing or
    // editing a library file changes the stamp (cache invalidated); identical
    // state is served from the pass-lifetime index cache (spec item D4).
    uint64_t h = 1469598103934665603ull;
    auto mixBytes = [&h](const char* data, size_t len) {
        for (size_t i = 0; i < len; ++i)
        {
            h ^= static_cast<uint8_t>(data[i]);
            h *= 1099511628211ull;
        }
    };
    auto mixStr = [&mixBytes](const std::string& s) { mixBytes(s.data(), s.size()); };
    auto mixU64 = [&mixBytes](uint64_t v) {
        uint8_t b[8];
        for (int i = 0; i < 8; ++i) b[i] = static_cast<uint8_t>((v >> (8 * i)) & 0xFF);
        mixBytes(reinterpret_cast<const char*>(b), 8);
    };

    const fs::path steamRoot = GetSteamRoot();
    mixStr(steamRoot.string());
    mixStr("storage=" + m_storageDir.string()); // tests switch storage roots too

    auto stampDir = [&](const fs::path& dir) {
        mixStr("DIR");
        mixStr(dir.string());
        std::error_code lec;
        uint64_t count = 0;
        if (fs::is_directory(dir, lec))
        {
            for (const auto& entry : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, lec))
            {
                if (lec) break;
                if (!entry.is_regular_file(lec)) continue;
                ++count;
                mixStr(entry.path().filename().string());
                std::error_code tec;
                mixU64(static_cast<uint64_t>(fs::last_write_time(entry.path(), tec).time_since_epoch().count()));
            }
        }
        mixU64(count);
    };

    std::error_code ec;
    for (const auto& saDir : GetSteamAppsDirs(steamRoot)) stampDir(saDir);
    stampDir(steamRoot / "config" / "stplug-in");
    stampDir(steamRoot / "config" / "stplug-in" / "manifests");
    stampDir(steamRoot / "depotcache");
    stampDir(steamRoot / "config" / "depotcache");

    // appinfo.vdf feeds the uninstalled-title fallback: tolerate a refresh.
    const fs::path appInfoPath = steamRoot / "appcache" / "appinfo.vdf";
    if (fs::is_regular_file(appInfoPath, ec))
    {
        mixStr("APPINFO");
        mixU64(static_cast<uint64_t>(fs::last_write_time(appInfoPath, ec).time_since_epoch().count()));
    }
    else
    {
        mixStr("APPINFO-missing");
    }
    (void)ec;

    return h;
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

    std::error_code mtimeEc;
    const uint64_t cliMtime = static_cast<uint64_t>(
        fs::last_write_time(cli, mtimeEc).time_since_epoch().count());
    const uint64_t cliMtimeVal = mtimeEc ? 0 : cliMtime;
    const uint64_t nowMs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    {
        std::lock_guard<std::mutex> lock(m_authCacheMutex);
        auto it = m_authStatusCache.find(provider);
        if (it != m_authStatusCache.end() &&
            it->second.checkedAtMs != 0 &&
            (nowMs - it->second.checkedAtMs) < 3000 &&
            it->second.cliPath == cli &&
            it->second.cliMtime == cliMtimeVal)
        {
            return it->second.status;
        }
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

        const uint64_t finishMs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        {
            std::lock_guard<std::mutex> lock(m_authCacheMutex);
            m_authStatusCache[provider] = AuthStatusCacheEntry{status, cli, cliMtimeVal, finishMs};
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

bool CCloudManager::DeleteAllBackups()
{
    try
    {
        fs::path backupDir = GetBackupDir();
        std::error_code ec;
        if (!fs::exists(backupDir, ec))
        {
            return true;
        }
        if (ec || !fs::is_directory(backupDir, ec))
        {
            return false;
        }

        bool success = true;
        fs::directory_iterator it(backupDir, ec);
        if (ec)
        {
            return false;
        }

        fs::directory_iterator end;
        while (it != end && !ec)
        {
            std::error_code rmEc;
            fs::remove_all(it->path(), rmEc);
            if (rmEc)
            {
                success = false;
            }
            it.increment(ec);
        }
        return success && !ec;
    }
    catch (...)
    {
        return false;
    }
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

                fs::path savePathFile = gameEntry.path() / "steam_settings" / "save_path.txt";
                if (!fs::exists(savePathFile, ec)) continue;

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

    /**
     * Builder for the one-pass library scan index (scan-once replacement for the
     * legacy per-candidate rescans FindDepotOwnerInAppManifests /
     * FindDepotOwnerInLuaPlugins / GetChildDepotIdsForApp / ResolveAppTitleFromManifests.
     * Semantics mirror the legacy scans exactly:
     *  - InstalledDepots ownership: first manifest in (dir, file) scan order wins.
     *  - Manifest titles: first "name" line is stored under both placeholder rules
     *    (1-arg for owner titles, 2-arg for the direct resolve path).
     *  - Lua ownership: the first owner whose gameName passes IsPlaceholderAppTitle
     *    wins; otherwise the first numerically-resolved owner tracks the depot.
     *  - Lua child ids merge depots -> dlcs -> manifests per file in scan order.
     */
    void BuildScanIndexInto(const fs::path& steamRoot,
                            const std::vector<fs::path>& steamAppsDirs,
                            const fs::path& stplugDir,
                            ScanIndex& out)
    {
        out.steamRoot = steamRoot;
        out.steamAppsDirs = steamAppsDirs;
        out.stplugDir = stplugDir;
        std::error_code ec;

        auto toAppId = [](const std::string& digits) -> AppId_t {
            try { return static_cast<AppId_t>(std::stoul(digits)); }
            catch (...) { return 0; }
        };

        // ---- Pass 1: every appmanifest_*.acf in every library folder, one read each ----
        for (const auto& saDir : steamAppsDirs)
        {
            if (!fs::is_directory(saDir, ec)) continue;
            for (const auto& entry : fs::directory_iterator(saDir, fs::directory_options::skip_permission_denied, ec))
            {
                if (ec) break;
                if (!entry.is_regular_file(ec)) continue;
                const fs::path& p = entry.path();
                const std::string fn = p.filename().string();
                if (fn.rfind("appmanifest_", 0) != 0 || fn.length() < 16 || !fn.ends_with(".acf")) continue;

                const std::string parentIdStr = fn.substr(12, fn.length() - 16);
                if (!IsDigitsOnly(parentIdStr)) continue;
                const AppId_t appId = static_cast<AppId_t>(std::strtoul(parentIdStr.c_str(), nullptr, 10));

                std::ifstream mf(p);
                if (!mf.is_open()) continue;
                NoteManifestFileRead();

                std::string line;
                bool inInstalledDepots = false;
                int depotsDepth = 0;
                std::string appName;       ///< first "name" passing the 1-arg placeholder rule
                std::string appNameStrict; ///< first "name" passing the 2-arg placeholder rule

                while (std::getline(mf, line))
                {
                    if (appName.empty() || appNameStrict.empty())
                    {
                        size_t nameKey = line.find("\"name\"");
                        if (nameKey != std::string::npos)
                        {
                            size_t q1 = line.find('"', nameKey + 6);
                            size_t q2 = (q1 != std::string::npos) ? line.find('"', q1 + 1) : std::string::npos;
                            if (q1 != std::string::npos && q2 != std::string::npos && q2 > q1 + 1)
                            {
                                std::string n = line.substr(q1 + 1, q2 - q1 - 1);
                                if (appName.empty() && !IsPlaceholderAppTitle(n)) appName = n;
                                if (appNameStrict.empty() && !IsPlaceholderAppTitle(n, appId)) appNameStrict = n;
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
                                    if (IsDigitsOnly(key))
                                    {
                                        const AppId_t depot = toAppId(key);
                                        out.childrenOfAppFromManifests[appId].push_back(depot);
                                        if (depot != 0 && depot != appId)
                                        {
                                            if (!out.parentOfDepotFromManifests.contains(depot))
                                            {
                                                out.parentOfDepotFromManifests.emplace(depot, appId);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                if (!appName.empty() && !out.manifestTitleOf.contains(appId))
                {
                    out.manifestTitleOf.emplace(appId, appName);
                }
                if (!appNameStrict.empty() && !out.manifestTitleOfStrict.contains(appId))
                {
                    out.manifestTitleOfStrict.emplace(appId, appNameStrict);
                }
            }
        }

        // ---- Pass 2: pre-index .manifest files across depotcache and stplug-in dirs ----
        {
            const fs::path manifestDirs[] = {
                steamRoot / "depotcache",
                steamRoot / "config" / "depotcache",
                stplugDir,
                stplugDir / "manifests",
            };
            for (const fs::path& mDir : manifestDirs)
            {
                std::error_code mEc;
                if (!fs::is_directory(mDir, mEc)) continue;
                for (const auto& entry : fs::directory_iterator(mDir, fs::directory_options::skip_permission_denied, mEc))
                {
                    if (mEc) break;
                    if (!entry.is_regular_file(mEc)) continue;
                    const fs::path& p = entry.path();
                    const std::string fn = p.filename().string();
                    if (!fn.ends_with(".manifest")) continue;
                    const std::string stem = p.stem().string();
                    const size_t underscore = stem.find('_');
                    const std::string idStr = (underscore != std::string::npos) ? stem.substr(0, underscore) : stem;
                    if (!IsDigitsOnly(idStr)) continue;
                    const AppId_t id = static_cast<AppId_t>(std::strtoul(idStr.c_str(), nullptr, 10));
                    if (id != 0 && std::to_string(id) == idStr)
                    {
                        out.manifestsByPrefixAppId[id].push_back(p);
                    }
                }
            }
        }

        // ---- Pass 3: every .lua plugin in stplug-in, one read+parse each ----
        if (!fs::is_directory(stplugDir, ec)) return;

        struct LuaOwnerProbe
        {
            bool seen{false};
            bool hasFirst{false};
            AppId_t firstId{0};
            bool titled{false};
            AppId_t titledId{0};
            std::string title;
        };
        std::unordered_map<AppId_t, LuaOwnerProbe> luaOwners; // depotId -> probe

        for (const auto& entry : fs::directory_iterator(stplugDir, fs::directory_options::skip_permission_denied, ec))
        {
            if (ec) break;
            if (!entry.is_regular_file(ec)) continue;
            if (entry.path().extension() != ".lua") continue;

            ScanLuaFileRecord rec;
            rec.path = entry.path();
            rec.stem = rec.path.stem().string();
            if (IsDigitsOnly(rec.stem))
            {
                rec.stemAppId = static_cast<AppId_t>(std::strtoul(rec.stem.c_str(), nullptr, 10));
            }

            {
                std::ifstream lf(rec.path);
                if (!lf.is_open())
                {
                    // Unreadable numeric-stem files still surface as discovery
                    // candidates (legacy addCandidate used the stem alone).
                    if (rec.stemAppId != 0 && !out.luaRecordIndexByStem.contains(rec.stemAppId))
                    {
                        out.luaRecordIndexByStem.emplace(rec.stemAppId, out.luaFiles.size());
                    }
                    out.luaFiles.push_back(std::move(rec));
                    continue;
                }
                NoteLuaFileParsed();
                std::string content((std::istreambuf_iterator<char>(lf)),
                                     std::istreambuf_iterator<char>());
                rec.readOk = true;

                const LuaParser::ParseResult parsed = LuaParser::parse(content);
                rec.parsedValid = parsed.valid;
                rec.parsedAppId = parsed.appId;
                rec.gameName = parsed.gameName;

                for (const auto& [depotIdStr, _] : parsed.depots)
                {
                    if (IsDigitsOnly(depotIdStr)) rec.depots.push_back(toAppId(depotIdStr));
                }
                for (const auto& [dlcIdStr, _] : parsed.dlcs)
                {
                    if (IsDigitsOnly(dlcIdStr)) rec.dlcs.push_back(toAppId(dlcIdStr));
                }
                for (const auto& [manifestIdStr, manifestGid] : parsed.manifests)
                {
                    if (IsDigitsOnly(manifestIdStr)) rec.manifests.push_back(toAppId(manifestIdStr));
                    if (!manifestIdStr.empty() && !manifestGid.empty() && manifestGid != "0")
                    {
                        rec.manifestPairs.emplace_back(manifestIdStr, manifestGid);
                    }
                }

                // Fallback-3 title: first addappid line carrying a trailing -- comment.
                {
                    std::istringstream lines(content);
                    std::string raw;
                    while (std::getline(lines, raw))
                    {
                        size_t addApp = raw.find("addappid(");
                        if (addApp == std::string::npos) continue;
                        size_t comment = raw.find("--", addApp);
                        if (comment == std::string::npos) continue;
                        std::string title = raw.substr(comment + 2);
                        title.erase(0, title.find_first_not_of(" \t"));
                        title.erase(title.find_last_not_of(" \t\r\n") + 1);
                        if (!title.empty())
                        {
                            rec.luaTitle = title;
                            break;
                        }
                    }
                }

                // Raw "addappid(<digits>" discovery scan (legacy list scan rule).
                {
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
                                rec.allAddAppIds.push_back(static_cast<AppId_t>(std::stoul(content.substr(pos, endPos - pos))));
                            }
                            catch (...) {}
                        }
                        pos = endPos;
                    }
                }
            }

            // Lua children (depots -> dlcs -> manifests, per-file order preserved).
            std::vector<AppId_t> childIds;
            childIds.reserve(rec.depots.size() + rec.dlcs.size() + rec.manifests.size());
            childIds.insert(childIds.end(), rec.depots.begin(), rec.depots.end());
            childIds.insert(childIds.end(), rec.dlcs.begin(), rec.dlcs.end());
            childIds.insert(childIds.end(), rec.manifests.begin(), rec.manifests.end());

            // Legacy GetChildDepotIdsForApp step 2: <stem>.lua seeds its stem app.
            if (rec.stemAppId != 0)
            {
                auto& list = out.childrenOfAppFromLuaStem[rec.stemAppId];
                list.insert(list.end(), childIds.begin(), childIds.end());
            }

            // Legacy step 3: files whose parsed appId names another game seed it.
            // The stem-file skip mirrors the legacy parentLua path-equality skip.
            if (rec.parsedValid && !rec.parsedAppId.empty() && IsDigitsOnly(rec.parsedAppId))
            {
                const AppId_t parsedAppId = static_cast<AppId_t>(std::strtoul(rec.parsedAppId.c_str(), nullptr, 10));
                if (rec.stem != std::to_string(parsedAppId))
                {
                    auto& list = out.childrenOfAppFromLuaMapped[parsedAppId];
                    list.insert(list.end(), childIds.begin(), childIds.end());
                }
            }

            // Lua title fallback keyed by numeric stem (first non-empty wins).
            if (rec.stemAppId != 0 && !rec.luaTitle.empty() && !out.luaTitleOfStem.contains(rec.stemAppId))
            {
                out.luaTitleOfStem.emplace(rec.stemAppId, rec.luaTitle);
            }

            // Lua ownership probes (mirrors the per-depot FindDepotOwnerInLuaPlugins scan).
            if (rec.readOk)
            {
                const std::string resolvedParentStr =
                    (rec.parsedValid && !rec.parsedAppId.empty() && IsDigitsOnly(rec.parsedAppId)) ? rec.parsedAppId : rec.stem;
                const bool resolvedParentNumeric = !resolvedParentStr.empty() && IsDigitsOnly(resolvedParentStr);
                const AppId_t resolvedParentId = resolvedParentNumeric
                    ? static_cast<AppId_t>(std::strtoul(resolvedParentStr.c_str(), nullptr, 10))
                    : 0u;
                const bool usableTitle = !rec.gameName.empty() && !IsPlaceholderAppTitle(rec.gameName);

                auto trackOwner = [&](AppId_t depotId) {
                    if (depotId == 0) return; // non-digit/invalid sources are never queried
                    // Guard 1 (legacy line 814 mirror): a file whose stem IS the
                    // depot id never owns that depot from itself.
                    if (rec.stem == std::to_string(depotId)) return;
                    // Guard 2 (legacy line 831 mirror): self-listing files do not
                    // own their depot via the resolved parent either.
                    if (resolvedParentNumeric && resolvedParentStr == std::to_string(depotId)) return;

                    LuaOwnerProbe& probe = luaOwners[depotId];
                    if (probe.titled) return; // an earlier titled owner already won (legacy early return)
                    if (usableTitle)
                    {
                        probe.titled = true;
                        probe.titledId = resolvedParentId;
                        probe.title = rec.gameName;
                        return;
                    }
                    if (!probe.hasFirst && resolvedParentNumeric)
                    {
                        probe.hasFirst = true;
                        probe.firstId = resolvedParentId;
                    }
                };

                for (AppId_t id : rec.depots) trackOwner(id);
                for (AppId_t id : rec.manifests) trackOwner(id);
                for (AppId_t id : rec.dlcs) trackOwner(id);
            }

            if (rec.stemAppId != 0 && !out.luaRecordIndexByStem.contains(rec.stemAppId))
            {
                out.luaRecordIndexByStem.emplace(rec.stemAppId, out.luaFiles.size());
            }
            out.luaFiles.push_back(std::move(rec));
        }

        // Resolve probes: titled owner wins, else the first referenced owner.
        for (const auto& [depotId, probe] : luaOwners)
        {
            if (probe.titled)
            {
                out.parentOfDepotFromLua.emplace(depotId, probe.titledId);
                out.luaOwnerTitleOf.emplace(depotId, probe.title);
            }
            else if (probe.firstId != 0)
            {
                out.parentOfDepotFromLua.emplace(depotId, probe.firstId);
            }
        }
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
    if (appId == 0) return "";
    std::shared_ptr<const ScanIndex> index = ObtainScanIndex();
    if (!index) return "";
    return ResolveAppTitleFromManifestsInternal(appId, 0, *index);
}

// Index-driven variant: every library fact below comes from ONE build scan
// (BuildScanIndexInto); depot ownership and owner-title recursion are map lookups.
std::string CCloudManager::ResolveAppTitleFromManifestsInternal(AppId_t appId, int depth, const ScanIndex& index) const
{
    if (appId == 0 || depth > 3) return "";
    if (appId == 7) return "Steam Client";
    if (appId == 241100) return "Steam Input Configs";
    if (appId == 250820) return "SteamVR";
    std::error_code ec;
    const fs::path steamRoot = index.steamRoot;

    // 1. Own appmanifest "name": first manifest in scan order passing the
    //    per-app placeholder rule (index captures it while scanning each
    //    appmanifest_*.acf exactly once).
    {
        auto it = index.manifestTitleOfStrict.find(appId);
        if (it != index.manifestTitleOfStrict.end() && !it->second.empty())
        {
            return it->second;
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
    {
        auto it = index.luaTitleOfStem.find(appId);
        if (it != index.luaTitleOfStem.end() && !it->second.empty())
        {
            return it->second;
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
                if (it->is_directory(fEc))
                {
                    std::string dname = it->path().filename().string();
                    if (it.depth() >= 3 || dname == "Temp" || dname == "Microsoft" || dname == "Wine" ||
                        dname == "BACKUP" || dname == "Steam" || dname == "Unity" ||
                        dname == "UnrealEngine" || dname == "CrashReportClient" ||
                        dname == "D3DSCache" || dname == "fontconfig")
                    {
                        it.disable_recursion_pending();
                    }
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
        auto mit = index.parentOfDepotFromManifests.find(appId);
        if (mit != index.parentOfDepotFromManifests.end())
        {
            auto ownerTitleIt = index.manifestTitleOf.find(mit->second);
            if (ownerTitleIt != index.manifestTitleOf.end() && !ownerTitleIt->second.empty())
            {
                return ownerTitleIt->second;
            }
            if (mit->second != 0 && mit->second != appId)
            {
                const std::string ownerTitle = ResolveAppTitleFromManifestsInternal(mit->second, depth + 1, index);
                if (!ownerTitle.empty())
                {
                    return ownerTitle;
                }
            }
        }

        auto lit = index.parentOfDepotFromLua.find(appId);
        if (lit != index.parentOfDepotFromLua.end())
        {
            auto ownerTitleIt = index.luaOwnerTitleOf.find(appId);
            if (ownerTitleIt != index.luaOwnerTitleOf.end() && !ownerTitleIt->second.empty())
            {
                return ownerTitleIt->second;
            }
            if (lit->second != 0 && lit->second != appId)
            {
                const std::string ownerTitle = ResolveAppTitleFromManifestsInternal(lit->second, depth + 1, index);
                if (!ownerTitle.empty())
                {
                    return ownerTitle;
                }
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

    std::shared_ptr<const ScanIndex> index = ObtainScanIndex();
    if (index) return GetParentAppIdForDepot(depotId, *index);

    // Fallback if index could not be obtained
    const auto dlcData = g_config.dlcData.get();
    for (const auto& [parentId, data] : dlcData)
    {
        if (parentId == 0 || parentId == depotId) continue;
        if (data.dlcIds.contains(depotId)) return parentId;
    }

    return 0;
}

AppId_t CCloudManager::GetParentAppIdForDepot(AppId_t depotId, const ScanIndex& index) const
{
    if (depotId == 0) return 0;
    // Shared Steamworks Common Redistributables
    if (depotId >= 228980 && depotId <= 229007) return 0;

    if (!index.steamRootMissing)
    {
        // 1. Check if an installed appmanifest_*.acf lists this depot in InstalledDepots
        auto mit = index.parentOfDepotFromManifests.find(depotId);
        if (mit != index.parentOfDepotFromManifests.end())
        {
            if (mit->second != 0 && mit->second != depotId) return mit->second;
        }

        // 2. Check if a Lua plugin lists this depot in depots, dlcs, or manifests
        auto lit = index.parentOfDepotFromLua.find(depotId);
        if (lit != index.parentOfDepotFromLua.end())
        {
            if (lit->second != 0 && lit->second != depotId) return lit->second;
        }
    }

    // 3. Fallback: check config DlcData mapping (child->parent reverse lookup)
    const auto dlcData = g_config.dlcData.get();
    for (const auto& [parentId, data] : dlcData)
    {
        if (parentId == 0 || parentId == depotId) continue;
        if (data.dlcIds.contains(depotId))
        {
            return parentId;
        }
    }

    return 0;
}

std::vector<AppId_t> CCloudManager::GetChildDepotIdsForApp(AppId_t parentAppId) const
{
    std::vector<AppId_t> children;
    if (parentAppId == 0) return children;

    std::shared_ptr<const ScanIndex> index = ObtainScanIndex();
    if (index) return GetChildDepotIdsForApp(parentAppId, *index);

    const auto dlcData = g_config.dlcData.get();
    auto dit = dlcData.find(parentAppId);
    if (dit != dlcData.end())
    {
        std::vector<AppId_t> dlcList;
        dlcList.reserve(dit->second.dlcIds.size());
        for (const auto& [dlcId, _] : dit->second.dlcIds) dlcList.push_back(dlcId);
        std::sort(dlcList.begin(), dlcList.end());
        for (AppId_t id : dlcList)
        {
            if (id != 0 && id != parentAppId && (id < 228980 || id > 229007))
            {
                children.push_back(id);
            }
        }
    }

    return children;
}

std::vector<AppId_t> CCloudManager::GetChildDepotIdsForApp(AppId_t parentAppId, const ScanIndex& index) const
{
    std::vector<AppId_t> children;
    if (parentAppId == 0) return children;

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

    if (!index.steamRootMissing)
    {
        // 1. InstalledDepots from appmanifest_<parentAppId>.acf across library folders
        auto mit = index.childrenOfAppFromManifests.find(parentAppId);
        if (mit != index.childrenOfAppFromManifests.end())
        {
            for (AppId_t id : mit->second) addChild(id);
        }

        // 2. Lua plugin config/stplug-in/<parentAppId>.lua
        auto sit = index.childrenOfAppFromLuaStem.find(parentAppId);
        if (sit != index.childrenOfAppFromLuaStem.end())
        {
            for (AppId_t id : sit->second) addChild(id);
        }

        // 3. Other .lua files whose primary addappid matches parentAppId
        auto lit = index.childrenOfAppFromLuaMapped.find(parentAppId);
        if (lit != index.childrenOfAppFromLuaMapped.end())
        {
            for (AppId_t id : lit->second) addChild(id);
        }
    }

    // 4. DLCs from config DlcData mapping (parent->children forward lookup)
    const auto dlcData = g_config.dlcData.get();
    auto dit = dlcData.find(parentAppId);
    if (dit != dlcData.end())
    {
        std::vector<AppId_t> dlcList;
        dlcList.reserve(dit->second.dlcIds.size());
        for (const auto& [dlcId, _] : dit->second.dlcIds)
        {
            dlcList.push_back(dlcId);
        }
        std::sort(dlcList.begin(), dlcList.end());
        for (AppId_t id : dlcList)
        {
            addChild(id);
        }
    }

    return children;
}

std::vector<CloudFileEntry_t> CCloudManager::ListAppCloudFilesDirect(AppId_t appId,
                                                                     const std::string& provider,
                                                                     uint32_t accountId,
                                                                     const ScanIndex* index) const
{
    std::vector<CloudFileEntry_t> results;
    if (appId == 0 || appId == kCloudCatalogAppId) return results;

    const std::string appIdStr = std::to_string(appId);
    const std::vector<uint32_t> accounts = ResolveCandidateAccountIds(accountId);
    const fs::path storageRoot = GetStorageDir();
    const bool isRemoteProvider = (!provider.empty() && provider != "local-only");
    std::error_code ec;

    // Map of logical fileName -> CloudFileEntry_t (D3: hash-indexed upsert).
    // Insert order is preserved via the slot vector; lookups are O(1).
    std::vector<std::pair<std::string, CloudFileEntry_t>> fileMap;
    std::unordered_map<std::string, size_t> fileIndex; // logicalName -> fileMap slot
    auto upsertFile = [&](const std::string& logicalName, CloudFileEntry_t entry) {
        if (IsInternalMetadataRelPath(logicalName)) return;
        NoteFileUpsertProbe(); // D3 instrumentation (one probe per upsert lookup)
        const auto slot = fileIndex.find(logicalName);
        if (slot != fileIndex.end())
        {
            auto& kv = fileMap[slot->second];
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
            }
            return;
        }
        fileIndex.emplace(logicalName, fileMap.size());
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

            // If sync-remote-app materialized a <cleanPath>/<sha1> directory leaf, strip the trailing sha1
            if (rel.has_parent_path() && IsHexHashSegment(it->path().filename().string()))
            {
                relStr = rel.parent_path().generic_string();
                if (IsInternalMetadataRelPath(relStr)) continue;
            }

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

    // 3. When running in local-only mode (not a remote cloud provider), also include local saves
    // in Steam userdata/<acct>/<appId>/remote, Goldberg paths, Proton Auto-Cloud roots, and Lua/manifest files.
    const fs::path steamRoot = GetSteamRoot();
    if (!isRemoteProvider && (m_storageDir.empty() || !m_steamRoot.empty()) && appId != 760 && !steamRoot.empty())
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

            // Parse Lua file to discover depot manifest files (using ScanIndex when available)
            const std::vector<std::pair<std::string, std::string>>* manifestPairsPtr = nullptr;
            std::vector<std::pair<std::string, std::string>> fallbackManifestPairs;
            if (index != nullptr)
            {
                auto recIt = index->luaRecordIndexByStem.find(appId);
                if (recIt != index->luaRecordIndexByStem.end() && recIt->second < index->luaFiles.size())
                {
                    manifestPairsPtr = &index->luaFiles[recIt->second].manifestPairs;
                }
            }
            if (manifestPairsPtr == nullptr)
            {
                auto luaData = LuaParser::parseFile(luaFile.string());
                for (const auto& [depotId, manifestGid] : luaData.manifests)
                {
                    if (!depotId.empty() && !manifestGid.empty() && manifestGid != "0")
                    {
                        fallbackManifestPairs.emplace_back(depotId, manifestGid);
                    }
                }
                manifestPairsPtr = &fallbackManifestPairs;
            }
            for (const auto& [depotId, manifestGid] : *manifestPairsPtr)
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
        if (index != nullptr && !index->steamRootMissing)
        {
            auto mIt = index->manifestsByPrefixAppId.find(appId);
            if (mIt != index->manifestsByPrefixAppId.end())
            {
                for (const fs::path& mfPath : mIt->second)
                {
                    std::error_code mfEc;
                    if (!fs::is_regular_file(mfPath, mfEc) || mfEc) continue;
                    std::string fn = mfPath.filename().string();
                    uint64_t mmtime = 0;
                    auto mftime = fs::last_write_time(mfPath, mfEc);
                    if (!mfEc)
                    {
                        auto sctp = std::chrono::clock_cast<std::chrono::system_clock>(mftime);
                        mmtime = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(sctp.time_since_epoch()).count());
                    }

                    std::string fullAppMfName = "depotcache/" + fn;
                    CloudFileEntry_t mfEntry;
                    mfEntry.fileName = fullAppMfName;
                    mfEntry.folder = "depotcache";
                    mfEntry.sizeBytes = static_cast<uint64_t>(fs::file_size(mfPath, mfEc));
                    mfEntry.modifiedTime = mmtime;
                    mfEntry.inLocalMirror = true;
                    upsertFile(fullAppMfName, std::move(mfEntry));
                }
            }
        }
        else
        {
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
    if (appId == 0 || appId == kCloudCatalogAppId) return {};
    std::shared_ptr<const ScanIndex> index = ObtainScanIndex();
    if (!index) return {};
    return ListAppCloudFiles(appId, provider, accountId, *index);
}

std::vector<CloudFileEntry_t> CCloudManager::ListAppCloudFiles(
    AppId_t appId,
    const std::string& provider,
    uint32_t accountId,
    const ScanIndex& index,
    const std::unordered_set<AppId_t>* remoteConfirmedApps,
    bool skipCatalogLookup) const
{
    if (appId == 0 || appId == kCloudCatalogAppId) return {};

    const bool isRemoteProvider = (!provider.empty() && provider != "local-only");
    if (isRemoteProvider && !skipCatalogLookup)
    {
        const std::vector<uint32_t> accounts = ResolveCandidateAccountIds(accountId);
        for (uint32_t acct : accounts)
        {
            std::unordered_map<AppId_t, CloudAppSummary_t> catalogSummaries;
            std::unordered_map<AppId_t, std::vector<CloudFileEntry_t>> catalogFilesByApp;
            if (LoadCloudCatalog(provider, acct, /*syncRemote=*/false, catalogSummaries, catalogFilesByApp))
            {
                auto it = catalogFilesByApp.find(appId);
                if (it != catalogFilesByApp.end() && !it->second.empty())
                {
                    return it->second;
                }
            }
        }
    }

    // 1. Files belonging directly to base game
    std::vector<CloudFileEntry_t> results;
    if (!isRemoteProvider || !remoteConfirmedApps || remoteConfirmedApps->contains(appId))
    {
        results = ListAppCloudFilesDirect(appId, provider, accountId, &index);
    }

    // 2. Discover and merge files for all child depots / addons
    const std::vector<AppId_t> childDepots = GetChildDepotIdsForApp(appId, index);
    std::unordered_set<std::string> resultNames; // O(1) lookup keys for the child merge (D3)
    resultNames.reserve(results.size());
    for (const auto& existing : results) resultNames.insert(existing.fileName);
    for (AppId_t childId : childDepots)
    {
        if (isRemoteProvider && remoteConfirmedApps && !remoteConfirmedApps->contains(childId))
        {
            continue;
        }
        std::vector<CloudFileEntry_t> childFiles = ListAppCloudFilesDirect(childId, provider, accountId, &index);
        for (auto& cf : childFiles)
        {
            // Do not duplicate files already added by the base game (e.g. depot manifests referenced in Lua)
            NoteFileDedupProbe(); // D3 instrumentation (one probe per set lookup)
            if (resultNames.contains(cf.fileName))
            {
                continue;
            }
            resultNames.insert(cf.fileName);

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

    // One-pass library scan index shared by every lookup in this request (D1).
    // Depot->parent, child lists and titles below are map lookups against it.
    std::shared_ptr<const ScanIndex> index = ObtainScanIndex();

    std::vector<AppId_t> candidateApps;
    std::vector<AppId_t> remoteConfirmedApps;
    std::unordered_set<AppId_t> remoteConfirmedSet;
    std::vector<AppId_t> localConfirmedApps;
    bool remoteHasCatalog = false;
    uint32_t activeRemoteAcct = 0;

    auto addCandidate = [&](AppId_t id) {
        if (id == 0 || id == kCloudCatalogAppId) return;
        if (std::find(candidateApps.begin(), candidateApps.end(), id) == candidateApps.end())
        {
            candidateApps.push_back(id);
        }
    };

    // 1. Scan remote cloud provider first using fast list-remote-app-ids (~0.7s vs ~4.7s for list-remote-apps)
    const bool isRemoteProvider = (!provider.empty() && provider != "local-only");
    if (isRemoteProvider)
    {
        bool hasCustomCli = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            hasCustomCli = !m_cliPath.empty();
        }

        if (hasCustomCli || CheckAuthStatus(provider).authenticated)
        {
            for (uint32_t acct : accounts)
            {
            std::string out = RunCliCommand({"list-remote-app-ids", provider, std::to_string(acct)}, 5000);
            std::string jsonStr = ExtractJsonSubstring(out);
            if (jsonStr.empty())
            {
                std::string lowerOut = out;
                std::transform(lowerOut.begin(), lowerOut.end(), lowerOut.begin(), [](unsigned char c) { return std::tolower(c); });
                if (lowerOut.find("unknown command") != std::string::npos || lowerOut.find("invalid command") != std::string::npos)
                {
                    out = RunCliCommand({"list-remote-apps", provider, std::to_string(acct)}, 5000);
                    jsonStr = ExtractJsonSubstring(out);
                }
            }
            if (jsonStr.empty()) continue;

            activeRemoteAcct = acct;

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
                            if (id == kCloudCatalogAppId)
                            {
                                remoteHasCatalog = true;
                            }
                            else if (id != 0)
                            {
                                addCandidate(id);
                                if (std::find(remoteConfirmedApps.begin(), remoteConfirmedApps.end(), id) == remoteConfirmedApps.end())
                                {
                                    remoteConfirmedApps.push_back(id);
                                }
                            }
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
                    if (id == kCloudCatalogAppId)
                    {
                        remoteHasCatalog = true;
                    }
                    else if (id != 0)
                    {
                        addCandidate(id);
                        if (std::find(remoteConfirmedApps.begin(), remoteConfirmedApps.end(), id) == remoteConfirmedApps.end())
                        {
                            remoteConfirmedApps.push_back(id);
                        }
                    }
                }
                pos = objEnd + 1;
            }

            break; // First valid response from active account is authoritative
        }
        }

        remoteConfirmedSet.insert(remoteConfirmedApps.begin(), remoteConfirmedApps.end());
        if (activeRemoteAcct != 0 && !remoteHasCatalog)
        {
            const uint64_t nowMs = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
            std::lock_guard<std::mutex> lock(m_catalogMutex);
            if (m_catalogCache.provider != provider ||
                m_catalogCache.accountId != activeRemoteAcct ||
                m_catalogCache.storageRoot != storageRoot)
            {
                m_catalogCache.summaries.clear();
                m_catalogCache.filesByApp.clear();
                m_catalogCache.loadedAtMs = 0;
            }
            m_catalogCache.provider = provider;
            m_catalogCache.accountId = activeRemoteAcct;
            m_catalogCache.storageRoot = storageRoot;
            m_catalogCache.remoteSyncedAtMs = nowMs;
            m_catalogCache.remoteCatalogAbsent = true;
        }

        // Strict remote filtering: when a remote provider is selected, ONLY return apps
        // that exist in the remote cloud provider. If none exist remotely, return empty immediately,
        // after first pulling catalog metadata (if 999999999 exists remotely) so providerKeys restore.
        if (remoteConfirmedApps.empty())
        {
            if (remoteHasCatalog)
            {
                const uint32_t targetAcct = activeRemoteAcct != 0 ? activeRemoteAcct : (accounts.empty() ? 0 : accounts.front());
                if (targetAcct != 0)
                {
                    std::unordered_map<AppId_t, CloudAppSummary_t> catalogSummaries;
                    std::unordered_map<AppId_t, std::vector<CloudFileEntry_t>> catalogFilesByApp;
                    LoadCloudCatalog(provider, targetAcct, /*syncRemote=*/true, catalogSummaries, catalogFilesByApp);
                }
            }
            return summaries;
        }
    }

    // 2. In local-only mode, scan local CloudRedirect storage directories (<storageDir>/<acct>/<appId>)
    if (!isRemoteProvider && fs::exists(storageRoot, ec) && fs::is_directory(storageRoot, ec))
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
                    if (id != 0 && id != kCloudCatalogAppId)
                    {
                        addCandidate(id);
                        localConfirmedApps.push_back(id);
                    }
                }
            }
        }
    }

    // 2b. In local-only mode, also discover apps with local saves in Steam userdata/<acct>/<appId>/remote,
    // Proton compatdata/<appId>/pfx/drive_c/users/steamuser, and games with Lua plugins or depot manifests
    const fs::path steamRoot = GetSteamRoot();
    if (!isRemoteProvider && (m_storageDir.empty() || !m_steamRoot.empty()) && !steamRoot.empty())
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
                    if (id == 0 || id == 760 || id == kCloudCatalogAppId) continue;
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
                if (id == 0 || id == kCloudCatalogAppId) continue;
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
            // The lua plugins were already read+parsed once while building the
            // scan index; discovery consumes those records instead of re-reading.
            for (const auto& lua : index->luaFiles)
            {
                if (lua.stemAppId != 0 && lua.stemAppId != kCloudCatalogAppId)
                {
                    addCandidate(lua.stemAppId);
                    localConfirmedApps.push_back(lua.stemAppId);
                }
                else
                {
                    for (AppId_t id : lua.allAddAppIds)
                    {
                        if (id != 0 && id != kCloudCatalogAppId)
                        {
                            addCandidate(id);
                            localConfirmedApps.push_back(id);
                        }
                    }
                }

                // setManifestid() entries name depot ids (legacy knownDepotIds).
                for (AppId_t depotId : lua.manifests)
                {
                    knownDepotIds.insert(depotId);
                }
            }

            // Depot manifests sitting in stplug-in accept apps like depotcache
            // does (pure name walk; the title check is an index lookup).
            for (const auto& entry : fs::directory_iterator(stplugDir, fs::directory_options::skip_permission_denied, ec))
            {
                if (ec) break;
                if (!entry.is_regular_file(ec)) continue;
                if (entry.path().extension() != ".manifest") continue;

                std::string stem = entry.path().stem().string();
                size_t underscore = stem.find('_');
                std::string appIdStr = (underscore != std::string::npos) ? stem.substr(0, underscore) : stem;
                if (IsDigitsOnly(appIdStr))
                {
                    AppId_t id = static_cast<AppId_t>(std::strtoul(appIdStr.c_str(), nullptr, 10));
                    if (id != 0 && id != kCloudCatalogAppId && !knownDepotIds.contains(id))
                    {
                        if (!ResolveAppTitleFromManifestsInternal(id, 0, *index).empty() ||
                            fs::exists(steamRoot / "steamapps" / ("appmanifest_" + appIdStr + ".acf"), ec))
                        {
                            addCandidate(id);
                            localConfirmedApps.push_back(id);
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
                        if (id != 0 && id != kCloudCatalogAppId && !knownDepotIds.contains(id))
                        {
                            if (!ResolveAppTitleFromManifestsInternal(id, 0, *index).empty() ||
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
        if (id == 0 || id == kCloudCatalogAppId) continue;
        AppId_t parentId = GetParentAppIdForDepot(id, *index);
        if (parentId != 0 && parentId != id && parentId != kCloudCatalogAppId)
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
        for (AppId_t cid : GetChildDepotIdsForApp(parentId, *index))
        {
            if (cid == kCloudCatalogAppId) continue;
            auto& cList = parentToChildren[parentId];
            if (std::find(cList.begin(), cList.end(), cid) == cList.end())
            {
                cList.push_back(cid);
            }
        }
    }

    // Load remote catalog metadata if a remote provider is active
    std::unordered_map<AppId_t, CloudAppSummary_t> catalogSummaries;
    std::unordered_map<AppId_t, std::vector<CloudFileEntry_t>> catalogFilesByApp;
    if (isRemoteProvider)
    {
        uint32_t targetAcct = activeRemoteAcct != 0 ? activeRemoteAcct : (accounts.empty() ? 0 : accounts.front());
        if (targetAcct != 0)
        {
            LoadCloudCatalog(provider, targetAcct, /*syncRemote=*/remoteHasCatalog, catalogSummaries, catalogFilesByApp);
        }
    }

    // 4. For each top-level app, compute exact user save file count & total size
    // across the main game and all its child depots/addons.
    for (AppId_t appId : topLevelApps)
    {
        if (appId == 0 || appId == kCloudCatalogAppId) continue;
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

        if (isRemoteProvider)
        {
            if (!isRemoteApp) continue;

            auto catIt = catalogSummaries.find(appId);
            if (catIt != catalogSummaries.end() && catIt->second.fileCount > 0)
            {
                CloudAppSummary_t summary = catIt->second;
                summary.inRemoteCloud = true;
                summary.inLocalMirror = true;
                for (AppId_t cid : children)
                {
                    if (std::find(summary.childAppIds.begin(), summary.childAppIds.end(), cid) == summary.childAppIds.end())
                    {
                        summary.childAppIds.push_back(cid);
                    }
                }
                if (summary.gameTitle.empty() || IsPlaceholderAppTitle(summary.gameTitle, appId))
                {
                    std::string resolved = ResolveAppTitleFromManifestsInternal(appId, 0, *index);
                    if (!resolved.empty())
                    {
                        summary.gameTitle = resolved;
                    }
                }
                summaries.push_back(std::move(summary));
                continue;
            }
        }

        const std::string effectiveProvider = (isRemoteProvider && isRemoteApp) ? provider : "local-only";
        const uint32_t effectiveAcct = (isRemoteProvider && activeRemoteAcct != 0) ? activeRemoteAcct : accountId;
        std::vector<CloudFileEntry_t> files = ListAppCloudFiles(
            appId,
            effectiveProvider,
            effectiveAcct,
            *index,
            (isRemoteProvider && isRemoteApp) ? &remoteConfirmedSet : nullptr,
            /*skipCatalogLookup=*/isRemoteProvider);
        if (files.empty())
        {
            continue;
        }

        CloudAppSummary_t summary;
        summary.appId = appId;
        summary.childAppIds = children;
        summary.gameTitle = ResolveAppTitleFromManifestsInternal(appId, 0, *index);
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

        if (isRemoteProvider && isRemoteApp && effectiveAcct != 0)
        {
            const uint64_t nowMs = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
            std::lock_guard<std::mutex> lock(m_catalogMutex);
            if (m_catalogCache.provider != provider ||
                m_catalogCache.accountId != effectiveAcct ||
                m_catalogCache.storageRoot != storageRoot)
            {
                m_catalogCache.summaries.clear();
                m_catalogCache.filesByApp.clear();
            }
            m_catalogCache.provider = provider;
            m_catalogCache.accountId = effectiveAcct;
            m_catalogCache.storageRoot = storageRoot;
            m_catalogCache.loadedAtMs = nowMs;
            m_catalogCache.summaries[appId] = summary;
            m_catalogCache.filesByApp[appId] = files;
        }

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
        if (fs::is_regular_file(fullPath, ec) && !ec)
        {
            std::ifstream in(fullPath, std::ios::binary);
            if (!in.is_open()) return false;
            std::ostringstream ss;
            ss << in.rdbuf();
            outContent = ss.str();
            return true;
        }
        if (fs::is_directory(fullPath, ec) && !ec)
        {
            for (const auto& entry : fs::directory_iterator(fullPath, ec))
            {
                if (!ec && entry.is_regular_file(ec) && IsHexHashSegment(entry.path().filename().string()))
                {
                    std::ifstream in(entry.path(), std::ios::binary);
                    if (!in.is_open()) return false;
                    std::ostringstream ss;
                    ss << in.rdbuf();
                    outContent = ss.str();
                    return true;
                }
            }
        }
        return false;
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
    std::shared_ptr<const ScanIndex> index = ObtainScanIndex();
    std::vector<AppId_t> childDepots;
    if (index) childDepots = GetChildDepotIdsForApp(appId, *index);
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

namespace
{
    std::string EscapeCatalogJsonString(const std::string& input)
    {
        std::string out;
        out.reserve(input.size() + 8);
        for (char c : input)
        {
            switch (c)
            {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\b': out += "\\b";  break;
                case '\f': out += "\\f";  break;
                case '\n': out += "\\n";  break;
                case '\r': out += "\\r";  break;
                case '\t': out += "\\t";  break;
                default:
                    if (static_cast<unsigned char>(c) >= 0x20)
                    {
                        out += c;
                    }
                    break;
            }
        }
        return out;
    }

    std::string ExtractCatalogJsonString(const std::string& json, const std::string& key, size_t maxSearchPos = std::string::npos)
    {
        const std::string needle = "\"" + key + "\"";
        size_t pos = json.find(needle);
        if (pos == std::string::npos || (maxSearchPos != std::string::npos && pos >= maxSearchPos)) return "";
        pos = json.find(':', pos + needle.size());
        if (pos == std::string::npos) return "";
        pos = json.find('"', pos + 1);
        if (pos == std::string::npos) return "";
        ++pos;

        std::string out;
        bool esc = false;
        for (size_t i = pos; i < json.size(); ++i)
        {
            char c = json[i];
            if (esc)
            {
                switch (c)
                {
                    case '"':  out += '"';  break;
                    case '\\': out += '\\'; break;
                    case 'b':  out += '\b'; break;
                    case 'f':  out += '\f'; break;
                    case 'n':  out += '\n'; break;
                    case 'r':  out += '\r'; break;
                    case 't':  out += '\t'; break;
                    default:   out += c;    break;
                }
                esc = false;
                continue;
            }
            if (c == '\\')
            {
                esc = true;
                continue;
            }
            if (c == '"')
            {
                break;
            }
            out += c;
        }
        return out;
    }

    std::vector<std::string> SplitCatalogJsonObjectArray(const std::string& json, const std::string& arrayKey)
    {
        std::vector<std::string> items;
        const std::string needle = "\"" + arrayKey + "\"";
        size_t pos = json.find(needle);
        if (pos == std::string::npos) return items;
        pos = json.find('[', pos + needle.size());
        if (pos == std::string::npos) return items;

        bool inStr = false;
        bool esc = false;
        int braceDepth = 0;
        int bracketDepth = 1;
        size_t objStart = 0;

        for (size_t i = pos + 1; i < json.size(); ++i)
        {
            char c = json[i];
            if (esc)
            {
                esc = false;
                continue;
            }
            if (c == '\\' && inStr)
            {
                esc = true;
                continue;
            }
            if (c == '"')
            {
                inStr = !inStr;
                continue;
            }
            if (inStr) continue;

            if (c == '[')
            {
                ++bracketDepth;
            }
            else if (c == ']')
            {
                --bracketDepth;
                if (bracketDepth == 0) break;
            }
            else if (c == '{')
            {
                if (braceDepth == 0 && bracketDepth == 1)
                {
                    objStart = i;
                }
                ++braceDepth;
            }
            else if (c == '}')
            {
                --braceDepth;
                if (braceDepth == 0 && bracketDepth == 1)
                {
                    items.push_back(json.substr(objStart, i - objStart + 1));
                }
            }
        }
        return items;
    }

    std::vector<AppId_t> ExtractCatalogUintArray(const std::string& json, const std::string& arrayKey)
    {
        std::vector<AppId_t> result;
        const std::string needle = "\"" + arrayKey + "\"";
        size_t pos = json.find(needle);
        if (pos == std::string::npos) return result;
        size_t arrStart = json.find('[', pos + needle.size());
        if (arrStart == std::string::npos) return result;
        size_t arrEnd = json.find(']', arrStart);
        if (arrEnd == std::string::npos) return result;

        size_t i = arrStart + 1;
        while (i < arrEnd)
        {
            while (i < arrEnd && !std::isdigit(static_cast<unsigned char>(json[i]))) ++i;
            if (i >= arrEnd) break;
            std::string numStr;
            while (i < arrEnd && std::isdigit(static_cast<unsigned char>(json[i])))
            {
                numStr += json[i];
                ++i;
            }
            if (!numStr.empty())
            {
                AppId_t id = static_cast<AppId_t>(std::strtoul(numStr.c_str(), nullptr, 10));
                if (id != 0) result.push_back(id);
            }
        }
        return result;
    }

    std::string ExtractCatalogJsonObject(const std::string& json, const std::string& objectKey)
    {
        const std::string needle = "\"" + objectKey + "\"";
        bool inStr = false;
        bool esc = false;
        int braceDepth = 0;

        for (size_t i = 0; i < json.size(); ++i)
        {
            const char c = json[i];
            if (esc)
            {
                esc = false;
                continue;
            }
            if (c == '\\' && inStr)
            {
                esc = true;
                continue;
            }
            if (c == '"')
            {
                if (!inStr && braceDepth == 1 && json.compare(i, needle.size(), needle) == 0)
                {
                    size_t afterKey = json.find_first_not_of(" \t\r\n", i + needle.size());
                    if (afterKey != std::string::npos && json[afterKey] == ':')
                    {
                        size_t valStart = json.find_first_not_of(" \t\r\n", afterKey + 1);
                        if (valStart != std::string::npos && json[valStart] == '{')
                        {
                            int objDepth = 0;
                            bool objInStr = false;
                            bool objEsc = false;
                            for (size_t j = valStart; j < json.size(); ++j)
                            {
                                const char oc = json[j];
                                if (objEsc)
                                {
                                    objEsc = false;
                                    continue;
                                }
                                if (oc == '\\' && objInStr)
                                {
                                    objEsc = true;
                                    continue;
                                }
                                if (oc == '"')
                                {
                                    objInStr = !objInStr;
                                    continue;
                                }
                                if (objInStr) continue;
                                if (oc == '{') ++objDepth;
                                else if (oc == '}')
                                {
                                    --objDepth;
                                    if (objDepth == 0)
                                    {
                                        return json.substr(valStart, j - valStart + 1);
                                    }
                                }
                            }
                            return "";
                        }
                    }
                }
                inStr = !inStr;
                continue;
            }
            if (inStr) continue;
            if (c == '{') ++braceDepth;
            else if (c == '}') --braceDepth;
        }
        return "";
    }

    bool RestoreMetadataFromCatalogJson(const std::string& catalogJson)
    {
        const std::string pkObj = ExtractCatalogJsonObject(catalogJson, "providerKeys");
        if (pkObj.empty())
        {
            return false;
        }
        if (!g_config.cloudSyncProviderKeys.get())
        {
            return true;
        }

        const std::string remoteRyuu = ExtractCatalogJsonString(pkObj, "ryuuKey");
        const std::string remoteDepotBox = ExtractCatalogJsonString(pkObj, "depotBoxKey");
        const std::string remoteHubcap = ExtractCatalogJsonString(pkObj, "hubcapKey");

        std::string mergedRyuu = g_config.ryuuKey.get();
        std::string mergedDepotBox = g_config.depotBoxKey.get();
        std::string mergedHubcap = g_config.hubcapKey.get();
        size_t restoredCount = 0;

        if (g_config.cloudSyncRyuuKey.get() && mergedRyuu.empty() && !remoteRyuu.empty())
        {
            mergedRyuu = remoteRyuu;
            g_config.ryuuKey.set(mergedRyuu);
            ++restoredCount;
        }
        if (g_config.cloudSyncDepotBoxKey.get() && mergedDepotBox.empty() && !remoteDepotBox.empty())
        {
            mergedDepotBox = remoteDepotBox;
            g_config.depotBoxKey.set(mergedDepotBox);
            ++restoredCount;
        }
        if (g_config.cloudSyncHubcapKey.get() && mergedHubcap.empty() && !remoteHubcap.empty())
        {
            mergedHubcap = remoteHubcap;
            g_config.hubcapKey.set(mergedHubcap);
            ++restoredCount;
        }

        if (restoredCount > 0)
        {
            g_config.createFile();
            g_config.updateApiAuth(mergedRyuu, mergedDepotBox, mergedHubcap);
            LOG_INFO("Restored %zu provider key(s) from CloudRedirect catalog\n", restoredCount);
        }

        return true;
    }

    void BumpCnFile(const fs::path& appDir)
    {
        const fs::path cnPath = appDir / "cn.cloudredirect";
        long long cn = 0;
        std::error_code ec;
        if (fs::is_regular_file(cnPath, ec))
        {
            std::ifstream in(cnPath);
            std::string line;
            if (std::getline(in, line))
            {
                while (!line.empty() && (line.back() == '\n' || line.back() == '\r' || line.back() == ' '))
                {
                    line.pop_back();
                }
                if (!line.empty())
                {
                    try { cn = std::stoll(line); } catch (...) { cn = 0; }
                }
            }
        }
        ++cn;
        std::ofstream out(cnPath, std::ios::trunc);
        out << cn;
    }
} // namespace

void CCloudManager::InvalidateCloudCatalogCache() const
{
    {
        std::lock_guard<std::mutex> lock(m_catalogMutex);
        m_catalogCache = CatalogCache{};
    }
    {
        std::lock_guard<std::mutex> authLock(m_authCacheMutex);
        m_authStatusCache.clear();
    }
}

std::string CCloudManager::ReadOrSyncCatalogJson(const fs::path& storageRoot,
                                                 const std::string& provider,
                                                 uint32_t accountId,
                                                 bool syncRemote,
                                                 bool pullWhenLocalMissing,
                                                 bool allowLocalFallbackWhenSyncedEmpty) const
{
    if (accountId == 0 || storageRoot.empty()) return "";

    std::error_code ec;
    const fs::path catalogDir = storageRoot / std::to_string(accountId) / std::to_string(kCloudCatalogAppId);
    const fs::path catalogFile = catalogDir / kCloudCatalogFileName;

    auto readCatalogOnDisk = [&]() -> std::string {
        if (fs::is_regular_file(catalogFile, ec) && !ec)
        {
            std::ifstream in(catalogFile, std::ios::binary);
            if (in.is_open())
            {
                std::ostringstream ss;
                ss << in.rdbuf();
                return ss.str();
            }
        }
        if (fs::is_directory(catalogFile, ec) && !ec)
        {
            for (const auto& entry : fs::directory_iterator(catalogFile, ec))
            {
                if (!ec && entry.is_regular_file(ec) && IsHexHashSegment(entry.path().filename().string()))
                {
                    std::ifstream in(entry.path(), std::ios::binary);
                    if (in.is_open())
                    {
                        std::ostringstream ss;
                        ss << in.rdbuf();
                        return ss.str();
                    }
                }
            }
        }
        return "";
    };

    std::string existingContent = readCatalogOnDisk();
    const bool shouldSync = (syncRemote || (pullWhenLocalMissing && existingContent.empty())) &&
                            !provider.empty() && provider != "local" && provider != "local-only";

    if (shouldSync)
    {
        const uint64_t nowMs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        {
            std::lock_guard<std::mutex> lock(m_catalogMutex);
            if (m_catalogCache.provider == provider &&
                m_catalogCache.accountId == accountId &&
                m_catalogCache.storageRoot == storageRoot &&
                m_catalogCache.remoteSyncedAtMs != 0 &&
                (nowMs - m_catalogCache.remoteSyncedAtMs) < 3000)
            {
                if (m_catalogCache.remoteCatalogAbsent)
                {
                    return allowLocalFallbackWhenSyncedEmpty ? existingContent : "";
                }
                if (!existingContent.empty())
                {
                    return existingContent;
                }
            }
        }

        fs::create_directories(catalogDir, ec);
        // Remove plain file before sync-remote-app so WriteFileNoIncrement("<cleanPath>/<sha1>") never hits ENOTDIR,
        // and remove local CN/state markers so sync-remote-app always pulls the newest remote catalog.
        if (fs::is_regular_file(catalogFile, ec))
        {
            fs::remove(catalogFile, ec);
        }
        fs::remove_all(catalogDir / "blobs", ec);
        fs::remove(catalogDir / "cn.cloudredirect", ec);
        fs::remove(catalogDir / "state.cloudredirect", ec);
        fs::remove(catalogDir / "manifest.cloudredirect", ec);

        RunCliCommand({"sync-remote-app", provider, std::to_string(accountId),
                       std::to_string(kCloudCatalogAppId), storageRoot.parent_path().string()}, 20000);

        std::string syncedContent = readCatalogOnDisk();
        const uint64_t finishMs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        {
            std::lock_guard<std::mutex> lock(m_catalogMutex);
            if (m_catalogCache.provider != provider ||
                m_catalogCache.accountId != accountId ||
                m_catalogCache.storageRoot != storageRoot)
            {
                m_catalogCache.summaries.clear();
                m_catalogCache.filesByApp.clear();
                m_catalogCache.loadedAtMs = 0;
            }
            m_catalogCache.provider = provider;
            m_catalogCache.accountId = accountId;
            m_catalogCache.storageRoot = storageRoot;
            m_catalogCache.remoteSyncedAtMs = finishMs;
            m_catalogCache.remoteCatalogAbsent = syncedContent.empty();
        }

        if (!syncedContent.empty())
        {
            if (fs::is_directory(catalogFile, ec))
            {
                fs::remove_all(catalogFile, ec);
                std::ofstream out(catalogFile, std::ios::binary | std::ios::trunc);
                out << syncedContent;
            }
            return syncedContent;
        }
        if (!existingContent.empty())
        {
            // Restore pre-existing local catalog if sync-remote-app was offline or remote did not host 999999999.
            if (fs::is_directory(catalogFile, ec)) fs::remove_all(catalogFile, ec);
            std::ofstream out(catalogFile, std::ios::binary | std::ios::trunc);
            out << existingContent;
            return allowLocalFallbackWhenSyncedEmpty ? existingContent : "";
        }
        return "";
    }

    return existingContent;
}

bool CCloudManager::LoadCloudCatalog(const std::string& provider,
                                     uint32_t accountId,
                                     bool syncRemote,
                                     std::unordered_map<AppId_t, CloudAppSummary_t>& outSummaries,
                                     std::unordered_map<AppId_t, std::vector<CloudFileEntry_t>>& outFilesByApp) const
{
    outSummaries.clear();
    outFilesByApp.clear();
    if (accountId == 0) return false;

    std::lock_guard<std::recursive_mutex> catalogSyncLock(m_catalogSyncMutex);

    const fs::path storageRoot = GetStorageDir();
    const uint64_t nowMs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());

    if (!syncRemote)
    {
        std::lock_guard<std::mutex> lock(m_catalogMutex);
        if (m_catalogCache.provider == provider &&
            m_catalogCache.accountId == accountId &&
            m_catalogCache.storageRoot == storageRoot &&
            !m_catalogCache.summaries.empty() &&
            (nowMs - m_catalogCache.loadedAtMs) < 30000)
        {
            outSummaries = m_catalogCache.summaries;
            outFilesByApp = m_catalogCache.filesByApp;
            return true;
        }
    }

    const std::string existingContent = ReadOrSyncCatalogJson(
        storageRoot,
        provider,
        accountId,
        syncRemote,
        /*pullWhenLocalMissing=*/syncRemote,
        /*allowLocalFallbackWhenSyncedEmpty=*/true);

    if (existingContent.empty())
    {
        return false;
    }

    const bool hasProviderKeysSection = RestoreMetadataFromCatalogJson(existingContent);

    const auto appObjs = SplitCatalogJsonObjectArray(existingContent, "apps");
    for (const auto& appObj : appObjs)
    {
        const size_t filesKeyPos = appObj.find("\"files\"");
        const std::string headerSlice = (filesKeyPos != std::string::npos) ? appObj.substr(0, filesKeyPos) : appObj;

        AppId_t appId = static_cast<AppId_t>(ExtractJsonUint64(headerSlice, "appId", 0));
        if (appId == 0 || appId == kCloudCatalogAppId) continue;

        CloudAppSummary_t summary;
        summary.appId = appId;
        summary.gameTitle = ExtractCatalogJsonString(headerSlice, "gameTitle");
        summary.fileCount = static_cast<uint32_t>(ExtractJsonUint64(headerSlice, "fileCount", 0));
        summary.totalSizeBytes = ExtractJsonUint64(headerSlice, "totalSizeBytes", 0);
        summary.redirected = ExtractJsonBool(headerSlice, "redirected", true);
        summary.inRemoteCloud = ExtractJsonBool(headerSlice, "inRemoteCloud", true);
        summary.inLocalMirror = ExtractJsonBool(headerSlice, "inLocalMirror", true);
        summary.childAppIds = ExtractCatalogUintArray(headerSlice, "childAppIds");

        std::vector<CloudFileEntry_t> files;
        const auto fileObjs = SplitCatalogJsonObjectArray(appObj, "files");
        files.reserve(fileObjs.size());
        uint64_t computedTotal = 0;
        for (const auto& fObj : fileObjs)
        {
            CloudFileEntry_t f;
            f.fileName = ExtractCatalogJsonString(fObj, "fileName");
            if (f.fileName.empty()) continue;
            f.folder = ExtractCatalogJsonString(fObj, "folder");
            if (f.folder.empty()) f.folder = "SteamCloudRemote";
            f.sizeBytes = ExtractJsonUint64(fObj, "sizeBytes", 0);
            f.modifiedTime = ExtractJsonUint64(fObj, "modifiedTime", 0);
            f.inRemoteCloud = true;
            f.inLocalMirror = true;
            computedTotal += f.sizeBytes;
            files.push_back(std::move(f));
        }

        if (!files.empty())
        {
            summary.fileCount = static_cast<uint32_t>(files.size());
            if (summary.totalSizeBytes == 0) summary.totalSizeBytes = computedTotal;
        }

        outSummaries[appId] = std::move(summary);
        outFilesByApp[appId] = std::move(files);
    }

    if (outSummaries.empty() && !hasProviderKeysSection)
    {
        return false;
    }

    if (!outSummaries.empty())
    {
        std::lock_guard<std::mutex> lock(m_catalogMutex);
        m_catalogCache.provider = provider;
        m_catalogCache.accountId = accountId;
        m_catalogCache.storageRoot = storageRoot;
        m_catalogCache.loadedAtMs = nowMs;
        m_catalogCache.summaries = outSummaries;
        m_catalogCache.filesByApp = outFilesByApp;
    }
    return true;
}

bool CCloudManager::StageAppFilesForCloudSync(AppId_t appId, uint32_t accountId)
{
    if (appId == 0 || appId == kCloudCatalogAppId) return false;

    const std::vector<uint32_t> accounts = ResolveCandidateAccountIds(accountId);
    if (accounts.empty()) return false;
    const uint32_t acct = accounts.front();

    const fs::path storageRoot = GetStorageDir();
    if (storageRoot.empty()) return false;

    std::error_code ec;
    const fs::path appDir = storageRoot / std::to_string(acct) / std::to_string(appId);
    fs::create_directories(appDir, ec);
    if (ec) return false;

    // Remove CAS cache dir so publish-full-manifest never treats blobs/ as user files
    fs::remove_all(appDir / "blobs", ec);
    ec.clear();

    // Collapse any <cleanPath>/<sha1> directory inside appDir into a flat file at <cleanPath>
    {
        std::vector<fs::path> shaLeaves;
        for (auto it = fs::recursive_directory_iterator(appDir, fs::directory_options::skip_permission_denied, ec);
             !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
        {
            std::error_code fEc;
            if (!it->is_regular_file(fEc) || fEc) continue;
            fs::path rel = fs::relative(it->path(), appDir, fEc);
            if (fEc || !rel.has_parent_path()) continue;
            if (IsHexHashSegment(it->path().filename().string()))
            {
                shaLeaves.push_back(it->path());
            }
        }
        for (const auto& leaf : shaLeaves)
        {
            if (!fs::is_regular_file(leaf, ec)) continue;
            fs::path parentDir = leaf.parent_path();
            if (parentDir == appDir) continue;
            fs::path tmpFile = parentDir.string() + ".navi_flatten_tmp";
            fs::copy_file(leaf, tmpFile, fs::copy_options::overwrite_existing, ec);
            if (!ec)
            {
                fs::remove_all(parentDir, ec);
                fs::rename(tmpFile, parentDir, ec);
            }
        }
    }

    // Read existing file_tokens.cloudredirect
    std::unordered_map<std::string, std::string> fileTokens;
    {
        std::ifstream ftIn(appDir / "file_tokens.cloudredirect");
        std::string line;
        while (std::getline(ftIn, line))
        {
            size_t tab = line.find('\t');
            if (tab != std::string::npos)
            {
                std::string k = line.substr(0, tab);
                std::string v = line.substr(tab + 1);
                while (!v.empty() && (v.back() == '\r' || v.back() == '\n')) v.pop_back();
                if (!k.empty() && !v.empty()) fileTokens[k] = v;
            }
        }
    }

    auto stageOneFile = [&](const fs::path& srcFile, const std::string& cleanRel, const std::string& token) {
        if (cleanRel.empty() || cleanRel.find("..") != std::string::npos || IsInternalMetadataRelPath(cleanRel))
        {
            return;
        }
        std::error_code sEc;
        if (!fs::is_regular_file(srcFile, sEc) || sEc) return;

        fs::path dst = appDir / cleanRel;
        if (fs::is_directory(dst, sEc))
        {
            fs::remove_all(dst, sEc);
        }
        fs::create_directories(dst.parent_path(), sEc);

        bool shouldCopy = true;
        if (fs::is_regular_file(dst, sEc) && !sEc)
        {
            std::error_code cEc;
            if (fs::equivalent(srcFile, dst, cEc) && !cEc)
            {
                shouldCopy = false;
            }
        }
        if (shouldCopy)
        {
            fs::copy_file(srcFile, dst, fs::copy_options::overwrite_existing, sEc);
        }
        if (!token.empty())
        {
            fileTokens[cleanRel] = token;
        }
    };

    std::shared_ptr<const ScanIndex> index = ObtainScanIndex();
    std::vector<AppId_t> targetIds = {appId};
    if (index)
    {
        for (AppId_t cid : GetChildDepotIdsForApp(appId, *index))
        {
            if (cid != 0 && cid != appId && cid != kCloudCatalogAppId)
            {
                targetIds.push_back(cid);
            }
        }
    }

    const fs::path steamRoot = GetSteamRoot();
    const bool useSteamRoot = (m_storageDir.empty() || !m_steamRoot.empty()) && !steamRoot.empty();

    for (AppId_t targetId : targetIds)
    {
        const bool isChild = (targetId != appId);
        const std::string targetIdStr = std::to_string(targetId);
        auto makeToken = [&](const std::string& baseFolder) -> std::string {
            if (isChild)
            {
                return "Depot " + targetIdStr + " / " + baseFolder;
            }
            return "%" + baseFolder + "%";
        };

        // A. If child depot has files in its own <storageRoot>/<acct>/<childId>/ dir, fold them into parent appDir
        if (isChild)
        {
            const fs::path childStorageDir = storageRoot / std::to_string(acct) / targetIdStr;
            if (fs::is_directory(childStorageDir, ec))
            {
                for (auto it = fs::recursive_directory_iterator(childStorageDir, fs::directory_options::skip_permission_denied, ec);
                     !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
                {
                    std::error_code fEc;
                    if (!it->is_regular_file(fEc) || fEc) continue;
                    fs::path rel = fs::relative(it->path(), childStorageDir, fEc);
                    if (fEc || rel.empty()) continue;
                    std::string relStr = rel.generic_string();
                    if (IsInternalMetadataRelPath(relStr)) continue;
                    if (fs::exists(appDir / relStr, fEc)) continue;
                    stageOneFile(it->path(), relStr, makeToken(NormalizeFolderToken("", relStr)));
                }
            }
        }

        if (!useSteamRoot) continue;

        // B. Steam userdata/<acct>/<targetId>/remote/
        if (targetId != 760)
        {
            const fs::path remoteDir = steamRoot / "userdata" / std::to_string(acct) / targetIdStr / "remote";
            if (fs::is_directory(remoteDir, ec))
            {
                for (auto it = fs::recursive_directory_iterator(remoteDir, fs::directory_options::skip_permission_denied, ec);
                     !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
                {
                    std::error_code fEc;
                    if (!it->is_regular_file(fEc) || fEc) continue;
                    fs::path rel = fs::relative(it->path(), remoteDir, fEc);
                    if (fEc || rel.empty()) continue;
                    std::string relStr = rel.generic_string();
                    if (IsInternalMetadataRelPath(relStr)) continue;
                    if (isChild && fs::exists(appDir / relStr, fEc)) continue;
                    stageOneFile(it->path(), relStr, makeToken("SteamCloudRemote"));
                }
            }
        }

        // C. Proton Auto-Cloud directories containing steam_autocloud.vdf
        static const std::pair<const char*, const char*> kAutoCloudRoots[] = {
            {"AppData/LocalLow", "WinAppDataLocalLow"},
            {"AppData/Local", "WinAppDataLocal"},
            {"AppData/Roaming", "WinAppDataRoaming"},
            {"Documents", "WinMyDocuments"},
            {"Saved Games", "WinSavedGames"},
        };

        const fs::path steamuserDir = steamRoot / "steamapps" / "compatdata" / targetIdStr / "pfx" / "drive_c" / "users" / "steamuser";
        if (fs::is_directory(steamuserDir, ec))
        {
            for (const auto& [relCat, catToken] : kAutoCloudRoots)
            {
                const fs::path catRoot = steamuserDir / relCat;
                if (!fs::is_directory(catRoot, ec)) continue;

                std::vector<fs::path> autoCloudDirs;
                for (auto it = fs::recursive_directory_iterator(catRoot, fs::directory_options::skip_permission_denied, ec);
                     !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
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
                         !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
                    {
                        std::error_code fEc;
                        if (!it->is_regular_file(fEc) || fEc) continue;
                        fs::path rel = fs::relative(it->path(), catRoot, fEc);
                        if (fEc || rel.empty()) continue;
                        std::string relStr = rel.generic_string();
                        if (IsInternalMetadataRelPath(relStr)) continue;
                        if (isChild && fs::exists(appDir / relStr, fEc)) continue;
                        stageOneFile(it->path(), relStr, makeToken(catToken));
                    }
                }
            }
        }

        // D. Goldberg save paths
        for (const auto& goldbergDir : ResolveGoldbergSavePaths(targetId))
        {
            if (!fs::is_directory(goldbergDir, ec)) continue;
            for (auto it = fs::recursive_directory_iterator(goldbergDir, fs::directory_options::skip_permission_denied, ec);
                 !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
            {
                std::error_code fEc;
                if (!it->is_regular_file(fEc) || fEc) continue;
                fs::path rel = fs::relative(it->path(), goldbergDir, fEc);
                if (fEc || rel.empty()) continue;
                std::string relStr = rel.generic_string();
                if (IsInternalMetadataRelPath(relStr)) continue;
                if (fs::exists(appDir / relStr, fEc)) continue;
                stageOneFile(it->path(), relStr, makeToken("WinAppDataRoaming"));
            }
        }

        // E. Steam Lua unlocker plugin (.lua, .provider) and depot manifest (.manifest) files
        const fs::path stplugDir = steamRoot / "config" / "stplug-in";
        const fs::path depotcacheDir = steamRoot / "depotcache";
        const fs::path legacyDepotcacheDir = steamRoot / "config" / "depotcache";

        const fs::path luaFile = stplugDir / (targetIdStr + ".lua");
        if (fs::is_regular_file(luaFile, ec) && !ec)
        {
            std::string luaRel = "stplug-in/" + targetIdStr + ".lua";
            if (!isChild || !fs::exists(appDir / luaRel, ec))
            {
                stageOneFile(luaFile, luaRel, makeToken("stplug-in"));
            }

            const fs::path provFile = stplugDir / (targetIdStr + ".provider");
            if (fs::is_regular_file(provFile, ec) && !ec)
            {
                std::string provRel = "stplug-in/" + targetIdStr + ".provider";
                if (!isChild || !fs::exists(appDir / provRel, ec))
                {
                    stageOneFile(provFile, provRel, makeToken("stplug-in"));
                }
            }

            auto luaData = LuaParser::parseFile(luaFile.string());
            for (const auto& [depotId, manifestGid] : luaData.manifests)
            {
                if (manifestGid.empty() || manifestGid == "0") continue;
                std::string mfName = depotId + "_" + manifestGid + ".manifest";
                fs::path mfPath;
                if (fs::is_regular_file(depotcacheDir / mfName, ec)) mfPath = depotcacheDir / mfName;
                else if (fs::is_regular_file(legacyDepotcacheDir / mfName, ec)) mfPath = legacyDepotcacheDir / mfName;
                else if (fs::is_regular_file(stplugDir / "manifests" / mfName, ec)) mfPath = stplugDir / "manifests" / mfName;
                else if (fs::is_regular_file(stplugDir / mfName, ec)) mfPath = stplugDir / mfName;

                if (!mfPath.empty())
                {
                    std::string mfRel = "depotcache/" + mfName;
                    if (!isChild || !fs::exists(appDir / mfRel, ec))
                    {
                        stageOneFile(mfPath, mfRel, makeToken("depotcache"));
                    }
                }
            }
        }

        auto stageDirManifests = [&](const fs::path& dir) {
            if (!fs::is_directory(dir, ec)) return;
            for (const auto& entry : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec))
            {
                if (ec) break;
                if (!entry.is_regular_file(ec)) continue;
                std::string fn = entry.path().filename().string();
                if (fn.ends_with(".manifest") && (fn.rfind(targetIdStr + "_", 0) == 0 || fn == targetIdStr + ".manifest"))
                {
                    std::string mfRel = "depotcache/" + fn;
                    if (!isChild || !fs::exists(appDir / mfRel, ec))
                    {
                        stageOneFile(entry.path(), mfRel, makeToken("depotcache"));
                    }
                }
            }
        };

        stageDirManifests(depotcacheDir);
        stageDirManifests(legacyDepotcacheDir);
        stageDirManifests(stplugDir);
        stageDirManifests(stplugDir / "manifests");
    }

    // Ensure root_token.cloudredirect exists
    const fs::path rtPath = appDir / "root_token.cloudredirect";
    if (!fs::is_regular_file(rtPath, ec))
    {
        std::ofstream rtOut(rtPath, std::ios::trunc);
        rtOut << "%SteamCloudRemote%\n";
    }

    // Ensure all regular user files in appDir are recorded in file_tokens.cloudredirect
    std::vector<std::string> stagedFiles;
    for (auto it = fs::recursive_directory_iterator(appDir, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
    {
        std::error_code fEc;
        if (!it->is_regular_file(fEc) || fEc) continue;
        fs::path rel = fs::relative(it->path(), appDir, fEc);
        if (fEc || rel.empty()) continue;
        std::string relStr = rel.generic_string();
        if (IsInternalMetadataRelPath(relStr)) continue;
        stagedFiles.push_back(relStr);
        if (!fileTokens.contains(relStr))
        {
            fileTokens[relStr] = "%" + NormalizeFolderToken("", relStr) + "%";
        }
    }

    std::sort(stagedFiles.begin(), stagedFiles.end());
    {
        std::ofstream ftOut(appDir / "file_tokens.cloudredirect", std::ios::trunc);
        for (const auto& relStr : stagedFiles)
        {
            ftOut << relStr << '\t' << fileTokens[relStr] << '\n';
        }
    }

    BumpCnFile(appDir);
    return !stagedFiles.empty();
}

bool CCloudManager::UpdateAndPublishCloudCatalog(const std::string& provider, uint32_t accountId, bool overwriteProviderKeys)
{
    const std::vector<uint32_t> accounts = ResolveCandidateAccountIds(accountId);
    if (accounts.empty()) return false;
    const uint32_t acct = accounts.front();

    const fs::path storageRoot = GetStorageDir();
    if (storageRoot.empty()) return false;

    std::shared_ptr<const ScanIndex> index = ObtainScanIndex();
    if (!index) return false;

    std::lock_guard<std::recursive_mutex> catalogSyncLock(m_catalogSyncMutex);

    const bool isRemoteProvider = (!provider.empty() && provider != "local-only");
    bool remoteHasCatalog = false;
    std::vector<AppId_t> remoteConfirmedApps;

    if (isRemoteProvider)
    {
        std::string out = RunCliCommand({"list-remote-app-ids", provider, std::to_string(acct)}, 15000);
        std::string jsonStr = ExtractJsonSubstring(out);
        if (jsonStr.empty())
        {
            out = RunCliCommand({"list-remote-apps", provider, std::to_string(acct)}, 15000);
            jsonStr = ExtractJsonSubstring(out);
        }

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
                        if (id == kCloudCatalogAppId)
                        {
                            remoteHasCatalog = true;
                        }
                        else if (id != 0 &&
                                 std::find(remoteConfirmedApps.begin(), remoteConfirmedApps.end(), id) == remoteConfirmedApps.end())
                        {
                            remoteConfirmedApps.push_back(id);
                        }
                    }
                    qPos = q2 + 1;
                }
            }
        }

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
                if (id == kCloudCatalogAppId)
                {
                    remoteHasCatalog = true;
                }
                else if (id != 0 &&
                         std::find(remoteConfirmedApps.begin(), remoteConfirmedApps.end(), id) == remoteConfirmedApps.end())
                {
                    remoteConfirmedApps.push_back(id);
                }
            }
            pos = objEnd + 1;
        }
    }

    // Group remote-confirmed apps into top-level parent apps
    std::vector<AppId_t> topLevelApps;
    std::unordered_map<AppId_t, std::vector<AppId_t>> parentToChildren;
    for (AppId_t id : remoteConfirmedApps)
    {
        AppId_t parentId = GetParentAppIdForDepot(id, *index);
        if (parentId != 0 && parentId != id && parentId != kCloudCatalogAppId)
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
    for (AppId_t parentId : topLevelApps)
    {
        for (AppId_t cid : GetChildDepotIdsForApp(parentId, *index))
        {
            if (cid == kCloudCatalogAppId) continue;
            auto& cList = parentToChildren[parentId];
            if (std::find(cList.begin(), cList.end(), cid) == cList.end())
            {
                cList.push_back(cid);
            }
        }
    }

    const std::unordered_set<AppId_t> remoteConfirmedSet(remoteConfirmedApps.begin(), remoteConfirmedApps.end());

    auto knownTitles = g_config.gameTitles.get();
    struct CatalogAppItem
    {
        CloudAppSummary_t summary;
        std::vector<CloudFileEntry_t> files;
    };
    std::vector<CatalogAppItem> items;

    for (AppId_t appId : topLevelApps)
    {
        if (appId == 0 || appId == kCloudCatalogAppId) continue;
        if (!g_config.IsAppCloudRedirected(appId)) continue;

        // Collect files directly from the cloud storage mirror / remote (without scanning un-staged local dirs)
        const std::string baseProv = (isRemoteProvider && remoteConfirmedSet.contains(appId)) ? provider : "local-only";
        std::vector<CloudFileEntry_t> files = ListAppCloudFilesDirect(appId, baseProv, acct, index.get());
        const auto& children = parentToChildren[appId];
        std::unordered_set<std::string> seenNames;
        for (const auto& f : files) seenNames.insert(f.fileName);
        for (AppId_t cid : children)
        {
            if (isRemoteProvider && !remoteConfirmedSet.contains(cid))
            {
                std::error_code cEc;
                if (!fs::is_directory(storageRoot / std::to_string(acct) / std::to_string(cid), cEc))
                {
                    continue;
                }
            }
            const std::string childProv = (isRemoteProvider && remoteConfirmedSet.contains(cid)) ? provider : "local-only";
            for (auto& cf : ListAppCloudFilesDirect(cid, childProv, acct, index.get()))
            {
                if (seenNames.insert(cf.fileName).second)
                {
                    if (cf.folder.rfind("Depot ", 0) != 0)
                    {
                        cf.folder = "Depot " + std::to_string(cid) + " / " + (cf.folder.empty() ? "SteamCloudRemote" : cf.folder);
                    }
                    files.push_back(std::move(cf));
                }
            }
        }
        if (files.empty()) continue;

        for (auto& f : files)
        {
            f.inRemoteCloud = true;
            f.inLocalMirror = true;
        }
        std::sort(files.begin(), files.end(), [](const CloudFileEntry_t& a, const CloudFileEntry_t& b) {
            if (a.folder != b.folder) return a.folder < b.folder;
            return a.fileName < b.fileName;
        });

        CloudAppSummary_t summary;
        summary.appId = appId;
        summary.childAppIds = children;
        auto tIt = knownTitles.find(appId);
        if (tIt != knownTitles.end() && !tIt->second.empty())
        {
            summary.gameTitle = tIt->second;
        }
        else
        {
            summary.gameTitle = ResolveAppTitleFromManifestsInternal(appId, 0, *index);
        }
        if (summary.gameTitle.empty())
        {
            summary.gameTitle = "App " + std::to_string(appId);
        }
        summary.fileCount = static_cast<uint32_t>(files.size());
        summary.redirected = true;
        summary.inRemoteCloud = true;
        summary.inLocalMirror = true;
        uint64_t totalBytes = 0;
        for (const auto& f : files) totalBytes += f.sizeBytes;
        summary.totalSizeBytes = totalBytes;

        items.push_back(CatalogAppItem{std::move(summary), std::move(files)});
    }

    std::sort(items.begin(), items.end(), [](const CatalogAppItem& a, const CatalogAppItem& b) {
        if (!a.summary.gameTitle.empty() && !b.summary.gameTitle.empty() && a.summary.gameTitle != b.summary.gameTitle)
        {
            return a.summary.gameTitle < b.summary.gameTitle;
        }
        return a.summary.appId < b.summary.appId;
    });

    const bool syncKeysEnabled = g_config.cloudSyncProviderKeys.get();
    const bool syncRyuu = syncKeysEnabled && g_config.cloudSyncRyuuKey.get();
    const bool syncDepotBox = syncKeysEnabled && g_config.cloudSyncDepotBoxKey.get();
    const bool syncHubcap = syncKeysEnabled && g_config.cloudSyncHubcapKey.get();

    if (!overwriteProviderKeys &&
        ((syncRyuu && g_config.ryuuKey.get().empty()) ||
         (syncDepotBox && g_config.depotBoxKey.get().empty()) ||
         (syncHubcap && g_config.hubcapKey.get().empty())))
    {
        std::unordered_map<AppId_t, CloudAppSummary_t> preSummaries;
        std::unordered_map<AppId_t, std::vector<CloudFileEntry_t>> preFiles;
        const bool shouldPullRemoteCatalog = (isRemoteProvider && remoteHasCatalog);
        LoadCloudCatalog(shouldPullRemoteCatalog ? provider : "local-only",
                         acct,
                         shouldPullRemoteCatalog,
                         preSummaries,
                         preFiles);
    }

    const std::string ryuuKeyVal = syncRyuu ? g_config.ryuuKey.get() : std::string{};
    const std::string depotBoxKeyVal = syncDepotBox ? g_config.depotBoxKey.get() : std::string{};
    const std::string hubcapKeyVal = syncHubcap ? g_config.hubcapKey.get() : std::string{};

    const uint64_t updatedAt = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    std::ostringstream js;
    js << "{\n"
       << "  \"version\": 1,\n"
       << "  \"accountId\": " << acct << ",\n"
       << "  \"updatedAt\": " << updatedAt << ",\n"
       << "  \"providerKeys\": {\n"
       << "    \"ryuuKey\": \"" << EscapeCatalogJsonString(ryuuKeyVal) << "\",\n"
       << "    \"depotBoxKey\": \"" << EscapeCatalogJsonString(depotBoxKeyVal) << "\",\n"
       << "    \"hubcapKey\": \"" << EscapeCatalogJsonString(hubcapKeyVal) << "\"\n"
       << "  },\n"
       << "  \"apps\": [\n";
    for (size_t i = 0; i < items.size(); ++i)
    {
        const auto& item = items[i];
        const auto& s = item.summary;
        if (i > 0) js << ",\n";
        js << "    {\n"
           << "      \"appId\": " << s.appId << ",\n"
           << "      \"gameTitle\": \"" << EscapeCatalogJsonString(s.gameTitle) << "\",\n"
           << "      \"fileCount\": " << s.fileCount << ",\n"
           << "      \"totalSizeBytes\": " << s.totalSizeBytes << ",\n"
           << "      \"redirected\": true,\n"
           << "      \"inRemoteCloud\": true,\n"
           << "      \"inLocalMirror\": true,\n"
           << "      \"childAppIds\": [";
        for (size_t c = 0; c < s.childAppIds.size(); ++c)
        {
            if (c > 0) js << ", ";
            js << s.childAppIds[c];
        }
        js << "],\n"
           << "      \"files\": [\n";
        for (size_t fIdx = 0; fIdx < item.files.size(); ++fIdx)
        {
            const auto& f = item.files[fIdx];
            if (fIdx > 0) js << ",\n";
            js << "        {\n"
               << "          \"fileName\": \"" << EscapeCatalogJsonString(f.fileName) << "\",\n"
               << "          \"folder\": \"" << EscapeCatalogJsonString(f.folder) << "\",\n"
               << "          \"sizeBytes\": " << f.sizeBytes << ",\n"
               << "          \"modifiedTime\": " << f.modifiedTime << "\n"
               << "        }";
        }
        js << "\n      ]\n"
           << "    }";
    }
    js << "\n  ]\n}\n";

    std::error_code ec;
    const fs::path catalogDir = storageRoot / std::to_string(acct) / std::to_string(kCloudCatalogAppId);
    fs::create_directories(catalogDir, ec);
    if (ec) return false;

    fs::remove_all(catalogDir / "blobs", ec);
    const fs::path catalogFile = catalogDir / kCloudCatalogFileName;
    if (fs::is_directory(catalogFile, ec))
    {
        fs::remove_all(catalogFile, ec);
    }

    {
        std::ofstream out(catalogFile, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return false;
        out << js.str();
    }
    {
        std::ofstream rtOut(catalogDir / "root_token.cloudredirect", std::ios::trunc);
        rtOut << "%SteamCloudRemote%\n";
    }
    {
        std::ofstream ftOut(catalogDir / "file_tokens.cloudredirect", std::ios::trunc);
        ftOut << kCloudCatalogFileName << "\t%SteamCloudRemote%\n";
    }
    BumpCnFile(catalogDir);

    if (isRemoteProvider)
    {
        RunCliCommand({"publish-full-manifest", provider, std::to_string(acct),
                       std::to_string(kCloudCatalogAppId), storageRoot.parent_path().string()}, 30000);
    }

    {
        std::lock_guard<std::mutex> lock(m_catalogMutex);
        m_catalogCache.provider = provider;
        m_catalogCache.accountId = acct;
        m_catalogCache.storageRoot = storageRoot;
        m_catalogCache.loadedAtMs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        m_catalogCache.remoteSyncedAtMs = 0;
        m_catalogCache.remoteCatalogAbsent = false;
        m_catalogCache.summaries.clear();
        m_catalogCache.filesByApp.clear();
        for (auto& item : items)
        {
            AppId_t id = item.summary.appId;
            m_catalogCache.summaries[id] = std::move(item.summary);
            m_catalogCache.filesByApp[id] = std::move(item.files);
        }
    }

    return true;
}

bool CCloudManager::RestoreMetadataFromCloudCatalog(const std::string& provider, uint32_t accountId, bool syncRemote)
{
    const std::string prov = provider.empty() ? g_config.cloudSavesProvider.get() : provider;
    if (prov.empty())
    {
        return false;
    }

    const std::vector<uint32_t> accounts = ResolveCandidateAccountIds(accountId);
    if (accounts.empty())
    {
        return false;
    }

    std::unordered_map<AppId_t, CloudAppSummary_t> summaries;
    std::unordered_map<AppId_t, std::vector<CloudFileEntry_t>> filesByApp;
    return LoadCloudCatalog(prov, accounts.front(), syncRemote, summaries, filesByApp);
}

bool CCloudManager::PublishMetadataCatalogIfAuthenticated(const std::string& provider, uint32_t accountId, bool overwriteProviderKeys)
{
    if (!g_config.cloudSavesEnabled.get())
    {
        return false;
    }

    std::string prov = provider.empty() ? g_config.cloudSavesProvider.get() : provider;
    if (prov.empty())
    {
        prov = "local-only";
    }

    const std::vector<uint32_t> accounts = ResolveCandidateAccountIds(accountId);
    if (accounts.empty())
    {
        LOG_DEBUG("Skipping metadata catalog publish: no Steam account ID resolved yet\n");
        return false;
    }

    const uint32_t targetAcct = accounts.front();
    if (prov != "local" && prov != "local-only")
    {
        const ProviderAuthStatus_t status = CheckAuthStatus(prov);
        if (!status.authenticated)
        {
            return UpdateAndPublishCloudCatalog("local-only", targetAcct, overwriteProviderKeys);
        }
    }

    return UpdateAndPublishCloudCatalog(prov, targetAcct, overwriteProviderKeys);
}

CloudProviderKeysStatus_t CCloudManager::GetCloudProviderKeysStatus(const std::string& provider, uint32_t accountId, bool syncRemote)
{
    CloudProviderKeysStatus_t status;
    std::string prov = provider.empty() ? g_config.cloudSavesProvider.get() : provider;
    if (prov.empty() || prov == "local")
    {
        prov = "local-only";
    }
    status.provider = prov;
    status.syncEnabled = g_config.cloudSyncProviderKeys.get();
    status.syncRyuu = g_config.cloudSyncRyuuKey.get();
    status.syncDepotBox = g_config.cloudSyncDepotBoxKey.get();
    status.syncHubcap = g_config.cloudSyncHubcapKey.get();

    const std::vector<uint32_t> accounts = ResolveCandidateAccountIds(accountId);
    if (accounts.empty())
    {
        return status;
    }

    const fs::path storageRoot = GetStorageDir();
    if (storageRoot.empty())
    {
        return status;
    }

    const bool isRemoteProvider = (prov != "local-only");
    if (syncRemote && isRemoteProvider)
    {
        bool hasCustomCli = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            hasCustomCli = !m_cliPath.empty();
        }
        if (!hasCustomCli && !CheckAuthStatus(prov).authenticated)
        {
            return status;
        }
    }

    std::lock_guard<std::recursive_mutex> catalogSyncLock(m_catalogSyncMutex);

    const std::string content = ReadOrSyncCatalogJson(
        storageRoot,
        prov,
        accounts.front(),
        syncRemote,
        /*pullWhenLocalMissing=*/false,
        /*allowLocalFallbackWhenSyncedEmpty=*/!isRemoteProvider || !syncRemote);
    if (content.empty())
    {
        return status;
    }

    const std::string pkObj = ExtractCatalogJsonObject(content, "providerKeys");
    const bool looksLikeCatalog = !pkObj.empty() ||
        content.find("\"version\"") != std::string::npos ||
        content.find("\"apps\"") != std::string::npos;
    if (!looksLikeCatalog)
    {
        return status;
    }

    status.catalogFound = true;
    status.updatedAt = ExtractJsonUint64(content, "updatedAt", 0);
    if (!pkObj.empty())
    {
        status.hasRyuuKey = !ExtractCatalogJsonString(pkObj, "ryuuKey").empty();
        status.hasDepotBoxKey = !ExtractCatalogJsonString(pkObj, "depotBoxKey").empty();
        status.hasHubcapKey = !ExtractCatalogJsonString(pkObj, "hubcapKey").empty();
    }
    return status;
}

bool CCloudManager::SyncAppToCloudAndUpdateCatalog(AppId_t appId, const std::string& provider, uint32_t accountId)
{
    if (appId == 0 || appId == kCloudCatalogAppId) return false;

    const std::vector<uint32_t> accounts = ResolveCandidateAccountIds(accountId);
    if (accounts.empty()) return false;
    const uint32_t acct = accounts.front();

    if (!StageAppFilesForCloudSync(appId, acct))
    {
        return false;
    }

    const fs::path storageRoot = GetStorageDir();
    if (!provider.empty() && provider != "local-only")
    {
        std::string out = RunCliCommand({"publish-full-manifest", provider, std::to_string(acct),
                                         std::to_string(appId), storageRoot.parent_path().string()}, 60000);
        if (out.find("\"success\":true") == std::string::npos &&
            out.find("\"success\": true") == std::string::npos)
        {
            return false;
        }
    }

    return UpdateAndPublishCloudCatalog(provider, acct);
}

void CCloudManager::SetCredentialsDirForTest(const fs::path& path)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_credentialsDir = path;
    }
    InvalidateCloudCatalogCache();
}

fs::path CCloudManager::GetCredentialsDir() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_credentialsDir.empty())
    {
        return m_credentialsDir;
    }
    return CCloudRedirectBridge::Instance().GetPaths().crConfigDir;
}

ProviderCredentials_t CCloudManager::GetProviderCredentials(const std::string& provider) const
{
    ProviderCredentials_t creds;
    creds.provider = provider;
    creds.configured = false;
    creds.hasSecretKey = false;
    creds.region = (provider == "r2") ? "auto" : ((provider == "s3") ? "us-east-1" : "");

    if (provider != "r2" && provider != "s3")
    {
        return creds;
    }

    const fs::path credFile = GetCredentialsDir() / (provider + "_credentials.json");
    std::error_code ec;
    if (!fs::exists(credFile, ec) || !fs::is_regular_file(credFile, ec))
    {
        return creds;
    }

    std::string rawSecret;
    try
    {
        YAML::Node root = YAML::LoadFile(credFile.string());
        if (root && root.IsMap())
        {
            if (root["account_id"] && root["account_id"].IsScalar())
                creds.accountId = root["account_id"].as<std::string>("");
            if (root["access_key_id"] && root["access_key_id"].IsScalar())
                creds.accessKeyId = root["access_key_id"].as<std::string>("");
            if (root["secret_access_key"] && root["secret_access_key"].IsScalar())
                rawSecret = root["secret_access_key"].as<std::string>("");
            if (root["bucket"] && root["bucket"].IsScalar())
                creds.bucket = root["bucket"].as<std::string>("");
            if (root["region"] && root["region"].IsScalar())
            {
                std::string reg = root["region"].as<std::string>("");
                if (!reg.empty()) creds.region = reg;
            }
            if (root["endpoint"] && root["endpoint"].IsScalar())
                creds.endpoint = root["endpoint"].as<std::string>("");
            if (root["key_prefix"] && root["key_prefix"].IsScalar())
                creds.keyPrefix = root["key_prefix"].as<std::string>("");
        }
    }
    catch (...)
    {
        std::ifstream in(credFile);
        std::ostringstream ss;
        ss << in.rdbuf();
        std::string content = ss.str();
        creds.accountId = ExtractCatalogJsonString(content, "account_id");
        creds.accessKeyId = ExtractCatalogJsonString(content, "access_key_id");
        rawSecret = ExtractCatalogJsonString(content, "secret_access_key");
        creds.bucket = ExtractCatalogJsonString(content, "bucket");
        std::string reg = ExtractCatalogJsonString(content, "region");
        if (!reg.empty()) creds.region = reg;
        creds.endpoint = ExtractCatalogJsonString(content, "endpoint");
        creds.keyPrefix = ExtractCatalogJsonString(content, "key_prefix");
    }

    if (!rawSecret.empty())
    {
        creds.hasSecretKey = true;
        creds.secretAccessKey = "********";
    }
    else
    {
        creds.hasSecretKey = false;
        creds.secretAccessKey.clear();
    }

    const bool hasR2EndpointOrAcct = (provider != "r2") || !creds.accountId.empty() || !creds.endpoint.empty();
    creds.configured = !creds.bucket.empty() && !creds.accessKeyId.empty() && creds.hasSecretKey && hasR2EndpointOrAcct;
    return creds;
}

bool CCloudManager::SaveProviderCredentials(const ProviderCredentials_t& creds, std::string& outError)
{
    outError.clear();
    if (creds.provider != "r2" && creds.provider != "s3")
    {
        outError = "Unsupported provider for credentials (must be r2 or s3)";
        return false;
    }

    const fs::path credDir = GetCredentialsDir();
    const fs::path credFile = credDir / (creds.provider + "_credentials.json");

    std::string secretKey = creds.secretAccessKey;
    if (secretKey.empty() || secretKey == "********")
    {
        secretKey.clear();
        std::error_code ec;
        if (fs::exists(credFile, ec) && fs::is_regular_file(credFile, ec))
        {
            try
            {
                YAML::Node existing = YAML::LoadFile(credFile.string());
                if (existing && existing.IsMap() && existing["secret_access_key"] && existing["secret_access_key"].IsScalar())
                {
                    secretKey = existing["secret_access_key"].as<std::string>("");
                }
            }
            catch (...)
            {
                std::ifstream in(credFile);
                std::ostringstream ss;
                ss << in.rdbuf();
                secretKey = ExtractCatalogJsonString(ss.str(), "secret_access_key");
            }
        }
    }

    if (creds.bucket.empty())
    {
        outError = "Bucket name is required";
        return false;
    }
    if (creds.accessKeyId.empty())
    {
        outError = "Access Key ID is required";
        return false;
    }
    if (secretKey.empty())
    {
        outError = "Secret Access Key is required";
        return false;
    }
    if (creds.provider == "r2" && creds.accountId.empty() && creds.endpoint.empty())
    {
        outError = "Account ID or Endpoint URL is required for Cloudflare R2";
        return false;
    }

    std::string region = creds.region;
    if (region.empty())
    {
        region = (creds.provider == "r2") ? "auto" : "us-east-1";
    }

    std::string endpoint = creds.endpoint;
    if (creds.provider == "r2" && endpoint.empty() && !creds.accountId.empty())
    {
        endpoint = "https://" + creds.accountId + ".r2.cloudflarestorage.com";
    }

    std::ostringstream js;
    js << "{\n";
    if (creds.provider == "r2" || !creds.accountId.empty())
    {
        js << "  \"account_id\": \"" << EscapeCatalogJsonString(creds.accountId) << "\",\n";
    }
    js << "  \"access_key_id\": \"" << EscapeCatalogJsonString(creds.accessKeyId) << "\",\n"
       << "  \"secret_access_key\": \"" << EscapeCatalogJsonString(secretKey) << "\",\n"
       << "  \"bucket\": \"" << EscapeCatalogJsonString(creds.bucket) << "\",\n"
       << "  \"region\": \"" << EscapeCatalogJsonString(region) << "\",\n"
       << "  \"endpoint\": \"" << EscapeCatalogJsonString(endpoint) << "\",\n"
       << "  \"key_prefix\": \"" << EscapeCatalogJsonString(creds.keyPrefix) << "\"\n"
       << "}\n";

    std::error_code ec;
    fs::create_directories(credDir, ec);
    if (!AtomicFile::write(credFile, js.str()))
    {
        outError = "Failed to write credentials file";
        return false;
    }

    ::chmod(credFile.c_str(), 0600);
    InvalidateCloudCatalogCache();
    return true;
}

bool CCloudManager::DeleteProviderCredentials(const std::string& provider)
{
    if (provider != "r2" && provider != "s3")
    {
        return false;
    }

    const fs::path credFile = GetCredentialsDir() / (provider + "_credentials.json");
    std::error_code ec;
    if (!fs::exists(credFile, ec))
    {
        InvalidateCloudCatalogCache();
        return true;
    }
    fs::remove(credFile, ec);
    if (!ec)
    {
        InvalidateCloudCatalogCache();
    }
    return !ec;
}

MigrationScanResult_t CCloudManager::ScanProviderForMigration(const std::string& provider)
{
    MigrationScanResult_t result;
    result.provider = provider;

    const std::string normProv = (provider.empty() || provider == "local-only") ? "local" : provider;
    const std::string out = RunCliCommand({"scan-all", normProv}, 30000);
    if (out.empty())
    {
        result.error = "Failed to execute cloud_redirect_cli scan-all";
        return result;
    }

    std::string completeJson;
    std::string errorJson;
    {
        std::istringstream iss(out);
        std::string line;
        while (std::getline(iss, line))
        {
            if (line.find("\"type\":\"complete\"") != std::string::npos ||
                line.find("\"type\": \"complete\"") != std::string::npos)
            {
                completeJson = line;
            }
            else if (line.find("\"type\":\"error\"") != std::string::npos ||
                     line.find("\"type\": \"error\"") != std::string::npos)
            {
                errorJson = line;
            }
        }
    }

    if (completeJson.empty() && errorJson.empty())
    {
        std::string extracted = ExtractJsonSubstring(out);
        if (extracted.find("\"type\":\"error\"") != std::string::npos ||
            extracted.find("\"type\": \"error\"") != std::string::npos)
        {
            errorJson = extracted;
        }
        else if (extracted.find("\"apps\"") != std::string::npos ||
                 extracted.find("\"total_apps\"") != std::string::npos)
        {
            completeJson = extracted;
        }
    }

    if (completeJson.empty())
    {
        result.success = false;
        if (!errorJson.empty())
        {
            result.error = ExtractCatalogJsonString(errorJson, "message");
            if (result.error.empty()) result.error = ExtractCatalogJsonString(errorJson, "error");
        }
        if (result.error.empty())
        {
            result.error = "Provider scan did not return completion payload";
        }
        return result;
    }

    result.totalApps = static_cast<uint32_t>(ExtractJsonUint64(completeJson, "total_apps", 0));
    result.totalFiles = static_cast<uint32_t>(ExtractJsonUint64(completeJson, "total_files", 0));
    result.totalBytes = ExtractJsonUint64(completeJson, "total_bytes", 0);

    const auto knownTitles = g_config.gameTitles.get();
    const auto appObjs = SplitCatalogJsonObjectArray(completeJson, "apps");
    uint32_t sumFiles = 0;
    uint64_t sumBytes = 0;

    for (const auto& appObj : appObjs)
    {
        std::string appIdStr = ExtractCatalogJsonString(appObj, "app_id");
        AppId_t appId = 0;
        if (!appIdStr.empty() && IsDigitsOnly(appIdStr))
        {
            appId = static_cast<AppId_t>(std::strtoul(appIdStr.c_str(), nullptr, 10));
        }
        else
        {
            appId = static_cast<AppId_t>(ExtractJsonUint64(appObj, "app_id", ExtractJsonUint64(appObj, "appId", 0)));
        }

        if (appId == 0 || appId == kCloudCatalogAppId)
        {
            continue;
        }

        MigrationAppEntry_t entry;
        entry.appId = appId;
        entry.accountId = ExtractCatalogJsonString(appObj, "account_id");
        if (entry.accountId.empty())
        {
            uint64_t acctNum = ExtractJsonUint64(appObj, "account_id", 0);
            if (acctNum != 0) entry.accountId = std::to_string(acctNum);
        }
        entry.fileCount = static_cast<uint32_t>(ExtractJsonUint64(appObj, "file_count", ExtractJsonUint64(appObj, "fileCount", 0)));
        entry.totalBytes = ExtractJsonUint64(appObj, "total_bytes", ExtractJsonUint64(appObj, "totalSizeBytes", 0));
        entry.totalSizeBytes = entry.totalBytes;

        auto tIt = knownTitles.find(appId);
        if (tIt != knownTitles.end() && !tIt->second.empty())
        {
            entry.gameTitle = tIt->second;
        }
        else
        {
            entry.gameTitle = ResolveAppTitleFromManifests(appId);
        }
        if (entry.gameTitle.empty())
        {
            entry.gameTitle = "App " + std::to_string(appId);
        }

        sumFiles += entry.fileCount;
        sumBytes += entry.totalBytes;
        result.apps.push_back(std::move(entry));
    }

    if (result.totalApps == 0 && !result.apps.empty())
    {
        result.totalApps = static_cast<uint32_t>(result.apps.size());
    }
    if (result.totalFiles == 0 && sumFiles > 0)
    {
        result.totalFiles = sumFiles;
    }
    if (result.totalBytes == 0 && sumBytes > 0)
    {
        result.totalBytes = sumBytes;
    }
    result.appCount = result.totalApps;
    result.fileCount = result.totalFiles;
    result.totalSizeBytes = result.totalBytes;

    result.success = true;
    return result;
}

MigrationResult_t CCloudManager::MigrateProviderData(const std::string& srcProvider,
                                                     const std::string& dstProvider,
                                                     bool switchActiveProvider)
{
    MigrationResult_t result;
    result.srcProvider = srcProvider;
    result.dstProvider = dstProvider;
    result.sourceProvider = srcProvider;
    result.targetProvider = dstProvider;

    const std::string normSrc = (srcProvider.empty() || srcProvider == "local-only") ? "local" : srcProvider;
    const std::string normDst = (dstProvider.empty() || dstProvider == "local-only") ? "local" : dstProvider;
    if (normSrc == normDst)
    {
        result.error = "Source and destination providers must be different";
        return result;
    }

    const std::string out = RunCliCommand({"migrate", normSrc, normDst}, 120000);
    if (out.empty())
    {
        result.error = "Failed to execute cloud_redirect_cli migrate";
        return result;
    }

    std::string completeJson;
    std::string errorJson;
    {
        std::istringstream iss(out);
        std::string line;
        while (std::getline(iss, line))
        {
            if (line.find("\"type\":\"complete\"") != std::string::npos ||
                line.find("\"type\": \"complete\"") != std::string::npos)
            {
                completeJson = line;
            }
            else if (line.find("\"type\":\"error\"") != std::string::npos ||
                     line.find("\"type\": \"error\"") != std::string::npos)
            {
                errorJson = line;
            }
        }
    }

    if (completeJson.empty() && errorJson.empty())
    {
        std::string extracted = ExtractJsonSubstring(out);
        if (extracted.find("\"type\":\"error\"") != std::string::npos ||
            extracted.find("\"type\": \"error\"") != std::string::npos)
        {
            errorJson = extracted;
        }
        else if (extracted.find("\"migrated\"") != std::string::npos)
        {
            completeJson = extracted;
        }
    }

    if (completeJson.empty())
    {
        result.success = false;
        if (!errorJson.empty())
        {
            result.error = ExtractCatalogJsonString(errorJson, "message");
            if (result.error.empty()) result.error = ExtractCatalogJsonString(errorJson, "error");
        }
        if (result.error.empty())
        {
            result.error = "Migration did not return completion payload";
        }
        return result;
    }

    result.success = true;
    result.migrated = static_cast<uint32_t>(ExtractJsonUint64(completeJson, "migrated", 0));
    result.skipped = static_cast<uint32_t>(ExtractJsonUint64(completeJson, "skipped", 0));
    result.failed = static_cast<uint32_t>(ExtractJsonUint64(completeJson, "failed", 0));
    result.total = static_cast<uint32_t>(ExtractJsonUint64(completeJson, "total", result.migrated + result.skipped + result.failed));
    result.totalBytes = ExtractJsonUint64(completeJson, "total_bytes", 0);

    if (switchActiveProvider)
    {
        const std::string cfgProv = (dstProvider == "local") ? "local-only" : dstProvider;
        g_config.cloudSavesProvider = cfgProv;

        EngineConfig_t ecfg;
        ecfg.provider = cfgProv;
        ecfg.notificationsEnabled = g_config.cloudNotificationsEnabled.get();
        ecfg.statsSyncEnabled = g_config.cloudStatsSyncEnabled.get();
        ecfg.syncAchievements = g_config.cloudSyncAchievements.get();
        ecfg.syncPlaytime = g_config.cloudSyncPlaytime.get();
        CCloudRedirectBridge::Instance().SyncEngineConfig(ecfg);
    }

    InvalidateCloudCatalogCache();
    return result;
}

std::vector<OrphanAppEntry_t> CCloudManager::ScanOrphanedSaves(const std::string& provider, uint32_t accountId)
{
    std::unordered_map<AppId_t, OrphanAppEntry_t> orphanMap;
    const auto knownTitles = g_config.gameTitles.get();

    auto resolveTitle = [&](AppId_t appId, const std::string& existingTitle) -> std::string {
        if (!existingTitle.empty() && !IsPlaceholderAppTitle(existingTitle, appId))
        {
            return existingTitle;
        }
        auto tIt = knownTitles.find(appId);
        if (tIt != knownTitles.end() && !tIt->second.empty())
        {
            return tIt->second;
        }
        std::string resolved = ResolveAppTitleFromManifests(appId);
        if (!resolved.empty())
        {
            return resolved;
        }
        return "App " + std::to_string(appId);
    };

    // 1. Scan via ListCloudApps(provider, accountId)
    for (const auto& summary : ListCloudApps(provider, accountId))
    {
        if (summary.appId == 0 || summary.appId == kCloudCatalogAppId) continue;
        if (g_config.IsAppCloudRedirected(summary.appId)) continue;
        if (summary.fileCount == 0) continue;

        OrphanAppEntry_t entry;
        entry.appId = summary.appId;
        entry.gameTitle = resolveTitle(summary.appId, summary.gameTitle);
        entry.fileCount = summary.fileCount;
        entry.totalSizeBytes = summary.totalSizeBytes;
        entry.inRemoteCloud = summary.inRemoteCloud;
        entry.inLocalMirror = summary.inLocalMirror;
        entry.inLocalStorage = summary.inLocalMirror;
        entry.inGoldberg = false;
        entry.inGoldbergPrefix = false;
        orphanMap[summary.appId] = std::move(entry);
    }

    // 2. Scan local <storageDir>/<acctId>/* directly (catches local mirror orphans even when provider is remote)
    std::error_code ec;
    const fs::path storageRoot = GetStorageDir();
    std::vector<uint32_t> accounts = ResolveCandidateAccountIds(accountId);
    if (accountId == 0 && fs::is_directory(storageRoot, ec))
    {
        for (const auto& entry : fs::directory_iterator(storageRoot, ec))
        {
            if (!entry.is_directory(ec)) continue;
            std::string name = entry.path().filename().string();
            if (IsDigitsOnly(name))
            {
                uint32_t id = static_cast<uint32_t>(std::strtoul(name.c_str(), nullptr, 10));
                if (id != 0 && std::find(accounts.begin(), accounts.end(), id) == accounts.end())
                {
                    accounts.push_back(id);
                }
            }
        }
    }

    if (fs::is_directory(storageRoot, ec))
    {
        for (uint32_t acct : accounts)
        {
            const fs::path acctDir = storageRoot / std::to_string(acct);
            if (!fs::is_directory(acctDir, ec)) continue;
            for (const auto& appEntry : fs::directory_iterator(acctDir, ec))
            {
                if (!appEntry.is_directory(ec)) continue;
                const std::string appName = appEntry.path().filename().string();
                if (!IsDigitsOnly(appName)) continue;

                AppId_t rawId = static_cast<AppId_t>(std::strtoul(appName.c_str(), nullptr, 10));
                if (rawId == 0 || rawId == kCloudCatalogAppId) continue;

                AppId_t parentId = GetParentAppIdForDepot(rawId);
                AppId_t appId = (parentId != 0 && parentId != kCloudCatalogAppId) ? parentId : rawId;
                if (g_config.IsAppCloudRedirected(appId) || g_config.IsAppCloudRedirected(rawId)) continue;

                uint32_t fCount = 0;
                uint64_t fBytes = 0;
                for (auto it = fs::recursive_directory_iterator(appEntry.path(), fs::directory_options::skip_permission_denied, ec);
                     !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
                {
                    std::error_code fEc;
                    if (!it->is_regular_file(fEc) || fEc) continue;
                    fs::path rel = fs::relative(it->path(), appEntry.path(), fEc);
                    if (fEc || rel.empty()) continue;
                    std::string relStr = rel.generic_string();
                    if (rel.has_parent_path() && IsHexHashSegment(it->path().filename().string()))
                    {
                        relStr = rel.parent_path().generic_string();
                    }
                    if (IsInternalMetadataRelPath(relStr)) continue;
                    ++fCount;
                    fBytes += static_cast<uint64_t>(it->file_size(fEc));
                }

                if (fCount == 0) continue;

                auto& item = orphanMap[appId];
                if (item.appId == 0)
                {
                    item.appId = appId;
                    item.gameTitle = resolveTitle(appId, "");
                    item.fileCount = fCount;
                    item.totalSizeBytes = fBytes;
                }
                else if (!item.inLocalMirror)
                {
                    item.fileCount = std::max(item.fileCount, fCount);
                    item.totalSizeBytes = std::max(item.totalSizeBytes, fBytes);
                }
                item.inLocalMirror = true;
                item.inLocalStorage = true;
            }
        }
    }

    // 3. Scan Goldberg/GSE save directories
    auto scanGoldbergAppDir = [&](AppId_t appId, const fs::path& remoteDir) {
        if (appId == 0 || appId == kCloudCatalogAppId) return;
        if (g_config.IsAppCloudRedirected(appId)) return;
        std::error_code gEc;
        if (!fs::is_directory(remoteDir, gEc)) return;

        uint32_t fCount = 0;
        uint64_t fBytes = 0;
        for (auto it = fs::recursive_directory_iterator(remoteDir, fs::directory_options::skip_permission_denied, gEc);
             !gEc && it != fs::recursive_directory_iterator(); it.increment(gEc))
        {
            std::error_code fEc;
            if (!it->is_regular_file(fEc) || fEc) continue;
            fs::path rel = fs::relative(it->path(), remoteDir, fEc);
            if (fEc || rel.empty()) continue;
            if (IsInternalMetadataRelPath(rel.generic_string())) continue;
            ++fCount;
            fBytes += static_cast<uint64_t>(it->file_size(fEc));
        }
        if (fCount == 0) return;

        auto& item = orphanMap[appId];
        if (item.appId == 0)
        {
            item.appId = appId;
            item.gameTitle = resolveTitle(appId, "");
            item.fileCount = fCount;
            item.totalSizeBytes = fBytes;
        }
        else if (!item.inGoldberg && !item.inLocalMirror)
        {
            item.fileCount = std::max(item.fileCount, fCount);
            item.totalSizeBytes = std::max(item.totalSizeBytes, fBytes);
        }
        item.inGoldberg = true;
        item.inGoldbergPrefix = true;
        item.inLocalMirror = true;
        item.inLocalStorage = true;
    };

    static const char* kEmuFolders[] = {"GSE Saves", "Goldberg SteamEmu Saves"};
    static const char* kAppDataCats[] = {"Roaming", "Local", "LocalLow"};

    const fs::path steamRoot = GetSteamRoot();
    if (!steamRoot.empty() && (m_storageDir.empty() || !m_steamRoot.empty()))
    {
        const fs::path compatRoot = steamRoot / "steamapps" / "compatdata";
        if (fs::is_directory(compatRoot, ec))
        {
            for (const auto& cEntry : fs::directory_iterator(compatRoot, fs::directory_options::skip_permission_denied, ec))
            {
                if (!cEntry.is_directory(ec)) continue;
                std::string appIdStr = cEntry.path().filename().string();
                if (!IsDigitsOnly(appIdStr)) continue;
                AppId_t appId = static_cast<AppId_t>(std::strtoul(appIdStr.c_str(), nullptr, 10));
                if (appId == 0 || appId == kCloudCatalogAppId || g_config.IsAppCloudRedirected(appId)) continue;

                const fs::path usersDir = cEntry.path() / "pfx" / "drive_c" / "users";
                if (!fs::is_directory(usersDir, ec)) continue;
                for (const auto& uEntry : fs::directory_iterator(usersDir, fs::directory_options::skip_permission_denied, ec))
                {
                    if (!uEntry.is_directory(ec)) continue;
                    for (const char* cat : kAppDataCats)
                    {
                        for (const char* emu : kEmuFolders)
                        {
                            scanGoldbergAppDir(appId, uEntry.path() / "AppData" / cat / emu / appIdStr / "remote");
                        }
                    }
                }
            }
        }
    }

    std::vector<OrphanAppEntry_t> results;
    results.reserve(orphanMap.size());
    for (auto& [_, entry] : orphanMap)
    {
        if (entry.fileCount > 0 && !g_config.IsAppCloudRedirected(entry.appId))
        {
            results.push_back(std::move(entry));
        }
    }

    std::sort(results.begin(), results.end(), [](const OrphanAppEntry_t& a, const OrphanAppEntry_t& b) {
        if (!a.gameTitle.empty() && !b.gameTitle.empty() && a.gameTitle != b.gameTitle)
        {
            return a.gameTitle < b.gameTitle;
        }
        return a.appId < b.appId;
    });

    return results;
}

GcResult_t CCloudManager::GarbageCollectAppBlobs(AppId_t appId, const std::string& provider, uint32_t accountId)
{
    GcResult_t result;
    result.appId = appId;
    if (appId == 0 || appId == kCloudCatalogAppId)
    {
        result.error = "Invalid app ID";
        return result;
    }

    uint32_t acctId = accountId;
    if (acctId == 0)
    {
        const auto accounts = ResolveCandidateAccountIds(0);
        if (!accounts.empty()) acctId = accounts.front();
    }

    const std::string normProv = (provider.empty() || provider == "local-only") ? "local" : provider;
    if (normProv == "local")
    {
        result.success = true;
        return result;
    }

    const fs::path storageRoot = GetStorageDir();
    const std::string out = RunCliCommand(
        {"gc-blobs", normProv, std::to_string(acctId), std::to_string(appId), storageRoot.string()},
        30000);

    if (out.empty())
    {
        result.error = "Failed to execute cloud_redirect_cli gc-blobs";
        return result;
    }

    std::string completeJson;
    std::string errorJson;
    {
        std::istringstream iss(out);
        std::string line;
        while (std::getline(iss, line))
        {
            if (line.find("\"type\":\"complete\"") != std::string::npos ||
                line.find("\"type\": \"complete\"") != std::string::npos)
            {
                completeJson = line;
            }
            else if (line.find("\"type\":\"error\"") != std::string::npos ||
                     line.find("\"type\": \"error\"") != std::string::npos)
            {
                errorJson = line;
            }
        }
    }

    if (completeJson.empty() && errorJson.empty())
    {
        std::string extracted = ExtractJsonSubstring(out);
        if (extracted.find("\"type\":\"error\"") != std::string::npos ||
            extracted.find("\"type\": \"error\"") != std::string::npos)
        {
            errorJson = extracted;
        }
        else if (extracted.find("\"deleted\"") != std::string::npos)
        {
            completeJson = extracted;
        }
    }

    if (completeJson.empty())
    {
        result.success = false;
        if (!errorJson.empty())
        {
            result.error = ExtractCatalogJsonString(errorJson, "message");
            if (result.error.empty()) result.error = ExtractCatalogJsonString(errorJson, "error");
        }
        if (result.error.empty())
        {
            result.error = "gc-blobs did not return completion payload";
        }
        return result;
    }

    result.success = true;
    result.deleted = static_cast<uint32_t>(ExtractJsonUint64(completeJson, "deleted", 0));
    result.kept = static_cast<uint32_t>(ExtractJsonUint64(completeJson, "kept", 0));
    result.failed = static_cast<uint32_t>(ExtractJsonUint64(completeJson, "failed", 0));
    result.blobsDeleted = result.deleted;
    result.totalBlobs = result.deleted + result.kept + result.failed;
    return result;
}

GcResult_t CCloudManager::CleanOrphanedApp(AppId_t appId,
                                           const std::string& provider,
                                           bool deleteLocalFiles,
                                           uint32_t accountId)
{
    GcResult_t result = GarbageCollectAppBlobs(appId, provider, accountId);
    if (appId == 0 || appId == kCloudCatalogAppId)
    {
        return result;
    }

    if (deleteLocalFiles)
    {
        if (g_config.IsAppCloudRedirected(appId))
        {
            if (result.error.empty())
            {
                result.error = "Refusing to delete local mirror for actively redirected app";
            }
            result.localFilesDeleted = false;
            result.localDeleted = false;
            return result;
        }

        std::error_code ec;
        const fs::path storageRoot = GetStorageDir();
        std::vector<uint32_t> accounts = ResolveCandidateAccountIds(accountId);
        if (accountId == 0 && fs::is_directory(storageRoot, ec))
        {
            for (const auto& entry : fs::directory_iterator(storageRoot, ec))
            {
                if (!entry.is_directory(ec)) continue;
                std::string name = entry.path().filename().string();
                if (IsDigitsOnly(name))
                {
                    uint32_t id = static_cast<uint32_t>(std::strtoul(name.c_str(), nullptr, 10));
                    if (id != 0 && std::find(accounts.begin(), accounts.end(), id) == accounts.end())
                    {
                        accounts.push_back(id);
                    }
                }
            }
        }

        bool rmSuccess = true;
        for (uint32_t acct : accounts)
        {
            const fs::path appDir = storageRoot / std::to_string(acct) / std::to_string(appId);
            if (fs::exists(appDir, ec))
            {
                fs::remove_all(appDir, ec);
                if (ec) rmSuccess = false;
            }
        }
        result.localFilesDeleted = rmSuccess;
        result.localDeleted = rmSuccess;
        InvalidateCloudCatalogCache();
    }

    return result;
}

namespace
{
    uint64_t ParseBackupTimestamp(const fs::path& filePath)
    {
        const std::string fn = filePath.filename().string();
        if (fn.ends_with(".tar.gz") && fn.size() > 7)
        {
            const std::string stem = fn.substr(0, fn.size() - 7);
            size_t digitEnd = stem.size();
            size_t digitStart = digitEnd;
            while (digitStart > 0 && std::isdigit(static_cast<unsigned char>(stem[digitStart - 1])))
            {
                --digitStart;
            }
            if (digitStart < digitEnd)
            {
                try
                {
                    uint64_t parsed = std::stoull(stem.substr(digitStart, digitEnd - digitStart));
                    if (parsed > 0) return parsed;
                }
                catch (...) {}
            }
        }

        std::error_code ec;
        auto ftime = fs::last_write_time(filePath, ec);
        if (!ec)
        {
            auto sctp = std::chrono::clock_cast<std::chrono::system_clock>(ftime);
            return static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::seconds>(sctp.time_since_epoch()).count());
        }
        return 0;
    }
} // namespace

std::vector<GlobalBackupEntry_t> CCloudManager::ListAllBackups()
{
    std::vector<GlobalBackupEntry_t> results;
    const fs::path backupRoot = GetBackupDir();
    std::error_code ec;
    if (!fs::exists(backupRoot, ec) || !fs::is_directory(backupRoot, ec))
    {
        return results;
    }

    const auto knownTitles = g_config.gameTitles.get();
    for (const auto& appDirEntry : fs::directory_iterator(backupRoot, ec))
    {
        if (!appDirEntry.is_directory(ec)) continue;
        const std::string appIdStr = appDirEntry.path().filename().string();
        if (!IsDigitsOnly(appIdStr)) continue;

        const AppId_t appId = static_cast<AppId_t>(std::strtoul(appIdStr.c_str(), nullptr, 10));
        if (appId == 0) continue;

        std::string title;
        auto tIt = knownTitles.find(appId);
        if (tIt != knownTitles.end() && !tIt->second.empty())
        {
            title = tIt->second;
        }
        else
        {
            title = ResolveAppTitleFromManifests(appId);
        }
        if (title.empty())
        {
            title = "App " + std::to_string(appId);
        }

        std::error_code fEc;
        for (const auto& fileEntry : fs::directory_iterator(appDirEntry.path(), fEc))
        {
            if (!fileEntry.is_regular_file(fEc)) continue;
            const std::string fn = fileEntry.path().filename().string();
            if (!fn.ends_with(".tar.gz")) continue;

            GlobalBackupEntry_t b;
            b.appId = appId;
            b.gameTitle = title;
            b.fileName = fn;
            b.fullPath = fileEntry.path().string();
            b.sizeBytes = static_cast<uint64_t>(fileEntry.file_size(fEc));
            b.timestamp = ParseBackupTimestamp(fileEntry.path());
            results.push_back(std::move(b));
        }
    }

    std::sort(results.begin(), results.end(), [](const GlobalBackupEntry_t& a, const GlobalBackupEntry_t& b) {
        if (a.timestamp != b.timestamp) return a.timestamp > b.timestamp;
        if (a.fileName != b.fileName) return a.fileName > b.fileName;
        return a.appId < b.appId;
    });

    return results;
}

bool CCloudManager::DeleteBackup(AppId_t appId, const std::string& fileName)
{
    if (appId == 0 || fileName.empty())
    {
        return false;
    }
    if (fileName.find('/') != std::string::npos ||
        fileName.find('\\') != std::string::npos ||
        fileName.find("..") != std::string::npos ||
        !fileName.ends_with(".tar.gz"))
    {
        return false;
    }

    const fs::path target = GetBackupDir() / std::to_string(appId) / fileName;
    std::error_code ec;
    if (!fs::exists(target, ec) || !fs::is_regular_file(target, ec))
    {
        return false;
    }
    return fs::remove(target, ec) && !ec;
}

uint32_t CCloudManager::PruneBackupsForApp(AppId_t appId, uint32_t keepCount)
{
    if (appId == 0 || keepCount == 0)
    {
        return 0;
    }

    const fs::path appBackupDir = GetBackupDir() / std::to_string(appId);
    std::error_code ec;
    if (!fs::exists(appBackupDir, ec) || !fs::is_directory(appBackupDir, ec))
    {
        return 0;
    }

    struct BackupCandidate
    {
        std::string fileName;
        uint64_t timestamp{0};
    };
    std::vector<BackupCandidate> candidates;

    for (const auto& entry : fs::directory_iterator(appBackupDir, ec))
    {
        if (!entry.is_regular_file(ec)) continue;
        const std::string fn = entry.path().filename().string();
        if (!fn.ends_with(".tar.gz")) continue;
        candidates.push_back(BackupCandidate{fn, ParseBackupTimestamp(entry.path())});
    }

    if (candidates.size() <= keepCount)
    {
        return 0;
    }

    std::sort(candidates.begin(), candidates.end(), [](const BackupCandidate& a, const BackupCandidate& b) {
        if (a.timestamp != b.timestamp) return a.timestamp > b.timestamp;
        return a.fileName > b.fileName;
    });

    uint32_t deletedCount = 0;
    for (size_t i = keepCount; i < candidates.size(); ++i)
    {
        if (DeleteBackup(appId, candidates[i].fileName))
        {
            ++deletedCount;
        }
    }
    return deletedCount;
}

} // namespace Navi::CloudRedirect



