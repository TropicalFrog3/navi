/**
 * @file  autocrack_manager.cpp
 * @brief Automated SteamStub DRM unpacker and payload manager
 */
#include "autocrack_manager.hpp"
#include "apps.hpp"
#include "config.hpp"
#include "log.hpp"
#include "paths.hpp"
#include "utils.hpp"
#include "IClientAppManager.hpp"
#include "CSteamEngine.hpp"
#include "CUser.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <vector>

#include <sys/file.h>
#include <sys/poll.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace AutoCrack {

struct Job {
    uint32_t appId;
    Origin origin;
    bool force;
    bool isRestore;
    int retryCount = 0;
};

static std::mutex g_queueMutex;
static std::condition_variable g_cv;
static std::deque<Job> g_queue;
static std::unordered_map<uint32_t, Status> g_statusMap;
static std::atomic<bool> g_running{false};
static std::thread g_workerThread;
static std::atomic<pid_t> g_childPid{0};

static std::string jsonEscape(const std::string& s) {
    std::ostringstream o;
    for (char c : s) {
        if (c == '"') o << "\\\"";
        else if (c == '\\') o << "\\\\";
        else if (c == '\b') o << "\\b";
        else if (c == '\f') o << "\\f";
        else if (c == '\n') o << "\\n";
        else if (c == '\r') o << "\\r";
        else if (c == '\t') o << "\\t";
        else if (static_cast<unsigned char>(c) <= 0x1f) {
            o << "\\u00" << std::hex << (int)(unsigned char)c;
        } else {
            o << c;
        }
    }
    return o.str();
}

std::filesystem::path getAutoCrackDir() {
    const char* home = getenv("HOME");
    if (!home) return "/tmp/navi/autocrack";
    return std::filesystem::path(home) / ".local" / "share" / "navi" / "autocrack";
}

std::filesystem::path getManifestPath(uint32_t appId) {
    return getAutoCrackDir() / "manifests" / (std::to_string(appId) + ".json");
}

static std::filesystem::path findCliBinary() {
    auto base = getAutoCrackDir() / "SteamAutoCrack.CLI";
    std::error_code ec;
    if (std::filesystem::exists(base, ec)) {
        return base;
    }
    std::filesystem::path repoCli = "pkg/autocrack/SteamAutoCrack.CLI";
    if (std::filesystem::exists(repoCli, ec)) {
        return repoCli;
    }
    return base;
}

static std::filesystem::path findGoldbergDir() {
    auto base = getAutoCrackDir() / "Goldberg";
    std::error_code ec;
    if (std::filesystem::exists(base, ec)) {
        return base;
    }
    std::filesystem::path pkgGb = "pkg/autocrack/Goldberg";
    if (std::filesystem::exists(pkgGb, ec)) {
        return pkgGb;
    }
    std::filesystem::path repoGb = "third_party/SteamAutoCrack/SteamAutoCrack/Goldberg";
    if (std::filesystem::exists(repoGb, ec)) {
        return repoGb;
    }
    return base;
}

