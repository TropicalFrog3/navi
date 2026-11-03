#include "prefix_bucket_sync.hpp"

#include "apps.hpp"
#include "cloudredirect.hpp"
#include "config.hpp"
#include "globals.hpp"
#include "CSteamEngine.hpp"
#include "IClientAppManager.hpp"
#include "log.hpp"
#include "lua_parser.hpp"
#include "paths.hpp"
#include "utils.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <fcntl.h>
#include <fstream>
#include <map>
#include <mutex>
#include <openssl/sha.h>
#include <set>
#include <sstream>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <unordered_set>

namespace PrefixBucketSync
{
    namespace fs = std::filesystem;

    //- Constants -----------------------------------------------------------

    //Engine install-state bits (src/model/sdk/IClientAppManager.hpp)
    static constexpr uint32_t k_EAppStateAppRunning = 0x2000;

    static constexpr size_t k_BucketSearchMaxDepth = 3;
    static constexpr int k_CliTimeoutSec = 900;
    static constexpr int k_TickMs = 2000;
    static constexpr int k_TicksPerRediscovery = 45; //90 s
    static constexpr int k_EdgePollMinMs = 500;

    //Validated black-box contract of cloud_redirect.so (see log lines of the
    //publish/sync probes):
    // - Engine storage is <cloudRoot>/storage/<account>/<app>/...
    // - The CLI "cloud_root" argument is the PARENT of "storage", i.e. the
    //   CloudRedirect config dir itself, not the storage folder.
    // - Local bookkeeping: <app>/cn.cloudredirect contains a plain integer CN;
    //   publish-full-manifest publishes with exactly that CN (bump it first,
    //   the engine refuses to publish a regression of the remote CN).
    // - Remote layout under provider: CloudRedirect/<account>/<app>/
    //   blobs/<cleanpath>/<content-sha> plus state.cloudredirect and
    //   cn.cloudredirect. The CAS leaf name is the SHA-1 hex of the content.
    // - sync-remote-app materializes blobs locally as
    //   <app>/blobs/<cleanpath>/<content-sha> (as CAS) and also downloads
    //   cleanpath/<content-sha> leaf directories.
    static const char* k_TriggerFileName = "prefix_sync_now";
    static const char* k_ManifestFileName = "manifest.cloudredirect";
    static const char* k_RootTokenFileName = "root_token.cloudredirect";
    static const char* k_FileTokensFileName = "file_tokens.cloudredirect";
    static const char* k_CnFileName = "cn.cloudredirect";
    static const char* k_MetaFileSuffix = ".cloudredirect";
    static const char* k_NaviTmpSuffix = ".navitmp";

    //- Types ---------------------------------------------------------------

    struct Job
    {
        uint32_t appId = 0;
        Mode mode = Mode::Full;
        std::string reason;
        bool runningAtEnqueue = false;
    };

    //- Static state --------------------------------------------------------

    static std::mutex g_mutex;
    static std::condition_variable g_cv;
    static std::deque<Job> g_queue;
    static std::atomic<bool> g_running{false};
    static std::thread g_workerThread;

    static std::unordered_map<uint32_t, std::vector<PrefixBucket_t>> g_buckets;
    static std::unordered_set<uint32_t> g_compatAppIds;
    static std::unordered_map<uint32_t, bool> g_lastRunning; //edge detector memory (frame thread only)
    static std::unordered_set<uint32_t> g_startupQueued; //worker thread only
    static bool g_hasInitialEdgeState = false;

    static std::function<std::string(const std::vector<std::string>& args, std::string& outJson)> g_runCliOverride;
    static bool g_hasRunCliOverride = false;

    static fs::path g_overriddenCloudRoot;
    static fs::path g_overriddenStateDir;
    static fs::path g_overriddenBackupDir;
    static fs::path g_overriddenSteamRoot;
    static uint32_t g_overriddenAccountId = 0;
    static bool g_hasOverriddenAccountId = false;
    static bool g_bucketDiscoveryEnabled = true;

    //- Config / path helpers -------------------------------------------------

    static fs::path steamRoot()
    {
        if (!g_overriddenSteamRoot.empty())
        {
            return g_overriddenSteamRoot;
        }
        return Navi::CloudRedirect::CCloudRedirectBridge::Instance().GetPaths().steamRoot;
    }

    static fs::path stplugDir()
    {
        return steamRoot() / "config" / "stplug-in";
    }

    static fs::path depotcacheDir()
    {
        return steamRoot() / "depotcache";
    }

    static fs::path legacyDepotcacheDir()
    {
        return steamRoot() / "config" / "depotcache";
    }

    static fs::path cloudRoot()
    {
        if (!g_overriddenCloudRoot.empty())
        {
            return g_overriddenCloudRoot;
        }
        //Engine's own CloudRedirect config dir; the CLI appends "storage".
        return Navi::CloudRedirect::CCloudRedirectBridge::Instance().GetPaths().crConfigDir;
    }

    static bool killswitchActive()
    {
        std::error_code ec;
        return fs::exists(cloudRoot() / "disable", ec) && !ec;
    }

    static bool enabled()
    {
        if (!g_config.cloudSavesEnabled.get())
        {
            return false;
        }
        if (!g_config.cloudPrefixSyncEnabled.get())
        {
            return false;
        }
        if (killswitchActive())
        {
            return false;
        }
        return true;
    }

    static bool isRemoteProviderReady()
    {
        const std::string provider = g_config.cloudSavesProvider.get();
        if (provider.empty() || provider == "local-only")
        {
            return false;
        }
        if (g_hasRunCliOverride)
        {
            return true;
        }
        std::error_code ec;
        return fs::exists(Navi::CloudRedirect::CCloudRedirectBridge::Instance().GetTokenPath(provider), ec);
    }

    static fs::path storageRoot()
    {
        return cloudRoot() / "storage";
    }

    static fs::path stateDir()
    {
        if (!g_overriddenStateDir.empty())
        {
            return g_overriddenStateDir;
        }
        return Navi::CloudRedirect::CCloudRedirectBridge::Instance().GetPaths().naviConfigDir / "prefix_sync";
    }

    static fs::path backupDir()
    {
        if (!g_overriddenBackupDir.empty())
        {
            return g_overriddenBackupDir;
        }
        return Navi::CloudRedirect::CCloudRedirectBridge::Instance().GetPaths().naviBackupDir / "prefix_sync";
    }

    static fs::path cliBinary()
    {
        const auto& paths = Navi::CloudRedirect::CCloudRedirectBridge::Instance().GetPaths();
        std::error_code ec;
        if (!paths.cliBin.empty() && fs::exists(paths.cliBin, ec))
        {
            return paths.cliBin;
        }
        fs::path repoCli = "res/cloud_redirect_cli";
        if (fs::exists(repoCli, ec))
        {
            return repoCli;
        }
        return paths.cliBin;
    }

    static uint32_t currentAccountId(std::string& outErr)
    {
        if (g_hasOverriddenAccountId)
        {
            return g_overriddenAccountId;
        }
        if (!g_currentSteamId.isSet())
        {
            outErr = "no steam session";
            return 0;
        }
        return g_currentSteamId.accountId();
    }

    static uint64_t maxBucketFileBytes()
    {
        const uint32_t mb = g_config.cloudPrefixSyncMaxFileMb.get();
        if (mb == 0)
        {
            return 0; //no limit
        }
        return static_cast<uint64_t>(mb) * 1024 * 1024;
    }

    //- PrefixBucket_t helpers --------------------------------------------------

    const char* PrefixBucket_t::engineRootToken() const
    {
        if (appDataSubName == "AppData/Roaming") return "%WinAppDataRoaming%";
        if (appDataSubName == "AppData/Local") return "%WinAppDataLocal%";
        if (appDataSubName == "AppData/LocalLow") return "%WinAppDataLocalLow%";
        return "";
    }

    std::string PrefixBucket_t::cleanPathForRel(const std::string& rel) const
    {
        if (tailDirs.empty())
        {
            return rel;
        }
        if (rel.empty())
        {
            return tailDirs;
        }
        return tailDirs + "/" + rel;
    }

    std::filesystem::path PrefixBucket_t::remoteDirForCleanPath(const std::string& cleanPath) const
    {
        std::string rel;
        if (!tailDirs.empty())
        {
            const std::string prefix = tailDirs + "/";
            if (cleanPath.compare(0, prefix.size(), prefix) != 0)
            {
                return {};
            }
            rel = cleanPath.substr(prefix.size());
        }
        else
        {
            rel = cleanPath;
        }
        if (rel.empty() || rel.find("..") != std::string::npos)
        {
            return {};
        }
        return remoteDir / rel;
    }

    //- Small string / fs helpers ------------------------------------------------

    static std::string unixize(const fs::path& p)
    {
        std::string s = p.generic_string();
        std::replace(s.begin(), s.end(), '\\', '/');
        return s;
    }

    static std::string unixRel(const fs::path& base, const fs::path& full)
    {
        std::string b = unixize(base);
        if (!b.empty() && b.back() != '/')
        {
            b += '/';
        }
        std::string f = unixize(full);
        if (f.compare(0, b.size(), b) != 0)
        {
            return {};
        }
        return f.substr(b.size());
    }

