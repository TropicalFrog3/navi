#include "cloud_pending_ops.hpp"
#include "cloud_manager.hpp"
#include "cloudredirect.hpp"
#include "atomic_file.hpp"
#include "log.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace Navi::CloudRedirect
{

std::string PendingOpTypeToString(PendingOpType type)
{
    switch (type)
    {
        case PendingOpType::ResetCloudSave: return "RESET";
        case PendingOpType::RestoreBackup:   return "RESTORE";
        default: return "RESET";
    }
}

PendingOpType StringToPendingOpType(const std::string& str)
{
    if (str == "RESTORE") return PendingOpType::RestoreBackup;
    return PendingOpType::ResetCloudSave;
}

std::string PendingOpStatusToString(PendingOpStatus status)
{
    switch (status)
    {
        case PendingOpStatus::Pending:    return "PENDING";
        case PendingOpStatus::InProgress: return "IN_PROGRESS";
        case PendingOpStatus::Completed:  return "COMPLETED";
        case PendingOpStatus::Failed:     return "FAILED";
        case PendingOpStatus::RolledBack: return "ROLLED_BACK";
        default: return "PENDING";
    }
}

PendingOpStatus StringToPendingOpStatus(const std::string& str)
{
    if (str == "IN_PROGRESS") return PendingOpStatus::InProgress;
    if (str == "COMPLETED")   return PendingOpStatus::Completed;
    if (str == "FAILED")      return PendingOpStatus::Failed;
    if (str == "ROLLED_BACK") return PendingOpStatus::RolledBack;
    return PendingOpStatus::Pending;
}

static std::string jsonEscape(const std::string& str)
{
    std::string out;
    for (char c : str)
    {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else out += c;
    }
    return out;
}

static std::string jsonExtractString(const std::string& json, const std::string& key)
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

static uint64_t jsonExtractUint64(const std::string& json, const std::string& key)
{
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return 0;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return 0;
    ++pos;
    while (pos < json.size() && (std::isspace(json[pos]) || json[pos] == '"')) ++pos;
    try {
        return std::stoull(json.substr(pos));
    } catch (...) {
        return 0;
    }
}

static std::vector<std::string> jsonSplitObjectArray(const std::string& json, const std::string& arrayKey)
{
    std::vector<std::string> items;
    std::string needle = "\"" + arrayKey + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return items;
    pos = json.find('[', pos + needle.size());
    if (pos == std::string::npos) return items;

    int depth = 0;
    size_t objStart = 0;
    bool inObj = false;

    for (size_t i = pos + 1; i < json.size(); ++i)
    {
        if (json[i] == ']' && depth == 0) break;
        if (json[i] == '{')
        {
            if (depth == 0) { objStart = i; inObj = true; }
            depth++;
        }
        else if (json[i] == '}')
        {
            depth--;
            if (depth == 0 && inObj)
            {
                items.push_back(json.substr(objStart, i - objStart + 1));
                inObj = false;
            }
        }
    }
    return items;
}

CPendingOpsQueue& CPendingOpsQueue::Instance()
{
    static CPendingOpsQueue instance;
    return instance;
}

fs::path CPendingOpsQueue::GetQueueFilePath() const
{
    if (!m_testQueuePath.empty())
    {
        return m_testQueuePath;
    }
    return CCloudRedirectBridge::Instance().GetPaths().crPendingOpsFile;
}

void CPendingOpsQueue::SetQueueFilePathForTest(const fs::path& path)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_testQueuePath = path;
}

void CPendingOpsQueue::ClearQueueForTest()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    fs::path p = GetQueueFilePath();
    std::error_code ec;
    fs::remove(p, ec);
}