void setStatus(uint32_t appId, State state, const std::string& stage, const std::string& msg) {
    std::lock_guard<std::mutex> lock(g_queueMutex);
    Status& s = g_statusMap[appId];
    s.appId = appId;
    s.state = state;
    s.stage = stage;
    s.message = msg;
    s.updatedAt = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

bool isDriftDetected(uint32_t appId) {
    auto mfPath = getManifestPath(appId);
    std::error_code ec;
    if (!std::filesystem::exists(mfPath, ec)) {
        return false;
    }

    std::ifstream f(mfPath);
    if (!f.is_open()) return false;
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();

    // Parse "patched": { "path": "hash", ... }
    size_t patchedPos = content.find("\"patched\"");
    if (patchedPos == std::string::npos) return false;
    size_t openBrace = content.find('{', patchedPos);
    if (openBrace == std::string::npos) return false;
    size_t closeBrace = content.find('}', openBrace);
    if (closeBrace == std::string::npos) return false;

    std::string block = content.substr(openBrace + 1, closeBrace - openBrace - 1);
    std::istringstream ss(block);
    std::string line;
    while (std::getline(ss, line)) {
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string p = line.substr(0, colon);
        std::string h = line.substr(colon + 1);

        auto clean = [](std::string& s) {
            size_t q1 = s.find('"');
            if (q1 == std::string::npos) { s.clear(); return; }
            size_t q2 = s.rfind('"');
            if (q2 == std::string::npos || q2 <= q1) { s.clear(); return; }
            s = s.substr(q1 + 1, q2 - q1 - 1);
        };
        clean(p);
        clean(h);
        if (p.empty() || h.empty()) continue;

        if (std::filesystem::exists(p, ec)) {
            try {
                std::string currentHash = Utils::getFileSHA256(p.c_str());
                if (currentHash != h) {
                    LOG_INFO("[AutoCrack:%u] Drift detected on %s: recorded=%s current=%s\n",
                             appId, p.c_str(), h.c_str(), currentHash.c_str());
                    return true;
                }
            } catch (...) {
                return true;
            }
        } else {
            // Patched file was deleted or replaced
            return true;
        }
    }
    return false;
}

Status inspectOrGetStatus(uint32_t appId) {
    std::lock_guard<std::mutex> lock(g_queueMutex);
    auto it = g_statusMap.find(appId);
    if (it != g_statusMap.end()) {
        if (it->second.state == State::Running || it->second.state == State::Queued) {
            return it->second;
        }
    }

    if (Apps::isAutoCrackInstalled(appId)) {
        if (isDriftDetected(appId)) {
            Status s;
            s.appId = appId;
            s.state = State::NeedsReapply;
            s.stage = "drift";
            s.message = "Game updated by Steam — re-apply AutoCrack?";
            s.updatedAt = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            g_statusMap[appId] = s;
            return s;
        } else if (it != g_statusMap.end() && it->second.state == State::NeedsReapply) {
            // Drift no longer detected
            it->second.state = State::Succeeded;
            it->second.message = "Applied";
            return it->second;
        }
    }

    if (it != g_statusMap.end()) {
        return it->second;
    }

    Status s;
    s.appId = appId;
    s.state = Apps::isAutoCrackInstalled(appId) ? State::Succeeded : State::Idle;
    s.stage = "";
    s.message = s.state == State::Succeeded ? "Applied" : "Not applied";
    s.updatedAt = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return s;
}

bool performRestore(uint32_t appId, std::string& outError) {
    auto mfPath = getManifestPath(appId);
    std::error_code ec;
    LOG_INFO("[AutoCrack:%u] Performing manifest restore\n", appId);

    if (std::filesystem::exists(mfPath, ec)) {
        std::ifstream f(mfPath);
        std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        f.close();

        // 1. Rename backups back
        // "backups": [ {"bak": "...", "orig": "..."}, ... ]
        size_t bPos = content.find("\"backups\"");
        if (bPos != std::string::npos) {
            size_t openBracket = content.find('[', bPos);
            size_t closeBracket = content.find(']', openBracket);
            if (openBracket != std::string::npos && closeBracket != std::string::npos) {
                std::string list = content.substr(openBracket + 1, closeBracket - openBracket - 1);
                size_t pos = 0;
                while ((pos = list.find('{', pos)) != std::string::npos) {
                    size_t endObj = list.find('}', pos);
                    if (endObj == std::string::npos) break;
                    std::string obj = list.substr(pos, endObj - pos + 1);
                    pos = endObj + 1;

                    auto extract = [&](const std::string& k) {
                        size_t kp = obj.find("\"" + k + "\"");
                        if (kp == std::string::npos) return std::string();
                        size_t c = obj.find(':', kp);
                        if (c == std::string::npos) return std::string();
                        size_t q1 = obj.find('"', c);
                        if (q1 == std::string::npos) return std::string();
                        size_t q2 = obj.find('"', q1 + 1);
                        if (q2 == std::string::npos) return std::string();
                        return obj.substr(q1 + 1, q2 - q1 - 1);
                    };

                    std::string bak = extract("bak");
                    std::string orig = extract("orig");
                    if (!bak.empty() && !orig.empty()) {
                        if (std::filesystem::exists(bak, ec)) {
                            std::filesystem::remove(orig, ec);
                            std::filesystem::rename(bak, orig, ec);
                            LOG_DEBUG("[AutoCrack:%u] Restored %s -> %s\n", appId, bak.c_str(), orig.c_str());
                        }
                    }
                }
            }
        }

        // 2. Delete created files/dirs
        // "created": [ "path", ... ]
        size_t cPos = content.find("\"created\"");
        if (cPos != std::string::npos) {
            size_t openBracket = content.find('[', cPos);
            size_t closeBracket = content.find(']', openBracket);
            if (openBracket != std::string::npos && closeBracket != std::string::npos) {
                std::string list = content.substr(openBracket + 1, closeBracket - openBracket - 1);
                std::istringstream ss(list);
                std::string line;
                while (std::getline(ss, line, ',')) {
                    size_t q1 = line.find('"');
                    size_t q2 = line.rfind('"');
                    if (q1 != std::string::npos && q2 != std::string::npos && q2 > q1) {
                        std::string path = line.substr(q1 + 1, q2 - q1 - 1);
                        if (!path.empty() && std::filesystem::exists(path, ec)) {
                            std::filesystem::remove_all(path, ec);
                            LOG_DEBUG("[AutoCrack:%u] Removed created path %s\n", appId, path.c_str());
                        }
                    }
                }
            }
        }

        std::filesystem::remove(mfPath, ec);
    } else {
        // Fallback: search game install dir and rename *.bak
        auto gameDir = Apps::resolveInstallDir(appId);
        if (gameDir.empty() || !std::filesystem::exists(gameDir, ec)) {
            outError = "Game install directory not found for restore";
            return false;
        }
        for (auto& entry : std::filesystem::recursive_directory_iterator(gameDir,
                 std::filesystem::directory_options::skip_permission_denied, ec)) {
            if (entry.is_regular_file(ec) && entry.path().extension() == ".bak") {
                auto orig = entry.path().parent_path() / entry.path().stem();
                std::filesystem::remove(orig, ec);
                std::filesystem::rename(entry.path(), orig, ec);
            } else if (entry.is_directory(ec) && entry.path().filename() == "steam_settings") {
                std::filesystem::remove_all(entry.path(), ec);
            }
        }
    }

    Apps::setAutoCrackInstalled(appId, false);
    return true;
}

static void rotateLogs(const std::filesystem::path& logDir) {
    std::error_code ec;
    std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> files;
    for (auto& entry : std::filesystem::directory_iterator(logDir, ec)) {
        if (entry.is_regular_file(ec) && entry.path().extension() == ".log") {
            files.push_back({entry.last_write_time(ec), entry.path()});
        }
    }
    if (files.size() <= 20) return;
    std::sort(files.begin(), files.end(), [](auto& a, auto& b) {
        return a.first > b.first;
    });
    for (size_t i = 20; i < files.size(); ++i) {
        std::filesystem::remove(files[i].second, ec);
    }
}

static bool runJob(Job& job) {
    uint32_t appId = job.appId;
    LOG_INFO("[AutoCrack:%u] Starting job (force=%d, restore=%d)\n", appId, job.force, job.isRestore);

    if (job.isRestore) {
        setStatus(appId, State::Restoring, "restore", "Restoring original files...");
        std::string err;
        if (performRestore(appId, err)) {
            setStatus(appId, State::Idle, "", "Restored original files");
            LOG_NOTIFY("AutoCrack removed for %u", appId);
            return true;
        } else {
            setStatus(appId, State::Failed, "restore", err.empty() ? "Restore failed" : err);
            LOG_NOTIFYERROR("AutoCrack restore failed for %u", appId);
            return false;
        }
    }

    // 1. Validate
    if (Apps::isNonSteamApp(appId)) {
        setStatus(appId, State::Skipped, "", "Non-Steam app or excluded");
        return true;
    }
    if (!Apps::gameFilesExist(appId)) {
        setStatus(appId, State::Failed, "", "Game files do not exist");
        return false;
    }

    // 2. Resolve path
    auto gameDir = Apps::resolveInstallDir(appId);
    if (gameDir.empty()) {
        setStatus(appId, State::Failed, "", "Could not resolve game install directory");
        return false;
    }

    // Check write permissions
    if (access(gameDir.c_str(), W_OK) != 0) {
        setStatus(appId, State::Failed, "", "Game directory is read-only (Flatpak or permission issue)");
        return false;
    }

    // 3. Preflight skip & force-reapply rules
    std::error_code ec;
    bool hasSteamApi = false;
    std::vector<std::filesystem::path> steamApiDlls;
    std::vector<std::filesystem::path> existingBaks;

    for (auto& entry : std::filesystem::recursive_directory_iterator(gameDir,
             std::filesystem::directory_options::skip_permission_denied, ec)) {

        if (!entry.is_regular_file(ec)) continue;
        std::string fn = entry.path().filename().string();
        std::transform(fn.begin(), fn.end(), fn.begin(), [](unsigned char c){ return std::tolower(c); });

        if (fn == "steam_api.dll" || fn == "steam_api64.dll") {
            hasSteamApi = true;
            steamApiDlls.push_back(entry.path());
        }
        if (fn.ends_with(".dll.bak") || fn.ends_with(".exe.bak")) {
            existingBaks.push_back(entry.path());
        }
    }

    if (!hasSteamApi && existingBaks.empty()) {
        setStatus(appId, State::Skipped, "", "Linux-native build or no Steamworks DLL");
        LOG_INFO("[AutoCrack:%u] Skipped: no steam_api(64).dll found\n", appId);
        return true;
    }

    if (job.force) {
        // User confirmed re-apply after a game update:
        // Stale .bak files must be removed so updated originals become the new .bak
        for (auto& dll : steamApiDlls) {
            auto bak = dll;
            bak.replace_extension(dll.extension().string() + ".bak");
            if (std::filesystem::exists(bak, ec)) {
                std::filesystem::remove(bak, ec);
            }
            auto settings = dll.parent_path() / "steam_settings";
            if (std::filesystem::exists(settings, ec)) {
                std::filesystem::remove_all(settings, ec);
            }
        }
    } else if (!existingBaks.empty() && Apps::isAutoCrackInstalled(appId) && !isDriftDetected(appId)) {
        setStatus(appId, State::Skipped, "", "Already applied");
        return true;
    }

    // 4. Settle wait
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // 5. Workspace setup
    auto autocrackDir = getAutoCrackDir();
    std::filesystem::create_directories(autocrackDir / "manifests", ec);
    std::filesystem::create_directories(autocrackDir / "logs", ec);

    auto nowTs = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    auto jobDir = autocrackDir / "jobs" / (std::to_string(appId) + "-" + std::to_string(nowTs));
    std::filesystem::create_directories(jobDir, ec);

    // Symlink or copy SteamAppList.db if present
    auto masterDb = autocrackDir / "SteamAppList.db";
    if (std::filesystem::exists(masterDb, ec)) {
        std::filesystem::create_symlink(masterDb, jobDir / "SteamAppList.db", ec);
    }

    auto jobConfigPath = jobDir / "job.json";
    {
        std::ofstream jcf(jobConfigPath);
        jcf << "{\n"
            << "  \"EMUGameInfoConfigs\": {\n"
            << "    \"AppID\": \"" << appId << "\",\n"
            << "    \"GameInfoAPI\": \"" << (g_config.autoCrackGameInfoApi.get() == "web" ? "GeneratorSteamWeb" : (g_config.autoCrackGameInfoApi.get() == "offline" ? "GeneratorOffline" : "GeneratorSteamClient")) << "\",\n"
            << "    \"SteamWebAPIKey\": \"" << jsonEscape(g_config.autoCrackWebApiKey.get()) << "\"\n"
            << "  },\n"
            << "  \"EMUApplyConfigs\": {\n"
            << "    \"UseGoldbergExperimental\": " << (g_config.autoCrackExperimentalEmu.get() ? "true" : "false") << "\n"
            << "  },\n"
            << "  \"ProcessConfigs\": {\n"
            << "    \"GenerateEMUGameInfo\": true,\n"
            << "    \"GenerateEMUConfig\": true,\n"
            << "    \"Unpack\": " << (g_config.autoCrackUnpackSteamStub.get() ? "true" : "false") << ",\n"
            << "    \"ApplyEMU\": true,\n"
            << "    \"GenerateCrackOnly\": false,\n"
            << "    \"Restore\": false\n"
            << "  }\n"
            << "}\n";
    }

    auto cliPath = findCliBinary();
    if (!std::filesystem::exists(cliPath, ec)) {
        setStatus(appId, State::Failed, "", "SteamAutoCrack CLI not found (please build/install)");
        std::filesystem::remove_all(jobDir, ec);
        return false;
    }

    auto goldbergDir = findGoldbergDir();
    auto logFilePath = autocrackDir / "logs" / (std::to_string(appId) + "-" + std::to_string(nowTs) + ".log");

    // Write inflight manifest before run
    auto mfPath = getManifestPath(appId);
    {
        std::ofstream mf(mfPath);
        mf << "{\n  \"state\": \"inflight\",\n  \"appId\": " << appId << ",\n  \"gameDir\": \"" << jsonEscape(gameDir.string()) << "\"\n}\n";
    }

    // 6. Spawn process
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        setStatus(appId, State::Failed, "", "Failed to create IPC pipe");
        std::filesystem::remove(mfPath, ec);
        std::filesystem::remove_all(jobDir, ec);
        return false;
    }

    setStatus(appId, State::Running, "init", "Starting crack process...");

    pid_t pid = fork();
    if (pid == 0) {
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        // Scrub LD_PRELOAD and LD_AUDIT
        unsetenv("LD_PRELOAD");
        unsetenv("LD_AUDIT");
        setenv("DOTNET_CLI_TELEMETRY_OPTOUT", "1", 1);
        setenv("DOTNET_gcServer", "0", 1);

        std::string cliStr = cliPath.string();
        std::string gameDirStr = gameDir.string();
        std::string appIdStr = std::to_string(appId);
        std::string cfgStr = jobConfigPath.string();
        std::string tempStr = jobDir.string();
        std::string gbStr = goldbergDir.string();

        const char* args[] = {
            cliStr.c_str(),
            "crack",
            gameDirStr.c_str(),
            "--appid", appIdStr.c_str(),
            "--config", cfgStr.c_str(),
            "--temp", tempStr.c_str(),
            "--goldberg", gbStr.c_str(),
            "--json-events",
            nullptr
        };

        execv(args[0], const_cast<char**>(args));
        _exit(127);
    }

    g_childPid = pid;
    close(pipefd[1]);

    // 7. Monitor output
    std::ofstream logFile(logFilePath, std::ios::app);
    struct pollfd pfd{};
    pfd.fd = pipefd[0];
    pfd.events = POLLIN;

    uint32_t inactTimeoutSec = g_config.autoCrackInactivityTimeoutSec.get();
    if (inactTimeoutSec == 0) inactTimeoutSec = 120;
    auto lastOutputTime = std::chrono::steady_clock::now();
    auto jobStartTime = std::chrono::steady_clock::now();

    std::string lineBuffer;
    char buf[1024];
    bool processFinished = false;

    while (!processFinished) {
        auto now = std::chrono::steady_clock::now();
        auto inactSec = std::chrono::duration_cast<std::chrono::seconds>(now - lastOutputTime).count();
        auto totalSec = std::chrono::duration_cast<std::chrono::seconds>(now - jobStartTime).count();

        if (inactSec > inactTimeoutSec || totalSec > 1200) {
            LOG_ERROR("[AutoCrack:%u] Timeout reached (inact=%lld, total=%lld). Killing child %d\n",
                      appId, inactSec, totalSec, pid);
            kill(pid, SIGTERM);
            std::this_thread::sleep_for(std::chrono::seconds(2));
            kill(pid, SIGKILL);
            break;
        }

        int ret = poll(&pfd, 1, 1000);
        if (ret > 0 && (pfd.revents & POLLIN)) {
            ssize_t n = read(pipefd[0], buf, sizeof(buf) - 1);
            if (n <= 0) {
                processFinished = true;
                break;
            }
            buf[n] = '\0';
            logFile << buf;
            logFile.flush();
            lastOutputTime = std::chrono::steady_clock::now();

            lineBuffer.append(buf, n);
            size_t nl;
            while ((nl = lineBuffer.find('\n')) != std::string::npos) {
                std::string line = lineBuffer.substr(0, nl);
                lineBuffer.erase(0, nl + 1);
                if (!line.empty() && line.back() == '\r') line.pop_back();

                if (line.starts_with("{\"ev\":\"stage\"")) {
                    // Extract stage and state
                    auto extract = [&](const std::string& key) {
                        size_t kp = line.find("\"" + key + "\":\"");
                        if (kp == std::string::npos) return std::string();
                        size_t start = kp + key.size() + 4;
                        size_t end = line.find('"', start);
                        if (end == std::string::npos) return std::string();
                        return line.substr(start, end - start);
                    };
                    std::string stg = extract("stage");
                    std::string ste = extract("state");
                    setStatus(appId, State::Running, stg, "Processing " + stg + " (" + ste + ")...");
                    LOG_INFO("[AutoCrack:%u] stage=%s state=%s\n", appId, stg.c_str(), ste.c_str());
                }
            }
        } else if (ret > 0 && (pfd.revents & (POLLHUP | POLLERR))) {
            // Drain remaining
            ssize_t n;
            while ((n = read(pipefd[0], buf, sizeof(buf) - 1)) > 0) {
                buf[n] = '\0';
                logFile << buf;
            }
            processFinished = true;
            break;
        }
    }

    close(pipefd[0]);
    logFile.close();

    int wstatus = 0;
    waitpid(pid, &wstatus, 0);
    g_childPid = 0;

    int exitCode = WIFEXITED(wstatus) ? WEXITSTATUS(wstatus) : -1;
    LOG_INFO("[AutoCrack:%u] Process exited with code %d\n", appId, exitCode);

    // 8. Verify
    bool hasBak = false;
    bool hasSettingsAppId = false;
    std::vector<std::pair<std::string, std::string>> backups;
    std::vector<std::string> createdFiles;
    std::unordered_map<std::string, std::string> patchedHashes;

    for (auto& entry : std::filesystem::recursive_directory_iterator(gameDir,
             std::filesystem::directory_options::skip_permission_denied, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        std::string p = entry.path().string();
        if (p.ends_with(".dll.bak") || p.ends_with(".exe.bak")) {
            hasBak = true;
            std::string orig = p.substr(0, p.size() - 4);
            backups.push_back({p, orig});
            if (std::filesystem::exists(orig, ec)) {
                try {
                    patchedHashes[orig] = Utils::getFileSHA256(orig.c_str());
                } catch (...) {}
            }
        }
        if (entry.path().filename() == "steam_appid.txt" && entry.path().parent_path().filename() == "steam_settings") {
            std::ifstream idf(entry.path());
            std::string txtId;
            idf >> txtId;
            if (txtId == std::to_string(appId)) {
                hasSettingsAppId = true;
            }
            createdFiles.push_back(entry.path().parent_path().string());
        }
    }

    bool success = (exitCode == 0) && hasBak && hasSettingsAppId;

    if (success) {
        // 9. Commit
        std::ofstream mf(mfPath);
        mf << "{\n"
           << "  \"appId\": " << appId << ",\n"
           << "  \"gameDir\": \"" << jsonEscape(gameDir.string()) << "\",\n"
           << "  \"backups\": [\n";
        for (size_t i = 0; i < backups.size(); ++i) {
            mf << "    {\"bak\": \"" << jsonEscape(backups[i].first) << "\", \"orig\": \"" << jsonEscape(backups[i].second) << "\"}"
               << (i + 1 < backups.size() ? ",\n" : "\n");
        }
        mf << "  ],\n  \"created\": [\n";
        for (size_t i = 0; i < createdFiles.size(); ++i) {
            mf << "    \"" << jsonEscape(createdFiles[i]) << "\"" << (i + 1 < createdFiles.size() ? ",\n" : "\n");
        }
        mf << "  ],\n  \"patched\": {\n";
        size_t idx = 0;
        for (auto& [path, hash] : patchedHashes) {
            mf << "    \"" << jsonEscape(path) << "\": \"" << hash << "\""
               << (++idx < patchedHashes.size() ? ",\n" : "\n");
        }
        mf << "  }\n}\n";

        Apps::setAutoCrackInstalled(appId, true);
        setStatus(appId, State::Succeeded, "done", "AutoCrack applied successfully");
        LOG_NOTIFY("AutoCrack applied to %u", appId);
    } else {
        LOG_ERROR("[AutoCrack:%u] Verification failed (code=%d bak=%d settings=%d). Rolling back...\n",
                  appId, exitCode, hasBak, hasSettingsAppId);
        std::string err;
        performRestore(appId, err);
        setStatus(appId, State::Failed, "verify", "Verification failed or exit error");
        LOG_NOTIFYERROR("AutoCrack failed for %u", appId);
    }

    // 10. Cleanup
    std::filesystem::remove_all(jobDir, ec);
    rotateLogs(autocrackDir / "logs");
    return success;
}

static void workerLoop() {
    while (g_running) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(g_queueMutex);
            g_cv.wait(lock, [] {
                return !g_running || !g_queue.empty();
            });
            if (!g_running) break;
            job = g_queue.front();
            g_queue.pop_front();
        }

        // Lock file across processes
        auto lockPath = getAutoCrackDir() / ".lock";
        int lockFd = open(lockPath.c_str(), O_CREAT | O_RDWR, 0666);
        if (lockFd >= 0) {
            flock(lockFd, LOCK_EX);
        }

        runJob(job);

        if (lockFd >= 0) {
            flock(lockFd, LOCK_UN);
            close(lockFd);
        }
    }
}