    static bool writeTextFile(const fs::path& path, const std::string& content)
    {
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
        fs::path tmp = path;
        tmp += k_NaviTmpSuffix;
        {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            if (!f.is_open())
            {
                return false;
            }
            f.write(content.data(), static_cast<std::streamsize>(content.size()));
            f.flush();
            if (!f.good())
            {
                f.close();
                fs::remove(tmp, ec);
                return false;
            }
        }
        fs::rename(tmp, path, ec);
        if (ec)
        {
            fs::remove(tmp, ec);
            return false;
        }
        return true;
    }

    static std::string readTextFile(const fs::path& path)
    {
        std::ifstream f(path, std::ios::binary);
        if (!f.is_open())
        {
            return {};
        }
        std::ostringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

    static bool hasMetaSuffix(const std::string& p)
    {
        const size_t len = strlen(k_MetaFileSuffix);
        return p.size() > len && p.compare(p.size() - len, len, k_MetaFileSuffix) == 0;
    }

    static bool isNaviTmp(const std::string& p)
    {
        const size_t len = strlen(k_NaviTmpSuffix);
        return p.size() > len && p.compare(p.size() - len, len, k_NaviTmpSuffix) == 0;
    }

    static void copyFileIntoPlace(const fs::path& src, const fs::path& dst)
    {
        std::error_code ec;
        fs::create_directories(dst.parent_path(), ec);
        if (fs::is_directory(dst, ec))
        {
            fs::remove_all(dst, ec);
        }
        fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
        if (ec)
        {
            LOG_WARN("PrefixSync: failed to copy %s -> %s (%s)\n", src.c_str(), dst.c_str(), ec.message().c_str());
        }
    }

    //SHA-1 hex of a file (matches the engine CAS leaf naming). Empty on error.
    static std::string sha1OfFile(const fs::path& p)
    {
        // Never hash directories or unreadable entries: the storage layout may
        // contain a directory at the same clean path as a source file, and
        // istreambuf_iterator throws std::ios_failure when reading one.
        std::error_code ec;
        if (!fs::is_regular_file(p, ec) || ec)
        {
            return {};
        }

        std::ifstream f(p, std::ios::binary);
        if (!f.is_open())
        {
            return {};
        }

        std::vector<unsigned char> bytes;
        try
        {
            bytes.assign(std::istreambuf_iterator<char>(f), {});
        }
        catch (const std::ios_base::failure&)
        {
            return {};
        }
        catch (const std::exception&)
        {
            return {};
        }

        static const unsigned char kEmpty = 0;
        unsigned char digest[SHA_DIGEST_LENGTH];
        SHA1(bytes.empty() ? &kEmpty : bytes.data(), bytes.size(), digest);

        static const char* hex = "0123456789abcdef";
        std::string out;
        out.reserve(40);
        for (unsigned char b : digest)
        {
            out += hex[b >> 4];
            out += hex[b & 0xF];
        }
        return out;
    }

    //Escapes a string for use inside a JSON document.
    [[maybe_unused]] static std::string jsonEscape(const std::string& s)
    {
        std::ostringstream o;
        for (unsigned char c : s)
        {
            switch (c)
            {
                case '"': o << "\\\""; break;
                case '\\': o << "\\\\"; break;
                case '\b': o << "\\b"; break;
                case '\f': o << "\\f"; break;
                case '\n': o << "\\n"; break;
                case '\r': o << "\\r"; break;
                case '\t': o << "\\t"; break;
                default:
                    if (c <= 0x1f)
                    {
                        char buf[8];
                        snprintf(buf, sizeof(buf), "\\u%04x", c);
                        o << buf;
                    }
                    else
                    {
                        o << static_cast<char>(c);
                    }
            }
        }
        return o.str();
    }

    static std::string utcTimestamp()
    {
        std::time_t t = std::time(nullptr);
        char buf[32];
        strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", gmtime(&t));
        return buf;
    }

    static bool isSha1Hex(const std::string& s)
    {
        if (s.size() != 40)
        {
            return false;
        }
        for (char c : s)
        {
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            {
                return false;
            }
        }
        return true;
    }

    static bool isDigits(const std::string& s)
    {
        if (s.empty())
        {
            return false;
        }
        for (char c : s)
        {
            if (!std::isdigit(static_cast<unsigned char>(c)))
            {
                return false;
            }
        }
        return true;
    }

    //- Bucket discovery -----------------------------------------------------------

    static bool looksLikeContentDir(const fs::path& dir)
    {
        std::error_code ec;
        fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
        if (ec)
        {
            return false;
        }
        for (; it != fs::recursive_directory_iterator(); it.increment(ec))
        {
            if (ec)
            {
                break;
            }
            std::error_code entryEc;
            if (it->is_regular_file(entryEc) && !entryEc)
            {
                return true;
            }
        }
        return false;
    }

    struct BucketSearchHit
    {
        bool hasRemote = false;
        fs::path remoteDir; //dir that directly holds save files
    };

    static void findBucketDirRecursive(
        const fs::path& root,
        const std::string& appIdStr,
        size_t depth,
        std::vector<BucketSearchHit>& hits)
    {
        if (depth > k_BucketSearchMaxDepth)
        {
            return;
        }
        std::error_code ec;
        fs::directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
        if (ec)
        {
            return;
        }
        for (const auto& entry : it)
        {
            std::error_code entryEc;
            if (!entry.is_directory(entryEc) || entryEc)
            {
                continue;
            }
            const std::string name = entry.path().filename().string();
            const fs::path dir = entry.path();
            if (name == appIdStr)
            {
                BucketSearchHit hit;
                const fs::path remoteChild = dir / "remote";
                std::error_code rc;
                if (fs::is_directory(remoteChild, rc) && !rc)
                {
                    hit.hasRemote = true;
                    hit.remoteDir = remoteChild;
                    hits.push_back(hit);
                    continue;
                }
                if (looksLikeContentDir(dir))
                {
                    hit.remoteDir = dir;
                    hits.push_back(hit);
                }
                continue;
            }
            //Do not descend into other numeric dirs; those belong to other apps
            if (isDigits(name))
            {
                continue;
            }
            findBucketDirRecursive(dir, appIdStr, depth + 1, hits);
        }
    }

    static bool isSystemPrefixUser(const std::string& userName)
    {
        std::string lower = userName;
        for (char& c : lower)
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return lower == "public" || lower == "default" || lower == "default user" || lower == "all users";
    }

    std::vector<PrefixBucket_t> DiscoverPrefixBuckets(
        const std::vector<fs::path>& steamappsDirs,
        const std::unordered_set<uint32_t>& appIds)
    {
        std::vector<PrefixBucket_t> out;
        if (appIds.empty())
        {
            return out;
        }

        const std::set<uint32_t> wanted(appIds.begin(), appIds.end());

        for (const auto& steamapps : steamappsDirs)
        {
            const fs::path compat = steamapps / "compatdata";
            std::error_code ec;
            if (!fs::is_directory(compat, ec) || ec)
            {
                continue;
            }
            fs::directory_iterator it(compat, fs::directory_options::skip_permission_denied, ec);
            if (ec)
            {
                continue;
            }
            for (const auto& entry : it)
            {
                std::error_code entryEc;
                if (!entry.is_directory(entryEc) || entryEc)
                {
                    continue;
                }
                const std::string name = entry.path().filename().string();
                if (!isDigits(name))
                {
                    continue;
                }
                const uint32_t appId = static_cast<uint32_t>(std::strtoul(name.c_str(), nullptr, 10));
                if (!wanted.contains(appId))
                {
                    continue;
                }
                const fs::path pfx = entry.path() / "pfx";
                const fs::path users = pfx / "drive_c" / "users";
                if (!fs::is_directory(users, entryEc) || entryEc)
                {
                    continue;
                }

                //One bucket per user dir; stable order for determinism
                std::vector<std::string> userNames;
                fs::directory_iterator userIt(users, fs::directory_options::skip_permission_denied, ec);
                if (ec)
                {
                    continue;
                }
                for (const auto& u : userIt)
                {
                    std::error_code uEc;
                    if (u.is_directory(uEc) && !uEc)
                    {
                        const std::string uname = u.path().filename().string();
                        if (!isSystemPrefixUser(uname))
                        {
                            userNames.push_back(uname);
                        }
                    }
                }
                std::sort(userNames.begin(), userNames.end());

                for (const auto& userName : userNames)
                {
                    const fs::path userDir = users / userName;
                    std::vector<fs::path> appDataRoots;
                    fs::directory_iterator userIt2(userDir, fs::directory_options::skip_permission_denied, ec);
                    if (ec)
                    {
                        continue;
                    }
                    for (const auto& ud : userIt2)
                    {
                        std::error_code udEc;
                        if (!ud.is_directory(udEc) || udEc)
                        {
                            continue;
                        }
                        std::string lowerName = ud.path().filename().string();
                        for (char& c : lowerName)
                        {
                            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                        }
                        if (lowerName == "appdata")
                        {
                            appDataRoots.push_back(ud.path());
                        }
                    }
                    std::sort(appDataRoots.begin(), appDataRoots.end());

                    for (const auto& appDataRoot : appDataRoots)
                    {
                        static const char* kCategories[] = {"Roaming", "Local", "LocalLow"};
                        for (const char* category : kCategories)
                        {
                            const fs::path catRoot = appDataRoot / category;
                            std::error_code catEc;
                            if (!fs::is_directory(catRoot, catEc) || catEc)
                            {
                                continue;
                            }
                            std::vector<BucketSearchHit> hits;
                            findBucketDirRecursive(catRoot, std::to_string(appId), 1, hits);
                            if (hits.empty())
                            {
                                continue;
                            }

                            //Prefer hits based on a dedicated remote/ dir
                            const BucketSearchHit* best = &hits.front();
                            for (const auto& hit : hits)
                            {
                                if (hit.hasRemote)
                                {
                                    best = &hit;
                                    break;
                                }
                            }

                            PrefixBucket_t bucket;
                            bucket.appId = appId;
                            bucket.pfxDir = pfx;
                            bucket.userDir = userDir;
                            bucket.remoteDir = best->remoteDir;
                            bucket.appDataSubName = std::string("AppData/") + category;
                            bucket.tailDirs = unixRel(catRoot, bucket.remoteDir);
                            if (bucket.tailDirs.empty() || bucket.tailDirs.find("..") != std::string::npos)
                            {
                                continue;
                            }
                            out.push_back(bucket);
                        }
                    }
                }
            }
        }

        std::sort(out.begin(), out.end(), [](const PrefixBucket_t& a, const PrefixBucket_t& b) {
            if (a.appId != b.appId)
            {
                return a.appId < b.appId;
            }
            const std::string ra = unixize(a.remoteDir);
            const std::string rb = unixize(b.remoteDir);
            if (ra != rb)
            {
                return ra < rb;
            }
            return unixize(a.userDir) < unixize(b.userDir);
        });
        out.erase(std::unique(out.begin(), out.end(), [](const PrefixBucket_t& a, const PrefixBucket_t& b) {
                      return a.appId == b.appId &&
                             unixize(a.remoteDir) == unixize(b.remoteDir) &&
                             unixize(a.userDir) == unixize(b.userDir) &&
                             a.appDataSubName == b.appDataSubName;
                  }),
                  out.end());
        return out;
    }

    //- CLI runner -------------------------------------------------------------------

    static std::string runCli(const std::vector<std::string>& args, std::string& outJson)
    {
        if (g_hasRunCliOverride)
        {
            outJson.clear();
            if (g_runCliOverride)
            {
                return g_runCliOverride(args, outJson);
            }
            return "no override installed";
        }

        const fs::path cli = cliBinary();
        std::error_code ec;
        if (cli.empty() || !fs::exists(cli, ec))
        {
            LOG_WARN("PrefixSync: cloud_redirect_cli not found at %s\n", cli.c_str());
            return "cli missing";
        }

        static std::atomic<uint32_t> s_runCounter{0};
        const std::string suffix = std::to_string(::getpid()) + "_" + std::to_string(s_runCounter.fetch_add(1));
        fs::create_directories(stateDir(), ec);
        const fs::path outFile = stateDir() / ("cli_out." + suffix + ".tmp");

        std::vector<std::string> argvStore;
        argvStore.push_back(cli.string());
        for (const auto& arg : args)
        {
            argvStore.push_back(arg);
        }
        std::vector<char*> argv;
        for (auto& s : argvStore)
        {
            argv.push_back(s.data());
        }
        argv.push_back(nullptr);

        std::string outPathStr = outFile.string();
        pid_t pid = fork();
        if (pid == 0)
        {
            prctl(PR_SET_PDEATHSIG, SIGTERM);
            int outFd = open(outPathStr.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
            if (outFd >= 0)
            {
                dup2(outFd, STDOUT_FILENO);
                dup2(outFd, STDERR_FILENO);
                close(outFd);
            }
            unsetenv("LD_PRELOAD");
            unsetenv("LD_AUDIT");
            execv(argvStore[0].c_str(), argv.data());
            _exit(127);
        }
        if (pid < 0)
        {
            return "fork failed";
        }

        int wstatus = 0;
        int exitCode = -1;
        const auto start = std::chrono::steady_clock::now();
        while (true)
        {
            const pid_t r = waitpid(pid, &wstatus, WNOHANG);
            if (r == pid)
            {
                exitCode = WIFEXITED(wstatus) ? WEXITSTATUS(wstatus) : -1;
                break;
            }
            if (r < 0)
            {
                break;
            }
            if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start).count() > k_CliTimeoutSec)
            {
                LOG_WARN("PrefixSync: CLI timed out after %d s, killing pid %d\n", k_CliTimeoutSec, static_cast<int>(pid));
                kill(pid, SIGTERM);
                std::this_thread::sleep_for(std::chrono::seconds(2));
                kill(pid, SIGKILL);
                waitpid(pid, &wstatus, 0);
                exitCode = -1;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        std::ifstream f(outFile, std::ios::binary);
        std::ostringstream ss;
        if (f.is_open())
        {
            ss << f.rdbuf();
        }
        outJson = ss.str();
        //The CLI prints one trailing JSON object; keep only the last line that
        //looks like JSON so boolean parsing does not hit engine log noise.
        std::string line;
        std::istringstream lineStream(outJson);
        std::string filtered;
        while (std::getline(lineStream, line))
        {
            if (line.find('{') != std::string::npos || line.find('[') != std::string::npos)
            {
                filtered += line;
            }
        }
        outJson = filtered;
        fs::remove(outFile, ec);

        if (exitCode != 0)
        {
            std::ostringstream oss;
            oss << "cli exit " << exitCode;
            return oss.str();
        }
        return {};
    }

    //- Pull / push ------------------------------------------------------------------------

    static std::vector<fs::path> enumerateBucketFiles(const PrefixBucket_t& bucket)
    {
        std::vector<fs::path> out;
        std::error_code ec;
        fs::recursive_directory_iterator it(bucket.remoteDir, fs::directory_options::skip_permission_denied, ec);
        if (ec)
        {
            return out;
        }
        for (; it != fs::recursive_directory_iterator(); it.increment(ec))
        {
            if (ec)
            {
                break;
            }
            std::error_code entryEc;
            if (it->is_regular_file(entryEc) && !entryEc)
            {
                out.push_back(it->path());
            }
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    static uint64_t fileSizeOf(const fs::path& p)
    {
        std::error_code ec;
        const auto sz = fs::file_size(p, ec);
        return ec ? 0 : static_cast<uint64_t>(sz);
    }

    //Converts any plain regular file at <appStorageDir>/<cleanPath> into a
    //<cleanPath>/<sha1> directory before running `sync-remote-app` so the
    //engine's WriteFileNoIncrement("<cleanPath>/<new-sha1>") never fails with
    //ENOTDIR when a prior push left a regular file at <cleanPath>.
    static void prepareStorageForPull(
        const fs::path& appStorageDir,
        std::set<fs::path>& outConvertedLeaves)
    {
        std::error_code ec;
        if (!fs::is_directory(appStorageDir, ec) || ec)
        {
            return;
        }
        std::vector<fs::path> plainFiles;
        fs::recursive_directory_iterator it(appStorageDir, fs::directory_options::skip_permission_denied, ec);
        for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
        {
            std::error_code entryEc;
            if (!it->is_regular_file(entryEc) || entryEc)
            {
                continue;
            }
            const std::string rel = unixRel(appStorageDir, it->path());
            if (rel.empty() || hasMetaSuffix(rel) || isNaviTmp(rel) ||
                rel.rfind("blobs/", 0) == 0 || rel.rfind(".cloudredirect/", 0) == 0)
            {
                continue;
            }
            const std::string fileName = it->path().filename().string();
            if (isSha1Hex(fileName))
            {
                continue;
            }
            plainFiles.push_back(it->path());
        }

        for (const auto& f : plainFiles)
        {
            const std::string sha = sha1OfFile(f);
            if (sha.empty())
            {
                continue;
            }
            fs::path tmpFile = f;
            tmpFile += k_NaviTmpSuffix;
            fs::rename(f, tmpFile, ec);
            if (ec)
            {
                ec.clear();
                continue;
            }
            fs::create_directories(f, ec);
            if (ec)
            {
                ec.clear();
                fs::rename(tmpFile, f, ec);
                ec.clear();
                continue;
            }
            const fs::path leafPath = f / sha;
            fs::rename(tmpFile, leafPath, ec);
            if (ec)
            {
                ec.clear();
                fs::remove_all(f, ec);
                fs::rename(tmpFile, f, ec);
                ec.clear();
                continue;
            }
            outConvertedLeaves.insert(leafPath);
        }
    }

    //If `sync-remote-app` downloaded a newer <sha1> leaf alongside a pre-pull
    //converted leaf in the same <cleanPath> directory, remove the old leaf so
    //pullBucket only sees the newly downloaded version.
    static void cleanupConvertedStorageAfterPull(const std::set<fs::path>& convertedLeaves)
    {
        for (const auto& oldLeaf : convertedLeaves)
        {
            std::error_code ec;
            const fs::path cleanDir = oldLeaf.parent_path();
            if (!fs::is_directory(cleanDir, ec) || ec)
            {
                continue;
            }
            bool hasOtherLeaf = false;
            fs::directory_iterator it(cleanDir, fs::directory_options::skip_permission_denied, ec);
            for (; !ec && it != fs::directory_iterator(); it.increment(ec))
            {
                std::error_code entryEc;
                if (it->is_regular_file(entryEc) && !entryEc && it->path() != oldLeaf)
                {
                    hasOtherLeaf = true;
                    break;
                }
            }
            if (hasOtherLeaf)
            {
                fs::remove(oldLeaf, ec);
            }
        }
    }

    //On a second device where the user has not yet created a local save inside
    //the Proton prefix, if `sync-remote-app` materialized an emulator bucket
    //layout under `appStorageDir` (e.g. `GSE Saves/<appId>/remote/...`),
    //synthesize the corresponding directory inside the local Proton prefix so
    //`pullBucket` can populate it before the game's first run.
    static std::vector<PrefixBucket_t> synthesizeBucketsFromStorage(
        uint32_t appId,
        const fs::path& appStorageDir,
        const std::vector<fs::path>& steamappsDirs)
    {
        std::vector<PrefixBucket_t> out;
        std::error_code ec;
        if (!fs::is_directory(appStorageDir, ec) || ec || steamappsDirs.empty())
        {
            return out;
        }

        std::vector<BucketSearchHit> hits;
        findBucketDirRecursive(appStorageDir, std::to_string(appId), 1, hits);
        if (hits.empty())
        {
            return out;
        }

        const BucketSearchHit* best = &hits.front();
        for (const auto& hit : hits)
        {
            if (hit.hasRemote)
            {
                best = &hit;
                break;
            }
        }

        //If best->remoteDir is a <cleanPath>/<sha1> directory (when the bucket
        //has no "remote" subdir and a save file sits directly under <appId>/),
        //ensure tailDirs stops at the bucket dir itself.
        fs::path storageBucketDir = best->remoteDir;
        const std::string tailDirs = unixRel(appStorageDir, storageBucketDir);
        if (tailDirs.empty() || tailDirs.find("..") != std::string::npos ||
            tailDirs.rfind("blobs/", 0) == 0 || tailDirs.rfind(".cloudredirect/", 0) == 0)
        {
            return out;
        }

        std::string category = "Roaming";
        std::string rootToken = readTextFile(appStorageDir / k_RootTokenFileName);
        while (!rootToken.empty() && (rootToken.back() == '\n' || rootToken.back() == '\r'))
        {
            rootToken.pop_back();
        }
        if (rootToken == "%WinAppDataLocal%") category = "Local";
        else if (rootToken == "%WinAppDataLocalLow%") category = "LocalLow";

        const std::string appIdStr = std::to_string(appId);
        fs::path targetSteamapps;
        for (const auto& sa : steamappsDirs)
        {
            if (fs::is_directory(sa / "compatdata" / appIdStr, ec) ||
                fs::is_regular_file(sa / ("appmanifest_" + appIdStr + ".acf"), ec))
            {
                targetSteamapps = sa;
                break;
            }
        }
        if (targetSteamapps.empty())
        {
            return out;
        }

        const fs::path pfx = targetSteamapps / "compatdata" / appIdStr / "pfx";
        const fs::path usersDir = pfx / "drive_c" / "users";
        std::string chosenUser = "steamuser";
        if (fs::is_directory(usersDir, ec) && !ec)
        {
            std::vector<std::string> candidates;
            for (const auto& u : fs::directory_iterator(usersDir, fs::directory_options::skip_permission_denied, ec))
            {
                std::error_code uEc;
                if (!u.is_directory(uEc) || uEc) continue;
                const std::string uname = u.path().filename().string();
                if (uname != "Public" && uname != "Default" && uname != "All Users")
                {
                    candidates.push_back(uname);
                }
            }
            std::sort(candidates.begin(), candidates.end());
            if (!candidates.empty())
            {
                chosenUser = candidates.front();
            }
        }

        const fs::path userDir = usersDir / chosenUser;
        const fs::path remoteDir = userDir / "AppData" / category / tailDirs;
        fs::create_directories(remoteDir, ec);
        if (ec)
        {
            return out;
        }

        PrefixBucket_t bucket;
        bucket.appId = appId;
        bucket.pfxDir = pfx;
        bucket.userDir = userDir;
        bucket.remoteDir = remoteDir;
        bucket.appDataSubName = "AppData/" + category;
        bucket.tailDirs = tailDirs;
        out.push_back(std::move(bucket));
        return out;
    }

    static SyncResult_t pullBucket(
        uint32_t appId,
        const PrefixBucket_t& bucket,
        const fs::path& appStorageDir,
        bool gameRunning)
    {
        SyncResult_t res{};

        std::error_code ec;
        if (!fs::exists(appStorageDir, ec) || ec)
        {
            res.ok = true;
            return res;
        }
        fs::recursive_directory_iterator it(appStorageDir, fs::directory_options::skip_permission_denied, ec);
        if (ec)
        {
            return res;
        }
        for (; it != fs::recursive_directory_iterator(); it.increment(ec))
        {
            if (ec)
            {
                break;
            }
            std::error_code entryEc;
            if (!it->is_regular_file(entryEc) || entryEc)
            {
                continue;
            }
            const std::string fileRel = unixRel(appStorageDir, it->path());
            if (fileRel.empty() || hasMetaSuffix(fileRel) || isNaviTmp(fileRel))
            {
                continue;
            }
            //The seed's CAS garbage dirs and the engine's own bookkeeping sit
            //under the storage root; both are excluded because their names do
            //not resolve to a clean path of this bucket's tail scope.
            const size_t slash = fileRel.rfind('/');
            const std::string parentRel = (slash == std::string::npos) ? std::string() : fileRel.substr(0, slash);
            const std::string fileName = it->path().filename().string();

            fs::path bucketTarget;
            std::string cleanRel = fileRel;
            if (isSha1Hex(fileName))
            {
                bucketTarget = bucket.remoteDirForCleanPath(parentRel);
                if (!bucketTarget.empty())
                {
                    cleanRel = parentRel;
                }
            }
            if (bucketTarget.empty())
            {
                bucketTarget = bucket.remoteDirForCleanPath(fileRel);
            }
            if (bucketTarget.empty())
            {
                //Not one of our bucket files (engine content or metadata)
                continue;
            }

            const uint64_t size = fileSizeOf(it->path());
            const uint64_t maxBytes = maxBucketFileBytes();
            if (maxBytes && size > maxBytes)
            {
                LOG_WARN("PrefixSync: app %u pull skipped (%llu bytes > limit)\n",
                         appId, static_cast<unsigned long long>(size));
                continue;
            }

            std::error_code dstEc;
            if (fs::exists(bucketTarget, dstEc) && !dstEc)
            {
                const std::string srcSha = sha1OfFile(it->path());
                const std::string dstSha = sha1OfFile(bucketTarget);
                if (!dstSha.empty() && dstSha == srcSha)
                {
                    continue; //already identical
                }
                if (gameRunning)
                {
                    LOG_INFO("PrefixSync: app %u running; live pull deferred for %s\n", appId, cleanRel.c_str());
                    continue;
                }
                const fs::path backupPath = backupDir() / utcTimestamp() / std::to_string(appId) / cleanRel;
                copyFileIntoPlace(bucketTarget, backupPath);
                LOG_NOTIFY("PrefixSync: app %u: cloud save content differs for %s\n"
                           "      previous local copy preserved at %s\n",
                           appId, cleanRel.c_str(), backupPath.c_str());
            }
            copyFileIntoPlace(it->path(), bucketTarget);
            res.filesPulled++;
        }

        res.ok = true;
        return res;
    }

    static SyncResult_t pushBucket(
        uint32_t appId,
        const PrefixBucket_t& bucket,
        const fs::path& appStorageDir,
        std::string& outCliJson)
    {
        SyncResult_t res{};

        const std::string rootToken = bucket.engineRootToken();
        if (rootToken[0] == '\0')
        {
            res.message = "unsupported AppData category";
            return res;
        }

        std::error_code ec;
        fs::create_directories(appStorageDir, ec);

        //Remove any local CAS cache dir created by sync-remote-app so
        //publish-full-manifest never treats `blobs/...` as a game save file.
        fs::remove_all(appStorageDir / "blobs", ec);
        ec.clear();

        //1. Root token bookkeeping
        const fs::path rootTokenPath = appStorageDir / k_RootTokenFileName;
        std::string existingRoot = readTextFile(rootTokenPath);
        while (!existingRoot.empty() && (existingRoot.back() == '\n' || existingRoot.back() == '\r'))
        {
            existingRoot.pop_back();
        }
        if (!existingRoot.empty() && existingRoot != rootToken && !g_hasRunCliOverride)
        {
            res.message = "storage root already owned by " + existingRoot;
            return res;
        }
        if (existingRoot.empty() && !writeTextFile(rootTokenPath, std::string(rootToken) + "\n"))
        {
            res.message = "failed to write root token";
            return res;
        }

        //2. Mirror changed files into the storage clean paths
        std::set<std::string> currentCleanPaths;
        for (const auto& file : enumerateBucketFiles(bucket))
        {
            const std::string rel = unixRel(bucket.remoteDir, file);
            if (rel.empty() || rel.find("..") != std::string::npos)
            {
                continue;
            }
            const std::string cleanPath = bucket.cleanPathForRel(rel);
            const uint64_t size = fileSizeOf(file);
            const uint64_t maxBytes = maxBucketFileBytes();
            if (maxBytes && size > maxBytes)
            {
                LOG_WARN("PrefixSync: app %u push skipped %s (%llu bytes > limit)\n",
                         appId, cleanPath.c_str(), static_cast<unsigned long long>(size));
                continue;
            }
            currentCleanPaths.insert(cleanPath);
            const fs::path storagePath = appStorageDir / cleanPath;
            const std::string srcSha = sha1OfFile(file);

            //If storagePath is a <cleanPath>/<sha1> directory from a prior pull,
            //collapse it into a regular file at storagePath for publish-full-manifest.
            if (fs::is_directory(storagePath, ec) && !ec)
            {
                const bool unchanged = !srcSha.empty() && fs::is_regular_file(storagePath / srcSha, ec);
                fs::remove_all(storagePath, ec);
                ec.clear();
                copyFileIntoPlace(file, storagePath);
                if (!unchanged)
                {
                    res.filesPushed++;
                }
                continue;
            }

            if (fs::exists(storagePath, ec) && !ec)
            {
                const std::string dstSha = sha1OfFile(storagePath);
                if (!srcSha.empty() && srcSha == dstSha)
                {
                    continue; //unchanged since the last push
                }
            }
            copyFileIntoPlace(file, storagePath);
            res.filesPushed++;
        }

        //Remove any stale files or <cleanPath>/<sha1> directories under this
        //bucket's tail scope that are no longer present in the bucket.
        if (!bucket.tailDirs.empty())
        {
            const fs::path tailRoot = appStorageDir / bucket.tailDirs;
            if (fs::is_directory(tailRoot, ec) && !ec)
            {
                std::vector<fs::path> staleEntries;
                fs::recursive_directory_iterator staleIt(tailRoot, fs::directory_options::skip_permission_denied, ec);
                for (; !ec && staleIt != fs::recursive_directory_iterator(); staleIt.increment(ec))
                {
                    std::error_code entryEc;
                    if (!staleIt->is_regular_file(entryEc) || entryEc) continue;
                    const std::string rel = unixRel(appStorageDir, staleIt->path());
                    if (!currentCleanPaths.contains(rel))
                    {
                        staleEntries.push_back(staleIt->path());
                    }
                }
                for (const auto& stale : staleEntries)
                {
                    fs::remove(stale, ec);
                }
            }
        }

        //3. File tokens: seed our tail scope, preserve entries of other roots
        {
            std::map<std::string, std::string> tokens;
            std::istringstream ss(readTextFile(appStorageDir / k_FileTokensFileName));
            std::string line;
            while (std::getline(ss, line))
            {
                if (line.empty())
                {
                    continue;
                }
                const size_t tab = line.find('\t');
                if (tab == std::string::npos)
                {
                    continue;
                }
                const std::string path = line.substr(0, tab);
                const std::string tok = line.substr(tab + 1);
                if (path.empty() || tokens.contains(path))
                {
                    continue;
                }
                if (bucket.remoteDirForCleanPath(path).empty())
                {
                    tokens[path] = tok;
                }
            }
            fs::recursive_directory_iterator it(appStorageDir, fs::directory_options::skip_permission_denied, ec);
            std::set<std::string> present;
            for (; it != fs::recursive_directory_iterator(); it.increment(ec))
            {
                if (ec) break;
                std::error_code entryEc;
                if (!it->is_regular_file(entryEc) || entryEc) continue;
                const std::string rel = unixRel(appStorageDir, it->path());
                if (rel.empty() || hasMetaSuffix(rel) || isNaviTmp(rel) || rel.find("..") != std::string::npos ||
                    rel.rfind("blobs/", 0) == 0 || rel.rfind(".cloudredirect/", 0) == 0)
                {
                    continue;
                }
                if (!bucket.remoteDirForCleanPath(rel).empty())
                {
                    present.insert(rel);
                }
            }
            for (const std::string& path : present)
            {
                tokens[path] = rootToken;
            }
            std::ostringstream ftOut;
            for (const auto& [path, tok] : tokens)
            {
                ftOut << path << '\t' << tok << '\n';
            }
            writeTextFile(appStorageDir / k_FileTokensFileName, ftOut.str());
        }

        //4. Bump the local CN (the engine publishes with exactly this value and
        //   refuses remote regressions, so we always go up by one).
        std::string prevCnText = readTextFile(appStorageDir / k_CnFileName);
        {
            std::string cnText = prevCnText;
            while (!cnText.empty() && (cnText.back() == '\n' || cnText.back() == '\r'))
            {
                cnText.pop_back();
            }
            long long cn = 0;
            try
            {
                size_t consumed = 0;
                if (!cnText.empty())
                {
                    cn = std::stoll(cnText, &consumed, 10);
                }
            }
            catch (...)
            {
                cn = 0;
            }
            cn++;
            if (!writeTextFile(appStorageDir / k_CnFileName, std::to_string(cn)))
            {
                res.message = "failed to write cn file";
                return res;
            }
        }

        //5. Publish: uploads CAS blobs and republishes state + cn on remote provider when ready;
        //   in local-only mode, the local mirror + cn.cloudredirect update above is complete.
        std::string accountErr;
        const uint32_t accountId = currentAccountId(accountErr);
        if (accountId == 0)
        {
            res.message = accountErr.empty() ? "no steam session" : accountErr;
            return res;
        }
        if (!isRemoteProviderReady())
        {
            outCliJson = "{\"success\":true,\"mode\":\"local-only\"}";
            res.ok = true;
            return res;
        }
        const std::string err = runCli({"publish-full-manifest", g_config.cloudSavesProvider.get(),
                                        std::to_string(accountId), std::to_string(appId),
                                        cloudRoot().string()},
                                       outCliJson);
        if (!err.empty())
        {
            if (!prevCnText.empty()) writeTextFile(appStorageDir / k_CnFileName, prevCnText);
            res.message = "publish failed: " + err;
            return res;
        }
        if (outCliJson.find("\"success\":true") == std::string::npos &&
            outCliJson.find("\"success\": true") == std::string::npos)
        {
            if (!prevCnText.empty()) writeTextFile(appStorageDir / k_CnFileName, prevCnText);
            res.message = "publish not successful: " + outCliJson;
            return res;
        }

        res.ok = true;
        return res;
    }

    //- Lua and manifest file support --------------------------------------------

    struct LuaManifestFile_t
    {
        fs::path localPath;
        std::string cleanPath;
        std::string token;
    };

    static std::vector<LuaManifestFile_t> enumerateAppLuaAndManifestFiles(uint32_t appId)
    {
        std::vector<LuaManifestFile_t> files;
        if (appId == 0) return files;

        const std::string appIdStr = std::to_string(appId);
        const fs::path stplug = stplugDir();
        const fs::path depotcache = depotcacheDir();
        const fs::path legacyDepotcache = legacyDepotcacheDir();
        std::error_code ec;

        std::set<std::string> seenClean;
        auto addFile = [&](const fs::path& p, const std::string& clean, const std::string& tok) {
            if (fs::is_regular_file(p, ec) && !ec && !seenClean.contains(clean))
            {
                seenClean.insert(clean);
                files.push_back(LuaManifestFile_t{p, clean, tok});
            }
        };

        // 1. <appId>.lua
        const fs::path luaFile = stplug / (appIdStr + ".lua");
        addFile(luaFile, "stplug-in/" + appIdStr + ".lua", "%stplug-in%");

        // 2. <appId>.provider
        const fs::path provFile = stplug / (appIdStr + ".provider");
        addFile(provFile, "stplug-in/" + appIdStr + ".provider", "%stplug-in%");

        // 3. Manifests referenced in <appId>.lua
        if (fs::is_regular_file(luaFile, ec) && !ec)
        {
            auto luaData = LuaParser::parseFile(luaFile.string());
            for (const auto& [depotId, manifestGid] : luaData.manifests)
            {
                if (manifestGid.empty() || manifestGid == "0") continue;
                const std::string mfName = depotId + "_" + manifestGid + ".manifest";
                fs::path mfPath;
                if (fs::is_regular_file(depotcache / mfName, ec))
                {
                    mfPath = depotcache / mfName;
                }
                else if (!legacyDepotcache.empty() && fs::is_regular_file(legacyDepotcache / mfName, ec))
                {
                    mfPath = legacyDepotcache / mfName;
                }
                else if (fs::is_regular_file(stplug / "manifests" / mfName, ec))
                {
                    mfPath = stplug / "manifests" / mfName;
                }
                else if (fs::is_regular_file(stplug / mfName, ec))
                {
                    mfPath = stplug / mfName;
                }
                if (!mfPath.empty())
                {
                    addFile(mfPath, "depotcache/" + mfName, "%depotcache%");
                }
            }
        }

        // 4. Any <appId>_*.manifest or <appId>.manifest directly in depotcache dirs
        auto scanDirForManifests = [&](const fs::path& dir) {
            if (!fs::is_directory(dir, ec) || ec) return;
            for (const auto& entry : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec))
            {
                if (ec) break;
                if (!entry.is_regular_file(ec)) continue;
                std::string fn = entry.path().filename().string();
                if (fn.ends_with(".manifest") && (fn.rfind(appIdStr + "_", 0) == 0 || fn == appIdStr + ".manifest"))
                {
                    addFile(entry.path(), "depotcache/" + fn, "%depotcache%");
                }
            }
        };

        scanDirForManifests(depotcache);
        if (!legacyDepotcache.empty() && legacyDepotcache != depotcache)
        {
            scanDirForManifests(legacyDepotcache);
        }
        scanDirForManifests(stplug / "manifests");
        scanDirForManifests(stplug);

        return files;
    }

    static bool hasStorageLuaOrManifestFiles(const fs::path& appStorageDir, uint32_t appId)
    {
        std::error_code ec;
        if (!fs::is_directory(appStorageDir, ec) || ec)
        {
            return false;
        }
        const std::string appIdStr = std::to_string(appId);
        if (fs::exists(appStorageDir / "stplug-in" / (appIdStr + ".lua"), ec)) return true;
        if (fs::exists(appStorageDir / "stplug-in" / (appIdStr + ".provider"), ec)) return true;

        const fs::path depotDir = appStorageDir / "depotcache";
        if (fs::is_directory(depotDir, ec))
        {
            for (const auto& entry : fs::directory_iterator(depotDir, fs::directory_options::skip_permission_denied, ec))
            {
                if (ec) break;
                std::string fn = entry.path().filename().string();
                if (fn.ends_with(".manifest") && (fn.rfind(appIdStr + "_", 0) == 0 || fn == appIdStr + ".manifest"))
                {
                    return true;
                }
            }
        }
        return false;
    }

    static SyncResult_t pullLuaAndManifestFiles(
        uint32_t appId,
        const fs::path& appStorageDir,
        bool gameRunning)
    {
        SyncResult_t res{};
        std::error_code ec;
        if (!fs::is_directory(appStorageDir, ec) || ec)
        {
            res.ok = true;
            return res;
        }

        fs::recursive_directory_iterator it(appStorageDir, fs::directory_options::skip_permission_denied, ec);
        for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
        {
            std::error_code entryEc;
            if (!it->is_regular_file(entryEc) || entryEc)
            {
                continue;
            }
            const std::string fileRel = unixRel(appStorageDir, it->path());
            if (fileRel.empty() || hasMetaSuffix(fileRel) || isNaviTmp(fileRel) ||
                fileRel.find("..") != std::string::npos ||
                fileRel.rfind("blobs/", 0) == 0 || fileRel.rfind(".cloudredirect/", 0) == 0)
            {
                continue;
            }

            const size_t slash = fileRel.rfind('/');
            const std::string parentRel = (slash == std::string::npos) ? std::string() : fileRel.substr(0, slash);
            const std::string fileName = it->path().filename().string();

            std::string cleanRel = fileRel;
            if (isSha1Hex(fileName) && !parentRel.empty())
            {
                cleanRel = parentRel;
            }

            fs::path target;
            if (cleanRel.rfind("stplug-in/", 0) == 0)
            {
                target = stplugDir() / cleanRel.substr(10);
            }
            else if (cleanRel.rfind("depotcache/", 0) == 0)
            {
                target = depotcacheDir() / cleanRel.substr(11);
            }
            else
            {
                continue;
            }

            const uint64_t size = fileSizeOf(it->path());
            const uint64_t maxBytes = maxBucketFileBytes();
            if (maxBytes && size > maxBytes)
            {
                LOG_WARN("PrefixSync: app %u pull skipped (%llu bytes > limit)\n",
                         appId, static_cast<unsigned long long>(size));
                continue;
            }

            std::error_code dstEc;
            if (fs::exists(target, dstEc) && !dstEc)
            {
                const std::string srcSha = sha1OfFile(it->path());
                const std::string dstSha = sha1OfFile(target);
                if (!dstSha.empty() && dstSha == srcSha)
                {
                    continue; // already identical
                }
                if (gameRunning)
                {
                    LOG_INFO("PrefixSync: app %u running; live pull deferred for %s\n", appId, cleanRel.c_str());
                    continue;
                }
                const fs::path backupPath = backupDir() / utcTimestamp() / std::to_string(appId) / cleanRel;
                copyFileIntoPlace(target, backupPath);
                LOG_NOTIFY("PrefixSync: app %u: cloud save content differs for %s\n"
                           "      previous local copy preserved at %s\n",
                           appId, cleanRel.c_str(), backupPath.c_str());
            }

            copyFileIntoPlace(it->path(), target);
            res.filesPulled++;
        }

        res.ok = true;
        return res;
    }

    static SyncResult_t pushLuaAndManifestFiles(
        uint32_t appId,
        const fs::path& appStorageDir,
        bool doPublish)
    {
        SyncResult_t res{};
        std::error_code ec;

        const auto files = enumerateAppLuaAndManifestFiles(appId);
        if (files.empty() && !doPublish)
        {
            res.ok = true;
            return res;
        }

        fs::create_directories(appStorageDir, ec);

        // 1. Root token bookkeeping (seed if not present)
        const fs::path rootTokenPath = appStorageDir / k_RootTokenFileName;
        std::string existingRoot = readTextFile(rootTokenPath);
        while (!existingRoot.empty() && (existingRoot.back() == '\n' || existingRoot.back() == '\r'))
        {
            existingRoot.pop_back();
        }
        if (existingRoot.empty())
        {
            writeTextFile(rootTokenPath, "%stplug-in%\n");
        }

        // 2. Mirror changed files into storage
        for (const auto& item : files)
        {
            const fs::path dst = appStorageDir / item.cleanPath;
            bool copyNeeded = true;
            if (fs::exists(dst, ec) && !ec)
            {
                const std::string srcSha = sha1OfFile(item.localPath);
                const std::string dstSha = sha1OfFile(dst);
                if (!srcSha.empty() && srcSha == dstSha)
                {
                    copyNeeded = false;
                }
            }
            if (copyNeeded)
            {
                copyFileIntoPlace(item.localPath, dst);
                res.filesPushed++;
            }
        }

        // 3. Update file_tokens.cloudredirect
        {
            std::map<std::string, std::string> tokens;
            std::istringstream ss(readTextFile(appStorageDir / k_FileTokensFileName));
            std::string line;
            while (std::getline(ss, line))
            {
                if (line.empty()) continue;
                const size_t tab = line.find('\t');
                if (tab == std::string::npos) continue;
                const std::string path = line.substr(0, tab);
                const std::string tok = line.substr(tab + 1);
                if (!path.empty())
                {
                    tokens[path] = tok;
                }
            }
            for (const auto& item : files)
            {
                tokens[item.cleanPath] = item.token;
            }
            std::ostringstream ftOut;
            for (const auto& [path, tok] : tokens)
            {
                ftOut << path << '\t' << tok << '\n';
            }
            writeTextFile(appStorageDir / k_FileTokensFileName, ftOut.str());
        }

        if (!doPublish)
        {
            res.ok = true;
            return res;
        }

        // 4. Bump CN and publish when doPublish is true (e.g. no emulator bucket exists)
        std::string prevCnText = readTextFile(appStorageDir / k_CnFileName);
        {
            std::string cnText = prevCnText;
            while (!cnText.empty() && (cnText.back() == '\n' || cnText.back() == '\r'))
            {
                cnText.pop_back();
            }
            long long cn = 0;
            try
            {
                size_t consumed = 0;
                if (!cnText.empty())
                {
                    cn = std::stoll(cnText, &consumed, 10);
                }
            }
            catch (...)
            {
                cn = 0;
            }
            cn++;
            if (!writeTextFile(appStorageDir / k_CnFileName, std::to_string(cn)))
            {
                res.message = "failed to write cn file";
                return res;
            }
        }

        // 5. Publish
        std::string accountErr;
        const uint32_t accountId = currentAccountId(accountErr);
        if (accountId == 0)
        {
            res.message = accountErr.empty() ? "no steam session" : accountErr;
            return res;
        }
        if (!isRemoteProviderReady())
        {
            res.ok = true;
            return res;
        }
        std::string outCliJson;
        const std::string err = runCli({"publish-full-manifest", g_config.cloudSavesProvider.get(),
                                        std::to_string(accountId), std::to_string(appId),
                                        cloudRoot().string()},
                                       outCliJson);
        if (!err.empty())
        {
            if (!prevCnText.empty()) writeTextFile(appStorageDir / k_CnFileName, prevCnText);
            res.message = "publish failed: " + err;
            return res;
        }
        if (outCliJson.find("\"success\":true") == std::string::npos &&
            outCliJson.find("\"success\": true") == std::string::npos)
        {
            if (!prevCnText.empty()) writeTextFile(appStorageDir / k_CnFileName, prevCnText);
            res.message = "publish not successful: " + outCliJson;
            return res;
        }

        res.ok = true;
        return res;
    }

    //- Job orchestration --------------------------------------------------------

    static SyncResult_t runJob(const Job& job)
    {
        SyncResult_t res{};

        if (!g_config.cloudSavesEnabled.get() || !g_config.cloudPrefixSyncEnabled.get())
        {
            res.message = "disabled";
            return res;
        }
        if (g_config.cloudSavesDisabledApps.get().contains(job.appId))
        {
            res.message = "disabled for app";
            return res;
        }
        if (killswitchActive())
        {
            res.message = "killswitch";
            return res;
        }

        std::string accountErr;
        const uint32_t accountId = currentAccountId(accountErr);
        if (accountId == 0)
        {
            res.message = accountErr.empty() ? "no steam session" : accountErr;
            return res;
        }

        const fs::path appStorageDir = storageRoot() / std::to_string(accountId) / std::to_string(job.appId);
        const bool hasLuaOrManifest = !enumerateAppLuaAndManifestFiles(job.appId).empty() ||
                                      hasStorageLuaOrManifestFiles(appStorageDir, job.appId);

        std::vector<PrefixBucket_t> buckets;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            const auto it = g_buckets.find(job.appId);
            if (it != g_buckets.end())
            {
                buckets = it->second;
            }
        }
        if (buckets.empty() && g_bucketDiscoveryEnabled)
        {
            buckets = DiscoverPrefixBuckets(Apps::getSteamLibraryPaths(), {job.appId});
            if (!buckets.empty())
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                g_buckets[job.appId] = buckets;
            }
        }
        if (buckets.empty() && !hasLuaOrManifest &&
            !(g_bucketDiscoveryEnabled && (job.mode == Mode::PullOnly || job.mode == Mode::Full)))
        {
            res.message = "no emulator bucket discovered";
            return res;
        }

        bool ok = true;

        if (job.mode == Mode::PullOnly || job.mode == Mode::Full)
        {
            std::string cliErr;
            std::string cliJson;
            if (isRemoteProviderReady())
            {
                std::set<fs::path> convertedLeaves;
                prepareStorageForPull(appStorageDir, convertedLeaves);

                cliErr = runCli({"sync-remote-app", g_config.cloudSavesProvider.get(),
                                 std::to_string(accountId), std::to_string(job.appId),
                                 cloudRoot().string()},
                                cliJson);
                cleanupConvertedStorageAfterPull(convertedLeaves);
            }
            else
            {
                cliJson = "{\"success\":true,\"mode\":\"local-only\"}";
            }

            if (!cliErr.empty())
            {
                LOG_WARN("PrefixSync: app %u pull CLI failed: %s\n", job.appId, cliErr.c_str());
                ok = false;
                res.message = "pull CLI failed: " + cliErr;
            }
            else
            {
                LOG_INFO("PrefixSync: app %u pull: %s\n", job.appId, cliJson.c_str());
                if (buckets.empty() && g_bucketDiscoveryEnabled)
                {
                    buckets = synthesizeBucketsFromStorage(job.appId, appStorageDir, Apps::getSteamLibraryPaths());
                    if (!buckets.empty())
                    {
                        std::lock_guard<std::mutex> lock(g_mutex);
                        g_buckets[job.appId] = buckets;
                    }
                }
                if (buckets.empty() && !hasLuaOrManifest && !hasStorageLuaOrManifestFiles(appStorageDir, job.appId))
                {
                    res.message = "no emulator bucket discovered";
                    return res;
                }
                for (auto& bucket : buckets)
                {
                    const SyncResult_t pullRes = pullBucket(job.appId, bucket, appStorageDir, job.runningAtEnqueue);
                    if (!pullRes.ok)
                    {
                        ok = false;
                    }
                    res.filesPulled += pullRes.filesPulled;
                    if (!pullRes.message.empty())
                    {
                        res.message = pullRes.message;
                    }
                }
                const SyncResult_t luaPullRes = pullLuaAndManifestFiles(job.appId, appStorageDir, job.runningAtEnqueue);
                if (!luaPullRes.ok)
                {
                    ok = false;
                }
                res.filesPulled += luaPullRes.filesPulled;
                if (!luaPullRes.message.empty())
                {
                    res.message = luaPullRes.message;
                }
            }
        }

        if (ok && (job.mode == Mode::PushOnly || job.mode == Mode::Full))
        {
            const SyncResult_t luaPushRes = pushLuaAndManifestFiles(job.appId, appStorageDir, buckets.empty());
            if (!luaPushRes.ok)
            {
                ok = false;
            }
            res.filesPushed += luaPushRes.filesPushed;
            if (!luaPushRes.message.empty())
            {
                res.message = luaPushRes.message;
            }

            if (ok && !buckets.empty())
            {
                for (auto& bucket : buckets)
                {
                    std::string cliJson;
                    const SyncResult_t pushRes = pushBucket(job.appId, bucket, appStorageDir, cliJson);
                    if (!pushRes.ok)
                    {
                        ok = false;
                    }
                    res.filesPushed += pushRes.filesPushed;
                    if (!pushRes.message.empty())
                    {
                        res.message = pushRes.message;
                    }
                }
            }
        }

        res.ok = ok;
        return res;
    }

    //- Worker ----------------------------------------------------------------

    static void enqueueJobLocked(const Job& job)
    {
        //Caller holds g_mutex
        for (const auto& existing : g_queue)
        {
            if (existing.appId == job.appId && existing.mode == job.mode)
            {
                return;
            }
        }
        g_queue.push_back(job);
        g_cv.notify_all();
    }

    static void enqueueJob(const Job& job)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        enqueueJobLocked(job);
    }