std::vector<PendingOperation_t> CPendingOpsQueue::LoadAll() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    fs::path p = GetQueueFilePath();
    std::vector<PendingOperation_t> result;

    std::error_code ec;
    if (!fs::exists(p, ec))
    {
        return result;
    }

    std::ifstream ifs(p);
    if (!ifs.is_open())
    {
        return result;
    }

    std::stringstream buffer;
    buffer << ifs.rdbuf();
    std::string content = buffer.str();

    auto rawOps = jsonSplitObjectArray(content, "operations");
    for (const auto& raw : rawOps)
    {
        PendingOperation_t op;
        op.id = jsonExtractString(raw, "id");
        op.appId = static_cast<AppId_t>(jsonExtractUint64(raw, "appId"));
        op.accountId = static_cast<uint32_t>(jsonExtractUint64(raw, "accountId"));
        op.type = StringToPendingOpType(jsonExtractString(raw, "type"));
        op.status = StringToPendingOpStatus(jsonExtractString(raw, "status"));
        op.timestamp = jsonExtractUint64(raw, "timestamp");
        op.backupPath = jsonExtractString(raw, "backupPath");
        op.restoreSourcePath = jsonExtractString(raw, "restoreSourcePath");
        op.retries = static_cast<uint32_t>(jsonExtractUint64(raw, "retries"));
        op.lastError = jsonExtractString(raw, "lastError");

        if (op.appId > 0 && !op.id.empty())
        {
            result.push_back(std::move(op));
        }
    }

    return result;
}

static bool WriteOpsAtomic(const fs::path& filePath, const std::vector<PendingOperation_t>& ops)
{
    std::ostringstream ss;
    ss << "{\n"
       << "  \"version\": 1,\n"
       << "  \"operations\": [\n";

    for (size_t i = 0; i < ops.size(); ++i)
    {
        const auto& op = ops[i];
        if (i > 0) ss << ",\n";
        ss << "    {\n"
           << "      \"id\": \"" << jsonEscape(op.id) << "\",\n"
           << "      \"appId\": " << op.appId << ",\n"
           << "      \"accountId\": " << op.accountId << ",\n"
           << "      \"type\": \"" << PendingOpTypeToString(op.type) << "\",\n"
           << "      \"status\": \"" << PendingOpStatusToString(op.status) << "\",\n"
           << "      \"timestamp\": " << op.timestamp << ",\n"
           << "      \"backupPath\": \"" << jsonEscape(op.backupPath.string()) << "\",\n"
           << "      \"restoreSourcePath\": \"" << jsonEscape(op.restoreSourcePath.string()) << "\",\n"
           << "      \"retries\": " << op.retries << ",\n"
           << "      \"lastError\": \"" << jsonEscape(op.lastError) << "\"\n"
           << "    }";
    }

    ss << "\n  ]\n}\n";
    std::string payload = ss.str();

    std::error_code ec;
    fs::create_directories(filePath.parent_path(), ec);

    fs::path tmpPath = filePath;
    tmpPath += ".tmp." + std::to_string(getpid()) + "_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());

    std::ofstream ofs(tmpPath, std::ios::trunc | std::ios::binary);
    if (!ofs.is_open())
    {
        return false;
    }
    ofs.write(payload.data(), payload.size());
    ofs.flush();
    ofs.close();

    chmod(tmpPath.c_str(), 0600);

    fs::rename(tmpPath, filePath, ec);
    if (ec)
    {
        fs::remove(tmpPath, ec);
        return false;
    }
    chmod(filePath.c_str(), 0600);
    return true;
}

