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
#include "cloud_oauth.hpp"
#include "cloud_manager.hpp"
#include "cloud_pending_ops.hpp"
#include "cloud_view.hpp"
#include "cloudredirect.hpp"
#include "autocrack_manager.hpp"
#include "prefix_bucket_sync.hpp"
#include "globals.hpp"
#include <unistd.h>
#include <openssl/crypto.h>
#include <openssl/rand.h>
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
#include <sys/eventfd.h>
#include <poll.h>

namespace StoreInject
{
    static std::thread g_workerThread;
    static std::thread g_autoThread;
    static std::set<uint32_t> g_pendingRestartApps;
    static std::atomic<bool> g_shouldStop(false);
    static std::atomic<int> g_serverFd{-1};
    static std::atomic<int> g_wakeFd{-1};
    static std::atomic<bool> g_shutdownCalled{false};
    static std::string g_naviAuthToken;

    const std::string& getNaviAuthToken()
    {
        if (g_naviAuthToken.empty())
        {
            unsigned char raw[32];
            if (RAND_bytes(raw, sizeof(raw)) == 1)
            {
                std::ostringstream ss;
                ss << std::hex << std::setfill('0');
                for (size_t i = 0; i < sizeof(raw); ++i)
                {
                    ss << std::setw(2) << static_cast<unsigned int>(raw[i]);
                }
                g_naviAuthToken = ss.str();
            }
        }
        return g_naviAuthToken;
    }

    void setNaviAuthTokenForTest(const std::string& token)
    {
        g_naviAuthToken = token;
    }

    static std::string extractHeaderValue(const std::string& request, const std::string& headerName)
    {
        std::string needle = headerName + ":";
        size_t pos = request.find(needle);
        if (pos == std::string::npos)
        {
            std::string lowerReq = request;
            std::string lowerNeedle = needle;
            std::transform(lowerReq.begin(), lowerReq.end(), lowerReq.begin(), ::tolower);
            std::transform(lowerNeedle.begin(), lowerNeedle.end(), lowerNeedle.begin(), ::tolower);
            pos = lowerReq.find(lowerNeedle);
        }
        if (pos == std::string::npos) return "";
        size_t start = request.find_first_not_of(" \t", pos + needle.size());
        if (start == std::string::npos) return "";
        size_t end = request.find_first_of("\r\n", start);
        if (end == std::string::npos) end = request.size();
        return request.substr(start, end - start);
    }

    static bool isOriginAllowed(const std::string& origin)
    {
        if (origin.empty()) return true;
        if (origin == "https://steamloopback.host") return true;
        if (origin == "http://127.0.0.1:9001") return true;
        if (origin == "http://localhost:9001") return true;
        return false;
    }

    static bool verifyNaviAuthToken(const std::string& request)
    {
        std::string token = extractHeaderValue(request, "X-Navi-Auth");
        if (token.empty())
        {
            token = extractHeaderValue(request, "x-navi-auth");
        }
        const std::string& expected = getNaviAuthToken();
        if (token.empty() || expected.empty()) return false;
        if (token.size() != expected.size()) return false;
        return CRYPTO_memcmp(token.data(), expected.data(), token.size()) == 0;
    }

    static void sendJsonResponse(int sock, int code, const std::string& statusMsg, const std::string& jsonBody)
    {
        std::string response = "HTTP/1.1 " + std::to_string(code) + " " + statusMsg + "\r\n"
                               "Content-Type: application/json\r\n"
                               "Access-Control-Allow-Origin: *\r\n"
                               "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                               "Access-Control-Allow-Headers: Content-Type\r\n"
                               "Access-Control-Allow-Headers: Content-Type, X-Navi-Auth\r\n"
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
                               "Access-Control-Allow-Headers: Content-Type, X-Navi-Auth\r\n"
                               "Connection: close\r\n"
                               "Content-Length: " + std::to_string(htmlBody.size()) + "\r\n\r\n" + htmlBody;
        send(sock, response.c_str(), response.size(), 0);
        close(sock);
    }

    static void sendBinaryDownloadResponse(int sock, const std::string& downloadName, const std::string& data)
    {
        std::string safeName;
        for (char c : downloadName)
        {
            if (c != '"' && c != '\r' && c != '\n' && c != '/' && c != '\\')
            {
                safeName += c;
            }
        }
        if (safeName.empty()) safeName = "cloud_save.bin";

        std::string header = "HTTP/1.1 200 OK\r\n"
                             "Content-Type: application/octet-stream\r\n"
                             "Content-Disposition: attachment; filename=\"" + safeName + "\"\r\n"
                             "Access-Control-Allow-Origin: *\r\n"
                             "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                             "Access-Control-Allow-Headers: Content-Type, X-Navi-Auth\r\n"
                             "Access-Control-Expose-Headers: Content-Disposition\r\n"
                             "Connection: close\r\n"
                             "Content-Length: " + std::to_string(data.size()) + "\r\n\r\n";
        send(sock, header.c_str(), header.size(), 0);
        size_t sentTotal = 0;
        while (sentTotal < data.size())
        {
            ssize_t n = send(sock, data.data() + sentTotal, data.size() - sentTotal, 0);
            if (n <= 0) break;
            sentTotal += static_cast<size_t>(n);
        }
        close(sock);
    }

