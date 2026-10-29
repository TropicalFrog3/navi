#include "storeinject.hpp"
#include "storeinject_view.hpp"
#include "cdpinject.hpp"
#include "luadownload.hpp"
#include "removelua.hpp"
#include "apps.hpp"
#include "nativeinstall.hpp"
#include "depotkeys.hpp"
#include "licenses.hpp"
#include "lua_parser.hpp"
#include "IClientAppManager.hpp"
#include "IClientApps.hpp"
#include "log.hpp"
#include "config.hpp"
#include "utils.hpp"
#include "atomic_file.hpp"
#include "ticket.hpp"
#include "paths.hpp"
#include "power_monitor.hpp"
#include "sync_crypto.hpp"
#include "sync_engine.hpp"
#include "sync_manifest.hpp"
#include "sync_snapshot.hpp"
#include "p2p_discovery.hpp"
#include "p2p_session.hpp"
#include "cloud_provider.hpp"
#include <unistd.h>
#include <openssl/crypto.h>
#include <yaml-cpp/yaml.h>

#include <thread>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <iomanip>
#include <sys/wait.h>
#include <regex>
#include <fstream>
#include <cctype>
#include <set>
#include <base64/base64.hpp>


static std::string urlDecode(const std::string& str) {
    std::string ret;
    for (size_t i = 0; i < str.length(); ++i) {
        if (str[i] == '%') {
            if (i + 2 < str.length()) {
                int value;
                std::istringstream is(str.substr(i + 1, 2));
                if (is >> std::hex >> value) {
                    ret += static_cast<char>(value);
                    i += 2;
                } else ret += '%';
            } else ret += '%';
        } else if (str[i] == '+') ret += ' ';
        else ret += str[i];
    }
    return ret;
}

static std::vector<int> parseProviderOrder(const std::string& value)
{
    std::vector<int> order;
    std::stringstream stream(value);
    std::string item;
    while (std::getline(stream, item, ','))
    {
        try
        {
            order.push_back(std::stoi(item));
        }
        catch (...) {}
    }
    return order;
}
#include <sstream>
#include <string>
#include <algorithm>
#include <set>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cerrno>

namespace StoreInject
{
    static std::thread g_workerThread;
    static std::thread g_autoThread;
    static std::set<uint32_t> g_pendingRestartApps;
    static std::atomic<bool> g_shouldStop(false);
    static std::atomic<int> g_serverFd{-1};
    static std::atomic<bool> g_shutdownCalled{false};

    static std::unique_ptr<Sync::PowerMonitor> g_powerMonitor;
    static std::string g_lastSyncTriggerReason = "launch";
    static std::string g_lastOverriderHostname;
    static uint64_t g_lastRemoteGeneration = 0;
    static uint64_t g_lastLocalGeneration = 0;
    static bool g_lastHasRemoteBump = false;
    static uint32_t g_syncKeyEpoch = 1;
    static std::vector<Sync::DiscoveredPeer_t> g_cachedP2pPeers;
    static std::vector<StoreInjectView::SyncDiffItemViewModel> g_cachedDiffItems;
    static std::vector<Sync::SaveConflict_t> g_cachedConflicts;
    static std::vector<uint32_t> g_stagedAcfRestartRequiredAppIds;
    static Sync::SyncStateManifest_t g_lastRemoteManifest;
    static std::unordered_map<std::string, std::vector<uint8_t>> g_lastPlaintextPayloads;
    static std::string g_activePairingPin;
    static std::string g_pendingOAuthState;
    static std::string g_pendingPkceVerifier;

    static void sendJsonResponse(int sock, int code, const std::string& statusMsg, const std::string& jsonBody)
    {
        std::string response = "HTTP/1.1 " + std::to_string(code) + " " + statusMsg + "\r\n"
                               "Content-Type: application/json\r\n"
                               "Access-Control-Allow-Origin: *\r\n"
                               "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                               "Access-Control-Allow-Headers: Content-Type\r\n"
                               "Connection: close\r\n"
                               "Content-Length: " + std::to_string(jsonBody.size()) + "\r\n\r\n" + jsonBody;
        send(sock, response.c_str(), response.size(), 0);
        close(sock);
    }

    static void sendHtmlResponse(int sock, int code, const std::string& statusMsg, const std::string& htmlBody)
    {
        std::string response = "HTTP/1.1 " + std::to_string(code) + " " + statusMsg + "\r\n"
                               "Content-Type: text/html; charset=utf-8\r\n"
                               "Access-Control-Allow-Origin: *\r\n"
                               "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                               "Access-Control-Allow-Headers: Content-Type\r\n"
                               "Connection: close\r\n"
                               "Content-Length: " + std::to_string(htmlBody.size()) + "\r\n\r\n" + htmlBody;
        send(sock, response.c_str(), response.size(), 0);
        close(sock);
    }

    static std::string extractHttpPostBody(int sockFd, const std::string& rawRequest)
    {
        size_t header_end = rawRequest.find("\r\n\r\n");
        if (header_end == std::string::npos)
        {
            header_end = rawRequest.find("\n\n");
        }
        if (header_end == std::string::npos) return "";

        size_t body_start = (rawRequest.find("\r\n\r\n") != std::string::npos) ? (header_end + 4) : (header_end + 2);
        std::string body = rawRequest.substr(body_start);

        size_t cl_pos = rawRequest.find("Content-Length:");
        if (cl_pos == std::string::npos)
        {
            cl_pos = rawRequest.find("content-length:");
        }

        size_t content_length = 0;
        if (cl_pos != std::string::npos && cl_pos < header_end)
        {
            size_t val_start = rawRequest.find_first_not_of(" \t", cl_pos + 15);
            size_t val_end = rawRequest.find_first_of("\r\n", val_start);
            if (val_start != std::string::npos && val_end != std::string::npos)
            {
                try { content_length = std::stoul(rawRequest.substr(val_start, val_end - val_start)); } catch (...) {}
            }
        }

        constexpr size_t MAX_BODY_SIZE = 1024 * 1024; // 1 MiB safety cap
        if (content_length > MAX_BODY_SIZE)
        {
            content_length = MAX_BODY_SIZE;
        }

        if (content_length > body.size() && sockFd >= 0)
        {
            char buf[4096];
            while (body.size() < content_length)
            {
                size_t to_read = std::min(sizeof(buf), content_length - body.size());
                ssize_t n = recv(sockFd, buf, to_read, 0);
                if (n <= 0) break;
                body.append(buf, n);
            }
        }

        return body;
    }

    struct ManualFile {
        std::string name;
        std::string base64Content;
    };

    static std::string readFullHttpRequest(int sock)
    {
        std::string request;
        char buffer[4096];
        ssize_t n;
        
        // Set a receive timeout on the socket
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 50000;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        
        // Read headers
        size_t header_end = std::string::npos;
        while (header_end == std::string::npos)
        {
            n = recv(sock, buffer, sizeof(buffer) - 1, 0);
            if (n <= 0)
            {
                break;
            }
            buffer[n] = '\0';
            request.append(buffer, n);
            header_end = request.find("\r\n\r\n");
            if (header_end == std::string::npos)
            {
                header_end = request.find("\n\n");
            }
        }
        
        if (header_end == std::string::npos)
        {
            return request;
        }
        
        // Find Content-Length
        size_t content_length_pos = request.find("Content-Length:");
        if (content_length_pos == std::string::npos)
        {
            content_length_pos = request.find("content-length:");
        }
        
        size_t content_length = 0;
        if (content_length_pos != std::string::npos && content_length_pos < header_end)
        {
            size_t value_start = request.find_first_not_of(" \t", content_length_pos + 15);
            size_t value_end = request.find_first_of("\r\n", value_start);
            if (value_start != std::string::npos && value_end != std::string::npos)
            {
                try
                {
                    content_length = std::stoul(request.substr(value_start, value_end - value_start));
                }
                catch (...) {}
            }
        }
        
        // Read the remaining body
        size_t header_len = (request.find("\r\n\r\n") != std::string::npos) ? (header_end + 4) : (header_end + 2);
        size_t body_received = request.size() - header_len;
        
        while (body_received < content_length)
        {
            size_t to_read = std::min(sizeof(buffer) - 1, content_length - body_received);
            n = recv(sock, buffer, to_read, 0);
            if (n <= 0)
            {
                break;
            }
            buffer[n] = '\0';
            request.append(buffer, n);
            body_received += n;
        }
        
        return request;
    }

    static std::string handleManualInstall(const std::string& request)
    {
        size_t bodyPos = request.find("\r\n\r\n");
        std::string body = (bodyPos == std::string::npos) ? "" : request.substr(bodyPos + 4);
        if (body.empty()) {
            return StoreInjectView::renderErrorJson("Empty request body.");
        }

        // Parse appid using simple string search (avoid regex on huge body)
        std::string appId;
        size_t appidKeyPos = body.find("\"appid\"");
        if (appidKeyPos != std::string::npos) {
            size_t colonPos = body.find(':', appidKeyPos + 7);
            if (colonPos != std::string::npos) {
                // Skip whitespace after colon
                size_t valStart = colonPos + 1;
                while (valStart < body.size() && (body[valStart] == ' ' || body[valStart] == '\t')) valStart++;
                if (valStart < body.size()) {
                    if (body[valStart] == '"') {
                        // String value: "12345"
                        size_t valEnd = body.find('"', valStart + 1);
                        if (valEnd != std::string::npos)
                            appId = body.substr(valStart + 1, valEnd - valStart - 1);
                    } else if (std::isdigit(body[valStart])) {
                        // Numeric value: 12345
                        size_t valEnd = valStart;
                        while (valEnd < body.size() && std::isdigit(body[valEnd])) valEnd++;
                        appId = body.substr(valStart, valEnd - valStart);
                    }
                }
            }
        }
        if (appId.empty()) {
            return StoreInjectView::renderErrorJson("AppID not found in request.");
        }

        // Parse files array using JSON-string-aware parsing
        // We need to find each {"name":"...","content":"..."} object
        // but base64 content can contain ], }, etc. so we must respect JSON string boundaries
        std::vector<ManualFile> files;
        size_t filesKeyPos = body.find("\"files\"");
        if (filesKeyPos != std::string::npos) {
            size_t startBracket = body.find("[", filesKeyPos);
            if (startBracket != std::string::npos) {
                size_t pos = startBracket + 1;
                while (pos < body.size()) {
                    // Skip whitespace and commas
                    while (pos < body.size() && (body[pos] == ' ' || body[pos] == ',' || body[pos] == '\n' || body[pos] == '\r' || body[pos] == '\t'))
                        pos++;
                    if (pos >= body.size() || body[pos] == ']') break;
                    if (body[pos] != '{') break;

                    // Find the matching closing brace by tracking JSON string boundaries
                    size_t objStart = pos;
                    int braceDepth = 0;
                    bool inString = false;
                    bool escaped = false;
                    size_t objEnd = std::string::npos;

                    for (size_t i = pos; i < body.size(); i++) {
                        char c = body[i];
                        if (escaped) {
                            escaped = false;
                            continue;
                        }
                        if (c == '\\' && inString) {
                            escaped = true;
                            continue;
                        }
                        if (c == '"') {
                            inString = !inString;
                            continue;
                        }
                        if (inString) continue;
                        if (c == '{') braceDepth++;
                        else if (c == '}') {
                            braceDepth--;
                            if (braceDepth == 0) {
                                objEnd = i;
                                break;
                            }
                        }
                    }

                    if (objEnd == std::string::npos) break;

                    // Extract name and content from the object using simple string search
                    std::string fileObject = body.substr(objStart, objEnd - objStart + 1);
                    pos = objEnd + 1;

                    std::string name;
                    std::string content;

                    // Find "name":"value"
                    size_t nameKeyPos = fileObject.find("\"name\"");
                    if (nameKeyPos != std::string::npos) {
                        size_t nc = fileObject.find(':', nameKeyPos + 6);
                        if (nc != std::string::npos) {
                            size_t ns = fileObject.find('"', nc + 1);
                            if (ns != std::string::npos) {
                                size_t ne = fileObject.find('"', ns + 1);
                                if (ne != std::string::npos) {
                                    name = fileObject.substr(ns + 1, ne - ns - 1);
                                }
                            }
                        }
                    }

                    // Find "content":"value" - content is base64 so no escaped quotes inside
                    size_t contentKeyPos = fileObject.find("\"content\"");
                    if (contentKeyPos != std::string::npos) {
                        size_t cc = fileObject.find(':', contentKeyPos + 9);
                        if (cc != std::string::npos) {
                            size_t cs = fileObject.find('"', cc + 1);
                            if (cs != std::string::npos) {
                                // Find the closing quote - base64 won't contain unescaped quotes
                                size_t ce = fileObject.find('"', cs + 1);
                                if (ce != std::string::npos) {
                                    content = fileObject.substr(cs + 1, ce - cs - 1);
                                }
                            }
                        }
                    }

                    if (!name.empty() && !content.empty()) {
                        files.push_back({name, content});
                    }
                }
            }
        }

        if (files.empty()) {
            return "{\"success\":false,\"message\":\"No files to install.\"}";
        }

        std::string steamRoot = LuaDownload::findSteamRoot();
        if (steamRoot.empty()) {
            return "{\"success\":false,\"message\":\"Steam root directory not found.\"}";
        }

        std::filesystem::path depotcache = std::filesystem::path(steamRoot) / "depotcache";
        std::filesystem::path legacyDepotcache = std::filesystem::path(steamRoot) / "config" / "depotcache";
        std::filesystem::path stplugin = std::filesystem::path(steamRoot) / "config" / "stplug-in";
        std::filesystem::create_directories(depotcache);
        std::filesystem::create_directories(legacyDepotcache);
        std::filesystem::create_directories(stplugin);

        int luaCount = 0;
        int manifestCount = 0;

        // First, check if there is a single lua file or a lua file matching appid
        std::string mainLuaIndex = "";
        int totalLuaFiles = 0;
        for (const auto& file : files) {
            std::string ext = std::filesystem::path(file.name).extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".lua") {
                totalLuaFiles++;
                std::string fname = std::filesystem::path(file.name).filename().string();
                if (fname == appId + ".lua" || fname.find(appId) != std::string::npos) {
                    mainLuaIndex = file.name;
                }
            }
        }