bool CPendingOpsQueue::Enqueue(const PendingOperation_t& op)
{
    if (op.appId == 0)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    fs::path p = GetQueueFilePath();

    // Load existing ops directly without double-locking
    std::vector<PendingOperation_t> ops;
    std::error_code ec;
    if (fs::exists(p, ec))
    {
        std::ifstream ifs(p);
        if (ifs.is_open())
        {
            std::stringstream buffer;
            buffer << ifs.rdbuf();
            auto rawOps = jsonSplitObjectArray(buffer.str(), "operations");
            for (const auto& raw : rawOps)
            {
                PendingOperation_t item;
                item.id = jsonExtractString(raw, "id");
                item.appId = static_cast<AppId_t>(jsonExtractUint64(raw, "appId"));
                item.accountId = static_cast<uint32_t>(jsonExtractUint64(raw, "accountId"));
                item.type = StringToPendingOpType(jsonExtractString(raw, "type"));
                item.status = StringToPendingOpStatus(jsonExtractString(raw, "status"));
                item.timestamp = jsonExtractUint64(raw, "timestamp");
                item.backupPath = jsonExtractString(raw, "backupPath");
                item.restoreSourcePath = jsonExtractString(raw, "restoreSourcePath");
                item.retries = static_cast<uint32_t>(jsonExtractUint64(raw, "retries"));
                item.lastError = jsonExtractString(raw, "lastError");
                if (item.appId > 0 && !item.id.empty())
                {
                    ops.push_back(std::move(item));
                }
            }
        }
    }

    PendingOperation_t newOp = op;
    if (newOp.timestamp == 0)
    {
        newOp.timestamp = static_cast<uint64_t>(std::time(nullptr));
    }
    if (newOp.id.empty())
    {
        newOp.id = "op_" + std::to_string(newOp.timestamp) + "_" + std::to_string(newOp.appId) + "_" +
                   (newOp.type == PendingOpType::ResetCloudSave ? "reset" : "restore");
    }

    bool updated = false;
    for (auto& existing : ops)
    {
        if (existing.id == newOp.id)
        {
            existing = newOp;
            updated = true;
            break;
        }
    }
    if (!updated)
    {
        ops.push_back(newOp);
    }

    return WriteOpsAtomic(p, ops);
}

bool CPendingOpsQueue::HasPendingOperations() const
{
    auto ops = LoadAll();
    for (const auto& op : ops)
    {
        if (op.status == PendingOpStatus::Pending)
        {
            return true;
        }
    }
    return false;
}

bool CPendingOpsQueue::UpdateStatus(const std::string& opId, PendingOpStatus status, const std::string& error)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    fs::path p = GetQueueFilePath();

    std::vector<PendingOperation_t> ops;
    std::error_code ec;
    if (fs::exists(p, ec))
    {
        std::ifstream ifs(p);
        if (ifs.is_open())
        {
            std::stringstream buffer;
            buffer << ifs.rdbuf();
            auto rawOps = jsonSplitObjectArray(buffer.str(), "operations");
            for (const auto& raw : rawOps)
            {
                PendingOperation_t item;
                item.id = jsonExtractString(raw, "id");
                item.appId = static_cast<AppId_t>(jsonExtractUint64(raw, "appId"));
                item.accountId = static_cast<uint32_t>(jsonExtractUint64(raw, "accountId"));
                item.type = StringToPendingOpType(jsonExtractString(raw, "type"));
                item.status = StringToPendingOpStatus(jsonExtractString(raw, "status"));
                item.timestamp = jsonExtractUint64(raw, "timestamp");
                item.backupPath = jsonExtractString(raw, "backupPath");
                item.restoreSourcePath = jsonExtractString(raw, "restoreSourcePath");
                item.retries = static_cast<uint32_t>(jsonExtractUint64(raw, "retries"));
                item.lastError = jsonExtractString(raw, "lastError");
                if (item.appId > 0 && !item.id.empty())
                {
                    ops.push_back(std::move(item));
                }
            }
        }
    }

    bool found = false;
    for (auto& item : ops)
    {
        if (item.id == opId)
        {
            item.status = status;
            if (!error.empty())
            {
                item.lastError = error;
            }
            found = true;
            break;
        }
    }

    if (!found) return false;
    return WriteOpsAtomic(p, ops);
}

bool CPendingOpsQueue::PruneCompleted(uint64_t maxAgeSeconds)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    fs::path p = GetQueueFilePath();

    std::vector<PendingOperation_t> ops;
    std::error_code ec;
    if (!fs::exists(p, ec)) return true;

    std::ifstream ifs(p);
    if (!ifs.is_open()) return false;

    std::stringstream buffer;
    buffer << ifs.rdbuf();
    auto rawOps = jsonSplitObjectArray(buffer.str(), "operations");
    uint64_t now = static_cast<uint64_t>(std::time(nullptr));

    for (const auto& raw : rawOps)
    {
        PendingOperation_t item;
        item.id = jsonExtractString(raw, "id");
        item.appId = static_cast<AppId_t>(jsonExtractUint64(raw, "appId"));
        item.accountId = static_cast<uint32_t>(jsonExtractUint64(raw, "accountId"));
        item.type = StringToPendingOpType(jsonExtractString(raw, "type"));
        item.status = StringToPendingOpStatus(jsonExtractString(raw, "status"));
        item.timestamp = jsonExtractUint64(raw, "timestamp");
        item.backupPath = jsonExtractString(raw, "backupPath");
        item.restoreSourcePath = jsonExtractString(raw, "restoreSourcePath");
        item.retries = static_cast<uint32_t>(jsonExtractUint64(raw, "retries"));
        item.lastError = jsonExtractString(raw, "lastError");

        if (item.status == PendingOpStatus::Completed || item.status == PendingOpStatus::RolledBack)
        {
            if (now >= item.timestamp && (now - item.timestamp) > maxAgeSeconds)
            {
                continue; // Prune
            }
        }
        ops.push_back(std::move(item));
    }

    return WriteOpsAtomic(p, ops);
}