    static std::string extractCloudQueryParam(const std::string& uriPath, const std::string& key)
    {
        size_t qmark = uriPath.find('?');
        if (qmark == std::string::npos) return "";
        std::string query = uriPath.substr(qmark + 1);
        size_t p = 0;
        while ((p = query.find(key + "=", p)) != std::string::npos)
        {
            if (p == 0 || query[p - 1] == '&')
            {
                size_t vStart = p + key.size() + 1;
                size_t vEnd = query.find('&', vStart);
                return urlDecode(query.substr(vStart, vEnd == std::string::npos ? std::string::npos : vEnd - vStart));
            }
            p += key.size() + 1;
        }
        return "";
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
            if (g_shouldStop) break;
            if (page.webSocketDebuggerUrl.empty() ||
                page.url.find("store.steampowered.com") == std::string::npos)
                continue;

            std::string authInjectJs = "if (!window.__naviAuthToken) { window.__naviAuthToken = \"" + getNaviAuthToken() + "\"; }";
            CDPInject::injectJS(page.webSocketDebuggerUrl, authInjectJs);
            if (g_shouldStop) break;

            std::string encoded = CDPInject::evalString(page.webSocketDebuggerUrl, kDrainExpr);
            if (encoded.empty()) continue;

            std::stringstream lines(urlDecode(encoded));
            std::string line;
            while (std::getline(lines, line))
            {
                if (g_shouldStop) break;
                size_t sp = line.find(' ');
                if (sp == std::string::npos || sp == 0 || sp + 1 >= line.size()) continue;
                std::string id = line.substr(0, sp);
                std::string path = line.substr(sp + 1);

                // Only ever forward plain endpoint paths to the callback server
                if (path[0] != '/' || path.find_first_of(" \r\n") != std::string::npos) continue;
                if (id.find_first_not_of("0123456789-") != std::string::npos) continue;

                std::string body;
                int status = loopbackRequest(path, body);
                if (g_shouldStop) break;

                std::string js = "window.__naviResolve && window.__naviResolve(\"" + id + "\"," +
                                 std::to_string(status) + ",decodeURIComponent(\"" + urlEncode(body) + "\"));";
                CDPInject::injectJS(page.webSocketDebuggerUrl, js);
            }
        }
    }

    static void automationWorker()
    {
        LOG_INFO("StoreInject: Automation worker started\n");

        for (int i = 0; i < 30 && !g_shouldStop; ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        int checkCounter = 100;
        int bridgeCounter = 0;

        while (!g_shouldStop)
        {
            // Try to connect to CDP 8080 endpoint (dual-stack IPv4/IPv6 with fallback)
            int sock = CDPInject::tcpConnect("127.0.0.1", 8080, 1);
            if (sock >= 0)
            {
                if (g_shouldStop)
                {
                    close(sock);
                    break;
                }

                // Serve store-page requests roughly every 300ms
                if (bridgeCounter++ >= 6)
                {
                    bridgeCounter = 0;
                    pollPageBridge();
                }

                if (g_shouldStop)
                {
                    close(sock);
                    break;
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

            for (int i = 0; i < 5 && !g_shouldStop; ++i)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
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

        int wake_fd = g_wakeFd.load();

        LOG_INFO("StoreInject: Callback server listening on port 9001\n");

        while (!g_shouldStop)
        {
            struct pollfd pfds[2];
            pfds[0].fd = server_fd;
            pfds[0].events = POLLIN;
            pfds[0].revents = 0;
            pfds[1].fd = wake_fd;
            pfds[1].events = POLLIN | POLLERR | POLLHUP | POLLNVAL;
            pfds[1].revents = 0;

            int ret = poll(pfds, 2, 1000);
            if (ret > 0)
            {
                if (wake_fd >= 0 && (pfds[1].revents & (POLLIN | POLLERR | POLLHUP | POLLNVAL)))
                {
                    uint64_t val = 0;
                    (void)read(wake_fd, &val, sizeof(val));
                    if (g_shouldStop) break;
                }

                if (pfds[0].revents & POLLIN)
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
                        else if (!isOriginAllowed(extractHeaderValue(request, "Origin")))
                        {
                            sendJsonResponse(new_socket, 403, "Forbidden", Navi::CloudView::renderErrorJson("Forbidden: invalid origin"));
                            handled = true;
                        }
                        else if (request.find("/cloud") != std::string::npos)
                        {
                            std::string method, uriPath;
                            size_t firstSpace = request.find(' ');
                            if (firstSpace != std::string::npos)
                            {
                                method = request.substr(0, firstSpace);
                                size_t secondSpace = request.find(' ', firstSpace + 1);
                                if (secondSpace != std::string::npos)
                                {
                                    uriPath = request.substr(firstSpace + 1, secondSpace - firstSpace - 1);
                                }
                            }

                            if (method == "GET" && (uriPath == "/cloud/status" || uriPath.rfind("/cloud/status?", 0) == 0))
                            {
                                bool enabled = Navi::CloudRedirect::CCloudRedirectBridge::Instance().IsEnabled();
                                std::string provider = g_config.cloudSavesProvider.get();
                                size_t managedAppsCount = g_config.cloudSavesManagedApps.get().size();
                                size_t pendingCount = 0;
                                for (const auto& op : Navi::CloudRedirect::CPendingOpsQueue::Instance().LoadAll())
                                {
                                    if (op.status == Navi::CloudRedirect::PendingOpStatus::Pending) pendingCount++;
                                }
                                std::string resp = Navi::CloudView::renderStatusJson(enabled, provider, managedAppsCount, pendingCount);
                                sendJsonResponse(new_socket, 200, "OK", resp);
                                handled = true;
                            }
                            else if (method == "POST" && uriPath == "/cloud/toggle")
                            {
                                if (!verifyNaviAuthToken(request))
                                {
                                    sendJsonResponse(new_socket, 401, "Unauthorized", Navi::CloudView::renderErrorJson("Unauthorized: invalid X-Navi-Auth token"));
                                    handled = true;
                                }
                                else
                                {
                                    std::string body = extractHttpPostBody(new_socket, request);
                                    bool enable = Navi::CloudView::parseBoolField(body, "enable", false);
                                    Navi::CloudRedirect::CCloudRedirectBridge::Instance().SetEnabledState(enable);
                                    g_config.cloudSavesEnabled.set(enable);
                                    sendJsonResponse(new_socket, 200, "OK", Navi::CloudView::renderSuccessJson("Cloud saves " + std::string(enable ? "enabled" : "disabled")));
                                    handled = true;
                                }
                            }
                            else if (method == "GET" && (uriPath == "/cloud/provider/status" || uriPath.rfind("/cloud/provider/status?", 0) == 0))
                            {
                                std::string provider;
                                size_t qmark = uriPath.find('?');
                                if (qmark != std::string::npos)
                                {
                                    std::string query = uriPath.substr(qmark + 1);
                                    size_t ppos = query.find("provider=");
                                    if (ppos != std::string::npos)
                                    {
                                        size_t vend = query.find('&', ppos + 9);
                                        provider = urlDecode(query.substr(ppos + 9, vend == std::string::npos ? std::string::npos : vend - (ppos + 9)));
                                    }
                                }
                                if (provider.empty()) provider = g_config.cloudSavesProvider.get();
                                auto st = Navi::CloudRedirect::CCloudManager::Instance().CheckAuthStatus(provider);
                                sendJsonResponse(new_socket, 200, "OK", Navi::CloudView::renderProviderStatusJson(st));
                                handled = true;
                            }
                            else if (method == "POST" && uriPath == "/cloud/provider/signin")
                            {
                                if (!verifyNaviAuthToken(request))
                                {
                                    sendJsonResponse(new_socket, 401, "Unauthorized", Navi::CloudView::renderErrorJson("Unauthorized: invalid X-Navi-Auth token"));
                                    handled = true;
                                }
                                else
                                {
                                    std::string body = extractHttpPostBody(new_socket, request);
                                    std::string provider = Navi::CloudView::parseStringField(body, "provider");
                                    if (provider.empty()) provider = g_config.cloudSavesProvider.get();
                                    auto provEnum = Navi::CloudRedirect::StringToOAuthProvider(provider);
                                    std::string outErr;
                                    std::string authUrl = Navi::CloudRedirect::COAuthEngine::Instance().StartAuthFlow(provEnum, outErr);
                                    if (authUrl.empty())
                                    {
                                        sendJsonResponse(new_socket, 409, "Conflict", Navi::CloudView::renderErrorJson(outErr));
                                    }
                                    else
                                    {
                                        std::string resp = "{\"authUrl\":\"" + authUrl + "\",\"provider\":\"" + provider + "\"}";
                                        sendJsonResponse(new_socket, 200, "OK", resp);
                                    }
                                    handled = true;
                                }
                            }
                            else if (method == "POST" && uriPath == "/cloud/provider/signout")
                            {
                                if (!verifyNaviAuthToken(request))
                                {
                                    sendJsonResponse(new_socket, 401, "Unauthorized", Navi::CloudView::renderErrorJson("Unauthorized: invalid X-Navi-Auth token"));
                                    handled = true;
                                }
                                else
                                {
                                    std::string body = extractHttpPostBody(new_socket, request);
                                    std::string provider = Navi::CloudView::parseStringField(body, "provider");
                                    if (provider.empty()) provider = g_config.cloudSavesProvider.get();
                                    auto provEnum = Navi::CloudRedirect::StringToOAuthProvider(provider);
                                    Navi::CloudRedirect::COAuthEngine::Instance().SignOut(provEnum);
                                    sendJsonResponse(new_socket, 200, "OK", Navi::CloudView::renderSuccessJson("Signed out"));
                                    handled = true;
                                }
                            }
                            else if (method == "GET" && (uriPath == "/cloud/apps" || uriPath.rfind("/cloud/apps?", 0) == 0))
                            {
                                std::string provider = extractCloudQueryParam(uriPath, "provider");
                                if (provider.empty()) provider = g_config.cloudSavesProvider.get();
                                uint32_t acctId = g_currentSteamId.isSet() ? g_currentSteamId.accountId() : 0;

                                auto apps = Navi::CloudRedirect::CCloudManager::Instance().ListCloudApps(provider, acctId);
                                auto managedSet = g_config.cloudSavesManagedApps.get();
                                auto knownTitles = g_config.gameTitles.get();

                                std::set<AppId_t> seenApps;
                                for (const auto& a : apps) seenApps.insert(a.appId);

                                if (provider.empty() || provider == "local-only")
                                {
                                    for (AppId_t mAppId : managedSet)
                                    {
                                        if (mAppId == 0 || seenApps.contains(mAppId)) continue;
                                        auto mFiles = Navi::CloudRedirect::CCloudManager::Instance().ListAppCloudFiles(mAppId, provider, acctId);
                                        Navi::CloudRedirect::CloudAppSummary_t summary;
                                        summary.appId = mAppId;
                                        summary.gameTitle = Navi::CloudRedirect::CCloudManager::Instance().ResolveAppTitleFromManifests(mAppId);
                                        summary.fileCount = mFiles.size();
                                        for (const auto& f : mFiles) summary.totalSizeBytes += f.sizeBytes;
                                        summary.redirected = true;
                                        apps.push_back(std::move(summary));
                                        seenApps.insert(mAppId);
                                    }
                                }

                                for (auto& a : apps)
                                {
                                    auto tIt = knownTitles.find(a.appId);
                                    if (tIt != knownTitles.end() && !tIt->second.empty())
                                    {
                                        a.gameTitle = tIt->second;
                                    }
                                    if (a.gameTitle.empty())
                                    {
                                        a.gameTitle = "App " + std::to_string(a.appId);
                                    }
                                    a.redirected = g_config.IsAppCloudRedirected(a.appId);
                                }

                                apps.erase(
                                    std::remove_if(apps.begin(), apps.end(),
                                                   [](const Navi::CloudRedirect::CloudAppSummary_t& a) { return !a.redirected; }),
                                    apps.end());

                                sendJsonResponse(new_socket, 200, "OK", Navi::CloudView::renderCloudAppsJson(provider, apps));
                                handled = true;
                            }
                            else if (method == "GET" && uriPath.rfind("/cloud/apps/", 0) == 0 && uriPath.find("/files/download") != std::string::npos)
                            {
                                size_t idStart = 12;
                                size_t slashEnd = uriPath.find('/', idStart);
                                AppId_t appId = 0;
                                try { appId = std::stoul(uriPath.substr(idStart, slashEnd - idStart)); } catch (...) {}
                                std::string fileName = extractCloudQueryParam(uriPath, "file");
                                std::string provider = extractCloudQueryParam(uriPath, "provider");
                                if (provider.empty()) provider = g_config.cloudSavesProvider.get();
                                uint32_t acctId = g_currentSteamId.isSet() ? g_currentSteamId.accountId() : 0;

                                std::string content;
                                std::string downloadName;
                                std::string err;
                                if (!Navi::CloudRedirect::CCloudManager::Instance().ReadAppCloudFile(appId, fileName, provider, acctId, content, downloadName, err))
                                {
                                    sendJsonResponse(new_socket, 404, "Not Found", Navi::CloudView::renderErrorJson(err));
                                }
                                else
                                {
                                    sendBinaryDownloadResponse(new_socket, downloadName, content);
                                }
                                handled = true;
                            }
                            else if (method == "POST" && uriPath.rfind("/cloud/apps/", 0) == 0 && uriPath.find("/files/download") != std::string::npos)
                            {
                                if (!verifyNaviAuthToken(request))
                                {
                                    sendJsonResponse(new_socket, 401, "Unauthorized", Navi::CloudView::renderErrorJson("Unauthorized: invalid X-Navi-Auth token"));
                                    handled = true;
                                }
                                else
                                {
                                    size_t idStart = 12;
                                    size_t slashEnd = uriPath.find('/', idStart);
                                    AppId_t appId = 0;
                                    try { appId = std::stoul(uriPath.substr(idStart, slashEnd - idStart)); } catch (...) {}
                                    std::string body = extractHttpPostBody(new_socket, request);
                                    std::string fileName = Navi::CloudView::parseStringField(body, "fileName");
                                    std::string provider = Navi::CloudView::parseStringField(body, "provider");
                                    if (provider.empty()) provider = g_config.cloudSavesProvider.get();
                                    uint32_t acctId = g_currentSteamId.isSet() ? g_currentSteamId.accountId() : 0;

                                    std::string content;
                                    std::string downloadName;
                                    std::string err;
                                    if (!Navi::CloudRedirect::CCloudManager::Instance().ReadAppCloudFile(appId, fileName, provider, acctId, content, downloadName, err))
                                    {
                                        sendJsonResponse(new_socket, 404, "Not Found", Navi::CloudView::renderErrorJson(err));
                                    }
                                    else
                                    {
                                        const char* homeEnv = std::getenv("HOME");
                                        fs::path dlDir = homeEnv ? (fs::path(homeEnv) / "Downloads") : fs::temp_directory_path();
                                        std::error_code ec;
                                        if (!fs::exists(dlDir, ec))
                                        {
                                            fs::create_directories(dlDir, ec);
                                        }
                                        std::string safeLeaf;
                                        for (char c : downloadName)
                                        {
                                            if (c != '/' && c != '\\' && c != '\0') safeLeaf += c;
                                        }
                                        if (safeLeaf.empty()) safeLeaf = "save.dat";
                                        fs::path targetPath = dlDir / (std::to_string(appId) + "_" + safeLeaf);
                                        std::ofstream out(targetPath, std::ios::binary | std::ios::trunc);
                                        if (!out.is_open())
                                        {
                                            sendJsonResponse(new_socket, 500, "Internal Server Error", Navi::CloudView::renderErrorJson("Failed to write downloaded file to " + targetPath.string()));
                                        }
                                        else
                                        {
                                            out.write(content.data(), static_cast<std::streamsize>(content.size()));
                                            out.close();
                                            auto esc = [](const std::string& s) {
                                                std::string r;
                                                for (char c : s) {
                                                    if (c == '"') r += "\\\"";
                                                    else if (c == '\\') r += "\\\\";
                                                    else r += c;
                                                }
                                                return r;
                                            };
                                            std::ostringstream oss;
                                            oss << "{\"success\":true,\"appId\":" << appId
                                                << ",\"fileName\":\"" << esc(downloadName) << "\""
                                                << ",\"savedPath\":\"" << esc(targetPath.string()) << "\""
                                                << ",\"sizeBytes\":" << content.size() << "}";
                                            sendJsonResponse(new_socket, 200, "OK", oss.str());
                                        }
                                    }
                                    handled = true;
                                }
                            }
                            else if (method == "GET" && uriPath.rfind("/cloud/apps/", 0) == 0 && uriPath.find("/files") != std::string::npos)
                            {
                                size_t idStart = 12;
                                size_t slashEnd = uriPath.find('/', idStart);
                                AppId_t appId = 0;
                                try { appId = std::stoul(uriPath.substr(idStart, slashEnd - idStart)); } catch (...) {}
                                std::string provider = extractCloudQueryParam(uriPath, "provider");
                                if (provider.empty()) provider = g_config.cloudSavesProvider.get();
                                uint32_t acctId = g_currentSteamId.isSet() ? g_currentSteamId.accountId() : 0;

                                auto files = Navi::CloudRedirect::CCloudManager::Instance().ListAppCloudFiles(appId, provider, acctId);
                                std::string gameTitle;
                                auto knownTitles = g_config.gameTitles.get();
                                auto tIt = knownTitles.find(appId);
                                if (tIt != knownTitles.end() && !tIt->second.empty())
                                {
                                    gameTitle = tIt->second;
                                }
                                else
                                {
                                    gameTitle = Navi::CloudRedirect::CCloudManager::Instance().ResolveAppTitleFromManifests(appId);
                                }
                                bool redirected = g_config.IsAppCloudRedirected(appId);
                                sendJsonResponse(new_socket, 200, "OK", Navi::CloudView::renderAppCloudFilesJson(appId, gameTitle, provider, redirected, files));
                                handled = true;
                            }
                            else if (method == "POST" && uriPath.rfind("/cloud/apps/", 0) == 0 && uriPath.find("/sync") != std::string::npos)
                            {
                                if (!verifyNaviAuthToken(request))
                                {
                                    sendJsonResponse(new_socket, 401, "Unauthorized", Navi::CloudView::renderErrorJson("Unauthorized: invalid X-Navi-Auth token"));
                                    handled = true;
                                }
                                else
                                {
                                    size_t idStart = 12;
                                    size_t slashEnd = uriPath.find('/', idStart);
                                    AppId_t appId = 0;
                                    try { appId = std::stoul(uriPath.substr(idStart, slashEnd - idStart)); } catch (...) {}

                                    uint32_t acctId = g_currentSteamId.isSet() ? g_currentSteamId.accountId() : 0;
                                    std::string provider = g_config.cloudSavesProvider.get();
                                    auto syncRes = PrefixBucketSync::RunJobForTest(appId, PrefixBucketSync::Mode::Full, "manual-ui");
                                    const auto childDepots = Navi::CloudRedirect::CCloudManager::Instance().GetChildDepotIdsForApp(appId);
                                    for (AppId_t cid : childDepots)
                                    {
                                        auto childRes = PrefixBucketSync::RunJobForTest(cid, PrefixBucketSync::Mode::Full, "manual-ui");
                                        syncRes.filesPulled += childRes.filesPulled;
                                        syncRes.filesPushed += childRes.filesPushed;
                                    }
                                    if (Navi::CloudRedirect::CCloudManager::Instance().SyncAppToCloudAndUpdateCatalog(appId, provider, acctId))
                                    {
                                        syncRes.ok = true;
                                    }
                                    auto files = Navi::CloudRedirect::CCloudManager::Instance().ListAppCloudFiles(appId, provider, acctId);

                                    auto esc = [](const std::string& s) {
                                        std::string r;
                                        for (char c : s) {
                                            if (c == '"') r += "\\\"";
                                            else if (c == '\\') r += "\\\\";
                                            else r += c;
                                        }
                                        return r;
                                    };
                                    std::ostringstream oss;
                                    oss << "{\"success\":true,\"appId\":" << appId
                                        << ",\"filesPulled\":" << syncRes.filesPulled
                                        << ",\"filesPushed\":" << syncRes.filesPushed
                                        << ",\"fileCount\":" << files.size()
                                        << ",\"message\":\"" << esc(syncRes.message.empty() ? "Cloud sync complete" : syncRes.message) << "\"}";
                                    sendJsonResponse(new_socket, 200, "OK", oss.str());
                                    handled = true;
                                }
                            }
                            else if (method == "GET" && uriPath.rfind("/cloud/apps/", 0) == 0 && uriPath.find("/status") != std::string::npos)
                            {
                                size_t idStart = 12; // length of "/cloud/apps/"
                                size_t slashEnd = uriPath.find('/', idStart);
                                AppId_t appId = 0;
                                try { appId = std::stoul(uriPath.substr(idStart, slashEnd - idStart)); } catch (...) {}
                                bool redirected = g_config.IsAppCloudRedirected(appId);
                                uint64_t usage = Navi::CloudRedirect::CCloudManager::Instance().GetStorageUsageBytes(appId);
                                auto backups = Navi::CloudRedirect::CCloudManager::Instance().ListBackups(appId);
                                sendJsonResponse(new_socket, 200, "OK", Navi::CloudView::renderAppStatusJson(appId, redirected, usage, backups));
                                handled = true;
                            }
                            else if (method == "POST" && uriPath.rfind("/cloud/apps/", 0) == 0 && uriPath.find("/toggle") != std::string::npos)
                            {
                                if (!verifyNaviAuthToken(request))
                                {
                                    sendJsonResponse(new_socket, 401, "Unauthorized", Navi::CloudView::renderErrorJson("Unauthorized: invalid X-Navi-Auth token"));
                                    handled = true;
                                }
                                else
                                {
                                    size_t idStart = 12;
                                    size_t slashEnd = uriPath.find('/', idStart);
                                    AppId_t appId = 0;
                                    try { appId = std::stoul(uriPath.substr(idStart, slashEnd - idStart)); } catch (...) {}
                                    std::string body = extractHttpPostBody(new_socket, request);
                                    bool redirect = Navi::CloudView::parseBoolField(body, "redirect", false);
                                    auto managedApps = g_config.cloudSavesManagedApps.get();
                                    auto disabledApps = g_config.cloudSavesDisabledApps.get();
                                    if (redirect)
                                    {
                                        managedApps.insert(appId);
                                        disabledApps.erase(appId);
                                    }
                                    else
                                    {
                                        managedApps.erase(appId);
                                        disabledApps.insert(appId);
                                        Navi::CloudRedirect::CCloudRedirectBridge::Instance().RevertSlsSteamConfig({appId});
                                    }
                                    g_config.cloudSavesManagedApps.set(managedApps);
                                    g_config.cloudSavesDisabledApps.set(disabledApps);
                                    Navi::CloudRedirect::CCloudRedirectBridge::Instance().SyncSlsSteamConfig(g_config.GetEffectiveCloudRedirectedApps());
                                    sendJsonResponse(new_socket, 200, "OK", Navi::CloudView::renderAppToggleJson(appId, redirect));
                                    handled = true;
                                }
                            }
                            else if (method == "POST" && uriPath.rfind("/cloud/apps/", 0) == 0 && uriPath.find("/backup") != std::string::npos)
                            {
                                if (!verifyNaviAuthToken(request))
                                {
                                    sendJsonResponse(new_socket, 401, "Unauthorized", Navi::CloudView::renderErrorJson("Unauthorized: invalid X-Navi-Auth token"));
                                    handled = true;
                                }
                                else
                                {
                                    size_t idStart = 12;
                                    size_t slashEnd = uriPath.find('/', idStart);
                                    AppId_t appId = 0;
                                    try { appId = std::stoul(uriPath.substr(idStart, slashEnd - idStart)); } catch (...) {}
                                    
                                    fs::path backupDir = Navi::CloudRedirect::CCloudRedirectBridge::Instance().GetPaths().naviBackupDir / std::to_string(appId);
                                    std::error_code ec;
                                    fs::create_directories(backupDir, ec);
                                    uint64_t ts = static_cast<uint64_t>(std::time(nullptr));
                                    fs::path backupFile = backupDir / ("manual-" + std::to_string(ts) + ".tar.gz");

                                    const auto& crPaths = Navi::CloudRedirect::CCloudRedirectBridge::Instance().GetPaths();
                                    fs::path steamRoot = crPaths.steamRoot;
                                    std::vector<std::string> tarArgs = {"tar", "-czf", backupFile.string(), "-P"};
                                    bool hasFiles = false;

                                    std::vector<AppId_t> targetAppIds = {appId};
                                    for (AppId_t cid : Navi::CloudRedirect::CCloudManager::Instance().GetChildDepotIdsForApp(appId))
                                    {
                                        targetAppIds.push_back(cid);
                                    }

                                    for (AppId_t tId : targetAppIds)
                                    {
                                        std::string tIdStr = std::to_string(tId);
                                        for (const fs::path& storageRoot : {crPaths.crConfigDir / "storage", crPaths.storageDir, steamRoot / "cloud_redirect" / "storage"})
                                        {
                                            if (!storageRoot.empty() && fs::exists(storageRoot, ec))
                                            {
                                                for (const auto& acct : fs::directory_iterator(storageRoot, ec))
                                                {
                                                    fs::path p = acct.path() / tIdStr;
                                                    if (fs::exists(p, ec)) { tarArgs.push_back(p.string()); hasFiles = true; }
                                                }
                                            }
                                        }
                                        fs::path userdataRoot = steamRoot / "userdata";
                                        if (fs::exists(userdataRoot, ec))
                                        {
                                            for (const auto& acct : fs::directory_iterator(userdataRoot, ec))
                                            {
                                                fs::path remoteDir = acct.path() / tIdStr / "remote";
                                                if (fs::exists(remoteDir, ec)) { tarArgs.push_back(remoteDir.string()); hasFiles = true; }
                                                fs::path rcFile = acct.path() / tIdStr / "remotecache.vdf";
                                                if (fs::exists(rcFile, ec)) { tarArgs.push_back(rcFile.string()); hasFiles = true; }
                                            }
                                        }
                                        for (const auto& gseDir : Navi::CloudRedirect::CCloudManager::Instance().ResolveGoldbergSavePaths(tId))
                                        {
                                            if (fs::exists(gseDir, ec)) { tarArgs.push_back(gseDir.string()); hasFiles = true; }
                                        }
                                    }
                                    if (!hasFiles) { tarArgs.push_back("--files-from"); tarArgs.push_back("/dev/null"); }

                                    pid_t pid = fork();
                                    if (pid == 0)
                                    {
                                        std::vector<char*> c_args;
                                        for (const auto& a : tarArgs) c_args.push_back(const_cast<char*>(a.c_str()));
                                        c_args.push_back(nullptr);
                                        execvp(c_args[0], c_args.data());
                                        _exit(127);
                                    }
                                    else if (pid > 0)
                                    {
                                        int status = 0;
                                        waitpid(pid, &status, 0);
                                    }

                                    sendJsonResponse(new_socket, 200, "OK", Navi::CloudView::renderBackupSuccessJson(appId, backupFile.string()));
                                    handled = true;
                                }
                            }
                            else if (method == "POST" && uriPath.rfind("/cloud/apps/", 0) == 0 && uriPath.find("/restore") != std::string::npos)
                            {
                                if (!verifyNaviAuthToken(request))
                                {
                                    sendJsonResponse(new_socket, 401, "Unauthorized", Navi::CloudView::renderErrorJson("Unauthorized: invalid X-Navi-Auth token"));
                                    handled = true;
                                }
                                else
                                {
                                    size_t idStart = 12;
                                    size_t slashEnd = uriPath.find('/', idStart);
                                    AppId_t appId = 0;
                                    try { appId = std::stoul(uriPath.substr(idStart, slashEnd - idStart)); } catch (...) {}
                                    std::string body = extractHttpPostBody(new_socket, request);
                                    std::string backupFile = Navi::CloudView::parseStringField(body, "backupFile");

                                    Navi::CloudRedirect::PendingOperation_t op;
                                    op.appId = appId;
                                    op.type = Navi::CloudRedirect::PendingOpType::RestoreBackup;
                                    op.restoreSourcePath = backupFile;
                                    op.timestamp = static_cast<uint64_t>(std::time(nullptr));
                                    op.id = "op_" + std::to_string(op.timestamp) + "_" + std::to_string(appId) + "_restore";

                                    Navi::CloudRedirect::CPendingOpsQueue::Instance().Enqueue(op);
                                    sendJsonResponse(new_socket, 202, "Accepted", Navi::CloudView::renderPendingOperationAcceptedJson(op.id));
                                    handled = true;
                                }
                            }
                            else if (method == "POST" && uriPath.rfind("/cloud/apps/", 0) == 0 && uriPath.find("/reset") != std::string::npos)
                            {
                                if (!verifyNaviAuthToken(request))
                                {
                                    sendJsonResponse(new_socket, 401, "Unauthorized", Navi::CloudView::renderErrorJson("Unauthorized: invalid X-Navi-Auth token"));
                                    handled = true;
                                }
                                else
                                {
                                    size_t idStart = 12;
                                    size_t slashEnd = uriPath.find('/', idStart);
                                    AppId_t appId = 0;
                                    try { appId = std::stoul(uriPath.substr(idStart, slashEnd - idStart)); } catch (...) {}
                                    std::string body = extractHttpPostBody(new_socket, request);

                                    if (!Navi::CloudView::parseConfirmReset(body))
                                    {
                                        sendJsonResponse(new_socket, 400, "Bad Request", Navi::CloudView::renderErrorJson("Confirmation required: payload must contain {\"confirm\": \"RESET\"}"));
                                        handled = true;
                                    }
                                    else
                                    {
                                        Navi::CloudRedirect::PendingOperation_t op;
                                        op.appId = appId;
                                        op.type = Navi::CloudRedirect::PendingOpType::ResetCloudSave;
                                        op.timestamp = static_cast<uint64_t>(std::time(nullptr));
                                        op.id = "op_" + std::to_string(op.timestamp) + "_" + std::to_string(appId) + "_reset";

                                        Navi::CloudRedirect::CPendingOpsQueue::Instance().Enqueue(op);
                                        sendJsonResponse(new_socket, 202, "Accepted", Navi::CloudView::renderPendingOperationAcceptedJson(op.id));
                                        handled = true;
                                    }
                                }
                            }
                            else if (method == "GET" && (uriPath == "/cloud/pending-ops" || uriPath.rfind("/cloud/pending-ops?", 0) == 0))
                            {
                                auto ops = Navi::CloudRedirect::CPendingOpsQueue::Instance().LoadAll();
                                sendJsonResponse(new_socket, 200, "OK", Navi::CloudView::renderPendingOpsJson(ops));
                                handled = true;
                            }
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
                                    auto acStatus = AutoCrack::inspectOrGetStatus(appId);
                                    std::string autoCrackState = AutoCrack::stateToString(acStatus.state);
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

                                    std::string json = StoreInjectView::renderCheckJson(exists, luaExists, gameExists, isUnlocked, pending, downloading, paused, onlineFixInstalled, autoCrackInstalled, providerName, autoCrackState);
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
                        else if (request.find("/install-crack?id=") != std::string::npos || request.find("/install-crack?") != std::string::npos)
                        {
                            try {
                                size_t idPos = request.find("id=");
                                if (idPos != std::string::npos)
                                {
                                    size_t endPos = request.find_first_of(" &\r\n", idPos);
                                    std::string idStr = request.substr(idPos + 3, (endPos == std::string::npos) ? std::string::npos : (endPos - (idPos + 3)));
                                    uint32_t appId = std::stoul(idStr);
                                    bool force = (request.find("force=1") != std::string::npos || request.find("force=true") != std::string::npos);
                                    LOG_INFO("StoreInject: Received /install-crack for AppID %u (force=%d)\n", appId, force);
                                    
                                    bool queued = AutoCrack::enqueue(appId, AutoCrack::Origin::Manual, force);
                                    std::string respBody = queued ? "{\"queued\":true}" : "{\"queued\":false,\"reason\":\"busy\"}";
                                    int statusCode = queued ? 202 : 409;
                                    std::string response = "HTTP/1.1 " + std::to_string(statusCode) + " " + (queued ? "Accepted" : "Conflict") +
                                                           "\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\nContent-Length: " +
                                                           std::to_string(respBody.size()) + "\r\n\r\n" + respBody;
                                    send(new_socket, response.c_str(), response.size(), 0);
                                    close(new_socket);
                                    handled = true;
                                }
                            } catch (...) {
                                handled = false;
                                close(new_socket);
                            }
                        }
                        else if (request.find("/remove-crack?id=") != std::string::npos || request.find("/remove-crack?") != std::string::npos)
                        {
                            try {
                                size_t idPos = request.find("id=");
                                if (idPos != std::string::npos)
                                {
                                    size_t endPos = request.find_first_of(" &\r\n", idPos);
                                    std::string idStr = request.substr(idPos + 3, (endPos == std::string::npos) ? std::string::npos : (endPos - (idPos + 3)));
                                    uint32_t appId = std::stoul(idStr);
                                    LOG_INFO("StoreInject: Received /remove-crack for AppID %u\n", appId);
                                    
                                    bool queued = AutoCrack::enqueueRestore(appId);
                                    std::string respBody = queued ? "{\"queued\":true}" : "{\"queued\":false,\"reason\":\"busy\"}";
                                    int statusCode = queued ? 202 : 409;
                                    std::string response = "HTTP/1.1 " + std::to_string(statusCode) + " " + (queued ? "Accepted" : "Conflict") +
                                                           "\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\nContent-Length: " +
                                                           std::to_string(respBody.size()) + "\r\n\r\n" + respBody;
                                    send(new_socket, response.c_str(), response.size(), 0);
                                    close(new_socket);
                                    handled = true;
                                }
                            } catch (...) {
                                handled = false;
                                close(new_socket);
                            }
                        }
                        else if (request.find("/crack-status?id=") != std::string::npos || request.find("/crack-status?") != std::string::npos)
                        {
                            try {
                                size_t idPos = request.find("id=");
                                if (idPos != std::string::npos)
                                {
                                    size_t endPos = request.find_first_of(" &\r\n", idPos);
                                    std::string idStr = request.substr(idPos + 3, (endPos == std::string::npos) ? std::string::npos : (endPos - (idPos + 3)));
                                    uint32_t appId = std::stoul(idStr);
                                    auto st = AutoCrack::inspectOrGetStatus(appId);

                                    std::string respBody = "{\"state\":\"" + std::string(AutoCrack::stateToString(st.state)) + "\"," +
                                                           "\"stage\":\"" + st.stage + "\"," +
                                                           "\"message\":\"" + st.message + "\"," +
                                                           "\"updatedAt\":" + std::to_string(st.updatedAt) + "}";
                                    std::string response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\nContent-Length: " +
                                                           std::to_string(respBody.size()) + "\r\n\r\n" + respBody;
                                    send(new_socket, response.c_str(), response.size(), 0);
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
                                "if [ -x ~/.local/share/navi/bin/navi-cloud-apply-pending ]; then ~/.local/share/navi/bin/navi-cloud-apply-pending --process-all 2>/dev/null; fi; "
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
    }
        int fd = g_serverFd.exchange(-1);
        if (fd >= 0) close(fd);
    }
    // patchIndexHtml removed.

    void init()
    {
        g_shutdownCalled = false;
        g_shouldStop = false;
        getNaviAuthToken();

        int prev_wfd = g_wakeFd.exchange(eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC));
        if (prev_wfd >= 0) ::close(prev_wfd);

        // Start the callback server immediately as it's just a passive listener
        g_workerThread = std::thread(callbackServer);

        // Automation worker delays internally and is joinable
        g_autoThread = std::thread(automationWorker);
    }

    void shutdown()
    {
        if (g_shutdownCalled.exchange(true)) return;
        g_shouldStop = true;
        int wfd_signal = g_wakeFd.load();
        if (wfd_signal >= 0)
        {
            uint64_t val = 1;
            (void)write(wfd_signal, &val, sizeof(val));
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
        if (g_autoThread.joinable())
        {
            g_autoThread.join();
        }
        int wfd = g_wakeFd.exchange(-1);
        if (wfd >= 0)
        {
            ::close(wfd);
        }
    }
}