void init() {
    if (g_running) return;
    g_running = true;

    // Check inflight manifests and rollback
    auto autocrackDir = getAutoCrackDir();
    std::error_code ec;
    auto mfDir = autocrackDir / "manifests";
    if (std::filesystem::exists(mfDir, ec)) {
        for (auto& entry : std::filesystem::directory_iterator(mfDir, ec)) {
            if (entry.is_regular_file(ec) && entry.path().extension() == ".json") {
                std::ifstream f(entry.path());
                std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                f.close();
                if (s.find("\"inflight\"") != std::string::npos) {
                    std::string stem = entry.path().stem().string();
                    char* endPtr = nullptr;
                    unsigned long parsedId = std::strtoul(stem.c_str(), &endPtr, 10);
                    if (endPtr && endPtr != stem.c_str() && *endPtr == '\0' && parsedId > 0) {
                        uint32_t appId = static_cast<uint32_t>(parsedId);
                        std::string err;
                        LOG_WARN("[AutoCrack:%u] Found leftover inflight manifest on init. Rolling back...\n", appId);
                        performRestore(appId, err);
                    }
                }
            }
        }
    }

    g_workerThread = std::thread(workerLoop);
}

void shutdown() {
    if (!g_running) return;
    g_running = false;
    g_cv.notify_all();

    pid_t cpid = g_childPid.load();
    if (cpid > 0) {
        kill(cpid, SIGTERM);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        kill(cpid, SIGKILL);
    }

    if (g_workerThread.joinable()) {
        g_workerThread.join();
    }
}

