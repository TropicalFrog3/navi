#include "cloud_view.hpp"
#include "cloud_manager.hpp"
#include "cloud_pending_ops.hpp"

#include <sstream>
#include <iomanip>
#include <cctype>

namespace Navi::CloudView
{
    static std::string escapeJsonString(const std::string& input)
    {
        std::ostringstream ss;
        for (char c : input)
        {
            switch (c)
            {
                case '"': ss << "\\\""; break;
                case '\\': ss << "\\\\"; break;
                case '\b': ss << "\\b"; break;
                case '\f': ss << "\\f"; break;
                case '\n': ss << "\\n"; break;
                case '\r': ss << "\\r"; break;
                case '\t': ss << "\\t"; break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20)
                    {
                        ss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
                    }
                    else
                    {
                        ss << c;
                    }
                    break;
            }
        }
        return ss.str();
    }

    std::string renderStatusJson(bool enabled, const std::string& provider, size_t managedAppsCount, size_t pendingOpsCount)
    {
        std::ostringstream ss;
        ss << "{\n"
           << "  \"enabled\": " << (enabled ? "true" : "false") << ",\n"
           << "  \"provider\": \"" << escapeJsonString(provider) << "\",\n"
           << "  \"managedAppsCount\": " << managedAppsCount << ",\n"
           << "  \"pendingOpsCount\": " << pendingOpsCount << "\n"
           << "}";
        return ss.str();
    }

    std::string renderProviderStatusJson(const CloudRedirect::ProviderAuthStatus_t& status)
    {
        std::ostringstream ss;
        ss << "{\n"
           << "  \"provider\": \"" << escapeJsonString(status.provider) << "\",\n"
           << "  \"authenticated\": " << (status.authenticated ? "true" : "false") << ",\n"
           << "  \"accountEmail\": \"" << escapeJsonString(status.accountEmail) << "\",\n"
           << "  \"raw\": " << (status.rawJson.empty() ? "{}" : status.rawJson) << "\n"
           << "}";
        return ss.str();
    }

    std::string renderAppStatusJson(AppId_t appId, bool redirected, uint64_t storageBytes, const std::vector<std::string>& backups)
    {
        std::ostringstream ss;
        ss << "{\n"
           << "  \"appId\": " << appId << ",\n"
           << "  \"redirected\": " << (redirected ? "true" : "false") << ",\n"
           << "  \"storageUsageBytes\": " << storageBytes << ",\n"
           << "  \"backups\": [";
        for (size_t i = 0; i < backups.size(); ++i)
        {
            if (i > 0) ss << ", ";
            ss << "\"" << escapeJsonString(backups[i]) << "\"";
        }
        ss << "]\n}";
        return ss.str();
    }

    std::string renderCloudAppsJson(const std::string& provider, const std::vector<CloudRedirect::CloudAppSummary_t>& apps)
    {
        std::ostringstream ss;
        ss << "{\n"
           << "  \"provider\": \"" << escapeJsonString(provider) << "\",\n"
           << "  \"apps\": [\n";
        for (size_t i = 0; i < apps.size(); ++i)
        {
            const auto& a = apps[i];
            if (i > 0) ss << ",\n";
            ss << "    {\n"
               << "      \"appId\": " << a.appId << ",\n"
               << "      \"gameTitle\": \"" << escapeJsonString(a.gameTitle) << "\",\n"
               << "      \"fileCount\": " << a.fileCount << ",\n"
               << "      \"totalSizeBytes\": " << a.totalSizeBytes << ",\n"
               << "      \"redirected\": " << (a.redirected ? "true" : "false") << ",\n"
               << "      \"inRemoteCloud\": " << (a.inRemoteCloud ? "true" : "false") << ",\n"
               << "      \"inLocalMirror\": " << (a.inLocalMirror ? "true" : "false") << ",\n"
               << "      \"childAppIds\": [";
            for (size_t j = 0; j < a.childAppIds.size(); ++j)
            {
                if (j > 0) ss << ", ";
                ss << a.childAppIds[j];
            }
            ss << "]\n"
               << "    }";
        }
        ss << "\n  ]\n}";
        return ss.str();
    }

    std::string renderAppCloudFilesJson(AppId_t appId,
                                        const std::string& gameTitle,
                                        const std::string& provider,
                                        bool redirected,
                                        const std::vector<CloudRedirect::CloudFileEntry_t>& files)
    {
        uint64_t totalSize = 0;
        for (const auto& f : files)
        {
            totalSize += f.sizeBytes;
        }

        std::ostringstream ss;
        ss << "{\n"
           << "  \"appId\": " << appId << ",\n"
           << "  \"gameTitle\": \"" << escapeJsonString(gameTitle) << "\",\n"
           << "  \"provider\": \"" << escapeJsonString(provider) << "\",\n"
           << "  \"redirected\": " << (redirected ? "true" : "false") << ",\n"
           << "  \"fileCount\": " << files.size() << ",\n"
           << "  \"totalSizeBytes\": " << totalSize << ",\n"
           << "  \"files\": [\n";
        for (size_t i = 0; i < files.size(); ++i)
        {
            const auto& f = files[i];
            if (i > 0) ss << ",\n";
            ss << "    {\n"
               << "      \"folder\": \"" << escapeJsonString(f.folder) << "\",\n"
               << "      \"fileName\": \"" << escapeJsonString(f.fileName) << "\",\n"
               << "      \"sizeBytes\": " << f.sizeBytes << ",\n"
               << "      \"modifiedTime\": " << f.modifiedTime << ",\n"
               << "      \"timestamp\": " << f.modifiedTime << ",\n"
               << "      \"inRemoteCloud\": " << (f.inRemoteCloud ? "true" : "false") << ",\n"
               << "      \"inLocalMirror\": " << (f.inLocalMirror ? "true" : "false") << "\n"
               << "    }";
        }
        ss << "\n  ]\n}";
        return ss.str();
    }

    std::string renderAppToggleJson(AppId_t appId, bool redirected)
    {
        std::ostringstream ss;
        ss << "{\n"
           << "  \"appId\": " << appId << ",\n"
           << "  \"redirected\": " << (redirected ? "true" : "false") << "\n"
           << "}";
        return ss.str();
    }

    std::string renderBackupSuccessJson(AppId_t appId, const std::string& backupPath)
    {
        std::ostringstream ss;
        ss << "{\n"
           << "  \"success\": true,\n"
           << "  \"appId\": " << appId << ",\n"
           << "  \"backupPath\": \"" << escapeJsonString(backupPath) << "\"\n"
           << "}";
        return ss.str();
    }

    std::string renderPendingOperationAcceptedJson(const std::string& opId)
    {
        std::ostringstream ss;
        ss << "{\n"
           << "  \"status\": \"pending\",\n"
           << "  \"restartRequired\": true,\n"
           << "  \"operationId\": \"" << escapeJsonString(opId) << "\"\n"
           << "}";
        return ss.str();
    }

    std::string renderPendingOpsJson(const std::vector<CloudRedirect::PendingOperation_t>& ops)
    {
        std::ostringstream ss;
        bool hasPending = false;
        ss << "{\n"
           << "  \"operations\": [\n";
        for (size_t i = 0; i < ops.size(); ++i)
        {
            const auto& op = ops[i];
            if (op.status == CloudRedirect::PendingOpStatus::Pending)
            {
                hasPending = true;
            }
            if (i > 0) ss << ",\n";
            ss << "    {\n"
               << "      \"id\": \"" << escapeJsonString(op.id) << "\",\n"
               << "      \"appId\": " << op.appId << ",\n"
               << "      \"accountId\": " << op.accountId << ",\n"
               << "      \"type\": \"" << CloudRedirect::PendingOpTypeToString(op.type) << "\",\n"
               << "      \"status\": \"" << CloudRedirect::PendingOpStatusToString(op.status) << "\",\n"
               << "      \"timestamp\": " << op.timestamp << ",\n"
               << "      \"backupPath\": \"" << escapeJsonString(op.backupPath.string()) << "\",\n"
               << "      \"restoreSourcePath\": \"" << escapeJsonString(op.restoreSourcePath.string()) << "\",\n"
               << "      \"retries\": " << op.retries << ",\n"
               << "      \"lastError\": \"" << escapeJsonString(op.lastError) << "\"\n"
               << "    }";
        }
        ss << "\n  ],\n"
           << "  \"hasPending\": " << (hasPending ? "true" : "false") << "\n"
           << "}";
        return ss.str();
    }

    std::string renderSuccessJson(const std::string& message)
    {
        std::ostringstream ss;
        ss << "{\n"
           << "  \"success\": true";
        if (!message.empty())
        {
            ss << ",\n  \"message\": \"" << escapeJsonString(message) << "\"";
        }
        ss << "\n}";
        return ss.str();
    }

    std::string renderErrorJson(const std::string& message)
    {
        std::ostringstream ss;
        ss << "{\n"
           << "  \"error\": \"" << escapeJsonString(message) << "\"\n"
           << "}";
        return ss.str();
    }

    bool parseBoolField(const std::string& json, const std::string& key, bool defaultValue)
    {
        std::string needle = "\"" + key + "\"";
        size_t pos = json.find(needle);
        if (pos == std::string::npos) return defaultValue;
        pos = json.find(':', pos + needle.size());
        if (pos == std::string::npos) return defaultValue;
        ++pos;
        while (pos < json.size() && (std::isspace(json[pos]) || json[pos] == '"')) ++pos;
        if (json.compare(pos, 4, "true") == 0) return true;
        if (json.compare(pos, 5, "false") == 0) return false;
        return defaultValue;
    }

    std::string parseStringField(const std::string& json, const std::string& key)
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

    bool parseConfirmReset(const std::string& json)
    {
        return parseStringField(json, "confirm") == "RESET";
    }
}

