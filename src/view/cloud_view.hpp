#pragma once

#include "sdk/sdk.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace Navi::CloudRedirect
{
    struct PendingOperation_t;
    struct ProviderAuthStatus_t;
}

namespace Navi::CloudView
{
    std::string renderStatusJson(bool enabled, const std::string& provider, size_t managedAppsCount, size_t pendingOpsCount);
    std::string renderProviderStatusJson(const CloudRedirect::ProviderAuthStatus_t& status);
    std::string renderAppStatusJson(AppId_t appId, bool redirected, uint64_t storageBytes, const std::vector<std::string>& backups);
    std::string renderAppToggleJson(AppId_t appId, bool redirected);
    std::string renderBackupSuccessJson(AppId_t appId, const std::string& backupPath);
    std::string renderPendingOperationAcceptedJson(const std::string& opId);
    std::string renderPendingOpsJson(const std::vector<CloudRedirect::PendingOperation_t>& ops);
    std::string renderSuccessJson(const std::string& message = "");
    std::string renderErrorJson(const std::string& message);

    bool parseBoolField(const std::string& json, const std::string& key, bool defaultValue = false);
    std::string parseStringField(const std::string& json, const std::string& key);
    bool parseConfirmReset(const std::string& json);
}