// ─────────────────────────────────────────────────────────────────────────────
// CPendingOpsExecutor Implementation
// ─────────────────────────────────────────────────────────────────────────────

CPendingOpsExecutor::CPendingOpsExecutor(fs::path steamRoot)
    : m_steamRoot(std::move(steamRoot))
{
    if (m_steamRoot.empty())
    {
        m_steamRoot = CCloudRedirectBridge::Instance().GetPaths().steamRoot;
    }
}

void CPendingOpsExecutor::SetProcPathForTest(const fs::path& path)
{
    m_testProcPath = path;
}

void CPendingOpsExecutor::SetBackupDirForTest(const fs::path& path)
{
    m_testBackupDir = path;
}

void CPendingOpsExecutor::SetSimulateFailureForTest(bool simFail)
{
    m_simulateFailure = simFail;
}

bool CPendingOpsExecutor::AssertSteamIdle() const
{
    fs::path procRoot = m_testProcPath.empty() ? fs::path("/proc") : m_testProcPath;
    std::error_code ec;

    if (!fs::exists(procRoot, ec))
    {
        return true;
    }

    for (const auto& entry : fs::directory_iterator(procRoot, ec))
    {
        if (!entry.is_directory(ec)) continue;

        std::string filename = entry.path().filename().string();
        if (filename.empty() || !std::all_of(filename.begin(), filename.end(), ::isdigit))
        {
            continue; // Not a PID
        }

        // Check comm
        fs::path commPath = entry.path() / "comm";
        std::ifstream commFile(commPath);
        if (commFile.is_open())
        {
            std::string comm;
            std::getline(commFile, comm);
            while (!comm.empty() && (comm.back() == '\r' || comm.back() == '\n' || comm.back() == ' '))
            {
                comm.pop_back();
            }
            if (comm == "steam" || comm == "steamwebhelper")
            {
                return false;
            }
        }
    }

    return true;
}

static bool RunCommand(const std::vector<std::string>& args)
{
    if (args.empty()) return false;

    pid_t pid = fork();
    if (pid == 0)
    {
        std::vector<char*> c_args;
        for (const auto& arg : args)
        {
            c_args.push_back(const_cast<char*>(arg.c_str()));
        }
        c_args.push_back(nullptr);

        execvp(c_args[0], c_args.data());
        _exit(127);
    }
    else if (pid > 0)
    {
        int status = 0;
        waitpid(pid, &status, 0);
        return WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }
    return false;
}

