#include "luadownload.hpp"
#include "cdpinject.hpp"
#include "config.hpp"
#include "log.hpp"
#include "globals.hpp"
#include "atomic_file.hpp"
#include "paths.hpp"
#include "zip.hpp"
#include "lua_parser.hpp"
#include "depotkeys.hpp"
#include "licenses.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include <curl/curl.h>

namespace LuaDownload
{
    // ── API provider list ──────────────────────────────────────────────

    struct ApiProvider
    {
        const char* name;
        const char* urlTemplate;  // <appid> is replaced with the actual app ID
        int successCode;
        int unavailableCode;
        int timeoutSec;           // CURLOPT_TIMEOUT — 0 means use default (60)
        bool useProxy;            // Enables custom headless browser proxy fallback (CDPInject::downloadViaPage)
    };

    static const ApiProvider g_apis[] = {
        {
            "Ryuu (API Key)",
            "https://generator.ryuu.lol/secure_download?appid=<appid>&source=SLSsteam-<steamid>&auth_code=",
            200, 404, 60, false
        },
        {
            "DepotBox",
            "https://depotbox.org/api/direct-download?appid=<appid>",
            200, 404, 900, false
        },
        {
            "HubcapDB",
            "https://hubcapmanifest.com/api/v1/manifest/<appid>",
            200, 404, 60, false
        },
        {
            "Spinoza",
            "https://github.com/SPIN0ZAi/SB_manifest_DB/archive/refs/heads/<appid>.zip",
            200, 404, 60, false
        },
        {
            "OpenSteamTool",
            "https://manifest.opensteamtool.com/<appid>",
            200, 404, 60, false
        },
        {
            "wudrm",
            "http://gmrc.wudrm.com/manifest/<appid>",
            200, 404, 60, false
        },
        {
            "steamrun",
            "https://manifest.steam.run/api/depot/<appid>",
            200, 404, 60, false
        },
    };

    // ── CDP status feedback ─────────────────────────────────────────────

    /**
     * Push a status update to the Steam store page button via CDP.
     * Finds the page matching /app/{appId} or /sub/{appId} and injects
     * JS to update the .sls-lua-btn button text and style.
     */
    static void pushStatus(const std::string& appId, const std::string& text,
                           const std::string& color = "")
    {
        auto pages = CDPInject::fetchPages();
        std::string matchApp = "/app/" + appId;
        std::string matchSub = "/sub/" + appId;
        std::string matchLib = "app_" + appId;

        for (auto& page : pages)
        {
            if (page.webSocketDebuggerUrl.empty()) continue;
            if (page.url.find(matchApp) == std::string::npos &&
                page.url.find(matchSub) == std::string::npos &&
                page.url.find(matchLib) == std::string::npos)
                continue;

            // Build JS to update button
            // Escape single quotes in text
            std::string safeText = text;
            for (size_t i = 0; i < safeText.size(); ++i)
            {
                if (safeText[i] == '\'' || safeText[i] == '\\')
                {
                    safeText.insert(i, "\\");
                    ++i;
                }
            }

            std::string js = "(function(){";
            js += "var btns=document.querySelectorAll('.sls-lua-btn');";
            js += "btns.forEach(function(btn){";
            js += "btn.dataset.slsStatus='" + safeText + "';";
            if (!color.empty())
            {
                js += "var a=btn.querySelector('a');";
                js += "if(a) a.style.filter='" + color + "';";
            }
            js += "});";
            js += "})();";

            CDPInject::injectJS(page.webSocketDebuggerUrl, js);
            break; // Only need the first matching page
        }
    }

    // ── Steam path detection ───────────────────────────────────────────

    // Delegates to Paths — single source of truth for Steam root detection.
    std::string findSteamRoot()
    {
        return Paths::findSteamRoot();
    }

    // ── libcurl helpers ────────────────────────────────────────────────

    static size_t curlWriteCallback(void* ptr, size_t size, size_t nmemb, void* userdata)
    {
        auto* file = static_cast<FILE*>(userdata);
        return fwrite(ptr, size, nmemb, file);
    }