        for (const auto& file : files) {
            std::string ext = std::filesystem::path(file.name).extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            
            std::string decodedData;
            try {
                decodedData = base64::from_base64(file.base64Content);
            } catch (...) {
                continue;
            }
            
            if (ext == ".zip") {
                std::string zipPath = "/tmp/sls_manual_" + appId + ".zip";
                if (!AtomicFile::write(zipPath, decodedData, true)) {
                    return "{\"success\":false,\"message\":\"Failed to create temporary zip file.\"}";
                }

                bool validZip = false;
                if (decodedData.size() >= 4) {
                    validZip = (decodedData[0] == 'P' && decodedData[1] == 'K' &&
                               (decodedData[2] == 0x03 || decodedData[2] == 0x05 || decodedData[2] == 0x07));
                }
                if (!validZip) {
                    std::filesystem::remove(zipPath);
                    return "{\"success\":false,\"message\":\"Uploaded file " + file.name + " is not a valid zip archive.\"}";
                }

                std::string extractDir = "/tmp/sls_manual_dir_" + appId;
                std::filesystem::remove_all(extractDir);
                std::filesystem::create_directories(extractDir);

                pid_t pid = fork();
                if (pid == 0) {
                    execlp("unzip", "unzip", "-o", "-q", zipPath.c_str(), "-d", extractDir.c_str(), nullptr);
                    _exit(127);
                }

                int status = 0;
                waitpid(pid, &status, 0);
                std::filesystem::remove(zipPath);

                if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
                    std::filesystem::remove_all(extractDir);
                    return "{\"success\":false,\"message\":\"Failed to extract zip archive using unzip command.\"}";
                }

                std::vector<std::string> zipLuaFiles;
                std::vector<std::string> zipManifestFiles;
                for (const auto& entry : std::filesystem::recursive_directory_iterator(extractDir)) {
                    if (!entry.is_regular_file()) continue;
                    std::string zext = entry.path().extension().string();
                    std::transform(zext.begin(), zext.end(), zext.begin(), ::tolower);
                    if (zext == ".lua") {
                        zipLuaFiles.push_back(entry.path().string());
                    } else if (zext == ".manifest") {
                        zipManifestFiles.push_back(entry.path().string());
                    }
                }

                if (zipLuaFiles.empty() && zipManifestFiles.empty()) {
                    std::filesystem::remove_all(extractDir);
                    return "{\"success\":false,\"message\":\"No .lua or .manifest files found inside the zip archive.\"}";
                }

                for (const auto& mf : zipManifestFiles) {
                    std::string mfName = std::filesystem::path(mf).filename().string();
                    std::string dest = (depotcache / mfName).string();
                    std::string legacyDest = (legacyDepotcache / mfName).string();
                    if (AtomicFile::copy(mf, dest)) {
                        manifestCount++;
                    }
                    AtomicFile::copy(mf, legacyDest);
                }

                if (!zipLuaFiles.empty()) {
                    std::string selectedLua;
                    for (const auto& lf : zipLuaFiles) {
                        std::string fname = std::filesystem::path(lf).filename().string();
                        if (fname == appId + ".lua" || fname.find(appId) != std::string::npos) {
                            selectedLua = lf;
                            break;
                        }
                    }
                    if (selectedLua.empty()) {
                        selectedLua = zipLuaFiles[0];
                    }
                    std::string dest = (stplugin / (appId + ".lua")).string();
                    if (AtomicFile::copy(selectedLua, dest)) {
                        luaCount++;
                    }
                }

                std::filesystem::remove_all(extractDir);
            }
            else if (ext == ".manifest") {
                std::string mfName = std::filesystem::path(file.name).filename().string();
                std::string dest = (depotcache / mfName).string();
                std::string legacyDest = (legacyDepotcache / mfName).string();
                if (AtomicFile::write(dest, decodedData, true)) {
                    manifestCount++;
                }
                AtomicFile::write(legacyDest, decodedData, true);
            }
            else if (ext == ".lua") {
                std::string destFilename = std::filesystem::path(file.name).filename().string();
                std::filesystem::path destPath;
                if (destFilename == appId + ".lua" || file.name == mainLuaIndex || (totalLuaFiles == 1 && mainLuaIndex.empty())) {
                    destPath = stplugin / (appId + ".lua");
                } else {
                    destPath = stplugin / file.name;
                }
                if (AtomicFile::write(destPath.string(), decodedData, true)) {
                    luaCount++;
                }
            }
        }

        uint32_t appIdVal = 0;
        try { appIdVal = std::stoul(appId); } catch (...) {}
        if (appIdVal > 0) {
            g_config.addAdditionalAppId(appIdVal);
            scanLuaPluginsAndUpdateConfig();
            DepotKeys::scanLuaPluginsForDepotKeys();
            Licenses::queueRefresh();
        }

        std::string msg = "Successfully installed";
        if (luaCount > 0) {
            msg += " " + std::to_string(luaCount) + " Lua script" + (luaCount > 1 ? "s" : "");
        }
        if (manifestCount > 0) {
            if (luaCount > 0) msg += " and";
            msg += " " + std::to_string(manifestCount) + " manifest(s)";
        }
        msg += ".";
        return StoreInjectView::renderSuccessJson(msg);
    }

    // generateInjectionJS and patchIndexHtml removed.
    // Cross-context Javascript execution is no longer allowed in modern Steam UI.
    // Store pages cannot fetch() http://127.0.0.1:9001 (their CSP only allows https:), so
    // store-page-script.js pushes "<id> <path>" strings into window.__naviQueue and the
    // automation worker drains it over CDP (see pollPageBridge).

    static std::string urlEncode(const std::string& str)
    {
        static const char* hex = "0123456789ABCDEF";
        std::string out;
        out.reserve(str.size() * 3);
        for (unsigned char c : str)
        {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
                out += static_cast<char>(c);
            else
            {
                out += '%';
                out += hex[c >> 4];
                out += hex[c & 0xF];
            }
        }
        return out;
    }

    // Replays a page request against the local callback server so every existing
    // endpoint handler is reused unchanged. Returns the HTTP status (0 on failure).
    static int loopbackRequest(const std::string& path, std::string& bodyOut)
    {
        bodyOut.clear();
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) return 0;

        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(9001);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

        struct timeval tv;
        tv.tv_sec = 10;
        tv.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0)
        {
            close(sock);
            return 0;
        }

        std::string req = "GET " + path + " HTTP/1.1\r\nHost: 127.0.0.1:9001\r\nConnection: close\r\n\r\n";
        send(sock, req.c_str(), req.size(), 0);

        std::string response;
        char buf[4096];
        ssize_t n;
        while ((n = read(sock, buf, sizeof(buf))) > 0)
            response.append(buf, static_cast<size_t>(n));
        close(sock);

        int status = 0;
        if (response.rfind("HTTP/", 0) == 0 && response.size() > 12)
        {
            try { status = std::stoi(response.substr(9, 3)); } catch (...) {}
        }
        size_t headerEnd = response.find("\r\n\r\n");
        if (headerEnd != std::string::npos)
            bodyOut = response.substr(headerEnd + 4);
        return status;
    }

    // Drains window.__naviQueue in every open store page and answers each request.
    static void pollPageBridge()
    {
        static const char* kDrainExpr =
            R"JS((function(){var q=window.__naviQueue;if(!q||!q.length)return '';return encodeURIComponent(q.splice(0).join('\n'));})())JS";

        auto pages = CDPInject::fetchPages();
        for (const auto& page : pages)
        {
            if (page.webSocketDebuggerUrl.empty() ||
                page.url.find("store.steampowered.com") == std::string::npos)
                continue;

            std::string encoded = CDPInject::evalString(page.webSocketDebuggerUrl, kDrainExpr);
            if (encoded.empty()) continue;

            std::stringstream lines(urlDecode(encoded));
            std::string line;
            while (std::getline(lines, line))
            {
                size_t sp = line.find(' ');
                if (sp == std::string::npos || sp == 0 || sp + 1 >= line.size()) continue;
                std::string id = line.substr(0, sp);
                std::string path = line.substr(sp + 1);

                // Only ever forward plain endpoint paths to the callback server
                if (path[0] != '/' || path.find_first_of(" \r\n") != std::string::npos) continue;
                if (id.find_first_not_of("0123456789-") != std::string::npos) continue;

                std::string body;
                int status = loopbackRequest(path, body);

                std::string js = "window.__naviResolve && window.__naviResolve(\"" + id + "\"," +
                                 std::to_string(status) + ",decodeURIComponent(\"" + urlEncode(body) + "\"));";
                CDPInject::injectJS(page.webSocketDebuggerUrl, js);
            }
        }
    }

    static void automationWorker()
    {
        LOG_INFO("StoreInject: Automation worker started\n");
        
        int checkCounter = 100;
        int bridgeCounter = 0;

        while (!g_shouldStop)
        {
            // Try to connect to CDP 8080 endpoint (dual-stack IPv4/IPv6 with fallback)
            int sock = CDPInject::tcpConnect("127.0.0.1", 8080, 1);
            if (sock >= 0)
            {
                // Serve store-page requests roughly every 300ms
                if (bridgeCounter++ >= 6)
                {
                    bridgeCounter = 0;
                    pollPageBridge();
                }

                // Trigger the C++ CDP injector every 5 seconds to inject new pages
                if (checkCounter++ >= 100)
                {
                    if (g_shouldStop)
                    {
                        close(sock);
                        break;
                    }
                    CDPInject::injectStorePages();
                    if (g_shouldStop)
                    {
                        close(sock);
                        break;
                    }
                    RemoveLua::injectRemoveLuaScript();
                    checkCounter = 0;
                }
                close(sock);
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    struct LocalSyncFile_t
    {
        Sync::ESyncCategory category = Sync::ESyncCategory::k_Configs;
        uint32_t appId = 0;
        std::string logicalPath;
        std::string absPath;
        std::string fileName;
        std::string description;
        std::string sha256Hex;
        uint64_t sizeBytes = 0;
        int64_t mtimeUnixSec = 0;
        std::vector<uint8_t> bytes;
    };

    struct SyncIndexEntry_t
    {
        std::string logicalPath;
        std::string sha256Hex;
        int64_t mtimeUnixSec = 0;
        uint64_t sizeBytes = 0;
        std::string category;
        uint32_t appId = 0;
    };

    struct SyncAutoAddedItem_t
    {
        std::string logicalPath;
        std::string fileName;
        std::string description;
        std::string direction; // "downloaded" or "uploaded"
        int64_t mtimeUnixSec = 0;
        uint64_t sizeBytes = 0;
    };

    struct SyncFolderProgress_t
    {
        std::string folderName;
        std::string folderPath;
        uint32_t totalFiles = 0;
        uint32_t completedFiles = 0;
        bool isDone = false;
    };

    struct SyncActiveProgress_t
    {
        bool inProgress = false;
        std::string operation;
        uint32_t totalFiles = 0;
        uint32_t completedFiles = 0;
        std::chrono::steady_clock::time_point startTime;
        double filesPerSecond = 0.0;
        int64_t estimatedSecondsRemaining = 0;
        std::vector<SyncFolderProgress_t> folders;
        std::mutex mtx;
    };

    static SyncActiveProgress_t g_syncActiveProgress;

    static std::string getGeneralFolderName(const std::string& logicalPath)
    {
        if (logicalPath.rfind("appcache/stats/", 0) == 0) return "Stats & Achievements";
        if (logicalPath.rfind("cache/", 0) == 0) return "Ownership Tickets";
        if (logicalPath.rfind("config/stplug-in/", 0) == 0 || logicalPath.rfind("stplug-in/", 0) == 0) return "Lua Unlock Scripts";
        if (logicalPath == "config/config.yaml" || logicalPath.rfind("config/", 0) == 0) return "Configuration";
        std::filesystem::path p(logicalPath);
        if (p.has_parent_path()) return p.parent_path().string();
        return "General Files";
    }

    static std::string getGeneralFolderPath(const std::string& logicalPath)
    {
        if (logicalPath.rfind("appcache/stats/", 0) == 0) return "appcache/stats";
        if (logicalPath.rfind("cache/", 0) == 0) return "cache";
        if (logicalPath.rfind("config/stplug-in/", 0) == 0 || logicalPath.rfind("stplug-in/", 0) == 0) return "stplug-in";
        if (logicalPath == "config/config.yaml" || logicalPath.rfind("config/", 0) == 0) return "config";
        std::filesystem::path p(logicalPath);
        if (p.has_parent_path()) return p.parent_path().string();
        return "";
    }

    static void initSyncProgressPaths(const std::string& op, const std::vector<std::string>& paths)
    {
        std::lock_guard<std::mutex> lock(g_syncActiveProgress.mtx);
        g_syncActiveProgress.inProgress = true;
        g_syncActiveProgress.operation = op;
        g_syncActiveProgress.totalFiles = static_cast<uint32_t>(paths.size());
        g_syncActiveProgress.completedFiles = 0;
        g_syncActiveProgress.startTime = std::chrono::steady_clock::now();
        g_syncActiveProgress.filesPerSecond = 0.0;
        g_syncActiveProgress.estimatedSecondsRemaining = 0;
        g_syncActiveProgress.folders.clear();

        std::map<std::string, std::pair<std::string, uint32_t>> folderMap;
        for (const auto& path : paths)
        {
            std::string fPath = getGeneralFolderPath(path);
            std::string fName = getGeneralFolderName(path);
            folderMap[fPath].first = fName;
            folderMap[fPath].second++;
        }

        std::vector<std::string> orderedPaths = {"appcache/stats", "cache", "config", "stplug-in"};
        for (const auto& p : orderedPaths)
        {
            auto it = folderMap.find(p);
            if (it != folderMap.end())
            {
                SyncFolderProgress_t fp;
                fp.folderPath = it->first;
                fp.folderName = it->second.first;
                fp.totalFiles = it->second.second;
                fp.completedFiles = 0;
                fp.isDone = false;
                g_syncActiveProgress.folders.push_back(std::move(fp));
                folderMap.erase(it);
            }
        }
        for (const auto& [fPath, info] : folderMap)
        {
            SyncFolderProgress_t fp;
            fp.folderPath = fPath;
            fp.folderName = info.first;
            fp.totalFiles = info.second;
            fp.completedFiles = 0;
            fp.isDone = false;
            g_syncActiveProgress.folders.push_back(std::move(fp));
        }
    }

    template <typename TFilesMap>
    static void initSyncProgress(const std::string& op, const TFilesMap& filesToSync)
    {
        std::vector<std::string> paths;
        paths.reserve(filesToSync.size());
        for (const auto& [path, _] : filesToSync)
        {
            paths.push_back(path);
        }
        initSyncProgressPaths(op, paths);
    }

    static void incrementSyncProgress(const std::string& logicalPath)
    {
        std::lock_guard<std::mutex> lock(g_syncActiveProgress.mtx);
        g_syncActiveProgress.completedFiles++;
        std::string fPath = getGeneralFolderPath(logicalPath);
        for (auto& f : g_syncActiveProgress.folders)
        {
            if (f.folderPath == fPath)
            {
                f.completedFiles++;
                if (f.completedFiles >= f.totalFiles)
                {
                    f.isDone = true;
                }
                break;
            }
        }

        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - g_syncActiveProgress.startTime).count();
        if (elapsedMs > 400 && g_syncActiveProgress.completedFiles > 0)
        {
            double sec = static_cast<double>(elapsedMs) / 1000.0;
            g_syncActiveProgress.filesPerSecond = static_cast<double>(g_syncActiveProgress.completedFiles) / sec;
            uint32_t remaining = g_syncActiveProgress.totalFiles > g_syncActiveProgress.completedFiles
                ? (g_syncActiveProgress.totalFiles - g_syncActiveProgress.completedFiles) : 0;
            if (g_syncActiveProgress.filesPerSecond > 0.05)
            {
                g_syncActiveProgress.estimatedSecondsRemaining = static_cast<int64_t>(remaining / g_syncActiveProgress.filesPerSecond);
            }
        }
    }

    static void finishSyncProgress()
    {
        std::lock_guard<std::mutex> lock(g_syncActiveProgress.mtx);
        g_syncActiveProgress.inProgress = false;
        for (auto& f : g_syncActiveProgress.folders)
        {
            f.completedFiles = f.totalFiles;
            f.isDone = true;
        }
        g_syncActiveProgress.completedFiles = g_syncActiveProgress.totalFiles;
        g_syncActiveProgress.estimatedSecondsRemaining = 0;
    }

    struct SyncDiffPromptItem_t
    {
        std::string logicalPath;
        std::string fileName;
        std::string description;
        std::string category;
        uint32_t appId = 0;
        int64_t localMtimeUnixSec = 0;
        uint64_t localSizeBytes = 0;
        std::string localSha256;
        int64_t remoteMtimeUnixSec = 0;
        uint64_t remoteSizeBytes = 0;
        std::string remoteSha256;
    };

    struct SyncRemovePromptItem_t
    {
        std::string logicalPath;
        std::string fileName;
        std::string description;
        std::string category;
        uint32_t appId = 0;
        std::string removedSide; // "local" (deleted locally, exists on cloud) or "remote" (deleted on cloud, exists locally)
        int64_t mtimeUnixSec = 0;
        uint64_t sizeBytes = 0;
    };

    static std::string escapeSyncJsonStr(const std::string& s)
    {
        std::string out;
        out.reserve(s.size() + 8);
        for (char c : s)
        {
            switch (c)
            {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\b': out += "\\b"; break;
                case '\f': out += "\\f"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20)
                    {
                        char buf[8];
                        snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                        out += buf;
                    }
                    else
                    {
                        out += c;
                    }
                    break;
            }
        }
        return out;
    }

    static std::string renderSyncProgressJson()
    {
        std::lock_guard<std::mutex> lock(g_syncActiveProgress.mtx);
        std::string json = "{";
        json += "\"inProgress\":" + std::string(g_syncActiveProgress.inProgress ? "true" : "false") + ",";
        json += "\"operation\":\"" + escapeSyncJsonStr(g_syncActiveProgress.operation) + "\",";
        json += "\"totalFiles\":" + std::to_string(g_syncActiveProgress.totalFiles) + ",";
        json += "\"completedFiles\":" + std::to_string(g_syncActiveProgress.completedFiles) + ",";
        uint32_t remaining = (g_syncActiveProgress.totalFiles > g_syncActiveProgress.completedFiles)
            ? (g_syncActiveProgress.totalFiles - g_syncActiveProgress.completedFiles) : 0;
        json += "\"remainingFiles\":" + std::to_string(remaining) + ",";
        char spdBuf[32];
        snprintf(spdBuf, sizeof(spdBuf), "%.2f", g_syncActiveProgress.filesPerSecond);
        json += "\"filesPerSecond\":" + std::string(spdBuf) + ",";
        json += "\"estimatedSecondsRemaining\":" + std::to_string(g_syncActiveProgress.estimatedSecondsRemaining) + ",";
        json += "\"folders\":[";
        for (size_t i = 0; i < g_syncActiveProgress.folders.size(); ++i)
        {
            const auto& f = g_syncActiveProgress.folders[i];
            if (i > 0) json += ",";
            uint32_t fRemaining = (f.totalFiles > f.completedFiles) ? (f.totalFiles - f.completedFiles) : 0;
            json += "{";
            json += "\"folderName\":\"" + escapeSyncJsonStr(f.folderName) + "\",";
            json += "\"folderPath\":\"" + escapeSyncJsonStr(f.folderPath) + "\",";
            json += "\"totalFiles\":" + std::to_string(f.totalFiles) + ",";
            json += "\"completedFiles\":" + std::to_string(f.completedFiles) + ",";
            json += "\"remainingFiles\":" + std::to_string(fRemaining) + ",";
            json += "\"isDone\":" + std::string(f.isDone ? "true" : "false");
            json += "}";
        }
        json += "]}";
        return json;
    }

    static int64_t getFileMtimeUnixSec(const std::filesystem::path& p)
    {
        struct stat st{};
        if (::stat(p.c_str(), &st) == 0)
        {
            return static_cast<int64_t>(st.st_mtime);
        }
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    static void setFileMtimeUnixSec(const std::filesystem::path& p, int64_t mtimeUnixSec)
    {
        if (mtimeUnixSec <= 0) return;
        struct timespec ts[2];
        ts[0].tv_sec = static_cast<time_t>(mtimeUnixSec);
        ts[0].tv_nsec = 0;
        ts[1].tv_sec = static_cast<time_t>(mtimeUnixSec);
        ts[1].tv_nsec = 0;
        ::utimensat(AT_FDCWD, p.c_str(), ts, 0);
    }

    static std::string resolveSyncLogicalPathToLocal(
        const std::string& logicalPath,
        const std::string& configDir,
        const std::string& steamRoot)
    {
        if (logicalPath == "config/config.yaml")
        {
            return g_config.getPath();
        }
        if (logicalPath.rfind("stplug-in/", 0) == 0 && logicalPath.size() > 10)
        {
            std::filesystem::path stDir = !steamRoot.empty()
                ? (std::filesystem::path(steamRoot) / "config" / "stplug-in")
                : (std::filesystem::path(configDir) / "stplug-in");
            if (!std::filesystem::exists(stDir) && std::filesystem::exists(std::filesystem::path(configDir) / "stplug-in"))
            {
                stDir = std::filesystem::path(configDir) / "stplug-in";
            }
            return (stDir / logicalPath.substr(10)).string();
        }
        if (logicalPath.rfind("cache/", 0) == 0 && logicalPath.size() > 6)
        {
            return (std::filesystem::path(configDir) / "cache" / logicalPath.substr(6)).string();
        }
        if (logicalPath.rfind("appcache/stats/", 0) == 0 && logicalPath.size() > 15)
        {
            std::filesystem::path statsDir = !steamRoot.empty()
                ? (std::filesystem::path(steamRoot) / "appcache" / "stats")
                : (std::filesystem::path(configDir) / "appcache" / "stats");
            return (statsDir / logicalPath.substr(15)).string();
        }
        return Paths::resolveLogicalSyncPath(logicalPath, 0);
    }

    static std::string describeSyncLogicalPath(const std::string& logicalPath, uint32_t appId)
    {
        std::string appSuffix;
        if (appId != 0)
        {
            std::string gameTitle;
            const auto& titles = g_config.gameTitles.get();
            auto it = titles.find(appId);
            if (it != titles.end() && !it->second.empty())
            {
                gameTitle = it->second;
            }
            if (!gameTitle.empty())
            {
                appSuffix = " — " + gameTitle + " (AppID " + std::to_string(appId) + ")";
            }
            else
            {
                appSuffix = " — AppID " + std::to_string(appId);
            }
        }

        if (logicalPath == "config/config.yaml")
        {
            return "Navi Client Configuration & Preferences";
        }
        if (logicalPath.rfind("stplug-in/", 0) == 0)
        {
            return "Lua Unlock Script & Depot Manifests" + appSuffix;
        }
        if (logicalPath.rfind("cache/ticket_", 0) == 0)
        {
            return "Encrypted App Ownership Ticket" + appSuffix;
        }
        if (logicalPath.rfind("appcache/stats/UserGameStatsSchema_", 0) == 0)
        {
            return "Achievement & Stat Schema Definition" + appSuffix;
        }
        if (logicalPath.rfind("appcache/stats/UserGameStats_", 0) == 0)
        {
            return "Unlocked Achievements & Stats Progress" + appSuffix;
        }
        return "Synchronized Data File (" + logicalPath + ")" + appSuffix;
    }

    static std::map<std::string, std::string> loadSyncBaseHashes(const std::string& configDir)
    {
        std::map<std::string, std::string> result;
        std::filesystem::path p = std::filesystem::path(configDir) / "sync_base_state.yaml";
        if (!std::filesystem::exists(p))
        {
            return result;
        }
        try
        {
            YAML::Node root = YAML::LoadFile(p.string());
            if (root && root.IsMap() && root["SyncedFiles"] && root["SyncedFiles"].IsMap())
            {
                for (const auto& kv : root["SyncedFiles"])
                {
                    if (kv.first.IsScalar() && kv.second.IsScalar())
                    {
                        result[kv.first.Scalar()] = kv.second.Scalar();
                    }
                }
            }
        }
        catch (...) {}
        return result;
    }

    static void saveSyncBaseHashes(const std::string& configDir, const std::map<std::string, std::string>& hashes)
    {
        try
        {
            YAML::Emitter out;
            out << YAML::BeginMap;
            out << YAML::Key << "SyncedFiles" << YAML::Value << YAML::BeginMap;
            for (const auto& [path, sha] : hashes)
            {
                out << YAML::Key << path << YAML::Value << sha;
            }
            out << YAML::EndMap;
            out << YAML::EndMap;
            std::filesystem::path p = std::filesystem::path(configDir) / "sync_base_state.yaml";
            AtomicFile::write(p.string(), std::string(out.c_str()) + "\n");
        }
        catch (...) {}
    }

    static std::map<std::string, SyncIndexEntry_t> loadSyncIndex(const std::string& configDir)
    {
        std::map<std::string, SyncIndexEntry_t> result;
        std::filesystem::path p = std::filesystem::path(configDir) / "sync_index.yaml";
        if (std::filesystem::exists(p))
        {
            try
            {
                YAML::Node root = YAML::LoadFile(p.string());
                if (root && root.IsMap() && root["files"] && root["files"].IsMap())
                {
                    for (const auto& kv : root["files"])
                    {
                        if (kv.first.IsScalar() && kv.second.IsMap())
                        {
                            std::string logPath = kv.first.Scalar();
                            const auto& node = kv.second;
                            SyncIndexEntry_t entry;
                            entry.logicalPath = logPath;
                            if (node["sha256"] && node["sha256"].IsScalar())
                                entry.sha256Hex = node["sha256"].Scalar();
                            if (node["mtime"] && node["mtime"].IsScalar())
                            {
                                try { entry.mtimeUnixSec = node["mtime"].as<int64_t>(); } catch (...) {}
                            }
                            if (node["size"] && node["size"].IsScalar())
                            {
                                try { entry.sizeBytes = node["size"].as<uint64_t>(); } catch (...) {}
                            }
                            if (node["category"] && node["category"].IsScalar())
                                entry.category = node["category"].Scalar();
                            if (node["app_id"] && node["app_id"].IsScalar())
                            {
                                try { entry.appId = node["app_id"].as<uint32_t>(); } catch (...) {}
                            }
                            result[logPath] = std::move(entry);
                        }
                    }
                }
            }
            catch (...) {}
        }

        // Fall back / merge entries from loadSyncBaseHashes(configDir)
        auto baseHashes = loadSyncBaseHashes(configDir);
        for (const auto& [bPath, bSha] : baseHashes)
        {
            auto it = result.find(bPath);
            if (it == result.end())
            {
                SyncIndexEntry_t entry;
                entry.logicalPath = bPath;
                entry.sha256Hex = bSha;
                result[bPath] = std::move(entry);
            }
            else if (it->second.sha256Hex.empty())
            {
                it->second.sha256Hex = bSha;
            }
        }

        return result;
    }

    static void saveSyncIndex(const std::string& configDir, const std::map<std::string, SyncIndexEntry_t>& index)
    {
        try
        {
            std::map<std::string, std::string> baseHashes;
            int64_t nowSec = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();

            YAML::Emitter out;
            out << YAML::BeginMap;
            out << YAML::Key << "schema_version" << YAML::Value << 1;
            out << YAML::Key << "updated_at_unix_sec" << YAML::Value << nowSec;
            out << YAML::Key << "files" << YAML::Value << YAML::BeginMap;

            for (const auto& [path, entry] : index)
            {
                out << YAML::Key << path << YAML::Value << YAML::BeginMap;
                out << YAML::Key << "sha256" << YAML::Value << entry.sha256Hex;
                out << YAML::Key << "mtime" << YAML::Value << entry.mtimeUnixSec;
                out << YAML::Key << "size" << YAML::Value << entry.sizeBytes;
                out << YAML::Key << "category" << YAML::Value << entry.category;
                out << YAML::Key << "app_id" << YAML::Value << entry.appId;
                out << YAML::EndMap;

                if (!entry.sha256Hex.empty())
                {
                    baseHashes[path] = entry.sha256Hex;
                }
            }

            out << YAML::EndMap;
            out << YAML::EndMap;

            std::filesystem::path p = std::filesystem::path(configDir) / "sync_index.yaml";
            AtomicFile::write(p.string(), std::string(out.c_str()) + "\n");

            saveSyncBaseHashes(configDir, baseHashes);
        }
        catch (...) {}
    }

    static bool ensureLocalSyncFileBytes(LocalSyncFile_t& lf)
    {
        if (!lf.bytes.empty())
        {
            return true;
        }
        if (lf.absPath.empty() || !std::filesystem::exists(lf.absPath))
        {
            return false;
        }
        std::ifstream ifs(lf.absPath, std::ios::binary);
        if (ifs.is_open())
        {
            lf.bytes.assign((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        }
        return !lf.bytes.empty() || (std::filesystem::is_regular_file(lf.absPath) && std::filesystem::file_size(lf.absPath) == 0);
    }

    static std::map<std::string, LocalSyncFile_t> collectLocalSyncFiles(
        const std::string& configDir,
        const std::string& steamRoot,
        const Sync::SyncSelectionFilter_t& filter)
    {
        std::map<std::string, LocalSyncFile_t> files;

        Sync::SyncSelectionFilter_t effectiveFilter = filter;
        if (!effectiveFilter.allConfigs && !effectiveFilter.allApiKeys && !effectiveFilter.allManifestsAndLua &&
            !effectiveFilter.allCloudSaves && !effectiveFilter.allAchievements &&
            effectiveFilter.configKeys.empty() && effectiveFilter.providerNames.empty() &&
            effectiveFilter.manifestAppIds.empty() && effectiveFilter.cloudSaveAppIds.empty() &&
            effectiveFilter.achievementAppIds.empty())
        {
            effectiveFilter.allConfigs = true;
            effectiveFilter.allManifestsAndLua = true;
            effectiveFilter.allAchievements = true;
            effectiveFilter.allCloudSaves = true;
            effectiveFilter.allApiKeys = true;
        }

        bool pushConfigs = effectiveFilter.allConfigs || effectiveFilter.allApiKeys || !effectiveFilter.configKeys.empty() || !effectiveFilter.providerNames.empty();
        bool pushLua = effectiveFilter.allManifestsAndLua || !effectiveFilter.manifestAppIds.empty();

        auto syncIndex = loadSyncIndex(configDir);
        bool indexModified = false;

        auto processFile = [&](const std::string& logicalPath,
                               const std::string& filePath,
                               const std::string& fileName,
                               Sync::ESyncCategory category,
                               uint32_t appId,
                               const std::string& description) {
            std::error_code ec;
            uint64_t sizeBytes = std::filesystem::file_size(filePath, ec);
            if (ec) return;
            int64_t mtimeUnixSec = getFileMtimeUnixSec(filePath);

            LocalSyncFile_t lf;
            lf.category = category;
            lf.appId = appId;
            lf.logicalPath = logicalPath;
            lf.absPath = filePath;
            lf.fileName = fileName;
            lf.description = description;
            lf.mtimeUnixSec = mtimeUnixSec;
            lf.sizeBytes = sizeBytes;

            auto it = syncIndex.find(logicalPath);
            if (it != syncIndex.end() && it->second.mtimeUnixSec == mtimeUnixSec &&
                it->second.sizeBytes == sizeBytes && !it->second.sha256Hex.empty())
            {
                lf.sha256Hex = it->second.sha256Hex;
                lf.bytes.clear(); // lazy load if needed
            }
            else
            {
                std::ifstream ifs(filePath, std::ios::binary);
                if (ifs.is_open())
                {
                    lf.bytes.assign((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
                    lf.sha256Hex = Sync::computeBufferSha256Hex(
                        std::string_view(reinterpret_cast<const char*>(lf.bytes.data()), lf.bytes.size()));

                    SyncIndexEntry_t entry;
                    entry.logicalPath = logicalPath;
                    entry.sha256Hex = lf.sha256Hex;
                    entry.mtimeUnixSec = mtimeUnixSec;
                    entry.sizeBytes = sizeBytes;
                    entry.category = Sync::syncCategoryToString(category);
                    entry.appId = appId;
                    syncIndex[logicalPath] = std::move(entry);
                    indexModified = true;
                }
                else
                {
                    return;
                }
            }
            files[logicalPath] = std::move(lf);
        };

        // 1. config/config.yaml
        if (pushConfigs)
        {
            std::string configPath = g_config.getPath();
            if (std::filesystem::exists(configPath))
            {
                processFile("config/config.yaml", configPath, "config.yaml",
                            Sync::ESyncCategory::k_Configs, 0,
                            describeSyncLogicalPath("config/config.yaml", 0));
            }
        }

        // 2. stplug-in/*.lua & cache/ticket_*.yaml
        if (pushLua)
        {
            std::filesystem::path stDir = !steamRoot.empty()
                ? (std::filesystem::path(steamRoot) / "config" / "stplug-in")
                : (std::filesystem::path(configDir) / "stplug-in");
            if (!std::filesystem::exists(stDir))
            {
                stDir = std::filesystem::path(configDir) / "stplug-in";
            }

            if (std::filesystem::exists(stDir) && std::filesystem::is_directory(stDir))
            {
                for (const auto& de : std::filesystem::directory_iterator(stDir))
                {
                    if (de.is_regular_file() && de.path().extension() == ".lua")
                    {
                        uint32_t fileAppId = 0;
                        {
                            std::string stemStr = de.path().stem().string();
                            char* endPtr = nullptr;
                            unsigned long parsed = std::strtoul(stemStr.c_str(), &endPtr, 10);
                            if (endPtr != stemStr.c_str() && *endPtr == '\0')
                                fileAppId = static_cast<uint32_t>(parsed);
                        }

                        if (!effectiveFilter.allManifestsAndLua && (fileAppId == 0 || effectiveFilter.manifestAppIds.count(fileAppId) == 0))
                        {
                            continue;
                        }

                        std::string fileName = de.path().filename().string();
                        std::string logicalPath = "stplug-in/" + fileName;
                        processFile(logicalPath, de.path().string(), fileName,
                                    Sync::ESyncCategory::k_ManifestsAndLua, fileAppId,
                                    describeSyncLogicalPath(logicalPath, fileAppId));
                    }
                }
            }

            std::filesystem::path cacheDir = std::filesystem::path(configDir) / "cache";
            if (std::filesystem::exists(cacheDir) && std::filesystem::is_directory(cacheDir))
            {
                for (const auto& de : std::filesystem::directory_iterator(cacheDir))
                {
                    if (de.is_regular_file() && de.path().extension() == ".yaml")
                    {
                        std::string stem = de.path().stem().string();
                        if (stem.rfind("ticket_", 0) == 0 && stem.size() > 7)
                        {
                            uint32_t fileAppId = 0;
                            {
                                std::string subStr = stem.substr(7);
                                char* endPtr = nullptr;
                                unsigned long parsed = std::strtoul(subStr.c_str(), &endPtr, 10);
                                if (endPtr != subStr.c_str() && *endPtr == '\0')
                                    fileAppId = static_cast<uint32_t>(parsed);
                            }

                            if (effectiveFilter.allManifestsAndLua || (fileAppId != 0 && effectiveFilter.manifestAppIds.count(fileAppId) > 0))
                            {
                                std::string fileName = de.path().filename().string();
                                std::string logicalPath = "cache/" + fileName;
                                processFile(logicalPath, de.path().string(), fileName,
                                            Sync::ESyncCategory::k_ManifestsAndLua, fileAppId,
                                            describeSyncLogicalPath(logicalPath, fileAppId));
                            }
                        }
                    }
                }
            }
        }

        // 3. appcache/stats/UserGameStats*.bin
        if ((effectiveFilter.allAchievements || !effectiveFilter.achievementAppIds.empty()) && !steamRoot.empty())
        {
            std::filesystem::path statsDir = std::filesystem::path(steamRoot) / "appcache" / "stats";
            if (std::filesystem::exists(statsDir) && std::filesystem::is_directory(statsDir))
            {
                for (const auto& de : std::filesystem::directory_iterator(statsDir))
                {
                    if (de.is_regular_file() && de.path().extension() == ".bin")
                    {
                        std::string stem = de.path().stem().string();
                        if (stem.rfind("UserGameStats", 0) == 0)
                        {
                            size_t lastUnderscore = stem.rfind('_');
                            if (lastUnderscore != std::string::npos && lastUnderscore + 1 < stem.size())
                            {
                                uint32_t fileAppId = 0;
                                {
                                    std::string subStr = stem.substr(lastUnderscore + 1);
                                    char* endPtr = nullptr;
                                    unsigned long parsed = std::strtoul(subStr.c_str(), &endPtr, 10);
                                    if (endPtr != subStr.c_str() && *endPtr == '\0')
                                        fileAppId = static_cast<uint32_t>(parsed);
                                }

                                if (effectiveFilter.allAchievements || (fileAppId != 0 && effectiveFilter.achievementAppIds.count(fileAppId) > 0))
                                {
                                    std::string fileName = de.path().filename().string();
                                    std::string logicalPath = "appcache/stats/" + fileName;
                                    processFile(logicalPath, de.path().string(), fileName,
                                                Sync::ESyncCategory::k_Achievements, fileAppId,
                                                describeSyncLogicalPath(logicalPath, fileAppId));
                                }
                            }
                        }
                    }
                }
            }
        }

        if (indexModified)
        {
            saveSyncIndex(configDir, syncIndex);
        }

        return files;
    }

    static std::map<std::string, std::string> extractJsonStringMap(const std::string& json, const std::string& key)
    {
        std::map<std::string, std::string> result;
        std::string needle = "\"" + key + "\"";
        size_t pos = json.find(needle);
        if (pos == std::string::npos) return result;
        pos = json.find(':', pos + needle.size());
        if (pos == std::string::npos) return result;
        pos = json.find('{', pos);
        if (pos == std::string::npos) return result;

        size_t depth = 1;
        size_t end = pos + 1;
        while (end < json.size() && depth > 0)
        {
            if (json[end] == '"')
            {
                end++;
                while (end < json.size())
                {
                    if (json[end] == '\\') { end += 2; continue; }
                    if (json[end] == '"') break;
                    end++;
                }
            }
            else if (json[end] == '{') depth++;
            else if (json[end] == '}') depth--;
            end++;
        }
        if (depth != 0) return result;

        std::string inside = json.substr(pos + 1, end - pos - 2);
        size_t p = 0;
        while (p < inside.size())
        {
            size_t kStart = inside.find('"', p);
            if (kStart == std::string::npos) break;
            size_t kEnd = kStart + 1;
            while (kEnd < inside.size())
            {
                if (inside[kEnd] == '\\') { kEnd += 2; continue; }
                if (inside[kEnd] == '"') break;
                kEnd++;
            }
            if (kEnd >= inside.size()) break;
            std::string mapKey = inside.substr(kStart + 1, kEnd - kStart - 1);

            size_t colon = inside.find(':', kEnd + 1);
            if (colon == std::string::npos) break;
            size_t vStart = inside.find('"', colon + 1);
            if (vStart == std::string::npos) break;
            size_t vEnd = vStart + 1;
            while (vEnd < inside.size())
            {
                if (inside[vEnd] == '\\') { vEnd += 2; continue; }
                if (inside[vEnd] == '"') break;
                vEnd++;
            }
            if (vEnd >= inside.size()) break;
            std::string mapVal = inside.substr(vStart + 1, vEnd - vStart - 1);

            result[mapKey] = mapVal;
            size_t comma = inside.find(',', vEnd + 1);
            if (comma == std::string::npos) break;
            p = comma + 1;
        }
        return result;
    }

    static void callbackServer()
    {
        int server_fd, new_socket;
        struct sockaddr_in address;
        int opt = 1;
        int addrlen = sizeof(address);

        if ((server_fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0)) < 0) return;
        g_serverFd = server_fd;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));

        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(9001);

        if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
        {
            int fd = g_serverFd.exchange(-1);
            if (fd >= 0) close(fd);
            return;
        }
        if (listen(server_fd, 5) < 0)
        {
            int fd = g_serverFd.exchange(-1);
            if (fd >= 0) close(fd);
            return;
        }

        LOG_INFO("StoreInject: Callback server listening on port 9001\n");

        while (!g_shouldStop)
        {
            struct timeval tv;
            tv.tv_sec = 1;
            tv.tv_usec = 0;
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(server_fd, &readfds);

            if (select(server_fd + 1, &readfds, NULL, NULL, &tv) > 0)
            {
                if ((new_socket = accept4(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen, SOCK_CLOEXEC)) >= 0)
                {
                    std::string request = readFullHttpRequest(new_socket);
                    bool handled = false;
                    if (!request.empty())
                    {
                        // Handle CORS preflight (OPTIONS) requests for all endpoints
                        if (request.rfind("OPTIONS ", 0) == 0)
                        {
                            send(new_socket, StoreInjectView::CORS_PREFLIGHT_RESPONSE, strlen(StoreInjectView::CORS_PREFLIGHT_RESPONSE), 0);
                            close(new_socket);
                            handled = true;
                        }
                        else if (request.find("/set-api-keys") != std::string::npos)
                        {
                            try {
                                size_t qPos = request.find("/set-api-keys");
                                size_t lineEnd = request.find("\r\n", qPos);
                                std::string reqLine = request.substr(qPos, lineEnd == std::string::npos ? std::string::npos : lineEnd - qPos);
                                size_t qmark = reqLine.find('?');
                                std::string queryString = (qmark != std::string::npos) ? reqLine.substr(qmark + 1) : "";
                                size_t spacePos = queryString.find(' ');
                                if (spacePos != std::string::npos) queryString = queryString.substr(0, spacePos);

                                auto extractQueryParam = [&](const std::string& paramName) -> std::string {
                                    size_t p = 0;
                                    while ((p = queryString.find(paramName + "=", p)) != std::string::npos) {
                                        if (p == 0 || queryString[p - 1] == '&' || queryString[p - 1] == '?') {
                                            size_t vStart = p + paramName.length() + 1;
                                            size_t vEnd = queryString.find('&', vStart);
                                            if (vEnd == std::string::npos) vEnd = queryString.length();
                                            return urlDecode(queryString.substr(vStart, vEnd - vStart));
                                        }
                                        p += paramName.length() + 1;
                                    }
                                    return "";
                                };

                                std::string ryuu = extractQueryParam("ryuu");
                                std::string dpbx = extractQueryParam("dpbx");
                                std::string hubcap = extractQueryParam("hubcap");
                                std::string morr = extractQueryParam("morr");
                                if (hubcap.empty() && !morr.empty()) hubcap = morr;

                                g_config.ryuuKey = ryuu;
                                g_config.depotBoxKey = dpbx;
                                g_config.hubcapKey = hubcap;
                                g_config.updateApiAuth(ryuu, dpbx, hubcap);

                                std::string respBody = StoreInjectView::renderSuccessJson("API keys saved");
                                std::string response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\nContent-Length: " + std::to_string(respBody.size()) + "\r\n\r\n" + respBody;
                                send(new_socket, response.c_str(), response.size(), 0);
                                close(new_socket);
                                handled = true;
                            } catch (...) {
                                std::string respBody = StoreInjectView::renderErrorJson("Failed to save API keys");
                                std::string response = "HTTP/1.1 500 Internal Server Error\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\nContent-Length: " + std::to_string(respBody.size()) + "\r\n\r\n" + respBody;
                                send(new_socket, response.c_str(), response.size(), 0);
                                close(new_socket);
                                handled = true;
                            }
                        }
                        else if (request.find("/api-keys") != std::string::npos)
                        {
                            try {
                                g_config.loadSettings();
                                std::string respBody = StoreInjectView::renderApiKeysJson(g_config.ryuuKey.get(), g_config.depotBoxKey.get(), g_config.hubcapKey.get());
                                std::string response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\nContent-Length: " + std::to_string(respBody.size()) + "\r\n\r\n" + respBody;
                                send(new_socket, response.c_str(), response.size(), 0);
                                close(new_socket);
                                handled = true;
                            } catch (...) {
                                std::string respBody = StoreInjectView::renderErrorJson("Failed to load API keys");
                                std::string response = "HTTP/1.1 500 Internal Server Error\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\nContent-Length: " + std::to_string(respBody.size()) + "\r\n\r\n" + respBody;
                                send(new_socket, response.c_str(), response.size(), 0);
                                close(new_socket);
                                handled = true;
                            }
                        }
                        else if (request.find("/manual-install") != std::string::npos)
                        {
                            try {
                                LOG_INFO("StoreInject: Received /manual-install request\n");
                                std::string respBody = handleManualInstall(request);
                                std::string response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\nContent-Length: " + std::to_string(respBody.size()) + "\r\n\r\n" + respBody;
                                send(new_socket, response.c_str(), response.size(), 0);
                                close(new_socket);
                                handled = true;
                            } catch (const std::exception& e) {
                                LOG_WARN("StoreInject: Exception during manual-install: %s\n", e.what());
                                std::string respBody = StoreInjectView::renderErrorJson("Internal server error: " + std::string(e.what()));
                                std::string response = "HTTP/1.1 500 Internal Server Error\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\nContent-Length: " + std::to_string(respBody.size()) + "\r\n\r\n" + respBody;
                                send(new_socket, response.c_str(), response.size(), 0);
                                close(new_socket);
                                handled = true;
                            } catch (...) {
                                std::string respBody = StoreInjectView::renderErrorJson("Unknown internal server error.");
                                std::string response = "HTTP/1.1 500 Internal Server Error\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\nContent-Length: " + std::to_string(respBody.size()) + "\r\n\r\n" + respBody;
                                send(new_socket, response.c_str(), response.size(), 0);
                                close(new_socket);
                                handled = true;
                            }
                        }
                        else if (request.find("/lua_apps") != std::string::npos)
                        {
                            try {
                                std::set<uint32_t> appIds;

                                // 1. Scan pluginDir for all <appid>.lua files
                                std::string pluginDir = g_config.getPluginDir();
                                if (!pluginDir.empty() && std::filesystem::exists(pluginDir))
                                {
                                    for (const auto& entry : std::filesystem::directory_iterator(pluginDir))
                                    {
                                        if (entry.is_regular_file() && entry.path().extension() == ".lua")
                                        {
                                            std::string stem = entry.path().stem().string();
                                            try {
                                                uint32_t aid = std::stoul(stem);
                                                if (aid > 0)
                                                    appIds.insert(aid);
                                            } catch (...) {}
                                        }
                                    }
                                }

                                // 2. Also include any addedAppIds from g_config
                                for (const auto& aid : g_config.addedAppIds.get())
                                {
                                    if (aid > 0)
                                        appIds.insert(aid);
                                }

                                std::string json = StoreInjectView::renderLuaAppsJson(appIds);
                                std::string response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\nContent-Length: " + std::to_string(json.size()) + "\r\n\r\n" + json;
                                send(new_socket, response.c_str(), response.size(), 0);
                                close(new_socket);
                                handled = true;
                            } catch (...) {
                                std::string respBody = "{\"appids\":[]}";
                                std::string response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\nContent-Length: " + std::to_string(respBody.size()) + "\r\n\r\n" + respBody;
                                send(new_socket, response.c_str(), response.size(), 0);
                                close(new_socket);
                                handled = true;
                            }
                        }
                        else if (request.find("/check?id=") != std::string::npos)
                        {
                            try {
                                size_t idPos = request.find("id=");
                                if (idPos != std::string::npos)
                                {
                                    size_t endPos = request.find_first_of(" &", idPos);
                                    std::string idStr = request.substr(idPos + 3, (endPos == std::string::npos) ? std::string::npos : (endPos - (idPos + 3)));
                                    uint32_t appId = std::stoul(idStr);
                                    LOG_INFO("StoreInject: Received /check for AppID %u\n", appId);

                                    if (Apps::isNonSteamApp(appId))
                                    {
                                        std::string json = StoreInjectView::renderCheckJson(false, false, false, false, false, false, false, false, false, "");
                                        std::string response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n" + json;
                                        send(new_socket, response.c_str(), response.size(), 0);
                                        close(new_socket);
                                        handled = true;
                                        continue;
                                    }
                                    
                                    bool gameExists = Apps::gameFilesExist(appId);
                                    bool luaExists = false;
                                    bool isUnlocked = g_config.isAddedAppId(appId);

                                    std::string pluginDir = g_config.getPluginDir();
                                    if (!pluginDir.empty()) {
                                        auto luaPath = std::filesystem::path(pluginDir) / (idStr + ".lua");
                                        if (std::filesystem::exists(luaPath)) {
                                            luaExists = true;
                                        }
                                    }

                                    bool locallyOwned = g_pSteamEngine && g_pSteamEngine->getUser(0) && g_pSteamEngine->getUser(0)->isSubscribed(appId);
                                    if (luaExists && !isUnlocked && (locallyOwned || DepotKeys::isKeyManagedApp(appId))) {
                                        g_config.addAdditionalAppId(appId);
                                        isUnlocked = true;
                                    }

                                    // Report exists as true if the Lua plugin exists or if the app is unlocked and has game files
                                    bool exists = (isUnlocked || luaExists) && (gameExists || luaExists);
                                    bool pending = g_pendingRestartApps.count(appId) > 0;
                                    bool downloading = false;
                                    bool onlineFixInstalled = Apps::isOnlineFixInstalled(appId);
                                    bool autoCrackInstalled = Apps::isAutoCrackInstalled(appId);
                                    bool paused = false;

                                    // Read provider name from sidecar file
                                    std::string providerName;
                                    if (!pluginDir.empty()) {
                                        auto providerPath = std::filesystem::path(pluginDir) / (idStr + ".provider");
                                        if (std::filesystem::exists(providerPath)) {
                                            std::ifstream pf(providerPath);
                                            if (pf.is_open()) {
                                                std::getline(pf, providerName);
                                            }
                                        }
                                    }

                                    std::string json = StoreInjectView::renderCheckJson(exists, luaExists, gameExists, isUnlocked, pending, downloading, paused, onlineFixInstalled, autoCrackInstalled, providerName);
                                    std::string response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n" + json;
                                    send(new_socket, response.c_str(), response.size(), 0);
                                    close(new_socket);
                                    handled = true;
                                }
                            } catch (...) {
                                handled = false;
                                close(new_socket);
                            }
                        }
                        else if (request.find("/get-depots?id=") != std::string::npos)
                        {
                            try {
                                size_t idPos = request.find("id=");
                                if (idPos != std::string::npos)
                                {
                                    size_t endPos = request.find_first_of(" &", idPos);
                                    std::string idStr = request.substr(idPos + 3, (endPos == std::string::npos) ? std::string::npos : (endPos - (idPos + 3)));
                                    uint32_t appId = std::stoul(idStr);
                                    LOG_INFO("StoreInject: Received /get-depots for AppID %u\n", appId);

                                    if (Apps::isNonSteamApp(appId))
                                    {
                                        std::string respBody = StoreInjectView::renderErrorJson("Non-Steam game has no Steam depots");
                                        std::string response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\nContent-Length: " + std::to_string(respBody.size()) + "\r\n\r\n" + respBody;
                                        send(new_socket, response.c_str(), response.size(), 0);
                                        close(new_socket);
                                        handled = true;
                                        continue;
                                    }

                                    std::string pluginDir = g_config.getPluginDir();
                                    auto luaPath = std::filesystem::path(pluginDir) / (idStr + ".lua");
                                    std::string respBody;
                                    if (std::filesystem::exists(luaPath)) {
                                        auto parseResult = LuaParser::parseFile(luaPath.string());
                                        if (parseResult.valid) {
                                            std::map<std::string, LuaParser::DepotInfo> allDepots;
                                            for (auto& [depotId, info] : parseResult.depots) {
                                                allDepots[depotId] = info;
                                            }
                                            for (auto& [depotId, gid] : parseResult.manifests) {
                                                if (allDepots.find(depotId) == allDepots.end()) {
                                                    LuaParser::DepotInfo info;
                                                    info.depotId = depotId;
                                                    info.manifestGid = gid;
                                                    auto szIt = parseResult.manifestSizes.find(depotId);
                                                    if (szIt != parseResult.manifestSizes.end()) {
                                                        info.sizeBytes = szIt->second;
                                                    }
                                                    allDepots[depotId] = info;
                                                }
                                            }

                                            size_t validKeysCount = 0;
                                            for (auto& [depotId, info] : allDepots) {
                                                uint32_t dId = 0;
                                                try { dId = std::stoul(depotId); } catch (...) {}
                                                std::string key = info.key;
                                                if (key.empty() && dId > 0) {
                                                    key = DepotKeys::getDepotKey(dId);
                                                }
                                                if (key.size() == 64) validKeysCount++;
                                            }

                                            bool locallyOwned = g_pSteamEngine && g_pSteamEngine->getUser(0) && g_pSteamEngine->getUser(0)->isSubscribed(appId);
                                            respBody = StoreInjectView::renderDepotsJson(allDepots, validKeysCount, locallyOwned);
                                        } else {
                                            respBody = StoreInjectView::renderErrorJson("Invalid Lua plugin file");
                                        }
                                    } else {
                                        respBody = StoreInjectView::renderErrorJson("Lua plugin file not found");
                                    }

                                    std::string response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\nContent-Length: " + std::to_string(respBody.size()) + "\r\n\r\n" + respBody;
                                    send(new_socket, response.c_str(), response.size(), 0);
                                    close(new_socket);
                                    handled = true;
                                }
                            } catch (...) { handled = false; close(new_socket); }
                        }
                        else if (request.find("/start-download?id=") != std::string::npos)
                        {
                            try {
                                size_t idPos = request.find("id=");
                                if (idPos != std::string::npos)
                                {
                                    size_t idEndPos = request.find("&", idPos);
                                    std::string idStr = request.substr(idPos + 3, (idEndPos == std::string::npos) ? std::string::npos : (idEndPos - (idPos + 3)));
                                    char* pEndApp = nullptr;
                                    uint32_t appId = static_cast<uint32_t>(std::strtoul(idStr.c_str(), &pEndApp, 10));
                                    if (pEndApp == idStr.c_str() || appId == 0) {
                                        close(new_socket);
                                        handled = true;
                                        continue;
                                    }

                                    if (Apps::isNonSteamApp(appId))
                                    {
                                        std::string respBody = StoreInjectView::renderErrorJson("Cannot download non-Steam game");
                                        std::string response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\nContent-Length: " + std::to_string(respBody.size()) + "\r\n\r\n" + respBody;
                                        send(new_socket, response.c_str(), response.size(), 0);
                                        close(new_socket);
                                        handled = true;
                                        continue;
                                    }

                                    std::string depots = "";
                                    size_t depotsPos = request.find("depots=");
                                    if (depotsPos != std::string::npos) {
                                        size_t depotsEndPos = request.find_first_of(" &", depotsPos);
                                        depots = request.substr(depotsPos + 7, (depotsEndPos == std::string::npos) ? std::string::npos : (depotsEndPos - (depotsPos + 7)));
                                    }

                                    LOG_INFO("StoreInject: Received /start-download for AppID %u with depots %s\n", appId, depots.c_str());

                                    std::string decodedDepots = urlDecode(depots);
                                    bool started = NativeInstall::arm(appId, decodedDepots);

                                    std::string respBody = started
                                        ? "{\"success\":true,\"mode\":\"native\"}"
                                        : StoreInjectView::renderErrorJson("Could not start install (no decryption keys available)");
                                    std::string response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\nContent-Length: " + std::to_string(respBody.size()) + "\r\n\r\n" + respBody;
                                    send(new_socket, response.c_str(), response.size(), 0);
                                    close(new_socket);
                                    handled = true;
                                }
                            } catch (...) { handled = false; close(new_socket); }
                        }
                        else if (request.find("/cancel-download?id=") != std::string::npos)
                        {
                            try {
                                size_t idPos = request.find("id=");
                                if (idPos != std::string::npos)
                                {
                                    size_t endPos = request.find_first_of(" &", idPos);
                                    std::string idStr = request.substr(idPos + 3, (endPos == std::string::npos) ? std::string::npos : (endPos - (idPos + 3)));
                                    uint32_t appId = std::stoul(idStr);
                                    LOG_INFO("StoreInject: Received /cancel-download for AppID %u\n", appId);

                                    NativeInstall::discardCachedInstall(appId);
                                    NativeInstall::disarm(appId);
                                    std::string respBody = StoreInjectView::renderSuccessJson();
                                    std::string response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\nContent-Length: " + std::to_string(respBody.size()) + "\r\n\r\n" + respBody;
                                    send(new_socket, response.c_str(), response.size(), 0);
                                    close(new_socket);
                                    handled = true;
                                }
                            } catch (...) { handled = false; close(new_socket); }
                        }
                        else if (request.find("/download?") != std::string::npos || request.find("/download ") != std::string::npos)
                        {
                            try {
                                size_t qPos = request.find("/download");
                                size_t lineEnd = request.find("\r\n", qPos);
                                std::string reqLine = request.substr(qPos, lineEnd == std::string::npos ? std::string::npos : lineEnd - qPos);
                                size_t qmark = reqLine.find('?');
                                std::string queryString = (qmark != std::string::npos) ? reqLine.substr(qmark + 1) : "";
                                size_t spacePos = queryString.find(' ');
                                if (spacePos != std::string::npos) queryString = queryString.substr(0, spacePos);

                                auto extractQueryParam = [&](const std::string& paramName) -> std::string {
                                    size_t p = 0;
                                    while ((p = queryString.find(paramName + "=", p)) != std::string::npos) {
                                        if (p == 0 || queryString[p - 1] == '&' || queryString[p - 1] == '?') {
                                            size_t vStart = p + paramName.length() + 1;
                                            size_t vEnd = queryString.find('&', vStart);
                                            if (vEnd == std::string::npos) vEnd = queryString.length();
                                            return urlDecode(queryString.substr(vStart, vEnd - vStart));
                                        }
                                        p += paramName.length() + 1;
                                    }
                                    return "";
                                };

                                std::string pid = extractQueryParam("id");
                                std::string provStr = extractQueryParam("provider");
                                std::string orderStr = extractQueryParam("order");

                                int providerIndex = -1;
                                if (!provStr.empty()) {
                                    try { providerIndex = std::stoi(provStr); } catch (...) {}
                                }
                                std::vector<int> order = parseProviderOrder(orderStr);

                                if (!pid.empty()) {
                                    LOG_INFO("StoreInject: Received /download for AppID %s (provider=%d, order=%s)\n",
                                             pid.c_str(), providerIndex, orderStr.c_str());

                                    std::thread([pid, providerIndex, order]() {
                                        bool ok = LuaDownload::downloadAndInstall(pid, providerIndex, order, true);
                                        if (ok)
                                        {
                                            LOG_INFO("LuaDownload: Completed for appid=%s\n", pid.c_str());
                                            try {
                                                uint32_t appId = std::stoul(pid);
                                                g_config.addAdditionalAppId(appId);
                                                Licenses::queueRefresh();
                                            } catch (...) {
                                                LOG_WARN("LuaDownload: Failed to register appid=%s in library\n", pid.c_str());
                                            }
                                        }
                                        else
                                        {
                                            LOG_INFO("LuaDownload: Failed for appid=%s\n", pid.c_str());
                                        }
                                    }).detach();
                                }

                                std::string respBody = "{\"success\":true}";
                                std::string response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\nContent-Length: " + std::to_string(respBody.size()) + "\r\n\r\n" + respBody;
                                send(new_socket, response.c_str(), response.size(), 0);
                                close(new_socket);
                                handled = true;
                            } catch (...) {
                                handled = false;
                                close(new_socket);
                            }
                        }
                        else if (request.find("/install-fix?id=") != std::string::npos)
                        {
                            try {
                                size_t idPos = request.find("id=");
                                if (idPos != std::string::npos)
                                {
                                    size_t endPos = request.find_first_of(" &", idPos);
                                    std::string idStr = request.substr(idPos + 3, (endPos == std::string::npos) ? std::string::npos : (endPos - (idPos + 3)));
                                    uint32_t appId = std::stoul(idStr);
                                    LOG_INFO("StoreInject: Received /install-fix for AppID %u\n", appId);
                                    
                                    Apps::setOnlineFixInstalled(appId, true);
                                    
                                    const char* response = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n";
                                    send(new_socket, response, strlen(response), 0);
                                    close(new_socket);
                                    handled = true;
                                }
                            } catch (...) {
                                handled = false;
                                close(new_socket);
                            }
                        }
                        else if (request.find("/remove-fix?id=") != std::string::npos)
                        {
                            try {
                                size_t idPos = request.find("id=");
                                if (idPos != std::string::npos)
                                {
                                    size_t endPos = request.find_first_of(" &", idPos);
                                    std::string idStr = request.substr(idPos + 3, (endPos == std::string::npos) ? std::string::npos : (endPos - (idPos + 3)));
                                    uint32_t appId = std::stoul(idStr);
                                    LOG_INFO("StoreInject: Received /remove-fix for AppID %u\n", appId);
                                    
                                    Apps::setOnlineFixInstalled(appId, false);
                                    
                                    const char* response = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n";
                                    send(new_socket, response, strlen(response), 0);
                                    close(new_socket);
                                    handled = true;
                                }
                            } catch (...) {
                                handled = false;
                                close(new_socket);
                            }
                        }
                        else if (request.find("/install-crack?id=") != std::string::npos)
                        {
                            try {
                                size_t idPos = request.find("id=");
                                if (idPos != std::string::npos)
                                {
                                    size_t endPos = request.find_first_of(" &", idPos);
                                    std::string idStr = request.substr(idPos + 3, (endPos == std::string::npos) ? std::string::npos : (endPos - (idPos + 3)));
                                    uint32_t appId = std::stoul(idStr);
                                    LOG_INFO("StoreInject: Received /install-crack for AppID %u\n", appId);
                                    
                                    Apps::setAutoCrackInstalled(appId, true);
                                    
                                    const char* response = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n";
                                    send(new_socket, response, strlen(response), 0);
                                    close(new_socket);
                                    handled = true;
                                }
                            } catch (...) {
                                handled = false;
                                close(new_socket);
                            }
                        }
                        else if (request.find("/remove-crack?id=") != std::string::npos)
                        {
                            try {
                                size_t idPos = request.find("id=");
                                if (idPos != std::string::npos)
                                {
                                    size_t endPos = request.find_first_of(" &", idPos);
                                    std::string idStr = request.substr(idPos + 3, (endPos == std::string::npos) ? std::string::npos : (endPos - (idPos + 3)));
                                    uint32_t appId = std::stoul(idStr);
                                    LOG_INFO("StoreInject: Received /remove-crack for AppID %u\n", appId);
                                    
                                    Apps::setAutoCrackInstalled(appId, false);
                                    
                                    const char* response = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n";
                                    send(new_socket, response, strlen(response), 0);
                                    close(new_socket);
                                    handled = true;
                                }
                            } catch (...) {
                                handled = false;
                                close(new_socket);
                            }
                        }
                        else if (request.find("/change-provider?") != std::string::npos)
                        {
                            try {
                                size_t idPos = request.find("id=");
                                size_t provPos = request.find("provider=");
                                if (idPos != std::string::npos && provPos != std::string::npos)
                                {
                                    size_t idEnd = request.find_first_of(" &", idPos + 3);
                                    std::string idStr = request.substr(idPos + 3, (idEnd == std::string::npos) ? std::string::npos : (idEnd - (idPos + 3)));
                                    size_t provEnd = request.find_first_of(" &", provPos + 9);
                                    std::string provStr = request.substr(provPos + 9, (provEnd == std::string::npos) ? std::string::npos : (provEnd - (provPos + 9)));
                                    int providerIndex = std::stoi(provStr);
                                    uint32_t appId = std::stoul(idStr);

                                    LOG_INFO("StoreInject: Change provider for AppID %u to provider %d\n", appId, providerIndex);

                                    // We DO NOT delete the existing lua, provider, or manifest files here!
                                    // If the download from the new provider fails, the user will still have their old working game.
                                    // Old manifests will be cleaned up in LuaDownload::downloadAndInstall right before success.

                                    // Also remove cached zip so a fresh download is forced
                                    {
                                        const char* home = getenv("HOME");
                                        std::string cachePath = std::string(home ? home : "/tmp") + "/.cache/navi/downloads/" + idStr + ".zip";
                                        std::filesystem::remove(cachePath);
                                    }

                                    // Re-download from the selected provider in a background thread
                                    std::string pid = idStr;
                                    std::thread([pid, providerIndex]() {
                                        LuaDownload::downloadAndInstall(pid, providerIndex, {}, true);
                                    }).detach();

                                    std::string respBody = StoreInjectView::renderSuccessJson();
                                    std::string response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\nContent-Length: " + std::to_string(respBody.size()) + "\r\n\r\n" + respBody;
                                    send(new_socket, response.c_str(), response.size(), 0);
                                    close(new_socket);
                                    handled = true;
                                }
                            } catch (...) {
                                handled = false;
                                close(new_socket);
                            }
                        }
                        else if (request.find("/clear-pending?id=") != std::string::npos)
                        {
                            size_t idPos = request.find("id=");
                            size_t endPos = request.find_first_of(" &", idPos);
                            if (endPos != std::string::npos)
                            {
                                std::string idStr = request.substr(idPos + 3, endPos - (idPos + 3));
                                uint32_t appId = std::stoul(idStr);
                                g_pendingRestartApps.erase(appId);
                                const char* response = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n";
                                send(new_socket, response, strlen(response), 0);
                                close(new_socket);
                                handled = true;
                            }
                        }
                        else if (request.find("/clear-cache?id=") != std::string::npos)
                        {
                            size_t idPos = request.find("id=");
                            size_t endPos = request.find_first_of(" &", idPos);
                            if (endPos != std::string::npos)
                            {
                                std::string idStr = request.substr(idPos + 3, endPos - (idPos + 3));
                                try
                                {
                                    uint32_t appId = std::stoul(idStr);
                                    Paths::clearAppCache(appId);
                                    Ticket::ticketMap.erase(appId);
                                    Ticket::encryptedTicketMap.erase(appId);
                                    g_pendingRestartApps.erase(appId);
                                    LOG_INFO("StoreInject: Cleared cache for AppID %u\n", appId);
                                    std::string respBody = StoreInjectView::renderSuccessJson("Cleared cache for AppID " + idStr);
                                    std::string response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\nContent-Length: " + std::to_string(respBody.size()) + "\r\n\r\n" + respBody;
                                    send(new_socket, response.c_str(), response.size(), 0);
                                    close(new_socket);
                                    handled = true;
                                }
                                catch (...)
                                {
                                    close(new_socket);
                                    handled = false;
                                }
                            }
                        }
                        else if (request.find("/clear-all-cache") != std::string::npos)
                        {
                            try
                            {
                                Paths::clearAllCache();
                                Ticket::ticketMap.clear();
                                Ticket::encryptedTicketMap.clear();
                                g_pendingRestartApps.clear();
                                LOG_INFO("StoreInject: Cleared all cache\n");
                                std::string respBody = StoreInjectView::renderSuccessJson("Cleared all cache");
                                std::string response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\nContent-Length: " + std::to_string(respBody.size()) + "\r\n\r\n" + respBody;
                                send(new_socket, response.c_str(), response.size(), 0);
                                close(new_socket);
                                handled = true;
                            }
                            catch (...)
                            {
                                close(new_socket);
                                handled = false;
                            }
                        }
                        else if (request.find("/remove?id=") != std::string::npos)
                        {
                            size_t idPos = request.find("id=");
                            size_t endPos = request.find_first_of(" &", idPos);
                            if (endPos != std::string::npos)
                            {
                                std::string idStr = request.substr(idPos + 3, endPos - (idPos + 3));
                                uint32_t appId = std::stoul(idStr);
                                if (request.find("pending=false") != std::string::npos) {
                                    g_pendingRestartApps.erase(appId);
                                } else {
                                    g_pendingRestartApps.insert(appId);
                                }
                                
                                bool deleteGame = (request.find("game=true") != std::string::npos);
                                
                                if (deleteGame) {
                                    Apps::deleteGameFiles(appId);
                                }
                                
                                // Remove Lua and Manifest files
                                std::string pluginDir = g_config.getPluginDir();
                                if (!pluginDir.empty()) {
                                    LOG_INFO("RemoveLua: Scanning %s for AppID %u\n", pluginDir.c_str(), appId);
                                    
                                    // 1. Smart Lua Deletion: Scan file contents for addappid(ID)
                                    try {
                                        for (const auto& entry : std::filesystem::directory_iterator(pluginDir)) {
                                            if (entry.is_regular_file() && entry.path().extension() == ".lua") {
                                                std::ifstream file(entry.path());
                                                if (file.is_open()) {
                                                    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                                                    file.close();
                                                    
                                                    // Look for addappid(appId) or addappid( appId )
                                                    std::string pattern = "addappid(" + idStr + ")";
                                                    if (content.find("addappid") != std::string::npos && content.find(idStr) != std::string::npos) {
                                                        std::filesystem::remove(entry.path());
                                                        LOG_INFO("RemoveLua: Deleted lua plugin %s (contained AppID %u)\n", entry.path().c_str(), appId);
                                                    }
                                                }
                                            }
                                        }
                                    } catch(const std::exception& e) { LOG_WARN("RemoveLua: Error scanning plugins: %s\n", e.what()); }
                                    
                                    // 2. Remove manifests from pluginDir
                                    try {
                                        for (const auto& entry : std::filesystem::directory_iterator(pluginDir)) {
                                            if (entry.is_regular_file() && entry.path().extension() == ".manifest") {
                                                std::string stem = entry.path().stem().string();
                                                if (stem.find(idStr) == 0) {
                                                    std::filesystem::remove(entry.path());
                                                    LOG_INFO("RemoveLua: Deleted manifest %s from pluginDir\n", entry.path().c_str());
                                                }
                                            }
                                        }
                                    } catch(...) {}

                                    // 2b. Remove provider sidecar
                                    {
                                        auto providerPath = std::filesystem::path(pluginDir) / (idStr + ".provider");
                                        if (std::filesystem::exists(providerPath)) {
                                            std::filesystem::remove(providerPath);
                                            LOG_INFO("RemoveLua: Deleted provider file %s\n", providerPath.c_str());
                                        }
                                    }

                                    // 3. Remove manifests from depotcache and config/depotcache
                                    std::filesystem::path configPath = std::filesystem::path(pluginDir).parent_path();
                                    for (const auto& dPath : { configPath.parent_path() / "depotcache", configPath / "depotcache" }) {
                                        if (std::filesystem::exists(dPath)) {
                                            try {
                                                for (const auto& entry : std::filesystem::directory_iterator(dPath)) {
                                                    if (entry.is_regular_file() && entry.path().extension() == ".manifest") {
                                                        std::string stem = entry.path().stem().string();
                                                        if (stem.find(idStr) == 0) {
                                                             std::filesystem::remove(entry.path());
                                                             LOG_INFO("RemoveLua: Deleted manifest %s from depotcache\n", entry.path().c_str());
                                                        }
                                                    }
                                                }
                                            } catch(...) {}
                                        }
                                    }
                                }

                                // 4. Finalize state and refresh memory
                                scanLuaPluginsAndUpdateConfig();
                                g_config.loadSettings();
                                

                                Apps::removeInstalled(appId); // Ensure it's removed from persistence too
                                g_config.removeAdditionalAppId(appId);
                                
                                // if (g_pClientApps) {
                                //     LOG_INFO("Triggering RequestAppInfoUpdate for %u\n", appId);
                                //     typedef void (*RequestAppInfoUpdate_t)(void*, uint32_t*, uint32_t, bool);
                                //     void** vtable = *reinterpret_cast<void***>(g_pClientApps);
                                //     RequestAppInfoUpdate_t requestUpdateFn = reinterpret_cast<RequestAppInfoUpdate_t>(vtable[7]);
                                //     uint32_t appIdArray[1] = { appId };
                                //     requestUpdateFn(g_pClientApps, appIdArray, 1, true);
                                // }
                                
                                const char* response = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n";
                                send(new_socket, response, strlen(response), 0);
                                close(new_socket);
                                handled = true;
                            }
                        }
                        else if (request.find("/restart") != std::string::npos)
                        {
                            LOG_INFO("Restart Steam requested via UI\n");

                            // Send response first
                            const char* response = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n";
                            send(new_socket, response, strlen(response), 0);
                            close(new_socket);
                            handled = true;
                            
                            // Preserve LD_AUDIT from current environment
                            const char* ldAudit = getenv("LD_AUDIT");
                            std::string ldAuditStr;
                            if (ldAudit && ldAudit[0] != '\0') {
                                ldAuditStr = ldAudit;
                            } else {
                                const char* home = getenv("HOME");
                                if (home) {
                                    ldAuditStr = std::string(home) + "/.local/share/navi/library-inject.so:"
                                               + std::string(home) + "/.local/share/navi/libnavi.so";
                                }
                            }
                            
                            // Find the real Steam binary
                            std::string steamBin;
                            for (const char* candidate : {"/usr/games/steam", "/usr/bin/steam", "/usr/local/bin/steam"}) {
                                if (std::filesystem::exists(candidate)) {
                                    steamBin = candidate;
                                    break;
                                }
                            }
                            if (steamBin.empty()) steamBin = "steam";
                            
                            // Build a restart script that:
                            // 1. Uses "steam -shutdown" for clean shutdown (like job_queue_manager.py)
                            // 2. Targets specific Steam processes by exact name, NOT "pkill -f steam"
                            //    which would also kill antigravity and anything with "steam" in its path
                            // 3. Waits for processes to die
                            // 4. Relaunches with LD_AUDIT (tier0 hook injects CDP pipe flags)
                            std::string script =
                                // Step 1: Ask Steam to shut down cleanly
                                "steam -shutdown 2>/dev/null; "
                                // Step 2: Wait for graceful shutdown
                                "sleep 3; "
                                // Step 3: Kill specific Steam processes if still alive (exact name match, not -f)
                                "pkill -TERM -x steam 2>/dev/null; "
                                "pkill -TERM -x steamwebhelper 2>/dev/null; "
                                "pkill -TERM -x steam-runtime-l 2>/dev/null; "
                                "sleep 2; "
                                // Step 4: Force-kill stragglers by exact name
                                "pkill -9 -x steam 2>/dev/null; "
                                "pkill -9 -x steamwebhelper 2>/dev/null; "
                                "sleep 1; "
                                // Step 4.5: Clean up stale PID and lock files left by SIGKILL
                                "rm -f ~/.steam/steam.pid ~/.steam/.steam/steam.pid ~/.steam/steam.pipe ~/.local/share/Steam/steam.pid ~/.local/share/Steam/steam.pipe 2>/dev/null; "
                                // Step 5: Relaunch with LD_AUDIT (CDP pipe injection handled by tier0 hook)
                                "env";
                            if (!ldAuditStr.empty()) {
                                script += " LD_AUDIT=\"" + ldAuditStr + "\"";
                            }
                            script += " " + steamBin
                                   + " </dev/null >/dev/null 2>&1 &";
                            
                            LOG_INFO("Restart script: %s\n", script.c_str());
                            
                            // Use fork()+exec() to fully detach the restart process.
                            // system() blocks and the script kills our parent, causing issues.
                            // fork() creates a child that survives our death.
                            pid_t pid = fork();
                            if (pid == 0) {
                                // Child process: detach from parent completely
                                setsid();  // New session leader
                                // Close inherited file descriptors
                                for (int fd = 3; fd < 1024; fd++) close(fd);
                                // Redirect stdin/stdout/stderr to /dev/null
                                freopen("/dev/null", "r", stdin);
                                freopen("/dev/null", "w", stdout);
                                freopen("/dev/null", "w", stderr);
                                // Execute the restart script
                                execl("/bin/bash", "bash", "-c", script.c_str(), nullptr);
                                _exit(1);  // Only reached if execl fails
                            } else if (pid > 0) {
                                LOG_INFO("Restart child spawned with PID %d\n", pid);
                            } else {
                                LOG_WARN("fork() failed for restart: %s\n", strerror(errno));
                            }
                        }
                        else if (request.find("/log?msg=") != std::string::npos)
                        {
                            size_t msgPos = request.find("msg=");
                            size_t endPos = request.find(" ", msgPos);
                            if (endPos != std::string::npos)
                            {
                                std::string msg = request.substr(msgPos + 4, endPos - (msgPos + 4));
                                // Simple URL decode
                                std::replace(msg.begin(), msg.end(), '+', ' ');
                                LOG_INFO("StoreInject JS: %s\n", msg.c_str());
                            }
                        }
                        else if (request.find("id=") != std::string::npos)
                        {
                            size_t idPos = request.find("id=");
                            size_t endPos = request.find(" ", idPos);
                            if (endPos != std::string::npos)
                            {
                                std::string idStr = request.substr(idPos + 3, endPos - (idPos + 3));
                                LOG_INFO("Download Lua clicked for Product ID: %s\n", idStr.c_str());
                            }
                        }
                        else if (request.find("/list") != std::string::npos)
                        {
                            auto addedApps = g_config.addedAppIds.get();
                            auto baseApps = g_config.appIds.get();
                            
                            std::unordered_set<uint32_t> allApps;
                            allApps.insert(addedApps.begin(), addedApps.end());
                            allApps.insert(baseApps.begin(), baseApps.end());
                            
                            std::string json = "[";
                            bool first = true;
                            for (auto app : allApps) {
                                if (!first) json += ",";
                                json += std::to_string(app);
                                first = false;
                            }
                            json += "]";
                            
                            std::string response = "HTTP/1.1 200 OK\r\n"
                                                   "Content-Type: application/json\r\n"
                                                   "Access-Control-Allow-Origin: *\r\n"
                                                   "Connection: close\r\n"
                                                   "Content-Length: " + std::to_string(json.length()) + "\r\n\r\n" + json;
                            send(new_socket, response.c_str(), response.length(), 0);
                            close(new_socket);
                            handled = true;
                        }
                        else if (request.find("/config/full") != std::string::npos)
                        {
                            try
                            {
                                if (request.find("POST") == 0 || request.find("POST ") != std::string::npos)
                                {
                                    std::string body = extractHttpPostBody(new_socket, request);
                                    std::string errMsg;
                                    if (StoreInjectView::applyFullConfigJson(g_config.getPath(), body, errMsg))
                                    {
                                        g_config.loadSettings(false);
                                        std::string respBody = StoreInjectView::renderSuccessJson("Configuration saved and reloaded");
                                        sendJsonResponse(new_socket, 200, "OK", respBody);
                                        handled = true;
                                    }
                                    else
                                    {
                                        std::string respBody = StoreInjectView::renderErrorJson(errMsg.empty() ? "Failed to apply configuration" : errMsg);
                                        sendJsonResponse(new_socket, 400, "Bad Request", respBody);
                                        handled = true;
                                    }
                                }
                                else
                                {
                                    std::string respBody = StoreInjectView::renderFullConfigJson(g_config.getPath());
                                    sendJsonResponse(new_socket, 200, "OK", respBody);
                                    handled = true;
                                }
                            }
                            catch (const std::exception& e)
                            {
                                std::string respBody = StoreInjectView::renderErrorJson(e.what());
                                sendJsonResponse(new_socket, 500, "Internal Server Error", respBody);
                                handled = true;
                            }
                        }
                        else if (request.find("/sync/status") != std::string::npos)
                        {
                            try
                            {
                                StoreInjectView::SyncStatusViewModel vm;
                                vm.role = g_config.syncRole.get();
                                vm.topology = g_config.syncTopology.get();
                                vm.cloudProvider = g_config.syncCloudProvider.get();

                                StoreInjectView::CloudCredentials_t creds;
                                StoreInjectView::loadCloudCredentials(g_config.getDir(), creds);
                                vm.gdriveAuthenticated = (!creds.gdriveRefreshToken.empty() || !creds.gdriveAccessToken.empty());
                                if (vm.cloudProvider == "gdrive")
                                {
                                    vm.cloudAuthenticated = vm.gdriveAuthenticated;
                                }
                                else if (vm.cloudProvider == "webdav")
                                {
                                    vm.cloudAuthenticated = !g_config.syncWebDavUrl.get().empty();
                                }
                                else
                                {
                                    vm.cloudAuthenticated = false;
                                }

                                vm.lastTriggerReason = g_lastSyncTriggerReason;
                                vm.overriderHostname = g_lastOverriderHostname.empty() ? "overrider-device" : g_lastOverriderHostname;
                                vm.remoteGeneration = g_lastRemoteGeneration;
                                vm.localGeneration = g_lastLocalGeneration;
                                vm.hasRemoteBump = g_lastHasRemoteBump;
                                vm.keyEpoch = g_syncKeyEpoch;

                                if (vm.role == "overrider")
                                {
                                    std::string masterKeyPath = g_config.getDir() + "/sync_keys/master.key";
                                    Sync::MasterKey_t mk{};
                                    if (Sync::ensureMasterKeyFile(masterKeyPath, mk))
                                    {
                                        vm.recoveryKeyMaskedOrFull = Sync::encodeRecoveryKey(mk);
                                    }
                                    OPENSSL_cleanse(mk.data(), mk.size());
                                }

                                vm.diffItems = g_cachedDiffItems;
                                vm.conflicts = g_cachedConflicts;
                                vm.p2pPeers = g_cachedP2pPeers;
                                vm.stagedAcfRestartRequiredAppIds = g_stagedAcfRestartRequiredAppIds;

                                std::string respBody = StoreInjectView::renderSyncStatusJson(vm);
                                sendJsonResponse(new_socket, 200, "OK", respBody);
                                handled = true;
                            }
                            catch (const std::exception& e)
                            {
                                std::string respBody = StoreInjectView::renderErrorJson(e.what());
                                sendJsonResponse(new_socket, 500, "Internal Server Error", respBody);
                                handled = true;
                            }
                        }
                        else if (request.find("/sync/progress") != std::string::npos)
                        {
                            std::string respBody = renderSyncProgressJson();
                            sendJsonResponse(new_socket, 200, "OK", respBody);
                            handled = true;
                        }
                        else if (request.find("/sync/check") != std::string::npos)
                        {
                            try
                            {
                                g_lastSyncTriggerReason = "manual";
                                std::string configDir = g_config.getDir();
                                std::string steamRoot = LuaDownload::findSteamRoot();

                                StoreInjectView::CloudCredentials_t creds;
                                StoreInjectView::loadCloudCredentials(configDir, creds);

                                bool canCheckCloud = false;
                                std::string provStr = g_config.syncCloudProvider.get();
                                if ((provStr.empty() || provStr == "none") && (!creds.gdriveRefreshToken.empty() || !creds.gdriveAccessToken.empty()))
                                {
                                    provStr = "gdrive";
                                    g_config.syncCloudProvider = std::string("gdrive");
                                }
                                if (provStr == "gdrive" && (!creds.gdriveRefreshToken.empty() || !creds.gdriveAccessToken.empty()))
                                {
                                    canCheckCloud = true;
                                }
                                else if (provStr == "webdav" && !g_config.syncWebDavUrl.get().empty())
                                {
                                    canCheckCloud = true;
                                }

                                size_t unchangedCount = 0;
                                std::vector<SyncAutoAddedItem_t> autoAddedFiles;
                                std::vector<SyncDiffPromptItem_t> diffPrompts;
                                std::vector<SyncRemovePromptItem_t> removePrompts;

                                if (canCheckCloud)
                                {
                                    std::string masterKeyPath = configDir + "/sync_keys/master.key";
                                    Sync::MasterKey_t mk{};
                                    if (Sync::ensureMasterKeyFile(masterKeyPath, mk))
                                    {
                                        Sync::CloudProviderConfig_t cpCfg;
                                        cpCfg.masterKey = mk;
                                        cpCfg.keyEpoch = g_syncKeyEpoch;
                                        if (provStr == "gdrive")
                                        {
                                            cpCfg.type = Sync::ECloudProviderType::k_GoogleDrive;
                                            cpCfg.oauthClientId = creds.gdriveClientId.empty() ? Sync::k_DefaultGDriveClientId : creds.gdriveClientId;
                                            cpCfg.oauthClientSecret = creds.gdriveClientSecret.empty() ? Sync::k_DefaultGDriveClientSecret : creds.gdriveClientSecret;
                                            cpCfg.accessToken = creds.gdriveAccessToken;
                                            cpCfg.refreshToken = creds.gdriveRefreshToken;
                                            cpCfg.tokenExpiresAtUnixSec = creds.gdriveTokenExpiresAt;
                                            if (!creds.gdriveDeviceAuthEndpoint.empty()) cpCfg.oauthDeviceAuthEndpoint = creds.gdriveDeviceAuthEndpoint;
                                            if (!creds.gdriveTokenEndpoint.empty()) cpCfg.oauthTokenEndpoint = creds.gdriveTokenEndpoint;
                                            if (!creds.gdriveApiBaseUrl.empty()) cpCfg.gdriveApiBaseUrl = creds.gdriveApiBaseUrl;
                                            if (!creds.gdriveUploadBaseUrl.empty()) cpCfg.gdriveUploadBaseUrl = creds.gdriveUploadBaseUrl;
                                        }
                                        else
                                        {
                                            cpCfg.type = Sync::ECloudProviderType::k_WebDav;
                                            cpCfg.webDavBaseUrl = g_config.syncWebDavUrl.get();
                                            cpCfg.webDavUsername = g_config.syncWebDavUser.get();
                                            cpCfg.webDavPasswordOrToken = creds.webdavPassword;
                                        }

                                        auto provider = Sync::createCloudProvider(cpCfg);
                                        if (provider)
                                        {
                                            uint64_t steamId64 = 0x0110000100000001ULL;
                                            Sync::SyncStateManifest_t remoteManifest;
                                            std::string etag;
                                            Sync::ECloudError fetchErr = provider->fetchRemoteManifest(steamId64, remoteManifest, etag);

                                            char hostBuf[256] = {0};
                                            if (gethostname(hostBuf, sizeof(hostBuf)) != 0) hostBuf[0] = '\0';
                                            std::string localHost = (hostBuf[0] != '\0') ? hostBuf : "local-device";

                                            if (fetchErr == Sync::ECloudError::k_AuthExpired)
                                            {
                                                OPENSSL_cleanse(mk.data(), mk.size());
                                                std::string respBody = StoreInjectView::renderErrorJson(
                                                    "Google Drive permissions changed to visible files. Please click 'Connect to Google' to re-authorize visible Drive access.");
                                                sendJsonResponse(new_socket, 401, "Unauthorized", respBody);
                                                handled = true;
                                            }
                                            else if (fetchErr == Sync::ECloudError::k_Ok || fetchErr == Sync::ECloudError::k_NotFound)
                                            {
                                                if (fetchErr == Sync::ECloudError::k_Ok)
                                                {
                                                    g_lastRemoteManifest = remoteManifest;
                                                    g_lastRemoteGeneration = remoteManifest.globalGeneration;
                                                    g_lastOverriderHostname = remoteManifest.deviceHostname;
                                                    g_lastHasRemoteBump = (remoteManifest.globalGeneration > g_lastLocalGeneration);
                                                }
                                                else
                                                {
                                                    remoteManifest.schemaVersion = 1;
                                                    remoteManifest.steamId64 = steamId64;
                                                    remoteManifest.keyEpoch = g_syncKeyEpoch;
                                                    remoteManifest.deviceId = localHost;
                                                    remoteManifest.deviceHostname = localHost;
                                                    remoteManifest.globalGeneration = 0;
                                                }

                                                g_cachedDiffItems.clear();
                                                g_lastPlaintextPayloads.clear();
                                                std::map<std::string, Sync::SyncFileEntry_t> remoteEntryMap;

                                                for (const auto& entry : remoteManifest.entries)
                                                {
                                                    remoteEntryMap[entry.logicalPath] = entry;

                                                    StoreInjectView::SyncDiffItemViewModel item;
                                                    item.appId = entry.appId;
                                                    item.category = Sync::syncCategoryToString(entry.category);
                                                    item.logicalPath = entry.logicalPath;
                                                    item.sizeBytes = entry.sizeBytes;
                                                    item.generation = entry.generation;
                                                    g_cachedDiffItems.push_back(item);
                                                }

                                                Sync::SyncSelectionFilter_t filter = g_config.syncFilter.get();
                                                if (!filter.allConfigs && !filter.allApiKeys && !filter.allManifestsAndLua &&
                                                    !filter.allCloudSaves && !filter.allAchievements &&
                                                    filter.configKeys.empty() && filter.providerNames.empty() &&
                                                    filter.manifestAppIds.empty() && filter.cloudSaveAppIds.empty() &&
                                                    filter.achievementAppIds.empty())
                                                {
                                                    filter.allConfigs = true;
                                                    filter.allManifestsAndLua = true;
                                                    filter.allAchievements = true;
                                                    filter.allCloudSaves = true;
                                                    filter.allApiKeys = true;
                                                }
                                                auto baseHashes = loadSyncBaseHashes(configDir);

                                                // First pass: Auto-download any new remote files (!existsLocally)
                                                // before scanning local files so that FileWatcher/scanLuaPluginsAndUpdateConfig
                                                // updates config.yaml synchronously before config/config.yaml is compared.
                                                bool autoDownloadedLua = false;
                                                std::set<std::string> autoDownloadedPaths;
                                                for (const auto& [path, rf] : remoteEntryMap)
                                                {
                                                    std::string destPath = resolveSyncLogicalPathToLocal(path, configDir, steamRoot);
                                                    if (!destPath.empty() && !std::filesystem::exists(destPath))
                                                    {
                                                        bool inBase = (baseHashes.find(path) != baseHashes.end());
                                                        bool wasDeletedLocally = inBase && (g_lastLocalGeneration > 0) && (baseHashes[path] == rf.sha256Hex);
                                                        if (!wasDeletedLocally)
                                                        {
                                                            auto pit = g_lastPlaintextPayloads.find(path);
                                                            if (pit == g_lastPlaintextPayloads.end())
                                                            {
                                                                std::string remoteFileName = std::filesystem::path(rf.logicalPath).filename().string();
                                                                std::vector<uint8_t> rawBlob;
                                                                if (provider->pullEncryptedObject(steamId64, rf.logicalPath, rawBlob) == Sync::ECloudError::k_Ok ||
                                                                    provider->pullEncryptedObject(steamId64, remoteFileName, rawBlob) == Sync::ECloudError::k_Ok ||
                                                                    provider->pullEncryptedObject(steamId64, rf.sha256Hex, rawBlob) == Sync::ECloudError::k_Ok)
                                                                {
                                                                    g_lastPlaintextPayloads[path] = std::move(rawBlob);
                                                                    pit = g_lastPlaintextPayloads.find(path);
                                                                }
                                                            }
                                                            if (pit != g_lastPlaintextPayloads.end())
                                                            {
                                                                std::string contentStr(reinterpret_cast<const char*>(pit->second.data()), pit->second.size());
                                                                if (AtomicFile::write(destPath, contentStr, true))
                                                                {
                                                                    setFileMtimeUnixSec(destPath, rf.mtimeUnixSec);
                                                                    baseHashes[path] = rf.sha256Hex;
                                                                    autoDownloadedPaths.insert(path);
                                                                    if (path.rfind("stplug-in/", 0) == 0)
                                                                    {
                                                                        autoDownloadedLua = true;
                                                                    }
                                                                    std::string fname = std::filesystem::path(path).filename().string();
                                                                    std::string desc = describeSyncLogicalPath(path, rf.appId);
                                                                    SyncAutoAddedItem_t added;
                                                                    added.logicalPath = path;
                                                                    added.fileName = fname;
                                                                    added.description = desc;
                                                                    added.direction = "downloaded";
                                                                    added.mtimeUnixSec = rf.mtimeUnixSec;
                                                                    added.sizeBytes = rf.sizeBytes;
                                                                    autoAddedFiles.push_back(std::move(added));
                                                                }
                                                            }
                                                        }
                                                    }
                                                }
                                                if (autoDownloadedLua)
                                                {
                                                    scanLuaPluginsAndUpdateConfig();
                                                    // If config/config.yaml was previously synced and matches the cloud base hash,
                                                    // ensure FileWatcher's temporary removal of the Lua app doesn't leave a formatting diff.
                                                    auto cfgRit = remoteEntryMap.find("config/config.yaml");
                                                    auto cfgBit = baseHashes.find("config/config.yaml");
                                                    auto cfgPit = g_lastPlaintextPayloads.find("config/config.yaml");
                                                    if (cfgPit == g_lastPlaintextPayloads.end() && cfgRit != remoteEntryMap.end())
                                                    {
                                                        std::vector<uint8_t> rawBlob;
                                                        if (provider->pullEncryptedObject(steamId64, cfgRit->second.logicalPath, rawBlob) == Sync::ECloudError::k_Ok ||
                                                            provider->pullEncryptedObject(steamId64, cfgRit->second.sha256Hex, rawBlob) == Sync::ECloudError::k_Ok)
                                                        {
                                                            g_lastPlaintextPayloads["config/config.yaml"] = std::move(rawBlob);
                                                            cfgPit = g_lastPlaintextPayloads.find("config/config.yaml");
                                                        }
                                                    }
                                                    if (cfgRit != remoteEntryMap.end() && cfgBit != baseHashes.end() &&
                                                        cfgBit->second == cfgRit->second.sha256Hex && cfgPit != g_lastPlaintextPayloads.end())
                                                    {
                                                        std::string cfgPath = g_config.getPath();
                                                        std::string cfgStr(reinterpret_cast<const char*>(cfgPit->second.data()), cfgPit->second.size());
                                                        AtomicFile::write(cfgPath, cfgStr, true);
                                                        setFileMtimeUnixSec(cfgPath, cfgRit->second.mtimeUnixSec);
                                                        g_config.loadSettings();
                                                    }
                                                }

                                                auto localFiles = collectLocalSyncFiles(configDir, steamRoot, filter);

                                                std::set<std::string> allPaths;
                                                for (const auto& [p, _] : localFiles) allPaths.insert(p);
                                                for (const auto& [p, _] : remoteEntryMap) allPaths.insert(p);

                                                std::vector<std::string> allPathsVec(allPaths.begin(), allPaths.end());
                                                initSyncProgressPaths("check", allPathsVec);

                                                bool cloudManifestNeedsCommit = false;
                                                uint64_t targetGen = std::max(g_lastLocalGeneration, g_lastRemoteGeneration) + 1;

                                                for (const auto& path : allPaths)
                                                {
                                                    incrementSyncProgress(path);
                                                    if (autoDownloadedPaths.count(path) > 0)
                                                    {
                                                        continue;
                                                    }
                                                    bool localExists = (localFiles.find(path) != localFiles.end());
                                                    bool remoteExists = (remoteEntryMap.find(path) != remoteEntryMap.end());
                                                    bool inBase = (baseHashes.find(path) != baseHashes.end());

                                                    if (localExists && remoteExists)
                                                    {
                                                        const auto& lf = localFiles[path];
                                                        const auto& rf = remoteEntryMap[path];
                                                        if (lf.sha256Hex == rf.sha256Hex)
                                                        {
                                                            unchangedCount++;
                                                            baseHashes[path] = lf.sha256Hex;
                                                        }
                                                        else
                                                        {
                                                            SyncDiffPromptItem_t dp;
                                                            dp.logicalPath = path;
                                                            dp.fileName = lf.fileName;
                                                            dp.description = lf.description;
                                                            dp.category = Sync::syncCategoryToString(lf.category);
                                                            dp.appId = lf.appId;
                                                            dp.localMtimeUnixSec = lf.mtimeUnixSec;
                                                            dp.localSizeBytes = lf.sizeBytes;
                                                            dp.localSha256 = lf.sha256Hex;
                                                            dp.remoteMtimeUnixSec = rf.mtimeUnixSec;
                                                            dp.remoteSizeBytes = rf.sizeBytes;
                                                            dp.remoteSha256 = rf.sha256Hex;
                                                            diffPrompts.push_back(std::move(dp));
                                                        }
                                                    }
                                                    else if (!localExists && remoteExists)
                                                    {
                                                        const auto& rf = remoteEntryMap[path];
                                                        bool inBase = (baseHashes.find(path) != baseHashes.end());
                                                        bool wasDeletedLocally = inBase && (g_lastLocalGeneration > 0) && (baseHashes[path] == rf.sha256Hex);
                                                        if (wasDeletedLocally)
                                                        {
                                                            std::string fname = std::filesystem::path(path).filename().string();
                                                            std::string desc = describeSyncLogicalPath(path, rf.appId);

                                                            // Previously synced file was removed locally -> Ask user if they want to remove
                                                            SyncRemovePromptItem_t rp;
                                                            rp.logicalPath = path;
                                                            rp.fileName = fname;
                                                            rp.description = desc;
                                                            rp.category = Sync::syncCategoryToString(rf.category);
                                                            rp.appId = rf.appId;
                                                            rp.removedSide = "local";
                                                            rp.mtimeUnixSec = rf.mtimeUnixSec;
                                                            rp.sizeBytes = rf.sizeBytes;
                                                            removePrompts.push_back(std::move(rp));
                                                        }
                                                    }
                                                    else if (localExists && !remoteExists)
                                                    {
                                                        auto& lf = localFiles[path];
                                                        if (!inBase)
                                                        {
                                                            // New file added locally -> Auto-add (upload) to cloud without asking
                                                            Sync::SyncFileEntry_t entry;
                                                            entry.category = lf.category;
                                                            entry.appId = lf.appId;
                                                            entry.logicalPath = lf.logicalPath;
                                                            entry.sha256Hex = lf.sha256Hex;
                                                            entry.sizeBytes = lf.sizeBytes;
                                                            entry.mtimeUnixSec = lf.mtimeUnixSec;
                                                            entry.generation = targetGen;
                                                            entry.originDeviceId = localHost;

                                                            ensureLocalSyncFileBytes(lf);
                                                            if (provider->pushEncryptedObject(steamId64, lf.logicalPath, lf.bytes) == Sync::ECloudError::k_Ok)
                                                            {
                                                                remoteManifest.entries.push_back(entry);
                                                                cloudManifestNeedsCommit = true;
                                                                baseHashes[path] = lf.sha256Hex;

                                                                SyncAutoAddedItem_t added;
                                                                added.logicalPath = path;
                                                                added.fileName = lf.fileName;
                                                                added.description = lf.description;
                                                                added.direction = "uploaded";
                                                                added.mtimeUnixSec = lf.mtimeUnixSec;
                                                                added.sizeBytes = lf.sizeBytes;
                                                                autoAddedFiles.push_back(std::move(added));
                                                            }
                                                        }
                                                        else
                                                        {
                                                            // Previously synced file was removed from cloud -> Ask user if they want to remove
                                                            SyncRemovePromptItem_t rp;
                                                            rp.logicalPath = path;
                                                            rp.fileName = lf.fileName;
                                                            rp.description = lf.description;
                                                            rp.category = Sync::syncCategoryToString(lf.category);
                                                            rp.appId = lf.appId;
                                                            rp.removedSide = "remote";
                                                            rp.mtimeUnixSec = lf.mtimeUnixSec;
                                                            rp.sizeBytes = lf.sizeBytes;
                                                            removePrompts.push_back(std::move(rp));
                                                        }
                                                    }
                                                }

                                                if (cloudManifestNeedsCommit)
                                                {
                                                    remoteManifest.schemaVersion = 1;
                                                    remoteManifest.steamId64 = steamId64;
                                                    remoteManifest.keyEpoch = g_syncKeyEpoch;
                                                    remoteManifest.deviceId = localHost;
                                                    remoteManifest.deviceHostname = localHost;
                                                    remoteManifest.globalGeneration = targetGen;
                                                    remoteManifest.generatedAtUnixSec = std::chrono::duration_cast<std::chrono::seconds>(
                                                        std::chrono::system_clock::now().time_since_epoch()).count();
                                                    remoteManifest.manifestRootSha256 = Sync::computeManifestRootSha256(remoteManifest.entries);

                                                    std::string newETag;
                                                    if (provider->commitRemoteManifest(steamId64, remoteManifest, "", newETag) == Sync::ECloudError::k_Ok)
                                                    {
                                                        g_lastRemoteManifest = remoteManifest;
                                                        g_lastRemoteGeneration = remoteManifest.globalGeneration;
                                                        g_lastLocalGeneration = remoteManifest.globalGeneration;
                                                        g_lastHasRemoteBump = false;
                                                    }
                                                }

                                                saveSyncBaseHashes(configDir, baseHashes);
                                            }
                                        }
                                        OPENSSL_cleanse(mk.data(), mk.size());
                                    }
                                }

                                if (!handled)
                                {
                                    if (g_lastRemoteGeneration > g_lastLocalGeneration)
                                    {
                                        g_lastHasRemoteBump = true;
                                    }

                                    StoreInjectView::SyncStatusViewModel vm;
                                    vm.role = g_config.syncRole.get();
                                    vm.topology = g_config.syncTopology.get();
                                    vm.cloudProvider = g_config.syncCloudProvider.get();
                                    vm.gdriveAuthenticated = (!creds.gdriveRefreshToken.empty() || !creds.gdriveAccessToken.empty());
                                    if (vm.cloudProvider == "gdrive")
                                    {
                                        vm.cloudAuthenticated = vm.gdriveAuthenticated;
                                    }
                                    else if (vm.cloudProvider == "webdav")
                                    {
                                        vm.cloudAuthenticated = !g_config.syncWebDavUrl.get().empty();
                                    }
                                    else
                                    {
                                        vm.cloudAuthenticated = false;
                                    }
                                    vm.lastTriggerReason = g_lastSyncTriggerReason;
                                    vm.overriderHostname = g_lastOverriderHostname.empty() ? "overrider-device" : g_lastOverriderHostname;
                                    vm.remoteGeneration = g_lastRemoteGeneration;
                                    vm.localGeneration = g_lastLocalGeneration;
                                    vm.hasRemoteBump = g_lastHasRemoteBump;
                                    vm.keyEpoch = g_syncKeyEpoch;

                                    if (vm.role == "overrider")
                                    {
                                        std::string masterKeyPath = configDir + "/sync_keys/master.key";
                                        Sync::MasterKey_t mk{};
                                        if (Sync::ensureMasterKeyFile(masterKeyPath, mk))
                                        {
                                            vm.recoveryKeyMaskedOrFull = Sync::encodeRecoveryKey(mk);
                                        }
                                        OPENSSL_cleanse(mk.data(), mk.size());
                                    }

                                    vm.diffItems = g_cachedDiffItems;
                                    vm.conflicts = g_cachedConflicts;
                                    vm.p2pPeers = g_cachedP2pPeers;
                                    vm.stagedAcfRestartRequiredAppIds = g_stagedAcfRestartRequiredAppIds;

                                    std::string baseJson = StoreInjectView::renderSyncStatusJson(vm);
                                    bool needsUserAction = (!diffPrompts.empty() || !removePrompts.empty());

                                    std::ostringstream extra;
                                    extra << ",\n  \"success\": true";
                                    extra << ",\n  \"needsUserAction\": " << (needsUserAction ? "true" : "false");
                                    extra << ",\n  \"unchangedCount\": " << unchangedCount;

                                    extra << ",\n  \"autoAddedFiles\": [";
                                    for (size_t i = 0; i < autoAddedFiles.size(); ++i)
                                    {
                                        const auto& a = autoAddedFiles[i];
                                        if (i > 0) extra << ",";
                                        extra << "\n    {\"logicalPath\":\"" << escapeSyncJsonStr(a.logicalPath)
                                              << "\",\"fileName\":\"" << escapeSyncJsonStr(a.fileName)
                                              << "\",\"description\":\"" << escapeSyncJsonStr(a.description)
                                              << "\",\"direction\":\"" << escapeSyncJsonStr(a.direction)
                                              << "\",\"mtimeUnixSec\":" << a.mtimeUnixSec
                                              << ",\"sizeBytes\":" << a.sizeBytes << "}";
                                    }
                                    extra << "]";

                                    extra << ",\n  \"diffPrompts\": [";
                                    for (size_t i = 0; i < diffPrompts.size(); ++i)
                                    {
                                        const auto& d = diffPrompts[i];
                                        if (i > 0) extra << ",";
                                        extra << "\n    {\"logicalPath\":\"" << escapeSyncJsonStr(d.logicalPath)
                                              << "\",\"fileName\":\"" << escapeSyncJsonStr(d.fileName)
                                              << "\",\"description\":\"" << escapeSyncJsonStr(d.description)
                                              << "\",\"category\":\"" << escapeSyncJsonStr(d.category)
                                              << "\",\"appId\":" << d.appId
                                              << ",\"localMtimeUnixSec\":" << d.localMtimeUnixSec
                                              << ",\"localSizeBytes\":" << d.localSizeBytes
                                              << ",\"localSha256\":\"" << escapeSyncJsonStr(d.localSha256)
                                              << "\",\"remoteMtimeUnixSec\":" << d.remoteMtimeUnixSec
                                              << ",\"remoteSizeBytes\":" << d.remoteSizeBytes
                                              << ",\"remoteSha256\":\"" << escapeSyncJsonStr(d.remoteSha256) << "\"}";
                                    }
                                    extra << "]";

                                    extra << ",\n  \"removePrompts\": [";
                                    for (size_t i = 0; i < removePrompts.size(); ++i)
                                    {
                                        const auto& r = removePrompts[i];
                                        if (i > 0) extra << ",";
                                        extra << "\n    {\"logicalPath\":\"" << escapeSyncJsonStr(r.logicalPath)
                                              << "\",\"fileName\":\"" << escapeSyncJsonStr(r.fileName)
                                              << "\",\"description\":\"" << escapeSyncJsonStr(r.description)
                                              << "\",\"category\":\"" << escapeSyncJsonStr(r.category)
                                              << "\",\"appId\":" << r.appId
                                              << ",\"removedSide\":\"" << escapeSyncJsonStr(r.removedSide)
                                              << "\",\"mtimeUnixSec\":" << r.mtimeUnixSec
                                              << ",\"sizeBytes\":" << r.sizeBytes << "}";
                                    }
                                    extra << "]\n}";

                                    size_t lastBrace = baseJson.rfind('}');
                                    std::string respBody = (lastBrace != std::string::npos)
                                        ? (baseJson.substr(0, lastBrace) + extra.str())
                                        : baseJson;

                                    finishSyncProgress();
                                    sendJsonResponse(new_socket, 200, "OK", respBody);
                                    handled = true;
                                }
                            }
                            catch (const std::exception& e)
                            {
                                finishSyncProgress();
                                std::string respBody = StoreInjectView::renderErrorJson(e.what());
                                sendJsonResponse(new_socket, 500, "Internal Server Error", respBody);
                                handled = true;
                            }
                        }
                        else if (request.find("/sync/resolve") != std::string::npos)
                        {
                            try
                            {
                                std::string body = extractHttpPostBody(new_socket, request);
                                auto diffChoices = extractJsonStringMap(body, "diffChoices");
                                auto removeChoices = extractJsonStringMap(body, "removeChoices");

                                std::string configDir = g_config.getDir();
                                std::string steamRoot = LuaDownload::findSteamRoot();
                                std::string masterKeyPath = configDir + "/sync_keys/master.key";
                                Sync::MasterKey_t mk{};
                                if (!Sync::ensureMasterKeyFile(masterKeyPath, mk))
                                {
                                    sendJsonResponse(new_socket, 400, "Bad Request", StoreInjectView::renderErrorJson("Master key not configured"));
                                    handled = true;
                                }
                                else
                                {
                                    StoreInjectView::CloudCredentials_t creds;
                                    StoreInjectView::loadCloudCredentials(configDir, creds);

                                    Sync::CloudProviderConfig_t cpCfg;
                                    cpCfg.masterKey = mk;
                                    cpCfg.keyEpoch = g_syncKeyEpoch;
                                    std::string provStr = g_config.syncCloudProvider.get();
                                    if ((provStr.empty() || provStr == "none") && (!creds.gdriveRefreshToken.empty() || !creds.gdriveAccessToken.empty()))
                                    {
                                        provStr = "gdrive";
                                        g_config.syncCloudProvider = std::string("gdrive");
                                    }
                                    if (provStr == "gdrive")
                                    {
                                        cpCfg.type = Sync::ECloudProviderType::k_GoogleDrive;
                                        cpCfg.oauthClientId = creds.gdriveClientId.empty() ? Sync::k_DefaultGDriveClientId : creds.gdriveClientId;
                                        cpCfg.oauthClientSecret = creds.gdriveClientSecret.empty() ? Sync::k_DefaultGDriveClientSecret : creds.gdriveClientSecret;
                                        cpCfg.accessToken = creds.gdriveAccessToken;
                                        cpCfg.refreshToken = creds.gdriveRefreshToken;
                                        cpCfg.tokenExpiresAtUnixSec = creds.gdriveTokenExpiresAt;
                                        if (!creds.gdriveDeviceAuthEndpoint.empty()) cpCfg.oauthDeviceAuthEndpoint = creds.gdriveDeviceAuthEndpoint;
                                        if (!creds.gdriveTokenEndpoint.empty()) cpCfg.oauthTokenEndpoint = creds.gdriveTokenEndpoint;
                                        if (!creds.gdriveApiBaseUrl.empty()) cpCfg.gdriveApiBaseUrl = creds.gdriveApiBaseUrl;
                                        if (!creds.gdriveUploadBaseUrl.empty()) cpCfg.gdriveUploadBaseUrl = creds.gdriveUploadBaseUrl;
                                    }
                                    else if (provStr == "webdav")
                                    {
                                        cpCfg.type = Sync::ECloudProviderType::k_WebDav;
                                        cpCfg.webDavBaseUrl = g_config.syncWebDavUrl.get();
                                        cpCfg.webDavUsername = g_config.syncWebDavUser.get();
                                        cpCfg.webDavPasswordOrToken = creds.webdavPassword;
                                    }

                                    auto provider = Sync::createCloudProvider(cpCfg);
                                    if (!provider)
                                    {
                                        OPENSSL_cleanse(mk.data(), mk.size());
                                        sendJsonResponse(new_socket, 400, "Bad Request", StoreInjectView::renderErrorJson("Cloud provider not configured"));
                                        handled = true;
                                    }
                                    else
                                    {
                                        uint64_t steamId64 = 0x0110000100000001ULL;
                                        char hostBuf[256] = {0};
                                        if (gethostname(hostBuf, sizeof(hostBuf)) != 0) hostBuf[0] = '\0';
                                        std::string localHost = (hostBuf[0] != '\0') ? hostBuf : "local-device";

                                        Sync::SyncStateManifest_t remoteManifest = g_lastRemoteManifest;
                                        if (remoteManifest.entries.empty())
                                        {
                                            std::string etag;
                                            provider->fetchRemoteManifest(steamId64, remoteManifest, etag);
                                        }

                                        std::map<std::string, Sync::SyncFileEntry_t> updatedEntries;
                                        for (const auto& e : remoteManifest.entries)
                                        {
                                            updatedEntries[e.logicalPath] = e;
                                        }

                                        Sync::SyncSelectionFilter_t filter = g_config.syncFilter.get();
                                        auto localFiles = collectLocalSyncFiles(configDir, steamRoot, filter);
                                        auto baseHashes = loadSyncBaseHashes(configDir);

                                        bool cloudModified = false;
                                        size_t appliedDiffs = 0;
                                        size_t appliedRemoves = 0;
                                        uint64_t targetGen = std::max(g_lastLocalGeneration, g_lastRemoteGeneration) + 1;

                                        std::vector<std::string> resolvePaths;
                                        for (const auto& [p, _] : diffChoices) resolvePaths.push_back(p);
                                        for (const auto& [p, _] : removeChoices) resolvePaths.push_back(p);
                                        initSyncProgressPaths("sync", resolvePaths);

                                        // 1. Apply diffChoices ("local" or "remote")
                                        for (const auto& [path, choice] : diffChoices)
                                        {
                                            incrementSyncProgress(path);
                                            if (choice == "remote")
                                            {
                                                auto rit = updatedEntries.find(path);
                                                if (rit != updatedEntries.end())
                                                {
                                                    const auto& rf = rit->second;
                                                    if (g_lastPlaintextPayloads.find(path) == g_lastPlaintextPayloads.end())
                                                    {
                                                        std::string remoteFileName = std::filesystem::path(rf.logicalPath).filename().string();
                                                        std::vector<uint8_t> rawBlob;
                                                        if (provider->pullEncryptedObject(steamId64, rf.logicalPath, rawBlob) == Sync::ECloudError::k_Ok ||
                                                            provider->pullEncryptedObject(steamId64, remoteFileName, rawBlob) == Sync::ECloudError::k_Ok ||
                                                            provider->pullEncryptedObject(steamId64, rf.sha256Hex, rawBlob) == Sync::ECloudError::k_Ok)
                                                        {
                                                            g_lastPlaintextPayloads[path] = std::move(rawBlob);
                                                        }
                                                    }

                                                    auto pit = g_lastPlaintextPayloads.find(path);
                                                    if (pit != g_lastPlaintextPayloads.end())
                                                    {
                                                        std::string destPath = resolveSyncLogicalPathToLocal(path, configDir, steamRoot);
                                                        if (!destPath.empty())
                                                        {
                                                            std::string contentStr(reinterpret_cast<const char*>(pit->second.data()), pit->second.size());
                                                            if (AtomicFile::write(destPath, contentStr, true))
                                                            {
                                                                setFileMtimeUnixSec(destPath, rf.mtimeUnixSec);
                                                                baseHashes[path] = rf.sha256Hex;
                                                                if (path == "config/config.yaml")
                                                                {
                                                                    g_config.loadSettings();
                                                                }
                                                                else if (path.rfind("stplug-in/", 0) == 0 && rf.appId != 0 && !g_config.isAddedAppId(rf.appId))
                                                                {
                                                                    g_config.addAdditionalAppId(rf.appId);
                                                                }
                                                                appliedDiffs++;
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                            else if (choice == "local")
                                            {
                                                auto lit = localFiles.find(path);
                                                if (lit != localFiles.end())
                                                {
                                                    auto& lf = lit->second;
                                                    Sync::SyncFileEntry_t entry;
                                                    entry.category = lf.category;
                                                    entry.appId = lf.appId;
                                                    entry.logicalPath = lf.logicalPath;
                                                    entry.sha256Hex = lf.sha256Hex;
                                                    entry.sizeBytes = lf.sizeBytes;
                                                    entry.mtimeUnixSec = lf.mtimeUnixSec;
                                                    entry.generation = targetGen;
                                                    entry.originDeviceId = localHost;

                                                    ensureLocalSyncFileBytes(lf);
                                                    if (provider->pushEncryptedObject(steamId64, lf.logicalPath, lf.bytes) == Sync::ECloudError::k_Ok)
                                                    {
                                                        updatedEntries[path] = entry;
                                                        baseHashes[path] = lf.sha256Hex;
                                                        cloudModified = true;
                                                        appliedDiffs++;
                                                    }
                                                }
                                            }
                                        }

                                        // 2. Apply removeChoices ("remove" or "keep")
                                        for (const auto& [path, choice] : removeChoices)
                                        {
                                            incrementSyncProgress(path);
                                            std::string destPath = resolveSyncLogicalPathToLocal(path, configDir, steamRoot);
                                            if (choice == "remove")
                                            {
                                                if (!destPath.empty() && std::filesystem::exists(destPath))
                                                {
                                                    std::error_code ec;
                                                    std::filesystem::remove(destPath, ec);
                                                }
                                                std::string remoteFileName = std::filesystem::path(path).filename().string();
                                                if (provider->deleteRemoteObject(steamId64, path) != Sync::ECloudError::k_Ok)
                                                {
                                                    provider->deleteRemoteObject(steamId64, remoteFileName);
                                                }
                                                if (updatedEntries.erase(path) > 0)
                                                {
                                                    cloudModified = true;
                                                }
                                                baseHashes.erase(path);
                                                appliedRemoves++;
                                            }
                                            else if (choice == "keep")
                                            {
                                                // Restore to whichever side is missing it
                                                if ((!destPath.empty() && !std::filesystem::exists(destPath)) && updatedEntries.count(path) > 0)
                                                {
                                                    const auto& rf = updatedEntries[path];
                                                    if (g_lastPlaintextPayloads.find(path) == g_lastPlaintextPayloads.end())
                                                    {
                                                        std::string remoteFileName = std::filesystem::path(rf.logicalPath).filename().string();
                                                        std::vector<uint8_t> rawBlob;
                                                        if (provider->pullEncryptedObject(steamId64, rf.logicalPath, rawBlob) == Sync::ECloudError::k_Ok ||
                                                            provider->pullEncryptedObject(steamId64, remoteFileName, rawBlob) == Sync::ECloudError::k_Ok ||
                                                            provider->pullEncryptedObject(steamId64, rf.sha256Hex, rawBlob) == Sync::ECloudError::k_Ok)
                                                        {
                                                            g_lastPlaintextPayloads[path] = std::move(rawBlob);
                                                        }
                                                    }
                                                    auto pit = g_lastPlaintextPayloads.find(path);
                                                    if (pit != g_lastPlaintextPayloads.end())
                                                    {
                                                        std::string contentStr(reinterpret_cast<const char*>(pit->second.data()), pit->second.size());
                                                        if (AtomicFile::write(destPath, contentStr, true))
                                                        {
                                                            setFileMtimeUnixSec(destPath, rf.mtimeUnixSec);
                                                            baseHashes[path] = rf.sha256Hex;
                                                            if (path.rfind("stplug-in/", 0) == 0)
                                                            {
                                                                scanLuaPluginsAndUpdateConfig();
                                                                auto cfgRit = updatedEntries.find("config/config.yaml");
                                                                auto cfgBit = baseHashes.find("config/config.yaml");
                                                                auto cfgPit = g_lastPlaintextPayloads.find("config/config.yaml");
                                                                if (cfgRit != updatedEntries.end() && cfgBit != baseHashes.end() &&
                                                                    cfgBit->second == cfgRit->second.sha256Hex && cfgPit != g_lastPlaintextPayloads.end())
                                                                {
                                                                    std::string cfgPath = g_config.getPath();
                                                                    std::string cfgStr(reinterpret_cast<const char*>(cfgPit->second.data()), cfgPit->second.size());
                                                                    AtomicFile::write(cfgPath, cfgStr, true);
                                                                    setFileMtimeUnixSec(cfgPath, cfgRit->second.mtimeUnixSec);
                                                                    g_config.loadSettings();
                                                                }
                                                            }
                                                            appliedRemoves++;
                                                        }
                                                    }
                                                }
                                                else if (localFiles.count(path) > 0 && updatedEntries.count(path) == 0)
                                                {
                                                    auto& lf = localFiles[path];
                                                    Sync::SyncFileEntry_t entry;
                                                    entry.category = lf.category;
                                                    entry.appId = lf.appId;
                                                    entry.logicalPath = lf.logicalPath;
                                                    entry.sha256Hex = lf.sha256Hex;
                                                    entry.sizeBytes = lf.sizeBytes;
                                                    entry.mtimeUnixSec = lf.mtimeUnixSec;
                                                    entry.generation = targetGen;
                                                    entry.originDeviceId = localHost;

                                                    ensureLocalSyncFileBytes(lf);
                                                    if (provider->pushEncryptedObject(steamId64, lf.logicalPath, lf.bytes) == Sync::ECloudError::k_Ok)
                                                    {
                                                        updatedEntries[path] = entry;
                                                        baseHashes[path] = lf.sha256Hex;
                                                        cloudModified = true;
                                                        appliedRemoves++;
                                                    }
                                                }
                                            }
                                        }

                                        if (cloudModified)
                                        {
                                            remoteManifest.schemaVersion = 1;
                                            remoteManifest.steamId64 = steamId64;
                                            remoteManifest.keyEpoch = g_syncKeyEpoch;
                                            remoteManifest.deviceId = localHost;
                                            remoteManifest.deviceHostname = localHost;
                                            remoteManifest.globalGeneration = targetGen;
                                            remoteManifest.generatedAtUnixSec = std::chrono::duration_cast<std::chrono::seconds>(
                                                std::chrono::system_clock::now().time_since_epoch()).count();
                                            remoteManifest.entries.clear();
                                            for (const auto& [_, e] : updatedEntries)
                                            {
                                                remoteManifest.entries.push_back(e);
                                            }
                                            remoteManifest.manifestRootSha256 = Sync::computeManifestRootSha256(remoteManifest.entries);

                                            std::string newETag;
                                            if (provider->commitRemoteManifest(steamId64, remoteManifest, "", newETag) == Sync::ECloudError::k_Ok)
                                            {
                                                g_lastRemoteManifest = remoteManifest;
                                                g_lastRemoteGeneration = remoteManifest.globalGeneration;
                                                g_lastLocalGeneration = remoteManifest.globalGeneration;
                                                g_lastHasRemoteBump = false;
                                            }
                                        }

                                        saveSyncBaseHashes(configDir, baseHashes);
                                        OPENSSL_cleanse(mk.data(), mk.size());
                                        finishSyncProgress();

                                        std::ostringstream ss;
                                        ss << "{\"success\":true,\"appliedDiffs\":" << appliedDiffs
                                           << ",\"appliedRemoves\":" << appliedRemoves
                                           << ",\"generation\":" << g_lastRemoteGeneration << "}";
                                        sendJsonResponse(new_socket, 200, "OK", ss.str());
                                        handled = true;
                                    }
                                }
                            }
                            catch (const std::exception& e)
                            {
                                finishSyncProgress();
                                sendJsonResponse(new_socket, 500, "Internal Server Error", StoreInjectView::renderErrorJson(e.what()));
                                handled = true;
                            }
                        }
                        else if (request.find("/sync/config") != std::string::npos)
                        {
                            try
                            {
                                std::string body = extractHttpPostBody(new_socket, request);

                                StoreInjectView::SyncConfigUpdateRequest_t cfgReq;
                                if (StoreInjectView::parseSyncConfigUpdateJson(body, cfgReq))
                                {
                                    if (!cfgReq.role.empty()) g_config.syncRole = cfgReq.role;
                                    if (!cfgReq.topology.empty()) g_config.syncTopology = cfgReq.topology;
                                    if (!cfgReq.cloudProvider.empty()) g_config.syncCloudProvider = cfgReq.cloudProvider;
                                    if (!cfgReq.webDavUrl.empty()) g_config.syncWebDavUrl = cfgReq.webDavUrl;
                                    if (!cfgReq.webDavUser.empty()) g_config.syncWebDavUser = cfgReq.webDavUser;

                                    try
                                    {
                                        std::string configPath = g_config.getPath();
                                        YAML::Node node;
                                        if (std::filesystem::exists(configPath))
                                        {
                                            node = YAML::LoadFile(configPath);
                                        }
                                        if (!cfgReq.role.empty()) node["SyncRole"] = cfgReq.role;
                                        if (!cfgReq.topology.empty()) node["SyncTopology"] = cfgReq.topology;
                                        if (!cfgReq.cloudProvider.empty()) node["SyncCloudProvider"] = cfgReq.cloudProvider;
                                        if (!cfgReq.webDavUrl.empty()) node["SyncWebDavUrl"] = cfgReq.webDavUrl;
                                        if (!cfgReq.webDavUser.empty()) node["SyncWebDavUser"] = cfgReq.webDavUser;

                                        std::stringstream yss;
                                        yss << node;
                                        AtomicFile::write(configPath, yss.str());
                                    }
                                    catch (...) {}

                                    std::string respBody = StoreInjectView::renderSuccessJson("Sync configuration updated");
                                    sendJsonResponse(new_socket, 200, "OK", respBody);
                                    handled = true;
                                }
                                else
                                {
                                    std::string respBody = StoreInjectView::renderErrorJson("Failed to parse sync configuration update");
                                    sendJsonResponse(new_socket, 400, "Bad Request", respBody);
                                    handled = true;
                                }
                            }
                            catch (const std::exception& e)
                            {
                                std::string respBody = StoreInjectView::renderErrorJson(e.what());
                                sendJsonResponse(new_socket, 500, "Internal Server Error", respBody);
                                handled = true;
                            }
                        }
                        else if (request.find("/sync/recovery-key") != std::string::npos)
                        {
                            try
                            {
                                std::string body = extractHttpPostBody(new_socket, request);

                                Sync::MasterKey_t key{};
                                bool rotateRequested = false;
                                std::string errorMsg;
                                if (!StoreInjectView::validateAndParseRecoveryKeyJson(body, key, rotateRequested, errorMsg))
                                {
                                    OPENSSL_cleanse(key.data(), key.size());
                                    std::string respBody = StoreInjectView::renderErrorJson(errorMsg);
                                    sendJsonResponse(new_socket, 400, "Bad Request", respBody);
                                    handled = true;
                                }
                                else if (rotateRequested)
                                {
                                    if (Sync::generateMasterKey(key))
                                    {
                                        std::string masterKeyPath = g_config.getDir() + "/sync_keys/master.key";
                                        Sync::saveMasterKeyFile(masterKeyPath, key);
                                        g_syncKeyEpoch++;
                                        std::string encodedKey = Sync::encodeRecoveryKey(key);
                                        OPENSSL_cleanse(key.data(), key.size());
                                        std::string respBody = "{\"success\":true,\"recoveryKey\":\"" + encodedKey + "\",\"keyEpoch\":" + std::to_string(g_syncKeyEpoch) + "}";
                                        sendJsonResponse(new_socket, 200, "OK", respBody);
                                        handled = true;
                                    }
                                    else
                                    {
                                        OPENSSL_cleanse(key.data(), key.size());
                                        std::string respBody = StoreInjectView::renderErrorJson("Failed to generate new master key");
                                        sendJsonResponse(new_socket, 500, "Internal Server Error", respBody);
                                        handled = true;
                                    }
                                }
                                else
                                {
                                    std::string masterKeyPath = g_config.getDir() + "/sync_keys/master.key";
                                    bool saved = Sync::saveMasterKeyFile(masterKeyPath, key);
                                    OPENSSL_cleanse(key.data(), key.size());
                                    if (saved)
                                    {
                                        std::string respBody = StoreInjectView::renderSuccessJson("Master key imported successfully");
                                        sendJsonResponse(new_socket, 200, "OK", respBody);
                                        handled = true;
                                    }
                                    else
                                    {
                                        std::string respBody = StoreInjectView::renderErrorJson("Failed to save imported master key");
                                        sendJsonResponse(new_socket, 500, "Internal Server Error", respBody);
                                        handled = true;
                                    }
                                }
                            }
                            catch (const std::exception& e)
                            {
                                std::string respBody = StoreInjectView::renderErrorJson(e.what());
                                sendJsonResponse(new_socket, 500, "Internal Server Error", respBody);
                                handled = true;
                            }
                        }
                        else if (request.find("/sync/p2p/pair") != std::string::npos)
                        {
                            try
                            {
                                std::string body = extractHttpPostBody(new_socket, request);

                                if (g_config.syncRole.get() == "overrider" || body.find("\"pin\"") == std::string::npos)
                                {
                                    g_activePairingPin = Sync::generateSixDigitPin();
                                    std::string respBody = "{\"success\":true,\"pin\":\"" + g_activePairingPin + "\",\"status\":\"awaiting_peer\"}";
                                    sendJsonResponse(new_socket, 200, "OK", respBody);
                                    handled = true;
                                }
                                else
                                {
                                    std::string respBody = "{\"success\":true,\"status\":\"paired\"}";
                                    sendJsonResponse(new_socket, 200, "OK", respBody);
                                    handled = true;
                                }
                            }
                            catch (const std::exception& e)
                            {
                                std::string respBody = StoreInjectView::renderErrorJson(e.what());
                                sendJsonResponse(new_socket, 500, "Internal Server Error", respBody);
                                handled = true;
                            }
                        }
                        else if (request.find("/sync/apply") != std::string::npos)
                        {
                            try
                            {
                                std::string body = extractHttpPostBody(new_socket, request);

                                StoreInjectView::SyncApplyRequest_t applyReq;
                                if (StoreInjectView::parseSyncApplyRequestJson(body, applyReq))
                                {
                                    Sync::SyncEngine engine(g_config.getDir());
                                    Sync::SyncApplyReport_t report;
                                    uint64_t steamId64 = 0x0110000100000001ULL;
                                    engine.applyRemoteSyncPackage(steamId64, g_lastRemoteManifest, g_lastPlaintextPayloads, applyReq.filter, applyReq.conflictChoices, report);

                                    g_stagedAcfRestartRequiredAppIds = report.stagedAcfRestartRequiredAppIds;
                                    g_lastHasRemoteBump = false;

                                    std::string respBody = StoreInjectView::renderSyncApplyResultJson(report);
                                    sendJsonResponse(new_socket, 200, "OK", respBody);
                                    handled = true;
                                }
                                else
                                {
                                    std::string respBody = StoreInjectView::renderErrorJson("Failed to parse sync apply request");
                                    sendJsonResponse(new_socket, 400, "Bad Request", respBody);
                                    handled = true;
                                }
                            }
                            catch (const std::exception& e)
                            {
                                std::string respBody = StoreInjectView::renderErrorJson(e.what());
                                sendJsonResponse(new_socket, 500, "Internal Server Error", respBody);
                                handled = true;
                            }
                        }
                        else if (request.find("/sync/rollback") != std::string::npos)
                        {
                            try
                            {
                                std::string body = extractHttpPostBody(new_socket, request);

                                std::string snapshotId;
                                size_t sPos = body.find("\"snapshotId\"");
                                if (sPos != std::string::npos)
                                {
                                    size_t cPos = body.find(':', sPos);
                                    if (cPos != std::string::npos)
                                    {
                                        size_t q1 = body.find('"', cPos);
                                        if (q1 != std::string::npos)
                                        {
                                            size_t q2 = body.find('"', q1 + 1);
                                            if (q2 != std::string::npos)
                                            {
                                                snapshotId = body.substr(q1 + 1, q2 - q1 - 1);
                                            }
                                        }
                                    }
                                }

                                uint64_t steamId64 = 0x0110000100000001ULL;
                                std::string backupRootDir = g_config.getDir() + "/sync_backups";
                                bool ok = Sync::SyncSnapshotManager::restoreSnapshot(backupRootDir, steamId64, snapshotId);
                                if (ok)
                                {
                                    std::string respBody = StoreInjectView::renderSuccessJson("Snapshot restored successfully");
                                    sendJsonResponse(new_socket, 200, "OK", respBody);
                                    handled = true;
                                }
                                else
                                {
                                    std::string respBody = StoreInjectView::renderErrorJson("Failed to restore snapshot");
                                    sendJsonResponse(new_socket, 500, "Internal Server Error", respBody);
                                    handled = true;
                                }
                            }
                            catch (const std::exception& e)
                            {
                                std::string respBody = StoreInjectView::renderErrorJson(e.what());
                                sendJsonResponse(new_socket, 500, "Internal Server Error", respBody);
                                handled = true;
                            }
                        }
                        else if (request.find("/sync/gdrive/browser-start") != std::string::npos)
                        {
                            try
                            {
                                std::string body = extractHttpPostBody(new_socket, request);
                                std::string clientId, clientSecret;
                                StoreInjectView::parseGDriveDeviceStartJson(body, clientId, clientSecret);

                                std::string configDir = g_config.getDir();
                                StoreInjectView::CloudCredentials_t creds;
                                StoreInjectView::loadCloudCredentials(configDir, creds);

                                if (!clientId.empty()) creds.gdriveClientId = clientId;
                                if (!clientSecret.empty()) creds.gdriveClientSecret = clientSecret;

                                if (creds.gdriveClientId.empty() ||
                                    creds.gdriveClientId == "1047814325852-c8u2l2r8j4uv4b11f44k112smf2c7pvs.apps.googleusercontent.com" ||
                                    creds.gdriveClientId == "202264815644.apps.googleusercontent.com")
                                {
                                    const char* envId = std::getenv("NAVI_GDRIVE_CLIENT_ID");
                                    if (envId && *envId) creds.gdriveClientId = envId;
                                    else creds.gdriveClientId = Sync::k_DefaultGDriveClientId;
                                }
                                if (creds.gdriveClientSecret.empty())
                                {
                                    const char* envSec = std::getenv("NAVI_GDRIVE_CLIENT_SECRET");
                                    if (envSec && *envSec) creds.gdriveClientSecret = envSec;
                                    else if (creds.gdriveClientId == Sync::k_DefaultGDriveClientId)
                                        creds.gdriveClientSecret = Sync::k_DefaultGDriveClientSecret;
                                }

                                if (!clientId.empty() || !clientSecret.empty())
                                {
                                    StoreInjectView::saveCloudCredentials(configDir, creds);
                                }

                                Sync::CloudProviderConfig_t cfg;
                                cfg.type = Sync::ECloudProviderType::k_GoogleDrive;
                                cfg.oauthClientId = creds.gdriveClientId;
                                cfg.oauthClientSecret = creds.gdriveClientSecret;
                                cfg.accessToken = creds.gdriveAccessToken;
                                cfg.refreshToken = creds.gdriveRefreshToken;
                                cfg.tokenExpiresAtUnixSec = creds.gdriveTokenExpiresAt;
                                if (!creds.gdriveDeviceAuthEndpoint.empty()) cfg.oauthDeviceAuthEndpoint = creds.gdriveDeviceAuthEndpoint;
                                if (!creds.gdriveTokenEndpoint.empty()) cfg.oauthTokenEndpoint = creds.gdriveTokenEndpoint;
                                if (!creds.gdriveApiBaseUrl.empty()) cfg.gdriveApiBaseUrl = creds.gdriveApiBaseUrl;
                                if (!creds.gdriveUploadBaseUrl.empty()) cfg.gdriveUploadBaseUrl = creds.gdriveUploadBaseUrl;

                                auto pkce = Sync::generatePkcePair();
                                std::string state = Sync::generateOAuthStateHex(32);
                                g_pendingOAuthState = state;
                                g_pendingPkceVerifier = pkce.codeVerifier;

                                std::string redirectUri = "http://127.0.0.1:9001/sync/gdrive/callback";
                                Sync::GoogleDriveAppDataProvider provider(cfg);
                                std::string authUrl = provider.buildBrowserAuthUrl(redirectUri, state, pkce.codeChallenge);

                                std::string respBody = StoreInjectView::renderGDriveBrowserStartJson(authUrl, state);
                                sendJsonResponse(new_socket, 200, "OK", respBody);
                                handled = true;
                            }
                            catch (const std::exception& e)
                            {
                                std::string respBody = StoreInjectView::renderErrorJson(e.what());
                                sendJsonResponse(new_socket, 500, "Internal Server Error", respBody);
                                handled = true;
                            }
                        }
                        else if (request.find("/sync/gdrive/callback") != std::string::npos)
                        {
                            try
                            {
                                size_t qPos = request.find("/sync/gdrive/callback");
                                size_t lineEnd = request.find("\r\n", qPos);
                                if (lineEnd == std::string::npos) lineEnd = request.find("\n", qPos);
                                std::string reqLine = request.substr(qPos, lineEnd == std::string::npos ? std::string::npos : lineEnd - qPos);

                                StoreInjectView::OAuthCallbackParams_t params;
                                StoreInjectView::parseOAuthCallbackQuery(reqLine, params);

                                if (!params.error.empty())
                                {
                                    g_pendingOAuthState.clear();
                                    if (!g_pendingPkceVerifier.empty())
                                    {
                                        OPENSSL_cleanse(g_pendingPkceVerifier.data(), g_pendingPkceVerifier.size());
                                        g_pendingPkceVerifier.clear();
                                    }
                                    std::string errMsg = "Google authorization error: " + params.error;
                                    if (!params.errorDescription.empty()) errMsg += " (" + params.errorDescription + ")";
                                    std::string html = StoreInjectView::renderOAuthCallbackHtml(false, errMsg);
                                    sendHtmlResponse(new_socket, 400, "Bad Request", html);
                                    handled = true;
                                }
                                else if (g_pendingOAuthState.empty() || params.state.empty() || params.state != g_pendingOAuthState)
                                {
                                    std::string html = StoreInjectView::renderOAuthCallbackHtml(false, "Invalid or expired OAuth state parameter. Please try signing in again.");
                                    sendHtmlResponse(new_socket, 400, "Bad Request", html);
                                    handled = true;
                                }
                                else if (params.code.empty())
                                {
                                    g_pendingOAuthState.clear();
                                    if (!g_pendingPkceVerifier.empty())
                                    {
                                        OPENSSL_cleanse(g_pendingPkceVerifier.data(), g_pendingPkceVerifier.size());
                                        g_pendingPkceVerifier.clear();
                                    }
                                    std::string html = StoreInjectView::renderOAuthCallbackHtml(false, "Missing authorization code from Google.");
                                    sendHtmlResponse(new_socket, 400, "Bad Request", html);
                                    handled = true;
                                }
                                else
                                {
                                    std::string configDir = g_config.getDir();
                                    StoreInjectView::CloudCredentials_t creds;
                                    StoreInjectView::loadCloudCredentials(configDir, creds);

                                    if (creds.gdriveClientId.empty() ||
                                    creds.gdriveClientId == "1047814325852-c8u2l2r8j4uv4b11f44k112smf2c7pvs.apps.googleusercontent.com" ||
                                    creds.gdriveClientId == "202264815644.apps.googleusercontent.com")
                                    {
                                        const char* envId = std::getenv("NAVI_GDRIVE_CLIENT_ID");
                                        if (envId && *envId) creds.gdriveClientId = envId;
                                        else creds.gdriveClientId = Sync::k_DefaultGDriveClientId;
                                    }
                                    if (creds.gdriveClientSecret.empty())
                                    {
                                        const char* envSec = std::getenv("NAVI_GDRIVE_CLIENT_SECRET");
                                        if (envSec && *envSec) creds.gdriveClientSecret = envSec;
                                        else if (creds.gdriveClientId == Sync::k_DefaultGDriveClientId)
                                            creds.gdriveClientSecret = Sync::k_DefaultGDriveClientSecret;
                                    }

                                    Sync::CloudProviderConfig_t cfg;
                                    cfg.type = Sync::ECloudProviderType::k_GoogleDrive;
                                    cfg.oauthClientId = creds.gdriveClientId.empty() ? Sync::k_DefaultGDriveClientId : creds.gdriveClientId;
                                    cfg.oauthClientSecret = creds.gdriveClientSecret.empty() ? Sync::k_DefaultGDriveClientSecret : creds.gdriveClientSecret;
                                    cfg.accessToken = creds.gdriveAccessToken;
                                    cfg.refreshToken = creds.gdriveRefreshToken;
                                    cfg.tokenExpiresAtUnixSec = creds.gdriveTokenExpiresAt;
                                    if (!creds.gdriveDeviceAuthEndpoint.empty()) cfg.oauthDeviceAuthEndpoint = creds.gdriveDeviceAuthEndpoint;
                                    if (!creds.gdriveTokenEndpoint.empty()) cfg.oauthTokenEndpoint = creds.gdriveTokenEndpoint;
                                    if (!creds.gdriveApiBaseUrl.empty()) cfg.gdriveApiBaseUrl = creds.gdriveApiBaseUrl;
                                    if (!creds.gdriveUploadBaseUrl.empty()) cfg.gdriveUploadBaseUrl = creds.gdriveUploadBaseUrl;

                                    Sync::GoogleDriveAppDataProvider provider(cfg);
                                    std::string redirectUri = "http://127.0.0.1:9001/sync/gdrive/callback";
                                    std::string codeVerifier = std::move(g_pendingPkceVerifier);
                                    g_pendingOAuthState.clear();
                                    g_pendingPkceVerifier.clear();

                                    Sync::ECloudError err = provider.exchangeAuthCodeForToken(params.code, codeVerifier, redirectUri);
                                    if (!codeVerifier.empty())
                                    {
                                        OPENSSL_cleanse(codeVerifier.data(), codeVerifier.size());
                                    }

                                    if (err == Sync::ECloudError::k_Ok)
                                    {
                                        creds.gdriveAccessToken = provider.config().accessToken;
                                        creds.gdriveRefreshToken = provider.config().refreshToken;
                                        creds.gdriveTokenExpiresAt = provider.config().tokenExpiresAtUnixSec;
                                        StoreInjectView::saveCloudCredentials(configDir, creds);

                                        std::string html = StoreInjectView::renderOAuthCallbackHtml(true, "Navi Cross-Device Sync is now connected to your Google Drive account! You can close this browser tab and return to Steam.");
                                        sendHtmlResponse(new_socket, 200, "OK", html);
                                        handled = true;
                                    }
                                    else
                                    {
                                        std::string html = StoreInjectView::renderOAuthCallbackHtml(false, "Failed to exchange authorization code for access token.");
                                        sendHtmlResponse(new_socket, (err == Sync::ECloudError::k_NetworkError ? 502 : 400), "Bad Request", html);
                                        handled = true;
                                    }
                                }
                            }
                            catch (const std::exception& e)
                            {
                                std::string html = StoreInjectView::renderOAuthCallbackHtml(false, std::string("Internal server error: ") + e.what());
                                sendHtmlResponse(new_socket, 500, "Internal Server Error", html);
                                handled = true;
                            }
                        }
                        else if (request.find("/sync/gdrive/disconnect") != std::string::npos)
                        {
                            try
                            {
                                std::string configDir = g_config.getDir();
                                StoreInjectView::CloudCredentials_t creds;
                                StoreInjectView::loadCloudCredentials(configDir, creds);

                                if (!creds.gdriveAccessToken.empty())
                                {
                                    OPENSSL_cleanse(creds.gdriveAccessToken.data(), creds.gdriveAccessToken.size());
                                    creds.gdriveAccessToken.clear();
                                }
                                if (!creds.gdriveRefreshToken.empty())
                                {
                                    OPENSSL_cleanse(creds.gdriveRefreshToken.data(), creds.gdriveRefreshToken.size());
                                    creds.gdriveRefreshToken.clear();
                                }
                                creds.gdriveTokenExpiresAt = 0;

                                bool saved = StoreInjectView::saveCloudCredentials(configDir, creds);

                                if (!g_pendingPkceVerifier.empty())
                                {
                                    OPENSSL_cleanse(g_pendingPkceVerifier.data(), g_pendingPkceVerifier.size());
                                    g_pendingPkceVerifier.clear();
                                }
                                g_pendingOAuthState.clear();

                                g_cachedDiffItems.clear();
                                g_lastPlaintextPayloads.clear();
                                g_lastHasRemoteBump = false;
                                g_lastRemoteGeneration = 0;

                                if (saved)
                                {
                                    std::string respBody = StoreInjectView::renderSuccessJson("Google Drive disconnected");
                                    sendJsonResponse(new_socket, 200, "OK", respBody);
                                    handled = true;
                                }
                                else
                                {
                                    std::string respBody = StoreInjectView::renderErrorJson("Failed to save cloud credentials during disconnect");
                                    sendJsonResponse(new_socket, 500, "Internal Server Error", respBody);
                                    handled = true;
                                }
                            }
                            catch (const std::exception& e)
                            {
                                std::string respBody = StoreInjectView::renderErrorJson(e.what());
                                sendJsonResponse(new_socket, 500, "Internal Server Error", respBody);
                                handled = true;
                            }
                        }
                        else if (request.find("/sync/gdrive/device-start") != std::string::npos)
                        {
                            try
                            {
                                std::string body = extractHttpPostBody(new_socket, request);
                                std::string clientId, clientSecret;
                                StoreInjectView::parseGDriveDeviceStartJson(body, clientId, clientSecret);

                                std::string configDir = g_config.getDir();
                                StoreInjectView::CloudCredentials_t creds;
                                StoreInjectView::loadCloudCredentials(configDir, creds);

                                if (!clientId.empty()) creds.gdriveClientId = clientId;
                                if (!clientSecret.empty()) creds.gdriveClientSecret = clientSecret;

                                if (creds.gdriveClientId.empty() ||
                                    creds.gdriveClientId == "1047814325852-c8u2l2r8j4uv4b11f44k112smf2c7pvs.apps.googleusercontent.com" ||
                                    creds.gdriveClientId == "202264815644.apps.googleusercontent.com")
                                {
                                    const char* envId = std::getenv("NAVI_GDRIVE_CLIENT_ID");
                                    if (envId && *envId) creds.gdriveClientId = envId;
                                    else creds.gdriveClientId = Sync::k_DefaultGDriveClientId;
                                }
                                if (creds.gdriveClientSecret.empty())
                                {
                                    const char* envSec = std::getenv("NAVI_GDRIVE_CLIENT_SECRET");
                                    if (envSec && *envSec) creds.gdriveClientSecret = envSec;
                                    else if (creds.gdriveClientId == Sync::k_DefaultGDriveClientId)
                                        creds.gdriveClientSecret = Sync::k_DefaultGDriveClientSecret;
                                }

                                if (!clientId.empty() || !clientSecret.empty())
                                {
                                    StoreInjectView::saveCloudCredentials(configDir, creds);
                                }

                                Sync::CloudProviderConfig_t cfg;
                                cfg.type = Sync::ECloudProviderType::k_GoogleDrive;
                                cfg.oauthClientId = creds.gdriveClientId;
                                cfg.oauthClientSecret = creds.gdriveClientSecret;
                                cfg.accessToken = creds.gdriveAccessToken;
                                cfg.refreshToken = creds.gdriveRefreshToken;
                                cfg.tokenExpiresAtUnixSec = creds.gdriveTokenExpiresAt;
                                if (!creds.gdriveDeviceAuthEndpoint.empty()) cfg.oauthDeviceAuthEndpoint = creds.gdriveDeviceAuthEndpoint;
                                if (!creds.gdriveTokenEndpoint.empty()) cfg.oauthTokenEndpoint = creds.gdriveTokenEndpoint;
                                if (!creds.gdriveApiBaseUrl.empty()) cfg.gdriveApiBaseUrl = creds.gdriveApiBaseUrl;
                                if (!creds.gdriveUploadBaseUrl.empty()) cfg.gdriveUploadBaseUrl = creds.gdriveUploadBaseUrl;

                                Sync::GoogleDriveAppDataProvider provider(cfg);
                                Sync::DeviceCodePrompt_t prompt;
                                Sync::ECloudError err = provider.startDeviceAuthFlow(prompt);

                                if (err == Sync::ECloudError::k_Ok)
                                {
                                    std::string respBody = StoreInjectView::renderGDriveDevicePromptJson(prompt);
                                    sendJsonResponse(new_socket, 200, "OK", respBody);
                                    handled = true;
                                }
                                else
                                {
                                    std::string respBody = StoreInjectView::renderErrorJson(
                                        "Google TV/Device Code flow is restricted for desktop applications. "
                                        "Please use the 'Sign in with Google' button above for 1-click browser sign-in.");
                                    sendJsonResponse(new_socket, (err == Sync::ECloudError::k_NetworkError ? 502 : 400), "Bad Request", respBody);
                                    handled = true;
                                }
                            }
                            catch (const std::exception& e)
                            {
                                std::string respBody = StoreInjectView::renderErrorJson(e.what());
                                sendJsonResponse(new_socket, 500, "Internal Server Error", respBody);
                                handled = true;
                            }
                        }
                        else if (request.find("/sync/gdrive/device-poll") != std::string::npos)
                        {
                            try
                            {
                                std::string body = extractHttpPostBody(new_socket, request);
                                std::string deviceCode;
                                if (!StoreInjectView::parseGDriveDevicePollJson(body, deviceCode))
                                {
                                    std::string respBody = StoreInjectView::renderErrorJson("Missing deviceCode in poll request");
                                    sendJsonResponse(new_socket, 400, "Bad Request", respBody);
                                    handled = true;
                                }
                                else
                                {
                                    std::string configDir = g_config.getDir();
                                    StoreInjectView::CloudCredentials_t creds;
                                    StoreInjectView::loadCloudCredentials(configDir, creds);

                                    Sync::CloudProviderConfig_t cfg;
                                    cfg.type = Sync::ECloudProviderType::k_GoogleDrive;
                                    cfg.oauthClientId = creds.gdriveClientId.empty() ? Sync::k_DefaultGDriveClientId : creds.gdriveClientId;
                                    cfg.oauthClientSecret = creds.gdriveClientSecret.empty() ? Sync::k_DefaultGDriveClientSecret : creds.gdriveClientSecret;
                                    cfg.accessToken = creds.gdriveAccessToken;
                                    cfg.refreshToken = creds.gdriveRefreshToken;
                                    cfg.tokenExpiresAtUnixSec = creds.gdriveTokenExpiresAt;
                                    if (!creds.gdriveDeviceAuthEndpoint.empty()) cfg.oauthDeviceAuthEndpoint = creds.gdriveDeviceAuthEndpoint;
                                    if (!creds.gdriveTokenEndpoint.empty()) cfg.oauthTokenEndpoint = creds.gdriveTokenEndpoint;
                                    if (!creds.gdriveApiBaseUrl.empty()) cfg.gdriveApiBaseUrl = creds.gdriveApiBaseUrl;
                                    if (!creds.gdriveUploadBaseUrl.empty()) cfg.gdriveUploadBaseUrl = creds.gdriveUploadBaseUrl;

                                    Sync::GoogleDriveAppDataProvider provider(cfg);
                                    Sync::ECloudError err = provider.pollDeviceAuthToken(deviceCode);

                                    if (err == Sync::ECloudError::k_Ok)
                                    {
                                        creds.gdriveAccessToken = provider.config().accessToken;
                                        creds.gdriveRefreshToken = provider.config().refreshToken;
                                        creds.gdriveTokenExpiresAt = provider.config().tokenExpiresAtUnixSec;
                                        StoreInjectView::saveCloudCredentials(configDir, creds);

                                        std::string respBody = "{\"success\":true,\"status\":\"authenticated\"}";
                                        sendJsonResponse(new_socket, 200, "OK", respBody);
                                        handled = true;
                                    }
                                    else if (err == Sync::ECloudError::k_RateLimited)
                                    {
                                        std::string respBody = "{\"success\":true,\"status\":\"pending\"}";
                                        sendJsonResponse(new_socket, 200, "OK", respBody);
                                        handled = true;
                                    }
                                    else
                                    {
                                        std::string respBody = StoreInjectView::renderErrorJson("Device authorization expired or denied");
                                        sendJsonResponse(new_socket, 400, "Bad Request", respBody);
                                        handled = true;
                                    }
                                }
                            }
                            catch (const std::exception& e)
                            {
                                std::string respBody = StoreInjectView::renderErrorJson(e.what());
                                sendJsonResponse(new_socket, 500, "Internal Server Error", respBody);
                                handled = true;
                            }
                        }
                        else if (request.find("/sync/push") != std::string::npos)
                        {
                            handled = true;
                            bool alreadyRunning = false;
                            {
                                std::lock_guard<std::mutex> lock(g_syncActiveProgress.mtx);
                                if (g_syncActiveProgress.inProgress)
                                {
                                    alreadyRunning = true;
                                }
                            }
                            if (alreadyRunning)
                            {
                                std::string respBody = StoreInjectView::renderErrorJson("A sync or push operation is already in progress.");
                                sendJsonResponse(new_socket, 409, "Conflict", respBody);
                            }
                            else
                            {
                                std::thread([new_socket]() {
                                    try
                                    {
                                        std::string configDir = g_config.getDir();
                                        std::string masterKeyPath = configDir + "/sync_keys/master.key";
                                        Sync::MasterKey_t mk{};
                                        if (!Sync::ensureMasterKeyFile(masterKeyPath, mk))
                                        {
                                            std::string respBody = StoreInjectView::renderErrorJson("Master key not configured");
                                            sendJsonResponse(new_socket, 400, "Bad Request", respBody);
                                            return;
                                        }

                                        StoreInjectView::CloudCredentials_t creds;
                                        StoreInjectView::loadCloudCredentials(configDir, creds);

                                        Sync::CloudProviderConfig_t cpCfg;
                                        cpCfg.masterKey = mk;
                                        cpCfg.keyEpoch = g_syncKeyEpoch;
                                        std::string provStr = g_config.syncCloudProvider.get();
                                        if ((provStr.empty() || provStr == "none") && (!creds.gdriveRefreshToken.empty() || !creds.gdriveAccessToken.empty()))
                                        {
                                            provStr = "gdrive";
                                            g_config.syncCloudProvider = std::string("gdrive");
                                        }
                                        if (provStr == "gdrive")
                                        {
                                            cpCfg.type = Sync::ECloudProviderType::k_GoogleDrive;
                                            cpCfg.oauthClientId = creds.gdriveClientId.empty() ? Sync::k_DefaultGDriveClientId : creds.gdriveClientId;
                                            cpCfg.oauthClientSecret = creds.gdriveClientSecret.empty() ? Sync::k_DefaultGDriveClientSecret : creds.gdriveClientSecret;
                                            cpCfg.accessToken = creds.gdriveAccessToken;
                                            cpCfg.refreshToken = creds.gdriveRefreshToken;
                                            cpCfg.tokenExpiresAtUnixSec = creds.gdriveTokenExpiresAt;
                                            if (!creds.gdriveDeviceAuthEndpoint.empty()) cpCfg.oauthDeviceAuthEndpoint = creds.gdriveDeviceAuthEndpoint;
                                            if (!creds.gdriveTokenEndpoint.empty()) cpCfg.oauthTokenEndpoint = creds.gdriveTokenEndpoint;
                                            if (!creds.gdriveApiBaseUrl.empty()) cpCfg.gdriveApiBaseUrl = creds.gdriveApiBaseUrl;
                                            if (!creds.gdriveUploadBaseUrl.empty()) cpCfg.gdriveUploadBaseUrl = creds.gdriveUploadBaseUrl;
                                        }
                                        else if (provStr == "webdav")
                                        {
                                            cpCfg.type = Sync::ECloudProviderType::k_WebDav;
                                            cpCfg.webDavBaseUrl = g_config.syncWebDavUrl.get();
                                            cpCfg.webDavUsername = g_config.syncWebDavUser.get();
                                            cpCfg.webDavPasswordOrToken = creds.webdavPassword;
                                        }
                                        else
                                        {
                                            cpCfg.type = Sync::ECloudProviderType::k_None;
                                        }

                                        auto provider = Sync::createCloudProvider(cpCfg);
                                        if (!provider)
                                        {
                                            OPENSSL_cleanse(mk.data(), mk.size());
                                            std::string respBody = StoreInjectView::renderErrorJson("Cloud provider not configured or invalid");
                                            sendJsonResponse(new_socket, 400, "Bad Request", respBody);
                                            return;
                                        }

                                        uint64_t steamId64 = 0x0110000100000001ULL;
                                        uint64_t targetGen = std::max(g_lastLocalGeneration, g_lastRemoteGeneration) + 1;
                                        char hostBuf[256] = {0};
                                        if (gethostname(hostBuf, sizeof(hostBuf)) != 0) hostBuf[0] = '\0';
                                        std::string localHost = (hostBuf[0] != '\0') ? hostBuf : "local-device";

                                        Sync::SyncSelectionFilter_t filter = g_config.syncFilter.get();
                                        std::string steamRoot = LuaDownload::findSteamRoot();
                                        auto localFiles = collectLocalSyncFiles(configDir, steamRoot, filter);

                                        initSyncProgress("push", localFiles);

                                        Sync::SyncStateManifest_t manifest;
                                        manifest.schemaVersion = 1;
                                        manifest.steamId64 = steamId64;
                                        manifest.keyEpoch = g_syncKeyEpoch;
                                        manifest.deviceId = localHost;
                                        manifest.deviceHostname = localHost;
                                        manifest.globalGeneration = targetGen;
                                        manifest.generatedAtUnixSec = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();

                                        Sync::SyncStateManifest_t remoteManifest = g_lastRemoteManifest;
                                        if (remoteManifest.entries.empty())
                                        {
                                            std::string etag;
                                            provider->fetchRemoteManifest(steamId64, remoteManifest, etag);
                                        }
                                        std::map<std::string, Sync::SyncFileEntry_t> remoteMap;
                                        for (const auto& e : remoteManifest.entries)
                                        {
                                            remoteMap[e.logicalPath] = e;
                                        }
                                        auto localIndex = loadSyncIndex(configDir);

                                        std::map<std::string, std::string> pushedHashes;
                                        for (auto& [path, lf] : localFiles)
                                        {
                                            auto rIt = remoteMap.find(path);
                                            if (rIt != remoteMap.end() && rIt->second.sha256Hex == lf.sha256Hex)
                                            {
                                                manifest.entries.push_back(rIt->second);
                                                pushedHashes[path] = lf.sha256Hex;
                                                incrementSyncProgress(lf.logicalPath);
                                                continue;
                                            }

                                            Sync::SyncFileEntry_t entry;
                                            entry.category = lf.category;
                                            entry.appId = lf.appId;
                                            entry.logicalPath = lf.logicalPath;
                                            entry.sha256Hex = lf.sha256Hex;
                                            entry.sizeBytes = lf.sizeBytes;
                                            entry.mtimeUnixSec = lf.mtimeUnixSec;
                                            entry.generation = targetGen;
                                            entry.originDeviceId = localHost;

                                            ensureLocalSyncFileBytes(lf);
                                            if (provider->pushEncryptedObject(steamId64, lf.logicalPath, lf.bytes) == Sync::ECloudError::k_Ok)
                                            {
                                                manifest.entries.push_back(entry);
                                                pushedHashes[path] = lf.sha256Hex;

                                                SyncIndexEntry_t idxEntry;
                                                idxEntry.logicalPath = lf.logicalPath;
                                                idxEntry.sha256Hex = lf.sha256Hex;
                                                idxEntry.mtimeUnixSec = lf.mtimeUnixSec;
                                                idxEntry.sizeBytes = lf.sizeBytes;
                                                idxEntry.category = Sync::syncCategoryToString(lf.category);
                                                idxEntry.appId = lf.appId;
                                                localIndex[path] = std::move(idxEntry);
                                            }
                                            incrementSyncProgress(lf.logicalPath);
                                        }

                                        manifest.manifestRootSha256 = Sync::computeManifestRootSha256(manifest.entries);

                                        std::string newETag;
                                        Sync::ECloudError commitErr = provider->commitRemoteManifest(steamId64, manifest, "", newETag);
                                        OPENSSL_cleanse(mk.data(), mk.size());
                                        finishSyncProgress();

                                        if (commitErr == Sync::ECloudError::k_Ok)
                                        {
                                            g_lastLocalGeneration = manifest.globalGeneration;
                                            g_lastRemoteGeneration = manifest.globalGeneration;
                                            g_lastRemoteManifest = manifest;
                                            g_lastHasRemoteBump = false;

                                            saveSyncIndex(configDir, localIndex);

                                            std::string indexYamlPath = configDir + "/sync_index.yaml";
                                            if (std::filesystem::exists(indexYamlPath))
                                            {
                                                std::ifstream idxIfs(indexYamlPath, std::ios::binary);
                                                if (idxIfs.is_open())
                                                {
                                                    std::vector<uint8_t> idxBytes((std::istreambuf_iterator<char>(idxIfs)), std::istreambuf_iterator<char>());
                                                    provider->pushEncryptedObject(steamId64, "sync_index.yaml", idxBytes);
                                                }
                                            }

                                            std::string respBody = StoreInjectView::renderSyncPushResultJson(manifest.globalGeneration, manifest.entries.size(), newETag);
                                            sendJsonResponse(new_socket, 200, "OK", respBody);
                                        }
                                        else if (commitErr == Sync::ECloudError::k_AuthExpired)
                                        {
                                            std::string respBody = StoreInjectView::renderErrorJson(
                                                "Google Drive permissions changed to visible files. Please click 'Connect to Google' to re-authorize visible Drive access.");
                                            sendJsonResponse(new_socket, 401, "Unauthorized", respBody);
                                        }
                                        else
                                        {
                                            std::string respBody = StoreInjectView::renderErrorJson("Failed to commit remote manifest to cloud provider");
                                            sendJsonResponse(new_socket, 502, "Bad Gateway", respBody);
                                        }
                                    }
                                    catch (const std::exception& e)
                                    {
                                        finishSyncProgress();
                                        std::string respBody = StoreInjectView::renderErrorJson(e.what());
                                        sendJsonResponse(new_socket, 500, "Internal Server Error", respBody);
                                    }
                                }).detach();
                            }
                        }
                    }

                    if (!handled)
                    {
                        const char* response = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n";
                        send(new_socket, response, strlen(response), 0);
                        close(new_socket);
                    }
                }
            }
        }
        int fd = g_serverFd.exchange(-1);
        if (fd >= 0) close(fd);
    }
    // patchIndexHtml removed.

    void init()
    {
        g_shutdownCalled = false;
        g_shouldStop = false;

        // Start PowerMonitor for sleep/wake and launch trigger detection
        g_powerMonitor = std::make_unique<Sync::PowerMonitor>(
            [](Sync::ESyncTriggerReason reason) {
                LOG_INFO("StoreInject: Power/Sync trigger fired (%d)\n", static_cast<int>(reason));
                if (g_config.syncRole.get() == "disabled") return;

                switch (reason)
                {
                    case Sync::ESyncTriggerReason::k_Launch:
                        g_lastSyncTriggerReason = "launch";
                        break;
                    case Sync::ESyncTriggerReason::k_WakeFromSleep:
                        g_lastSyncTriggerReason = "wake_from_sleep";
                        break;
                    case Sync::ESyncTriggerReason::k_ManualUserCheck:
                        g_lastSyncTriggerReason = "manual";
                        break;
                    case Sync::ESyncTriggerReason::k_P2PPeerDiscovered:
                        g_lastSyncTriggerReason = "p2p";
                        break;
                }

                CDPInject::injectSyncOverlayModal();
            }
        );
        g_powerMonitor->start();

        // Start the callback server immediately as it's just a passive listener
        g_workerThread = std::thread(callbackServer);

        // Delay the automation worker by 3 seconds to let Steam initialize safely
        std::thread([]() {
            std::this_thread::sleep_for(std::chrono::seconds(3));
            if (!g_shouldStop) {
                g_autoThread = std::thread(automationWorker);
                g_autoThread.detach(); 
            }
        }).detach();
    }

    void shutdown()
    {
        if (g_shutdownCalled.exchange(true)) return;
        g_shouldStop = true;
        if (g_powerMonitor)
        {
            g_powerMonitor->stop();
            g_powerMonitor.reset();
        }
        int fd = g_serverFd.exchange(-1);
        if (fd >= 0)
        {
            ::shutdown(fd, SHUT_RDWR);
            ::close(fd);
        }
        if (g_workerThread.joinable())
        {
            g_workerThread.join();
        }
    }
}