bool CPendingOpsExecutor::CreatePreOpBackup(const PendingOperation_t& op, fs::path& outBackupFile)
{
    if (outBackupFile.empty())
    {
        fs::path backupBase = m_testBackupDir.empty() ? CCloudRedirectBridge::Instance().GetPaths().naviBackupDir : m_testBackupDir;
        outBackupFile = backupBase / std::to_string(op.appId) /
                        ("pre-" + (op.type == PendingOpType::ResetCloudSave ? std::string("reset") : std::string("restore")) +
                         "-" + std::to_string(op.timestamp > 0 ? op.timestamp : static_cast<uint64_t>(std::time(nullptr))) + ".tar.gz");
    }

    std::error_code ec;
    fs::create_directories(outBackupFile.parent_path(), ec);

    std::vector<std::string> tarArgs = {"tar", "-czf", outBackupFile.string(), "-P"};
    bool hasFiles = false;

    // Collect paths to archive
    std::vector<fs::path> targetPaths;

    std::vector<std::string> accts;
    if (op.accountId != 0)
    {
        accts.push_back(std::to_string(op.accountId));
    }
    else
    {
        fs::path userdataRoot = m_steamRoot / "userdata";
        if (fs::exists(userdataRoot, ec))
        {
            for (const auto& e : fs::directory_iterator(userdataRoot, ec))
            {
                if (e.is_directory(ec))
                {
                    accts.push_back(e.path().filename().string());
                }
            }
        }
    }

    for (const auto& acct : accts)
    {
        fs::path crStorageApp = m_steamRoot / "cloud_redirect" / "storage" / acct / std::to_string(op.appId);
        if (fs::exists(crStorageApp, ec))
        {
            targetPaths.push_back(crStorageApp);
        }

        fs::path remoteDir = m_steamRoot / "userdata" / acct / std::to_string(op.appId) / "remote";
        if (fs::exists(remoteDir, ec))
        {
            targetPaths.push_back(remoteDir);
        }

        fs::path remotecache = m_steamRoot / "userdata" / acct / std::to_string(op.appId) / "remotecache.vdf";
        if (fs::exists(remotecache, ec))
        {
            targetPaths.push_back(remotecache);
        }
    }

    // Also include Goldberg emulator save directories — these bypass the
    // CUserRemoteStorage hook and are stored outside userdata/remote/.
    for (const auto& goldbergDir : CCloudManager::Instance().ResolveGoldbergSavePaths(op.appId))
    {
        if (fs::exists(goldbergDir, ec) && fs::is_directory(goldbergDir, ec))
        {
            targetPaths.push_back(goldbergDir);
        }
    }

    for (const auto& tp : targetPaths)
    {
        tarArgs.push_back(tp.string());
        hasFiles = true;
    }

    if (!hasFiles)
    {
        // Create an empty tarball if no files exist currently
        tarArgs.push_back("--files-from");
        tarArgs.push_back("/dev/null");
    }

    return RunCommand(tarArgs);
}

bool CPendingOpsExecutor::PerformGranularReset(const PendingOperation_t& op)
{
    std::error_code ec;

    std::vector<std::string> accts;
    if (op.accountId != 0)
    {
        accts.push_back(std::to_string(op.accountId));
    }
    else
    {
        fs::path userdataRoot = m_steamRoot / "userdata";
        if (fs::exists(userdataRoot, ec))
        {
            for (const auto& e : fs::directory_iterator(userdataRoot, ec))
            {
                if (e.is_directory(ec))
                {
                    accts.push_back(e.path().filename().string());
                }
            }
        }
    }

    for (const auto& acct : accts)
    {
        fs::path appUserdata = m_steamRoot / "userdata" / acct / std::to_string(op.appId);
        fs::path remoteDir = appUserdata / "remote";
        fs::path remotecache = appUserdata / "remotecache.vdf";

        // Granular reset invariant: NEVER touch localconfig.vdf or gamerecordings/
        fs::path localconfig = appUserdata / "localconfig.vdf";
        fs::path gamerecordings = appUserdata / "gamerecordings";

        bool localconfigExisted = fs::exists(localconfig, ec);
        bool gamerecordingsExisted = fs::exists(gamerecordings, ec);

        // Wipe scoped remote/ and remotecache.vdf
        if (fs::exists(remoteDir, ec))
        {
            fs::remove_all(remoteDir, ec);
        }
        if (fs::exists(remotecache, ec))
        {
            fs::remove(remotecache, ec);
        }

        // Verify protected targets were not removed
        if (localconfigExisted && !fs::exists(localconfig, ec))
        {
            return false;
        }
        if (gamerecordingsExisted && !fs::exists(gamerecordings, ec))
        {
            return false;
        }

        // Wipe redirect storage
        fs::path storageApp = m_steamRoot / "cloud_redirect" / "storage" / acct / std::to_string(op.appId);
        if (fs::exists(storageApp, ec))
        {
            fs::remove_all(storageApp, ec);
        }

        // Reset cn.dat -> 0
        fs::create_directories(storageApp, ec);
        fs::path cnFile = storageApp / "cn.dat";
        std::ofstream ofs(cnFile, std::ios::trunc);
        if (ofs.is_open())
        {
            ofs << "0\n";
            ofs.close();
        }

        // Also if storage root has cn.dat, ensure reset
        fs::path storageRootCn = m_steamRoot / "cloud_redirect" / "storage" / "cn.dat";
        if (fs::exists(storageRootCn, ec))
        {
            std::ofstream rootOfs(storageRootCn, std::ios::trunc);
            if (rootOfs.is_open())
            {
                rootOfs << "0\n";
                rootOfs.close();
            }
        }
    }

    // Wipe Goldberg emulator save directories.
    // These bypass the CUserRemoteStorage hook, so we must clean them
    // explicitly during a reset to match the invariant that all cloud-redirected
    // save state has been cleared.
    for (const auto& goldbergDir : CCloudManager::Instance().ResolveGoldbergSavePaths(op.appId))
    {
        if (fs::exists(goldbergDir, ec) && fs::is_directory(goldbergDir, ec))
        {
            fs::remove_all(goldbergDir, ec);
        }
    }

    return true;
}

