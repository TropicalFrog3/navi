#pragma once

#include <string>
#include <vector>
#include <set>
#include <map>
#include <filesystem>
#include <cstdint>
#include <unordered_map>
#include "lua_parser.hpp"
#include "p2p_discovery.hpp"
#include "sync_crypto.hpp"
#include "sync_engine.hpp"
#include "sync_manifest.hpp"
#include "cloud_provider.hpp"

namespace StoreInjectView
{
    // Marker comments used to identify injected content in index.html
    constexpr const char* INJECT_MARKER_BEGIN = "<!-- SLS_STORE_INJECT_V7_BEGIN -->";
    constexpr const char* INJECT_MARKER_END   = "<!-- SLS_STORE_INJECT_V7_END -->";

    // CORS headers and preflight responses
    constexpr const char* CORS_HEADERS = "Access-Control-Allow-Origin: *\r\n";
    constexpr const char* CORS_PREFLIGHT_RESPONSE =
        "HTTP/1.1 204 No Content\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Access-Control-Max-Age: 86400\r\n"
        "Connection: close\r\n\r\n";

    /**
     * Locate Steam's steamui/index.html.
     */
    std::filesystem::path findSteamUIIndexHtml();

    /**
     * Remove injected scripts and markers from index.html.
     */
    void removeStoreInjectScript();

    /**
     * Render JSON response for successful operations.
     */
    std::string renderSuccessJson(const std::string& message = "");

    /**
     * Render JSON response for failed operations.
     */
    std::string renderErrorJson(const std::string& message);

    /**
     * Render JSON response for /lua_apps list of app IDs.
     */
    std::string renderLuaAppsJson(const std::set<uint32_t>& appIds);

    /**
     * Render JSON response for /api-keys endpoint.
     */
    std::string renderApiKeysJson(const std::string& ryuuKey, const std::string& depotBoxKey, const std::string& hubcapKey);

    struct CheckViewModel {
        bool exists{false};
        bool hasLua{false};
        bool gameExists{false};
        bool isUnlocked{false};
        bool pending{false};
        bool downloading{false};
        bool paused{false};
        bool onlineFixInstalled{false};
        bool autoCrackInstalled{false};
        std::string provider;
    };

    /**
     * Render JSON response for /check?id= endpoint.
     */
    std::string renderCheckJson(const CheckViewModel& info);
    std::string renderCheckJson(bool exists, bool hasLua, bool gameExists, bool isUnlocked,
                                bool pending, bool downloading, bool paused,
                                bool onlineFixInstalled, bool autoCrackInstalled,
                                const std::string& provider);

    struct DepotEntryViewModel {
        std::string id;
        std::string name;
        std::string size;
        std::string os;
        bool hasKey{false};
    };

    /**
     * Render JSON response for /get-depots?id= endpoint.
     */
    std::string renderDepotsJson(const std::vector<DepotEntryViewModel>& depots,
                                 bool locallyOwned,
                                 size_t validKeysCount);
    std::string renderDepotsJson(const std::map<std::string, LuaParser::DepotInfo>& allDepots,
                                 size_t validKeysCount,
                                 bool locallyOwned);

    // Cross-device sync view models & IPC payloads
    struct SyncDiffItemViewModel
    {
        uint32_t appId = 0;
        std::string title;
        std::string category;
        std::string logicalPath;
        uint64_t sizeBytes = 0;
        uint64_t generation = 0;
        bool isGameRunning = false;
    };

    struct SyncStatusViewModel
    {
        std::string role = "disabled"; // "disabled", "overrider", "receiver", "peer"
        std::string topology = "cloud"; // "cloud", "p2p_and_cloud"
        std::string cloudProvider = "none"; // "none", "gdrive", "webdav"
        bool cloudAuthenticated = false;
        bool gdriveAuthenticated = false;
        std::string recoveryKeyMaskedOrFull; // 55-char Base32 recovery key when role == "overrider"
        uint32_t keyEpoch = 1;
        bool hasRemoteBump = false;
        std::string lastTriggerReason; // "launch", "wake_from_sleep", "manual", "p2p"
        std::string overriderHostname;
        uint64_t remoteGeneration = 0;
        uint64_t localGeneration = 0;
        std::vector<SyncDiffItemViewModel> diffItems;
        std::vector<Sync::SaveConflict_t> conflicts;
        std::vector<Sync::DiscoveredPeer_t> p2pPeers;
        std::vector<uint32_t> stagedAcfRestartRequiredAppIds;
    };

    std::string renderSyncStatusJson(const SyncStatusViewModel& vm);
    std::string renderSyncApplyResultJson(const Sync::SyncApplyReport_t& report);

    struct SyncApplyRequest_t
    {
        Sync::SyncSelectionFilter_t filter;
        std::unordered_map<std::string, Sync::EConflictChoice> conflictChoices;
    };