bool enqueue(uint32_t appId, Origin o, bool force) {
    std::lock_guard<std::mutex> lock(g_queueMutex);
    for (auto& j : g_queue) {
        if (j.appId == appId) {
            return false; // Already queued
        }
    }
    auto it = g_statusMap.find(appId);
    if (it != g_statusMap.end() && it->second.state == State::Running) {
        return false; // Already running
    }

    Job j;
    j.appId = appId;
    j.origin = o;
    j.force = force;
    j.isRestore = false;
    g_queue.push_back(j);

    Status& s = g_statusMap[appId];
    s.appId = appId;
    s.state = State::Queued;
    s.stage = "";
    s.message = "Queued";
    s.updatedAt = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    g_cv.notify_one();
    return true;
}

bool enqueueRestore(uint32_t appId) {
    std::lock_guard<std::mutex> lock(g_queueMutex);
    for (auto& j : g_queue) {
        if (j.appId == appId) {
            return false;
        }
    }
    auto it = g_statusMap.find(appId);
    if (it != g_statusMap.end() && it->second.state == State::Running) {
        return false;
    }

    Job j;
    j.appId = appId;
    j.origin = Origin::Manual;
    j.force = true;
    j.isRestore = true;
    g_queue.push_back(j);

    Status& s = g_statusMap[appId];
    s.appId = appId;
    s.state = State::Queued;
    s.stage = "restore";
    s.message = "Queued for restore";
    s.updatedAt = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    g_cv.notify_one();
    return true;
}

} // namespace AutoCrack