    static std::vector<uint32_t> consumeTriggerFile()
    {
        const fs::path trigger = stateDir().parent_path() / k_TriggerFileName;
        std::error_code ec;
        if (!fs::exists(trigger, ec) || ec)
        {
            return {};
        }
        const std::string text = readTextFile(trigger);
        fs::remove(trigger, ec);

        std::vector<uint32_t> ids;
        if (text.find_first_not_of(" \t\r\n") == std::string::npos)
        {
            //Empty file: full sync for every observed bucket
            std::lock_guard<std::mutex> lock(g_mutex);
            for (const auto& [appId, buckets] : g_buckets)
            {
                (void)buckets;
                ids.push_back(appId);
            }
            return ids;
        }
        std::istringstream ss(text);
        std::string tok;
        while (std::getline(ss, tok, ','))
        {
            while (!tok.empty() && !std::isalnum(static_cast<unsigned char>(tok.front())) && tok.front() != ',')
            {
                tok.erase(0, 1);
            }
            while (!tok.empty() && !std::isalnum(static_cast<unsigned char>(tok.back())) && tok.back() != ',')
            {
                tok.pop_back();
            }
            if (isDigits(tok))
            {
                ids.push_back(static_cast<uint32_t>(std::strtoul(tok.c_str(), nullptr, 10)));
            }
        }
        return ids;
    }