bool CPendingOpsExecutor::PerformRestore(const PendingOperation_t& op)
{
    std::error_code ec;
    if (op.restoreSourcePath.empty() || !fs::exists(op.restoreSourcePath, ec))
    {
        return false;
    }

    std::vector<std::string> untarArgs = {"tar", "-xzf", op.restoreSourcePath.string(), "-P"};
    return RunCommand(untarArgs);
}

bool CPendingOpsExecutor::RollbackFromBackup(const PendingOperation_t& op)
{
    std::error_code ec;
    if (op.backupPath.empty() || !fs::exists(op.backupPath, ec))
    {
        return false;
    }

    std::vector<std::string> untarArgs = {"tar", "-xzf", op.backupPath.string(), "-P"};
    return RunCommand(untarArgs);
}

bool CPendingOpsExecutor::ExecuteOperation(PendingOperation_t& op)
{
    CPendingOpsQueue::Instance().UpdateStatus(op.id, PendingOpStatus::InProgress);

    if (m_simulateFailure)
    {
        CreatePreOpBackup(op, op.backupPath);
        RollbackFromBackup(op);
        op.status = PendingOpStatus::RolledBack;
        op.lastError = "Simulated failure";
        CPendingOpsQueue::Instance().UpdateStatus(op.id, PendingOpStatus::RolledBack, op.lastError);
        return false;
    }

    fs::path backupFile;
    if (!CreatePreOpBackup(op, backupFile))
    {
        op.status = PendingOpStatus::Failed;
        op.lastError = "Failed to create pre-operation backup tarball";
        CPendingOpsQueue::Instance().UpdateStatus(op.id, PendingOpStatus::Failed, op.lastError);
        return false;
    }
    op.backupPath = backupFile;

    bool success = false;
    if (op.type == PendingOpType::ResetCloudSave)
    {
        success = PerformGranularReset(op);
    }
    else if (op.type == PendingOpType::RestoreBackup)
    {
        success = PerformRestore(op);
    }

    if (!success)
    {
        RollbackFromBackup(op);
        op.status = PendingOpStatus::RolledBack;
        op.lastError = "Operation failed during execution; rolled back to pre-op backup";
        CPendingOpsQueue::Instance().UpdateStatus(op.id, PendingOpStatus::RolledBack, op.lastError);
        return false;
    }

    op.status = PendingOpStatus::Completed;
    CPendingOpsQueue::Instance().UpdateStatus(op.id, PendingOpStatus::Completed);
    return true;
}

int CPendingOpsExecutor::ExecuteAllPending()
{
    if (!AssertSteamIdle())
    {
        return 2; // Steam is running
    }

    auto ops = CPendingOpsQueue::Instance().LoadAll();
    bool anyFailed = false;

    for (auto& op : ops)
    {
        if (op.status == PendingOpStatus::Pending)
        {
            if (!ExecuteOperation(op))
            {
                anyFailed = true;
            }
        }
    }

    return anyFailed ? 1 : 0;
}

} // namespace Navi::CloudRedirect

