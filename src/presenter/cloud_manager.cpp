#include "cloud_manager.hpp"
#include "cloudredirect.hpp"
#include "paths.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
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
    return CCloudRedirectBridge::Instance().GetPaths().storageDir;
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

    return totalBytes;
}

} // namespace Navi::CloudRedirect