    static void refreshBucketCache()
    {
        //Worker-thread only
        if (!g_bucketDiscoveryEnabled)
        {
            return;
        }

        const std::vector<fs::path> libraries = Apps::getSteamLibraryPaths();
        std::unordered_set<uint32_t> compatIds;
        for (const auto& steamapps : libraries)
        {
            std::error_code ec;
            const fs::path compat = steamapps / "compatdata";
            if (!fs::is_directory(compat, ec) || ec)
            {
                continue;
            }
            for (const auto& entry : fs::directory_iterator(compat, fs::directory_options::skip_permission_denied, ec))
            {
                std::error_code entryEc;
                if (!entry.is_directory(entryEc) || entryEc) continue;
                const std::string name = entry.path().filename().string();
                if (isDigits(name))
                {
                    const uint32_t id = static_cast<uint32_t>(std::strtoul(name.c_str(), nullptr, 10));
                    if (id != 0)
                    {
                        compatIds.insert(id);
                    }
                }
            }
        }

        // Also scan config/stplug-in/ for <appId>.lua files
        std::error_code ec;
        const fs::path stplug = stplugDir();
        if (fs::is_directory(stplug, ec) && !ec)
        {
            for (const auto& entry : fs::directory_iterator(stplug, fs::directory_options::skip_permission_denied, ec))
            {
                std::error_code entryEc;
                if (!entry.is_regular_file(entryEc) || entryEc) continue;
                std::string fn = entry.path().filename().string();
                if (fn.ends_with(".lua"))
                {
                    std::string stem = fn.substr(0, fn.size() - 4);
                    if (isDigits(stem))
                    {
                        uint32_t id = static_cast<uint32_t>(std::strtoul(stem.c_str(), nullptr, 10));
                        if (id != 0)
                        {
                            compatIds.insert(id);
                        }
                    }
                }
            }
        }

        std::unordered_set<uint32_t> watchIds = compatIds;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_compatAppIds = compatIds;
            for (const auto& [appId, buckets] : g_buckets)
            {
                (void)buckets;
                watchIds.insert(appId);
            }
            for (uint32_t id : g_config.GetEffectiveCloudRedirectedApps())
            {
                watchIds.insert(id);
            }
            for (uint32_t disabledId : g_config.cloudSavesDisabledApps.get())
            {
                watchIds.erase(disabledId);
            }
        }
        if (watchIds.empty())
        {
            return;
        }

