/**
 * @file  cloud_view.cpp
 * @brief Presenter/view adapter rendering CloudRedirect UI into Steam CEF
 */
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
        std::string out;
        bool esc = false;
        for (size_t i = pos; i < json.size(); ++i)
        {
            char c = json[i];
            if (esc)
            {
                switch (c)
                {
                    case '"':  out += '"'; break;
                    case '\\': out += '\\'; break;
                    case 'n':  out += '\n'; break;
                    case 'r':  out += '\r'; break;
                    case 't':  out += '\t'; break;
                    default:   out += c; break;
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

    uint32_t parseUint32Field(const std::string& json, const std::string& key, uint32_t defaultValue)
    {
        std::string needle = "\"" + key + "\"";
        size_t pos = json.find(needle);
        if (pos == std::string::npos) return defaultValue;
        pos = json.find(':', pos + needle.size());
        if (pos == std::string::npos) return defaultValue;
        ++pos;
        while (pos < json.size() && (std::isspace(static_cast<unsigned char>(json[pos])) || json[pos] == '"')) ++pos;
        std::string digits;
        while (pos < json.size() && std::isdigit(static_cast<unsigned char>(json[pos])))
        {
            digits.push_back(json[pos]);
            ++pos;
        }
        if (digits.empty()) return defaultValue;
        try
        {
            return static_cast<uint32_t>(std::stoul(digits));
        }
        catch (...)
        {
            return defaultValue;
        }
    }

    std::string renderProviderCredentialsJson(const CloudRedirect::ProviderCredentials_t& creds)
    {
        std::ostringstream ss;
        ss << "{\n"
           << "  \"success\": true,\n"
           << "  \"authenticated\": " << (creds.configured ? "true" : "false") << ",\n"
           << "  \"provider\": \"" << escapeJsonString(creds.provider) << "\",\n"
           << "  \"configured\": " << (creds.configured ? "true" : "false") << ",\n"
           << "  \"accountId\": \"" << escapeJsonString(creds.accountId) << "\",\n"
           << "  \"account_id\": \"" << escapeJsonString(creds.accountId) << "\",\n"
           << "  \"accessKeyId\": \"" << escapeJsonString(creds.accessKeyId) << "\",\n"
           << "  \"access_key_id\": \"" << escapeJsonString(creds.accessKeyId) << "\",\n"
           << "  \"secretAccessKey\": \"" << escapeJsonString(creds.secretAccessKey) << "\",\n"
           << "  \"hasSecretKey\": " << (creds.hasSecretKey ? "true" : "false") << ",\n"
           << "  \"bucket\": \"" << escapeJsonString(creds.bucket) << "\",\n"
           << "  \"region\": \"" << escapeJsonString(creds.region) << "\",\n"
           << "  \"endpoint\": \"" << escapeJsonString(creds.endpoint) << "\",\n"
           << "  \"keyPrefix\": \"" << escapeJsonString(creds.keyPrefix) << "\",\n"
           << "  \"key_prefix\": \"" << escapeJsonString(creds.keyPrefix) << "\"\n"
           << "}";
        return ss.str();
    }

    CloudRedirect::ProviderCredentials_t parseProviderCredentialsJson(const std::string& json)
    {
        CloudRedirect::ProviderCredentials_t creds;
        creds.provider = parseStringField(json, "provider");
        creds.accountId = parseStringField(json, "accountId");
        if (creds.accountId.empty()) creds.accountId = parseStringField(json, "account_id");
        creds.accessKeyId = parseStringField(json, "accessKeyId");
        if (creds.accessKeyId.empty()) creds.accessKeyId = parseStringField(json, "access_key_id");
        creds.secretAccessKey = parseStringField(json, "secretAccessKey");
        if (creds.secretAccessKey.empty()) creds.secretAccessKey = parseStringField(json, "secret_access_key");
        creds.bucket = parseStringField(json, "bucket");
        creds.region = parseStringField(json, "region");
        creds.endpoint = parseStringField(json, "endpoint");
        creds.keyPrefix = parseStringField(json, "keyPrefix");
        if (creds.keyPrefix.empty()) creds.keyPrefix = parseStringField(json, "key_prefix");
        return creds;
    }

    std::string renderMigrationScanJson(const CloudRedirect::MigrationScanResult_t& scan)
    {
        std::ostringstream ss;
        ss << "{\n"
           << "  \"success\": " << (scan.success ? "true" : "false") << ",\n"
           << "  \"provider\": \"" << escapeJsonString(scan.provider) << "\",\n"
           << "  \"totalApps\": " << scan.totalApps << ",\n"
           << "  \"appCount\": " << scan.totalApps << ",\n"
           << "  \"totalFiles\": " << scan.totalFiles << ",\n"
           << "  \"fileCount\": " << scan.totalFiles << ",\n"
           << "  \"totalBytes\": " << scan.totalBytes << ",\n"
           << "  \"totalSizeBytes\": " << scan.totalBytes << ",\n"
           << "  \"apps\": [\n";
        for (size_t i = 0; i < scan.apps.size(); ++i)
        {
            const auto& a = scan.apps[i];
            if (i > 0) ss << ",\n";
            ss << "    {\n"
               << "      \"appId\": " << a.appId << ",\n"
               << "      \"gameTitle\": \"" << escapeJsonString(a.gameTitle) << "\",\n"
               << "      \"accountId\": \"" << escapeJsonString(a.accountId) << "\",\n"
               << "      \"fileCount\": " << a.fileCount << ",\n"
               << "      \"totalBytes\": " << a.totalBytes << "\n"
               << "      \"totalBytes\": " << a.totalBytes << ",\n"
               << "      \"totalSizeBytes\": " << a.totalBytes << "\n"
               << "    }";
        }
        ss << "\n  ]";
        if (!scan.error.empty())
        {
            ss << ",\n  \"error\": \"" << escapeJsonString(scan.error) << "\"";
        }
        ss << "\n}";
        return ss.str();
    }

    std::string renderMigrationResultJson(const CloudRedirect::MigrationResult_t& res)
    {
        std::ostringstream ss;
        ss << "{\n"
           << "  \"success\": " << (res.success ? "true" : "false") << ",\n"
           << "  \"srcProvider\": \"" << escapeJsonString(res.srcProvider) << "\",\n"
           << "  \"dstProvider\": \"" << escapeJsonString(res.dstProvider) << "\",\n"
           << "  \"migrated\": " << res.migrated << ",\n"
           << "  \"skipped\": " << res.skipped << ",\n"
           << "  \"failed\": " << res.failed << ",\n"
           << "  \"total\": " << res.total << ",\n"
           << "  \"totalBytes\": " << res.totalBytes;
        if (!res.error.empty())
        {
            ss << ",\n  \"error\": \"" << escapeJsonString(res.error) << "\"";
        }
        ss << "\n}";
        return ss.str();
    }

    std::string renderOrphanAppsJson(const std::string& provider, const std::vector<CloudRedirect::OrphanAppEntry_t>& orphans)
    {
        std::ostringstream ss;
        ss << "{\n"
           << "  \"success\": true,\n"
           << "  \"provider\": \"" << escapeJsonString(provider) << "\",\n"
           << "  \"orphans\": [\n";
        for (size_t i = 0; i < orphans.size(); ++i)
        {
            const auto& o = orphans[i];
            if (i > 0) ss << ",\n";
            ss << "    {\n"
               << "      \"appId\": " << o.appId << ",\n"
               << "      \"gameTitle\": \"" << escapeJsonString(o.gameTitle) << "\",\n"
               << "      \"fileCount\": " << o.fileCount << ",\n"
               << "      \"totalSizeBytes\": " << o.totalSizeBytes << ",\n"
               << "      \"inRemoteCloud\": " << (o.inRemoteCloud ? "true" : "false") << ",\n"
               << "      \"inLocalMirror\": " << (o.inLocalMirror ? "true" : "false") << ",\n"
               << "      \"inLocalStorage\": " << (o.inLocalMirror ? "true" : "false") << ",\n"
               << "      \"inGoldberg\": " << (o.inGoldberg ? "true" : "false") << ",\n"
               << "      \"inGoldbergPrefix\": " << (o.inGoldberg ? "true" : "false") << "\n"
               << "    }";
        }
        ss << "\n  ]\n}";
        return ss.str();
    }

    std::string renderGcResultJson(const CloudRedirect::GcResult_t& res)
    {
        std::ostringstream ss;
        ss << "{\n"
           << "  \"success\": " << (res.success ? "true" : "false") << ",\n"
           << "  \"appId\": " << res.appId << ",\n"
           << "  \"deleted\": " << res.deleted << ",\n"
           << "  \"blobsDeleted\": " << res.deleted << ",\n"
           << "  \"kept\": " << res.kept << ",\n"
           << "  \"failed\": " << res.failed << ",\n"
           << "  \"localDeleted\": " << (res.localFilesDeleted ? "true" : "false") << ",\n"
           << "  \"localFilesDeleted\": " << (res.localFilesDeleted ? "true" : "false");
        if (!res.error.empty())
        {
            ss << ",\n  \"error\": \"" << escapeJsonString(res.error) << "\"";
        }
        ss << "\n}";
        return ss.str();
    }

    std::string renderGlobalBackupsJson(const std::vector<CloudRedirect::GlobalBackupEntry_t>& backups)
    {
        std::ostringstream ss;
        ss << "{\n"
           << "  \"success\": true,\n"
           << "  \"backups\": [\n";
        for (size_t i = 0; i < backups.size(); ++i)
        {
            const auto& b = backups[i];
            if (i > 0) ss << ",\n";
            ss << "    {\n"
               << "      \"appId\": " << b.appId << ",\n"
               << "      \"gameTitle\": \"" << escapeJsonString(b.gameTitle) << "\",\n"
               << "      \"fileName\": \"" << escapeJsonString(b.fileName) << "\",\n"
               << "      \"fullPath\": \"" << escapeJsonString(b.fullPath) << "\",\n"
               << "      \"sizeBytes\": " << b.sizeBytes << ",\n"
               << "      \"timestamp\": " << b.timestamp << "\n"
               << "    }";
        }
        ss << "\n  ]\n}";
        return ss.str();
    }

    bool parseConfirmReset(const std::string& json)
    {
        return parseStringField(json, "confirm") == "RESET";
    }
}