    /**
     * Download a URL to a file using libcurl.
     * Returns the HTTP status code, or -1 on connection/curl error.
     */
    static int downloadToFile(const std::string& url, const std::string& destPath, const std::string& apiName, const std::string& authHeader = "", int timeoutSec = 60)
    {
        CURL* curl = curl_easy_init();
        if (!curl) return -1;

        const std::string tmpPath = AtomicFile::makeTempPath(destPath);
        AtomicFile::TempFileGuard guard(tmpPath);

        FILE* fp = fopen(tmpPath.c_str(), "wb");
        if (!fp)
        {
            curl_easy_cleanup(curl);
            return -1;
        }

        struct curl_slist* headers = NULL;
        if (!authHeader.empty()) {
            headers = curl_slist_append(headers, authHeader.c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        }

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(timeoutSec > 0 ? timeoutSec : 60));
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
        
        // Use a more browser-like User-Agent to avoid some basic blocks
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
        
        // Robust SSL settings
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
        
        // Enable all supported encodings (gzip, deflate, etc.)
        curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
        
        // Handle hanging connections
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);

        // Disable signal-based timeout handling (safe for threads)
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

        CURLcode res = curl_easy_perform(curl);
        fclose(fp);

        if (res != CURLE_OK)
        {
            LOG_DEBUG("LuaDownload: API '%s' curl error: %s (code %d)\n", apiName.c_str(), curl_easy_strerror(res), res);
            
            // If it's an SSL error on a known problematic host, suggest a reason
            if (res == CURLE_SSL_CONNECT_ERROR || res == CURLE_PEER_FAILED_VERIFICATION) {
                LOG_DEBUG("LuaDownload: TLS handshake failed — check certificates or DNS settings.\n");
            }

            if (headers) curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
            return -1;
        }

        long httpCode = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
        
        if (headers) curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (httpCode >= 200 && httpCode < 300)
        {
            std::error_code ec;
            fs::rename(tmpPath, destPath, ec);
            if (ec)
            {
                fs::copy_file(tmpPath, destPath, fs::copy_options::overwrite_existing, ec);
            }
            guard.dismiss();
        }