        std::vector<PrefixBucket_t> found = DiscoverPrefixBuckets(libraries, watchIds);
        std::unordered_map<uint32_t, std::vector<PrefixBucket_t>> newBuckets;
        for (auto& b : found)
        {
            newBuckets[b.appId].push_back(b);
        }

        bool changed = false;
        std::vector<uint32_t> allBucketAppIds;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            for (const auto& [appId, list] : newBuckets)
            {
                (void)list;
                allBucketAppIds.push_back(appId);
                if (!g_buckets.contains(appId))
                {
                    changed = true;
                }
            }
            g_buckets = std::move(newBuckets);
        }

        std::string accountErr;
        const uint32_t accountId = currentAccountId(accountErr);
        const bool sessionReady = enabled() && (accountId != 0);
        if (sessionReady && g_config.cloudPrefixSyncAtStartUp.get())
        {
            for (uint32_t id : allBucketAppIds)
            {
                if (!g_startupQueued.contains(id))
                {
                    g_startupQueued.insert(id);
                    enqueueJob(Job{id, Mode::Full, "startup", false});
                }
            }

            static std::string s_bootstrappedProvider;
            const std::string provider = g_config.cloudSavesProvider.get();
            if (!g_hasRunCliOverride && isRemoteProviderReady() && s_bootstrappedProvider != provider)
            {
                s_bootstrappedProvider = provider;
                std::string remoteJson;
                const std::string cliErr = runCli({"list-remote-app-ids", provider, std::to_string(accountId)}, remoteJson);
                if (cliErr.empty() && !remoteJson.empty())
                {
                    const size_t arrStart = remoteJson.find('[');
                    const size_t arrEnd = (arrStart == std::string::npos) ? std::string::npos : remoteJson.find(']', arrStart);
                    if (arrStart != std::string::npos && arrEnd != std::string::npos)
                    {
                        std::string numTok;
                        for (size_t i = arrStart + 1; i <= arrEnd; ++i)
                        {
                            const char c = remoteJson[i];
                            if (std::isdigit(static_cast<unsigned char>(c)))
                            {
                                numTok.push_back(c);
                            }
                            else if (!numTok.empty())
                            {
                                const uint32_t remoteId = static_cast<uint32_t>(std::strtoul(numTok.c_str(), nullptr, 10));
                                numTok.clear();
                                if (remoteId != 0 && watchIds.contains(remoteId) && !g_startupQueued.contains(remoteId))
                                {
                                    g_startupQueued.insert(remoteId);
                                    enqueueJob(Job{remoteId, Mode::PullOnly, "startup-remote", false});
                                }
                            }
                        }
                    }
                }
            }
        }
        if (changed)
        {
            LOG_INFO("PrefixSync: watching %zu app(s) with emulator buckets\n", g_buckets.size());
        }
    }

    static void processJob(const Job& job)
    {
        const SyncResult_t res = runJob(job);
        if (res.ok)
        {
            LOG_INFO("PrefixSync: %u %s: pulled %u, pushed %u (reason=%s)\n",
                     job.appId, ModeToString(job.mode), res.filesPulled, res.filesPushed, job.reason.c_str());
        }
        else
        {
            LOG_WARN("PrefixSync: %u %s: %s (reason=%s)\n",
                     job.appId, ModeToString(job.mode), res.message.c_str(), job.reason.c_str());
        }
    }

    static void workerLoop()
    {
        int tick = 0;
        while (g_running.load())
        {
            std::deque<Job> batch;
            {
                std::unique_lock<std::mutex> lock(g_mutex);
                g_cv.wait_for(lock, std::chrono::milliseconds(k_TickMs), [] {
                    return !g_queue.empty() || !g_running.load();
                });
                g_queue.swap(batch);
                if (batch.size() > 8)
                {
                    //Process in small bursts; the remainder reverts to the queue
                    g_queue.insert(g_queue.begin(), batch.begin() + 8, batch.end());
                    batch.resize(8);
                }
            }

            for (const auto& job : batch)
            {
                if (!g_running.load())
                {
                    break;
                }
                processJob(job);
            }

            if (!g_running.load())
            {
                break;
            }

            ++tick;
            if (tick == 1 ||
                (tick % k_TicksPerRediscovery) == 0 ||
                (tick <= 30 && (tick % 2) == 0 && g_startupQueued.empty()))
            {
                refreshBucketCache();
            }
            std::vector<uint32_t> triggerIds = consumeTriggerFile();
            for (uint32_t id : triggerIds)
            {
                enqueueJob(Job{id, Mode::Full, "manual trigger", false});
            }
        }
    }

    //- notifyFrame ------------------------------------------------------------

    void notifyFrame()
    {
        if (!g_running.load() || !enabled())
        {
            return;
        }
        static std::chrono::steady_clock::time_point s_lastPoll = std::chrono::steady_clock::time_point{};
        static bool s_lastPollSane = false;
        const auto now = std::chrono::steady_clock::now();
        if (s_lastPollSane &&
            std::chrono::duration_cast<std::chrono::milliseconds>(now - s_lastPoll).count() < k_EdgePollMinMs)
        {
            return;
        }
        s_lastPoll = now;
        s_lastPollSane = true;

        std::unordered_set<uint32_t> candidateIds;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            for (const auto& [appId, buckets] : g_buckets)
            {
                (void)buckets;
                candidateIds.insert(appId);
            }
            for (uint32_t id : g_compatAppIds)
            {
                candidateIds.insert(id);
            }
        }
        for (uint32_t id : g_config.addedAppIds.get())
        {
            candidateIds.insert(id);
        }
        for (uint32_t id : g_config.cloudSavesManagedApps.get())
        {
            candidateIds.insert(id);
        }
        if (candidateIds.empty())
        {
            return;
        }
        if (!g_pSteamEngine)
        {
            return;
        }
        const auto usr = g_pSteamEngine->getUser();
        if (!usr)
        {
            return;
        }
        auto appManager = usr->getAppManager();
        if (!appManager)
        {
            return;
        }

        for (uint32_t appId : candidateIds)
        {
            const uint32_t state = static_cast<uint32_t>(appManager->getAppInstallState(appId));
            const bool isRunning = (state & k_EAppStateAppRunning) != 0;
            auto it = g_lastRunning.find(appId);
            if (!g_hasInitialEdgeState || it == g_lastRunning.end())
            {
                g_lastRunning[appId] = isRunning;
                continue;
            }
            const bool wasRunning = it->second;
            if (wasRunning == isRunning)
            {
                continue;
            }
            g_lastRunning[appId] = isRunning;
            const bool launch = isRunning;
            if (launch && !g_config.cloudPrefixSyncOnLaunch.get())
            {
                continue;
            }
            if (!launch && !g_config.cloudPrefixSyncOnExit.get())
            {
                continue;
            }
            const Mode mode = launch ? Mode::PullOnly : Mode::PushOnly;
            Job job{appId, mode, launch ? "launch" : "exit", false};
            LOG_INFO("PrefixSync: app %u %s edge detected\n", appId, job.reason.c_str());
            {
                std::lock_guard<std::mutex> pushLock(g_mutex);
                enqueueJobLocked(std::move(job));
            }
        }
        g_hasInitialEdgeState = true;
    }

    //- Lifecycle ----------------------------------------------------------------

    void init()
    {
        if (g_running.load())
        {
            return;
        }
        g_running = true;
        g_workerThread = std::thread(workerLoop);
    }

    void shutdown()
    {
        if (!g_running.load())
        {
            return;
        }
        g_running = false;
        g_cv.notify_all();
        if (g_workerThread.joinable())
        {
            g_workerThread.join();
        }
    }

    bool requestSyncForApp(uint32_t appId, Mode mode, const char* reason)
    {
        if (!enabled())
        {
            return false;
        }
        enqueueJob(Job{appId, mode, reason ? reason : "requested", false});
        return true;
    }

    bool requestSyncForAllBuckets(Mode mode, const char* reason)
    {
        const std::vector<uint32_t> ids = GetWatchedAppIds();
        bool any = false;
        for (uint32_t id : ids)
        {
            any |= requestSyncForApp(id, mode, reason);
        }
        return any;
    }

    std::vector<uint32_t> GetWatchedAppIds()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        std::vector<uint32_t> ids;
        ids.reserve(g_buckets.size());
        for (const auto& [appId, buckets] : g_buckets)
        {
            (void)buckets;
            ids.push_back(appId);
        }
        return ids;
    }

    //- Test hooks -----------------------------------------------------------------

    void SetRunCliOverrideForTest(
        const std::function<std::string(const std::vector<std::string>& args, std::string& outJson)>& fn)
    {
        g_runCliOverride = fn;
        g_hasRunCliOverride = true;
    }

    void SetCloudRootForTest(const fs::path& dir)
    {
        g_overriddenCloudRoot = dir;
    }

    void SetStateDirForTest(const fs::path& dir)
    {
        g_overriddenStateDir = dir;
    }

    void SetBackupDirForTest(const fs::path& dir)
    {
        g_overriddenBackupDir = dir;
    }

    void SetAccountIdForTest(uint32_t accountId)
    {
        g_overriddenAccountId = accountId;
        g_hasOverriddenAccountId = accountId != 0;
    }

    void SetBucketDiscoverForTestEnabled(bool enabledFlag)
    {
        g_bucketDiscoveryEnabled = enabledFlag;
    }

    void SetBucketsForTest(const std::vector<PrefixBucket_t>& buckets)
    {
        std::unordered_map<uint32_t, std::vector<PrefixBucket_t>> map;
        for (auto b : buckets)
        {
            map[b.appId].push_back(b);
        }
        std::lock_guard<std::mutex> lock(g_mutex);
        g_buckets = std::move(map);
    }

    void SetSteamRootForTest(const std::filesystem::path& dir)
    {
        g_overriddenSteamRoot = dir;
    }

    SyncResult_t RunJobForTest(uint32_t appId, Mode mode, const char* reason)
    {
        return runJob(Job{appId, mode, reason ? reason : "test", false});
    }

    void ResetForTest()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_queue.clear();
        g_buckets.clear();
        g_compatAppIds.clear();
        g_lastRunning.clear();
        g_startupQueued.clear();
        g_hasInitialEdgeState = false;
        g_hasRunCliOverride = false;
        g_runCliOverride = nullptr;
        g_overriddenCloudRoot.clear();
        g_overriddenStateDir.clear();
        g_overriddenBackupDir.clear();
        g_overriddenSteamRoot.clear();
        g_overriddenAccountId = 0;
        g_hasOverriddenAccountId = false;
        g_bucketDiscoveryEnabled = true;
    }
}
