/**
 * @file  storeinject_view.hpp
 * @brief Declarations for store DOM event listeners and action buttons
 */
#pragma once

#include <string>
#include <vector>
#include <set>
#include <map>
#include <filesystem>
#include <cstdint>
#include <unordered_map>
#include "lua_parser.hpp"

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
        "Access-Control-Allow-Headers: Content-Type, X-Navi-Auth\r\n"
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
        std::string autoCrackState;
        std::string provider;
    };

    /**
     * Render JSON response for /check?id= endpoint.
     */
    std::string renderCheckJson(const CheckViewModel& info);
    std::string renderCheckJson(bool exists, bool hasLua, bool gameExists, bool isUnlocked,
                                bool pending, bool downloading, bool paused,
                                bool onlineFixInstalled, bool autoCrackInstalled,
                                const std::string& provider,
                                const std::string& autoCrackState = "");

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
    using StoreInjectView::renderFullConfigJson;
    using StoreInjectView::applyFullConfigJson;
}

