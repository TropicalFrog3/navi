/**
 * @file  cloud_view.hpp
 * @brief Declarations for cloud save UI state management and modals
 */
#pragma once

#include "sdk/sdk.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace Navi::CloudRedirect
{
    struct PendingOperation_t;
    struct ProviderAuthStatus_t;
    struct CloudFileEntry_t;
    struct CloudAppSummary_t;
    struct ProviderCredentials_t;
    struct MigrationScanResult_t;
    struct MigrationResult_t;
    struct OrphanAppEntry_t;
    struct GcResult_t;
    struct GlobalBackupEntry_t;
}

namespace Navi::CloudView
{
    std::string renderStatusJson(bool enabled, const std::string& provider, size_t managedAppsCount, size_t pendingOpsCount);
    std::string renderProviderStatusJson(const CloudRedirect::ProviderAuthStatus_t& status);
    std::string renderProviderCredentialsJson(const CloudRedirect::ProviderCredentials_t& creds);
    CloudRedirect::ProviderCredentials_t parseProviderCredentialsJson(const std::string& json);
    std::string renderMigrationScanJson(const CloudRedirect::MigrationScanResult_t& scan);
    std::string renderMigrationResultJson(const CloudRedirect::MigrationResult_t& res);
    std::string renderOrphanAppsJson(const std::string& provider, const std::vector<CloudRedirect::OrphanAppEntry_t>& orphans);
    std::string renderGcResultJson(const CloudRedirect::GcResult_t& res);
    std::string renderGlobalBackupsJson(const std::vector<CloudRedirect::GlobalBackupEntry_t>& backups);
    std::string renderAppStatusJson(AppId_t appId, bool redirected, uint64_t storageBytes, const std::vector<std::string>& backups);
    std::string renderCloudAppsJson(const std::string& provider, const std::vector<CloudRedirect::CloudAppSummary_t>& apps);
    std::string renderAppCloudFilesJson(AppId_t appId,
                                        const std::string& gameTitle,
                                        const std::string& provider,
                                        bool redirected,
                                        const std::vector<CloudRedirect::CloudFileEntry_t>& files);
    std::string renderAppToggleJson(AppId_t appId, bool redirected);
    std::string renderBackupSuccessJson(AppId_t appId, const std::string& backupPath);
    std::string renderPendingOperationAcceptedJson(const std::string& opId);
    std::string renderPendingOpsJson(const std::vector<CloudRedirect::PendingOperation_t>& ops);
    std::string renderSuccessJson(const std::string& message = "");
    std::string renderErrorJson(const std::string& message);

    bool parseBoolField(const std::string& json, const std::string& key, bool defaultValue = false);
    std::string parseStringField(const std::string& json, const std::string& key);
    uint32_t parseUint32Field(const std::string& json, const std::string& key, uint32_t defaultValue = 0);
    bool parseConfirmReset(const std::string& json);
}