    bool parseSyncApplyRequestJson(const std::string& jsonBody, SyncApplyRequest_t& outReq);

    struct SyncConfigUpdateRequest_t
    {
        std::string role;
        std::string topology;
        std::string cloudProvider; // "none", "gdrive", "webdav"
        std::string webDavUrl;
        std::string webDavUser;
        std::string webDavPassword;
    };

    bool parseSyncConfigUpdateJson(const std::string& jsonBody, SyncConfigUpdateRequest_t& outReq);

    bool validateAndParseRecoveryKeyJson(
        const std::string& jsonBody,
        Sync::MasterKey_t& outKey,
        bool& outRotateRequested,
        std::string& outErrorMsg);

    struct CloudCredentials_t
    {
        std::string gdriveClientId;
        std::string gdriveClientSecret;
        std::string gdriveAccessToken;
        std::string gdriveRefreshToken;
        int64_t gdriveTokenExpiresAt = 0;
        std::string gdriveDeviceAuthEndpoint = "https://oauth2.googleapis.com/device/code";
        std::string gdriveTokenEndpoint = "https://oauth2.googleapis.com/token";
        std::string gdriveApiBaseUrl = "https://www.googleapis.com/drive/v3";
        std::string gdriveUploadBaseUrl = "https://www.googleapis.com/upload/drive/v3";
        std::string webdavPassword;
    };

    bool loadCloudCredentials(const std::string& configDir, CloudCredentials_t& outCreds);
    bool saveCloudCredentials(const std::string& configDir, const CloudCredentials_t& creds);

    bool parseGDriveDeviceStartJson(const std::string& jsonBody, std::string& outClientId, std::string& outClientSecret);
    std::string renderGDriveDevicePromptJson(const Sync::DeviceCodePrompt_t& prompt);
    bool parseGDriveDevicePollJson(const std::string& jsonBody, std::string& outDeviceCode);
    std::string renderSyncPushResultJson(uint64_t generation, size_t pushedFiles, const std::string& etag);

    /* 1-Click Browser OAuth2 Flow */
    struct OAuthCallbackParams_t
    {
        std::string code;
        std::string state;
        std::string error;
        std::string errorDescription;
    };

    bool parseOAuthCallbackQuery(const std::string& uriOrQuery, OAuthCallbackParams_t& outParams);
    std::string renderOAuthCallbackHtml(bool success, const std::string& message = "");
    std::string renderGDriveBrowserStartJson(const std::string& authUrl, const std::string& state);

    /**
     * Render full configuration JSON for config.yaml.
     */
    std::string renderFullConfigJson(const std::string& configYamlPath);

    /**
     * Apply full configuration JSON to config.yaml and reload settings.
     */
    bool applyFullConfigJson(const std::string& configYamlPath, const std::string& jsonBody, std::string& outErrorMsg);
}

namespace StoreInject
{
    using StoreInjectView::findSteamUIIndexHtml;
    using StoreInjectView::removeStoreInjectScript;
    using StoreInjectView::INJECT_MARKER_BEGIN;
    using StoreInjectView::INJECT_MARKER_END;
    using StoreInjectView::CORS_PREFLIGHT_RESPONSE;
    using StoreInjectView::renderSuccessJson;
    using StoreInjectView::renderErrorJson;
    using StoreInjectView::renderLuaAppsJson;
    using StoreInjectView::renderApiKeysJson;
    using StoreInjectView::renderCheckJson;
    using StoreInjectView::renderDepotsJson;
    using StoreInjectView::SyncDiffItemViewModel;
    using StoreInjectView::SyncStatusViewModel;
    using StoreInjectView::renderSyncStatusJson;
    using StoreInjectView::renderSyncApplyResultJson;
    using StoreInjectView::SyncApplyRequest_t;
    using StoreInjectView::parseSyncApplyRequestJson;
    using StoreInjectView::SyncConfigUpdateRequest_t;
    using StoreInjectView::parseSyncConfigUpdateJson;
    using StoreInjectView::validateAndParseRecoveryKeyJson;
    using StoreInjectView::CloudCredentials_t;
    using StoreInjectView::loadCloudCredentials;
    using StoreInjectView::saveCloudCredentials;
    using StoreInjectView::parseGDriveDeviceStartJson;
    using StoreInjectView::renderGDriveDevicePromptJson;
    using StoreInjectView::parseGDriveDevicePollJson;
    using StoreInjectView::renderSyncPushResultJson;
    using StoreInjectView::OAuthCallbackParams_t;
    using StoreInjectView::parseOAuthCallbackQuery;
    using StoreInjectView::renderOAuthCallbackHtml;
    using StoreInjectView::renderGDriveBrowserStartJson;
    using StoreInjectView::renderFullConfigJson;
    using StoreInjectView::applyFullConfigJson;
}