        return static_cast<int>(httpCode);
    }

    // ── ZIP validation ─────────────────────────────────────────────────

    // Delegates to Zip — single implementation, no duplication.
    static bool isValidZip(const std::string& path)
    {
        return Zip::isValid(path);
    }

    // ── ZIP extraction ─────────────────────────────────────────────────

    // Delegates to Zip::extract.
    static bool extractZip(const std::string& zipPath, const std::string& destDir)
    {
        return Zip::extract(zipPath, destDir);
    }

    // ── File discovery in extracted zip ────────────────────────────────

    using ExtractedFiles = Zip::ExtractedFiles;

    static ExtractedFiles findExtractedFiles(const std::string& extractDir,
                                             const std::string& appId)
    {
        return Zip::findFiles(extractDir, appId);
    }

    // ── Key validation helper ──────────────────────────────────────────

    static bool hasUsableKeys(uint32_t appId, const LuaParser::ParseResult& luaData)
    {
        if (g_pSteamEngine && g_pSteamEngine->getUser(0) && g_pSteamEngine->getUser(0)->isSubscribed(appId))
        {
            return true; // Owned on Steam, Steam fetches depot keys directly
        }

        DepotKeys::loadDepotKeysYaml();

        // Check if any depot in luaData has a valid 64-hex key
        for (const auto& [depotIdStr, info] : luaData.depots)
        {
            if (info.key.size() == 64) return true;
            uint32_t dId = 0;
            try { dId = static_cast<uint32_t>(std::stoul(depotIdStr)); } catch (...) {}
            if (dId > 0 && DepotKeys::getDepotKey(dId).size() == 64) return true;
        }

        // Also check manifests in case depots weren't in addappid
        for (const auto& [depotIdStr, _] : luaData.manifests)
        {
            uint32_t dId = 0;
            try { dId = static_cast<uint32_t>(std::stoul(depotIdStr)); } catch (...) {}
            if (dId > 0 && DepotKeys::getDepotKey(dId).size() == 64) return true;
        }

        // Also check if depot_keys.yaml has any keys registered for this app
        if (DepotKeys::hasKeysForApp(appId)) return true;

        return false;
    }

    // ── Main download & install ────────────────────────────────────────

    bool downloadAndInstall(const std::string& appId, int providerIndex,
                            const std::vector<int>& providerOrder,
                            bool forceDownload)
    {
        LOG_INFO("LuaDownload: Starting download for appid=%s\n", appId.c_str());
        pushStatus(appId, "Checking APIs...");

        uint32_t targetAppId = 0;
        try { targetAppId = static_cast<uint32_t>(std::stoul(appId)); } catch (...) {}

        // Find Steam root
        std::string steamRoot = findSteamRoot();
        if (steamRoot.empty())
        {
            LOG_INFO("LuaDownload: Could not find Steam installation\n");
            pushStatus(appId, "Steam not found", "hue-rotate(0deg) brightness(1.0)");
            return false;
        }
        LOG_DEBUG("LuaDownload: Steam root: %s\n", steamRoot.c_str());

        // Prepare paths — delegated to Paths (creates dirs if absent)
        std::string stplugDir    = Paths::stplugDir();
        std::string depotcacheDir = Paths::depotcacheDir();

        if (stplugDir.empty() || depotcacheDir.empty())
        {
            LOG_INFO("LuaDownload: failed to resolve Steam config directories\n");
            pushStatus(appId, "Steam not found", "hue-rotate(0deg) brightness(1.0)");
            return false;
        }

        // Check if already installed
        std::string existingLua = stplugDir + "/" + appId + ".lua";
        if (std::filesystem::exists(existingLua))
        {
            auto existingData = LuaParser::parseFile(existingLua);
            bool luaValid = existingData.valid && !existingData.manifests.empty() && hasUsableKeys(targetAppId, existingData);

            if (forceDownload)
            {
                LOG_INFO("LuaDownload: Force download requested for appid=%s, bypassing already-installed check\n", appId.c_str());
                // Do NOT delete existingLua here. If the new download fails, we want to keep it!
            }
            else
            {
                if (luaValid)
                {
                    LOG_INFO("LuaDownload: Lua script already exists and valid for appid=%s, skipping\n", appId.c_str());
                    pushStatus(appId, "Already installed", "hue-rotate(110deg) brightness(1.2)");
                    return true;
                }

                LOG_INFO("LuaDownload: Existing lua for appid=%s is invalid or lacks decryption keys, re-downloading\n", appId.c_str());
                std::filesystem::remove(existingLua);
            }
        }

        // Temp directory for this download
        const char* home = getenv("HOME");
        std::string tempDir = std::string(home ? home : "/tmp") + "/.cache/SLSsteam/downloads";
        std::filesystem::create_directories(tempDir);
        std::string zipPath = tempDir + "/" + appId + ".zip";
        std::string extractDir = tempDir + "/" + appId + "_extracted";

        bool downloaded = false;
        bool missingKeysDetected = false;
        std::string successApi;

        // Check if zip is already cached
        if (std::filesystem::exists(zipPath) && isValidZip(zipPath))
        {
            LOG_INFO("LuaDownload: Found cached zip for appid=%s\n", appId.c_str());
            downloaded = true;
            successApi = "Cache";
        }
        else
        {
            std::vector<int> apiOrder;
            if (providerIndex >= 0)
            {
                apiOrder.push_back(providerIndex);
            }
            else if (!providerOrder.empty())
            {
                apiOrder = providerOrder;
            }
            else
            {
                for (size_t i = 0; i < std::size(g_apis); ++i)
                    apiOrder.push_back(static_cast<int>(i));
            }

            // Try each selected API provider in the requested order.
            for (const int apiIndex : apiOrder)
            {
                if (apiIndex < 0 || apiIndex >= static_cast<int>(std::size(g_apis)))
                    continue;
                const auto& api = g_apis[apiIndex];
                // Build URL from template
                std::string url = api.urlTemplate;
                size_t pos = url.find("<appid>");
                if (pos != std::string::npos)
                {
                    url.replace(pos, 7, appId);
                }

                size_t steamidPos = url.find("<steamid>");
                if (steamidPos != std::string::npos)
                {
                    url.replace(steamidPos, 9, std::to_string(g_currentSteamId.steamId64));
                }

                std::string authHeader;

                if (std::string(api.name) == "HubcapDB")
                {
                    if (!g_config.hubcapKey.get().empty())
                    {
                        authHeader = "Authorization: Bearer " + g_config.hubcapKey.get();
                    }
                }
                else if (std::string(api.name) == "Ryuu (API Key)")
                {
                    url += g_config.ryuuKey.get();
                }
                else if (std::string(api.name) == "DepotBox")
                {
                    if (g_config.depotBoxKey.get().empty())
                    {
                        LOG_DEBUG("LuaDownload: Skipping DepotBox (no API key configured)\n");
                        continue;
                    }
                    authHeader = "X-API-Key: " + g_config.depotBoxKey.get();
                }

                LOG_INFO("LuaDownload: Trying API '%s' -> %s\n", api.name, url.c_str());
                pushStatus(appId, std::string("Trying ") + api.name + "...");

                // Download
                int httpCode = downloadToFile(url, zipPath, api.name, authHeader, api.timeoutSec);
                LOG_DEBUG("LuaDownload: API '%s' returned HTTP %d\n", api.name, httpCode);

                // Custom headless browser proxy fallback when curl fails (e.g. Cloudflare / anti-bot blocks direct requests)
                if (api.useProxy && httpCode != api.successCode)
                {
                    LOG_INFO("LuaDownload: API '%s' curl failed (HTTP %d), trying custom browser proxy fallback...\n", api.name, httpCode);
                    pushStatus(appId, std::string("Trying ") + api.name + " (browser proxy)...");
                    std::filesystem::remove(zipPath); // Remove any partial/invalid file from curl
                    httpCode = CDPInject::downloadViaPage(url, zipPath);
                    LOG_DEBUG("LuaDownload: API '%s' browser proxy fallback returned HTTP %d\n", api.name, httpCode);
                }

                if (httpCode == api.unavailableCode)
                {
                    LOG_DEBUG("LuaDownload: API '%s' - not available (HTTP %d)\n", api.name, httpCode);
                    std::filesystem::remove(zipPath);
                    continue;
                }

                if (httpCode != api.successCode)
                {
                    LOG_DEBUG("LuaDownload: API '%s' - unexpected status %d\n", api.name, httpCode);
                    std::filesystem::remove(zipPath);
                    continue;
                }

                // If downloaded file is not a zip archive (e.g. raw Lua file, steamrun JSON, or wudrm plain manifest GID)
                if (!isValidZip(zipPath))
                {
                    // 1. Check if it's already a valid Lua file with manifests (e.g. HubcapDB raw Lua)
                    auto luaData = LuaParser::parseFile(zipPath);
                    if (luaData.valid && !luaData.manifests.empty())
                    {
                        if (!hasUsableKeys(targetAppId, luaData))
                        {
                            LOG_INFO("LuaDownload: API '%s' returned raw Lua without decryption keys for unowned app %s\n", api.name, appId.c_str());
                            missingKeysDetected = true;
                            std::filesystem::remove(zipPath);
                            continue;
                        }

                        LOG_INFO("LuaDownload: API '%s' returned a raw Lua script for appid=%s\n", api.name, appId.c_str());
                        std::filesystem::create_directories(extractDir);
                        std::string targetLuaPath = extractDir + "/" + appId + ".lua";
                        std::filesystem::rename(zipPath, targetLuaPath);

                        // Save provider sidecar
                        std::string provSidecar = extractDir + "/" + appId + ".provider";
                        AtomicFile::write(provSidecar, api.name);

                        // Package into a valid zip archive at zipPath so it is cached
                        if (Zip::assemble(zipPath, targetLuaPath, {}, provSidecar))
                        {
                            LOG_INFO("LuaDownload: Assembled cached zip for raw Lua appid=%s at %s\n",
                                     appId.c_str(), zipPath.c_str());
                        }
                        else
                        {
                            LOG_WARN("LuaDownload: Failed to assemble cached zip for raw Lua appid=%s\n",
                                     appId.c_str());
                        }

                        downloaded = true;
                        successApi = api.name;
                        break;
                    }

                    // Read content for structured text / JSON inspection
                    std::string rawContent;
                    {
                        std::ifstream ifs(zipPath, std::ios::binary);
                        if (ifs.is_open())
                        {
                            rawContent.assign((std::istreambuf_iterator<char>(ifs)),
                                              std::istreambuf_iterator<char>());
                        }
                    }

                    // 2. Check for steamrun depot list JSON (contains "depots" array)
                    if (rawContent.find("\"depots\"") != std::string::npos)
                    {
                        struct DepotManifestEntry
                        {
                            std::string depotId;
                            std::string manifestId;
                            std::string sizeBytes;
                        };
                        std::vector<DepotManifestEntry> depotEntries;

                        const std::regex objRegex(R"(\{[^{}]+\})");
                        const std::regex depotIdRe(R"lit("depotid"\s*:\s*(\d+))lit");
                        const std::regex manifestIdRe(R"lit("manifestid"\s*:\s*"(\d+)")lit");
                        const std::regex sizeRe(R"lit("size_bytes"\s*:\s*(\d+))lit");

                        auto objBegin = std::sregex_iterator(rawContent.begin(), rawContent.end(), objRegex);
                        auto objEnd = std::sregex_iterator();

                        for (auto it = objBegin; it != objEnd; ++it)
                        {
                            std::string block = it->str();
                            std::smatch mDepot, mManifest, mSize;
                            if (std::regex_search(block, mDepot, depotIdRe) &&
                                std::regex_search(block, mManifest, manifestIdRe))
                            {
                                DepotManifestEntry entry;
                                entry.depotId = mDepot[1].str();
                                entry.manifestId = mManifest[1].str();
                                if (std::regex_search(block, mSize, sizeRe))
                                {
                                    entry.sizeBytes = mSize[1].str();
                                }
                                depotEntries.push_back(entry);
                            }
                        }

                        if (!depotEntries.empty())
                        {
                            bool steamrunKeysOk = false;
                            if (g_pSteamEngine && g_pSteamEngine->getUser(0) && g_pSteamEngine->getUser(0)->isSubscribed(targetAppId))
                            {
                                steamrunKeysOk = true;
                            }
                            else
                            {
                                DepotKeys::loadDepotKeysYaml();
                                if (DepotKeys::hasKeysForApp(targetAppId))
                                {
                                    steamrunKeysOk = true;
                                }
                                else
                                {
                                    for (const auto& d : depotEntries)
                                    {
                                        uint32_t dId = 0;
                                        try { dId = static_cast<uint32_t>(std::stoul(d.depotId)); } catch (...) {}
                                        if (dId > 0 && DepotKeys::getDepotKey(dId).size() == 64)
                                        {
                                            steamrunKeysOk = true;
                                            break;
                                        }
                                    }
                                }
                            }

                            if (!steamrunKeysOk)
                            {
                                LOG_INFO("LuaDownload: API '%s' provides manifests without decryption keys for unowned app %s\n",
                                         api.name, appId.c_str());
                                missingKeysDetected = true;
                                std::filesystem::remove(zipPath);
                                continue;
                            }

                            LOG_INFO("LuaDownload: API '%s' returned %zu depot(s) for appid=%s\n",
                                     api.name, depotEntries.size(), appId.c_str());
                            std::filesystem::create_directories(extractDir);

                            std::string synthLuaPath = extractDir + "/" + appId + ".lua";
                            std::ofstream luaOut(synthLuaPath);
                            if (luaOut.is_open())
                            {
                                luaOut << "addappid(" << appId << ")\n";
                                for (const auto& d : depotEntries)
                                {
                                    if (!d.sizeBytes.empty())
                                    {
                                        luaOut << "setManifestid(" << d.depotId << ", \"" << d.manifestId << "\", " << d.sizeBytes << ")\n";
                                    }
                                    else
                                    {
                                        luaOut << "setManifestid(" << d.depotId << ", \"" << d.manifestId << "\")\n";
                                    }
                                }
                                luaOut.close();
                            }

                            std::vector<std::string> manifestPaths;
                            // Download binary manifests for each depot from steamrun
                            for (const auto& d : depotEntries)
                            {
                                std::string manifestUrl = "https://manifest.steam.run/api/download_manifest?depot_id=" +
                                                          d.depotId + "&manifest_id=" + d.manifestId;
                                std::string manifestFile = extractDir + "/" + d.depotId + "_" + d.manifestId + ".manifest";
                                pushStatus(appId, "Downloading depot " + d.depotId + "...");
                                int code = downloadToFile(manifestUrl, manifestFile, api.name, authHeader, 60);
                                if (code == 200)
                                {
                                    LOG_INFO("LuaDownload: Downloaded manifest for depot %s -> %s\n",
                                             d.depotId.c_str(), manifestFile.c_str());
                                    manifestPaths.push_back(manifestFile);
                                }
                                else
                                {
                                    LOG_INFO("LuaDownload: Failed to download manifest for depot %s (HTTP %d)\n",
                                             d.depotId.c_str(), code);
                                }
                            }

                            // Save provider sidecar
                            std::string provSidecar = extractDir + "/" + appId + ".provider";
                            AtomicFile::write(provSidecar, api.name);

                            // Package into a valid zip archive at zipPath so it is cached
                            std::filesystem::remove(zipPath);
                            if (Zip::assemble(zipPath, synthLuaPath, manifestPaths, provSidecar))
                            {
                                LOG_INFO("LuaDownload: Assembled cached zip for steamrun appid=%s at %s (%zu manifests)\n",
                                         appId.c_str(), zipPath.c_str(), manifestPaths.size());
                            }
                            else
                            {
                                LOG_WARN("LuaDownload: Failed to assemble cached zip for steamrun appid=%s\n",
                                         appId.c_str());
                            }

                            downloaded = true;
                            successApi = api.name;
                            break;
                        }
                    }

                    // 3. Check for wudrm plain-text manifest GID (numeric string)
                    std::string trimmed = rawContent;
                    trimmed.erase(trimmed.begin(), std::find_if(trimmed.begin(), trimmed.end(), [](unsigned char ch) {
                        return !std::isspace(ch);
                    }));
                    trimmed.erase(std::find_if(trimmed.rbegin(), trimmed.rend(), [](unsigned char ch) {
                        return !std::isspace(ch);
                    }).base(), trimmed.end());

                    bool isNumeric = !trimmed.empty() && std::all_of(trimmed.begin(), trimmed.end(), ::isdigit);
                    if (isNumeric && trimmed.length() > 5 && trimmed != "0")
                    {
                        bool wudrmKeysOk = false;
                        if (g_pSteamEngine && g_pSteamEngine->getUser(0) && g_pSteamEngine->getUser(0)->isSubscribed(targetAppId))
                        {
                            wudrmKeysOk = true;
                        }
                        else
                        {
                            DepotKeys::loadDepotKeysYaml();
                            uint32_t dId = targetAppId;
                            if (DepotKeys::hasKeysForApp(targetAppId) || DepotKeys::getDepotKey(dId).size() == 64)
                            {
                                wudrmKeysOk = true;
                            }
                        }

                        if (!wudrmKeysOk)
                        {
                            LOG_INFO("LuaDownload: API '%s' provides manifests without decryption keys for unowned app %s\n",
                                     api.name, appId.c_str());
                            missingKeysDetected = true;
                            std::filesystem::remove(zipPath);
                            continue;
                        }

                        LOG_INFO("LuaDownload: API '%s' returned a plain-text manifest ID: %s\n",
                                 api.name, trimmed.c_str());
                        std::filesystem::create_directories(extractDir);
                        std::string synthLuaPath = extractDir + "/" + appId + ".lua";
                        std::ofstream luaOut(synthLuaPath);
                        if (luaOut.is_open())
                        {
                            luaOut << "addappid(" << appId << ")\n";
                            luaOut << "setManifestid(" << appId << ", \"" << trimmed << "\")\n";
                            luaOut.close();

                            // Save provider sidecar
                            std::string provSidecar = extractDir + "/" + appId + ".provider";
                            AtomicFile::write(provSidecar, api.name);

                            // Package into a valid zip archive at zipPath so it is cached
                            std::filesystem::remove(zipPath);
                            if (Zip::assemble(zipPath, synthLuaPath, {}, provSidecar))
                            {
                                LOG_INFO("LuaDownload: Assembled cached zip for %s appid=%s at %s\n",
                                         api.name, appId.c_str(), zipPath.c_str());
                            }
                            else
                            {
                                LOG_WARN("LuaDownload: Failed to assemble cached zip for %s appid=%s\n",
                                         api.name, appId.c_str());
                            }

                            downloaded = true;
                            successApi = api.name;
                            break;
                        }
                    }

                    LOG_INFO("LuaDownload: API '%s' returned non-zip / invalid Lua file\n", api.name);
                    std::filesystem::remove(zipPath);
                    continue;
                }

                downloaded = true;
                successApi = api.name;
                LOG_INFO("LuaDownload: Downloaded zip from '%s' for appid=%s\n", api.name, appId.c_str());
                break;
            }
        }

        if (!downloaded)
        {
            LOG_INFO("LuaDownload: No API had the game or usable decryption keys for appid=%s\n", appId.c_str());
            if (missingKeysDetected)
            {
                pushStatus(appId, "No decryption key", "hue-rotate(0deg) brightness(1.0)");
            }
            else
            {
                pushStatus(appId, "Not available", "hue-rotate(0deg) brightness(1.0)");
            }
            return false;
        }

        // Extract zip if zip file exists
        if (std::filesystem::exists(zipPath))
        {
            if (std::filesystem::exists(extractDir))
            {
                std::filesystem::remove_all(extractDir);
            }

            pushStatus(appId, "Extracting...");
            if (!extractZip(zipPath, extractDir))
            {
                LOG_INFO("LuaDownload: Failed to extract zip for appid=%s\n", appId.c_str());
                pushStatus(appId, "Extract failed", "hue-rotate(0deg) brightness(1.0)");
                std::filesystem::remove(zipPath);
                return false;
            }
        }

        // Find the relevant files
        auto files = findExtractedFiles(extractDir, appId);

        if (files.luaFile.empty())
        {
            if (!files.manifestFiles.empty())
            {
                LOG_INFO("LuaDownload: No %s.lua found, but found %zu manifest(s). Synthesizing Lua file...\n",
                         appId.c_str(), files.manifestFiles.size());
                std::string synthLuaPath = extractDir + "/" + appId + ".lua";
                std::ofstream luaOut(synthLuaPath);
                if (luaOut.is_open())
                {
                    luaOut << "addappid(" << appId << ")\n";
                    for (const auto& mf : files.manifestFiles)
                    {
                        std::string stem = std::filesystem::path(mf).stem().string();
                        size_t underPos = stem.find('_');
                        if (underPos != std::string::npos)
                        {
                            std::string depotId = stem.substr(0, underPos);
                            std::string manifestGid = stem.substr(underPos + 1);
                            luaOut << "setManifestid(" << depotId << ", \"" << manifestGid << "\")\n";
                        }
                        else
                        {
                            luaOut << "setManifestid(" << stem << ", \"0\")\n";
                        }
                    }
                    luaOut.close();
                    files.luaFile = synthLuaPath;
                }
            }

            if (files.luaFile.empty())
            {
                LOG_INFO("LuaDownload: No %s.lua found in the zip\n", appId.c_str());
                std::filesystem::remove_all(extractDir);
                std::filesystem::remove(zipPath);
                return false;
            }
        }

        LOG_INFO("LuaDownload: Found lua: %s, manifests: %zu\n",
                      files.luaFile.c_str(), files.manifestFiles.size());

        // Validate the Lua file — must have at least one manifest, and for unowned apps, must have decryption key(s)
        {
            auto luaData = LuaParser::parseFile(files.luaFile);
            if (!luaData.valid || luaData.manifests.empty())
            {
                LOG_INFO("LuaDownload: Lua file has no valid manifests (via %s) — unusable\n",
                         successApi.c_str());
                std::filesystem::remove_all(extractDir);
                std::filesystem::remove(zipPath);
                pushStatus(appId, "Invalid Lua (no manifests)", "hue-rotate(0deg) brightness(1.0)");
                return false;
            }

            if (!hasUsableKeys(targetAppId, luaData))
            {
                LOG_WARN("LuaDownload: BAIL! No decryption key available for app %s depots (via %s) — cannot decrypt\n",
                         appId.c_str(), successApi.c_str());
                std::filesystem::remove_all(extractDir);
                std::filesystem::remove(zipPath);
                pushStatus(appId, "No decryption key", "hue-rotate(0deg) brightness(1.0)");
                return false;
            }
        }

        pushStatus(appId, "Installing...");

        // Clean up any old manifests from previous provider in depotcache to prevent orphan bloat
        std::string legacyDepotcache = Paths::findSteamRoot() + "/config/depotcache";
        for (const auto& d : { depotcacheDir, legacyDepotcache }) {
            if (d.empty() || !std::filesystem::exists(d)) continue;
            try {
                for (const auto& entry : std::filesystem::directory_iterator(d)) {
                    if (entry.is_regular_file() && entry.path().extension() == ".manifest") {
                        std::string stem = entry.path().stem().string();
                        if (stem.find(appId) == 0) {
                            std::filesystem::remove(entry.path());
                        }
                    }
                }
            } catch (...) {}
        }

        // Install manifest files to depotcache and mirror to config/depotcache
        for (const auto& manifestPath : files.manifestFiles)
        {
            std::string filename = std::filesystem::path(manifestPath).filename().string();
            std::string destPath = depotcacheDir + "/" + filename;

            if (AtomicFile::copy(manifestPath, destPath))
            {
                LOG_INFO("LuaDownload: Installed manifest -> %s\n", destPath.c_str());
            }
            else
            {
                LOG_INFO("LuaDownload: Failed to atomically copy manifest %s\n", filename.c_str());
            }

            if (!legacyDepotcache.empty() && legacyDepotcache != depotcacheDir)
            {
                std::filesystem::create_directories(legacyDepotcache);
                AtomicFile::copy(manifestPath, legacyDepotcache + "/" + filename);
            }
        }

        // Install the .lua file
        if (!AtomicFile::copy(files.luaFile, existingLua))
        {
            LOG_INFO("LuaDownload: Failed to atomically copy lua file %s\n", files.luaFile.c_str());
            std::filesystem::remove_all(extractDir);
            std::filesystem::remove(zipPath);
            return false;
        }

        LOG_INFO("LuaDownload: Installed lua -> %s (via %s)\n",
                      existingLua.c_str(), successApi.c_str());

        // Save provider name to a sidecar file so the UI can show it
        {
            std::string providerPath = stplugDir + "/" + appId + ".provider";
            std::string provNameToSave = successApi;
            if (provNameToSave == "Cache")
            {
                if (!files.providerFile.empty() && std::filesystem::exists(files.providerFile))
                {
                    std::ifstream pIn(files.providerFile);
                    std::string line;
                    if (std::getline(pIn, line) && !line.empty())
                    {
                        provNameToSave = line;
                    }
                }
                else if (std::filesystem::exists(providerPath))
                {
                    // Keep existing provider attribution
                    provNameToSave.clear();
                }
            }
            if (!provNameToSave.empty())
            {
                if (AtomicFile::write(providerPath, provNameToSave))
                {
                    LOG_DEBUG("LuaDownload: Saved provider info -> %s\n", providerPath.c_str());
                }
            }
        }

        // Clean up extracted files, but keep the zip cached
        std::filesystem::remove_all(extractDir);

        LOG_INFO("LuaDownload: Successfully installed appid=%s\n", appId.c_str());

        // Ensure configuration and licenses are updated immediately for live library hot-reload
        try {
            uint32_t id = static_cast<uint32_t>(std::stoul(appId));
            g_config.addAdditionalAppId(id);
            DepotKeys::scanLuaPluginsForDepotKeys();
            Licenses::queueRefresh();
            CDPInject::ensureCompatTool(id);
        } catch (...) {}

        pushStatus(appId, "Installed!", "hue-rotate(110deg) brightness(1.2)");
        return true;
    }
}
