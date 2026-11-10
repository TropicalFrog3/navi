/**
 * @file  process_lock.cpp
 * @brief Filesystem process lock ensuring idle Steam during cloud ops
 */
#include "process_lock.hpp"
#include "paths.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <system_error>
#include <unordered_set>
#include <vector>

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace Sync
{
    namespace
    {
        std::mutex s_hookMutex;
        std::unordered_set<uint32_t> s_hookedRunningApps;

        std::string readProcFile(const fs::path& filePath)
        {
            std::ifstream ifs(filePath, std::ios::binary);
            if (!ifs.is_open()) return "";
            std::string result;
            char buf[4096];
            while (ifs.read(buf, sizeof(buf)) || ifs.gcount() > 0)
            {
                result.append(buf, static_cast<size_t>(ifs.gcount()));
                if (result.size() > 1024 * 1024) break;
            }
            return result;
        }

        std::vector<std::string> splitNullSeparated(const std::string& data)
        {
            std::vector<std::string> tokens;
            size_t start = 0;
            for (size_t i = 0; i < data.size(); ++i)
            {
                if (data[i] == '\0')
                {
                    if (i > start)
                    {
                        tokens.emplace_back(data.data() + start, i - start);
                    }
                    start = i + 1;
                }
            }
            if (start < data.size())
            {
                tokens.emplace_back(data.data() + start, data.size() - start);
            }
            return tokens;
        }
    } // anonymous namespace

    void GameProcessLock::setAppRunningFromHook(uint32_t appId, bool running)
    {
        if (appId == 0) return;
        std::lock_guard<std::mutex> lock(s_hookMutex);
        if (running)
        {
            s_hookedRunningApps.insert(appId);
        }
        else
        {
            s_hookedRunningApps.erase(appId);
        }
    }

    void GameProcessLock::clearAllHookedRunningApps()
    {
        std::lock_guard<std::mutex> lock(s_hookMutex);
        s_hookedRunningApps.clear();
    }

    bool GameProcessLock::isAppRunning(
        uint32_t targetAppId,
        const std::string& procRootOverride,
        const std::string& steamRootOverride)
    {
        if (targetAppId == 0) return false;

        // 1. Check hook tracking
        {
            std::lock_guard<std::mutex> lock(s_hookMutex);
            if (s_hookedRunningApps.contains(targetAppId))
            {
                return true;
            }
        }

        // 2. Scan proc directories
        std::error_code ec;
        fs::path procRoot(procRootOverride);
        if (!fs::exists(procRoot, ec) || !fs::is_directory(procRoot, ec))
        {
            return false;
        }

        const uid_t myEuid = geteuid();
        const pid_t myPid = getpid();
        const bool isRealProc = (procRootOverride == "/proc");

        const std::string targetAppIdStr = std::to_string(targetAppId);
        const std::string envSteamAppId = "SteamAppId=" + targetAppIdStr;
        const std::string envSteamGameId = "SteamGameId=" + targetAppIdStr;
        const std::string cmdAppIdExact = "AppId=" + targetAppIdStr;
        const std::string compatPfx = "compatdata/" + targetAppIdStr + "/pfx";
        const std::string remotePfx = "/" + targetAppIdStr + "/remote/";

        std::string steamRoot = !steamRootOverride.empty() ? steamRootOverride : Paths::findSteamRoot();

        for (const auto& entry : fs::directory_iterator(procRoot, fs::directory_options::skip_permission_denied, ec))
        {
            if (ec) break;
            if (!entry.is_directory(ec)) continue;

            const std::string dirName = entry.path().filename().string();
            if (dirName.empty() || !std::all_of(dirName.begin(), dirName.end(), ::isdigit))
            {
                continue;
            }

            pid_t pid = 0;
            try { pid = static_cast<pid_t>(std::stoul(dirName)); } catch (...) { continue; }
            if (pid <= 0) continue;
            if (isRealProc && pid == myPid) continue;

            struct stat st;
            if (lstat(entry.path().c_str(), &st) != 0) continue;
            if (st.st_uid != myEuid) continue;

            // Check <pid>/environ
            const std::string envData = readProcFile(entry.path() / "environ");
            if (!envData.empty())
            {
                const auto envTokens = splitNullSeparated(envData);
                for (const auto& tok : envTokens)
                {
                    if (tok == envSteamAppId || tok == envSteamGameId)
                    {
                        return true;
                    }
                }
            }

            // Check <pid>/cmdline
            const std::string cmdData = readProcFile(entry.path() / "cmdline");
            if (!cmdData.empty())
            {
                const auto cmdTokens = splitNullSeparated(cmdData);
                for (const auto& tok : cmdTokens)
                {
                    if (tok == cmdAppIdExact)
                    {
                        return true;
                    }
                    size_t pos = tok.find("AppId=" + targetAppIdStr);
                    if (pos != std::string::npos)
                    {
                        size_t endPos = pos + ("AppId=" + targetAppIdStr).size();
                        if (endPos == tok.size() || !std::isdigit(static_cast<unsigned char>(tok[endPos])))
                        {
                            return true;
                        }
                    }
                }
            }

            // Check <pid>/fd/* symlinks if steamRoot is non-empty
            if (!steamRoot.empty())
            {
                fs::path fdDir = entry.path() / "fd";
                if (fs::exists(fdDir, ec) && fs::is_directory(fdDir, ec))
                {
                    for (const auto& fdEntry : fs::directory_iterator(fdDir, fs::directory_options::skip_permission_denied, ec))
                    {
                        if (ec) break;
                        std::error_code symEc;
                        fs::path linkTarget = fs::read_symlink(fdEntry.path(), symEc);
                        if (!symEc)
                        {
                            std::string targetStr = linkTarget.string();
                            if (targetStr.find(compatPfx) != std::string::npos || targetStr.find(remotePfx) != std::string::npos)
                            {
                                return true;
                            }
                        }
                    }
                }
            }
        }

        return false;
    }

    std::unordered_set<uint32_t> GameProcessLock::getAllRunningAppIds(
        const std::string& procRootOverride)
    {
        std::unordered_set<uint32_t> running;
        {
            std::lock_guard<std::mutex> lock(s_hookMutex);
            running.insert(s_hookedRunningApps.begin(), s_hookedRunningApps.end());
        }

        std::error_code ec;
        fs::path procRoot(procRootOverride);
        if (!fs::exists(procRoot, ec) || !fs::is_directory(procRoot, ec))
        {
            return running;
        }

        const uid_t myEuid = geteuid();
        const pid_t myPid = getpid();
        const bool isRealProc = (procRootOverride == "/proc");

        for (const auto& entry : fs::directory_iterator(procRoot, fs::directory_options::skip_permission_denied, ec))
        {
            if (ec) break;
            if (!entry.is_directory(ec)) continue;

            const std::string dirName = entry.path().filename().string();
            if (dirName.empty() || !std::all_of(dirName.begin(), dirName.end(), ::isdigit))
            {
                continue;
            }

            pid_t pid = 0;
            try { pid = static_cast<pid_t>(std::stoul(dirName)); } catch (...) { continue; }
            if (pid <= 0) continue;
            if (isRealProc && pid == myPid) continue;

            struct stat st;
            if (lstat(entry.path().c_str(), &st) != 0) continue;
            if (st.st_uid != myEuid) continue;

            const std::string envData = readProcFile(entry.path() / "environ");
            if (!envData.empty())
            {
                const auto envTokens = splitNullSeparated(envData);
                for (const auto& tok : envTokens)
                {
                    if (tok.rfind("SteamAppId=", 0) == 0)
                    {
                        try {
                            uint32_t parsed = std::stoul(tok.substr(11));
                            if (parsed > 0) running.insert(parsed);
                        } catch (...) {}
                    }
                    else if (tok.rfind("SteamGameId=", 0) == 0)
                    {
                        try {
                            uint32_t parsed = std::stoul(tok.substr(12));
                            if (parsed > 0) running.insert(parsed);
                        } catch (...) {}
                    }
                }
            }

            const std::string cmdData = readProcFile(entry.path() / "cmdline");
            if (!cmdData.empty())
            {
                const auto cmdTokens = splitNullSeparated(cmdData);
                for (const auto& tok : cmdTokens)
                {
                    size_t pos = tok.find("AppId=");
                    if (pos != std::string::npos)
                    {
                        size_t start = pos + 6;
                        size_t end = start;
                        while (end < tok.size() && std::isdigit(static_cast<unsigned char>(tok[end])))
                        {
                            end++;
                        }
                        if (end > start)
                        {
                            try {
                                uint32_t parsed = std::stoul(tok.substr(start, end - start));
                                if (parsed > 0) running.insert(parsed);
                            } catch (...) {}
                        }
                    }
                }
            }
        }

        return running;
    }
} // namespace Sync
