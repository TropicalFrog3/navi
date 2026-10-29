#include "cloud_provider.hpp"

#include <base64/base64.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <regex>
#include <sstream>
#include <thread>

#include <openssl/evp.h>
#include <openssl/rand.h>

namespace Sync
{
    // Minimal JSON extractors
    static std::string jsonGetString(const std::string& json, const std::string& key)
    {
        std::string needle = "\"" + key + "\"";
        auto pos = json.find(needle);
        if (pos == std::string::npos) return {};

        pos = json.find(':', pos + needle.size());
        if (pos == std::string::npos) return {};

        pos = json.find('"', pos + 1);
        if (pos == std::string::npos) return {};
        pos++;

        size_t end = pos;
        while (end < json.size())
        {
            if (json[end] == '\\')
            {
                end += 2;
                continue;
            }
            if (json[end] == '"')
            {
                break;
            }
            end++;
        }
        if (end >= json.size()) return {};

        return json.substr(pos, end - pos);
    }

    static int jsonGetInt(const std::string& json, const std::string& key)
    {
        std::string needle = "\"" + key + "\"";
        auto pos = json.find(needle);
        if (pos == std::string::npos) return -1;

        pos = json.find(':', pos + needle.size());
        if (pos == std::string::npos) return -1;

        while (pos < json.size() && (std::isspace(static_cast<unsigned char>(json[pos])) || json[pos] == ':' || json[pos] == '"'))
        {
            pos++;
        }

        size_t end = pos;
        while (end < json.size() && (std::isdigit(static_cast<unsigned char>(json[end])) || json[end] == '-'))
        {
            end++;
        }

        if (end > pos)
        {
            try { return std::stoi(json.substr(pos, end - pos)); } catch (...) {}
        }
        return -1;
    }

    static std::string urlEncode(const std::string& s)
    {
        std::ostringstream escaped;
        escaped.fill('0');
        escaped << std::hex;
        for (char c : s)
        {
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~')
            {
                escaped << c;
            }
            else
            {
                escaped << '%' << std::setw(2) << std::uppercase << static_cast<int>(static_cast<unsigned char>(c));
            }
        }
        return escaped.str();
    }

    static int64_t currentUnixTime()
    {
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    static std::string toBase64Url(std::string b64)
    {
        for (char& c : b64)
        {
            if (c == '+') c = '-';
            else if (c == '/') c = '_';
        }
        while (!b64.empty() && b64.back() == '=')
        {
            b64.pop_back();
        }
        return b64;
    }

    PkcePair_t generatePkcePair()
    {
        PkcePair_t pair;
        std::vector<uint8_t> randBytes(32);
        if (RAND_bytes(randBytes.data(), static_cast<int>(randBytes.size())) != 1)
        {
            for (size_t i = 0; i < randBytes.size(); ++i)
            {
                randBytes[i] = static_cast<uint8_t>(rand() & 0xFF);
            }
        }

        std::string rawB64 = base64::to_base64(std::string_view(reinterpret_cast<const char*>(randBytes.data()), randBytes.size()));
        pair.codeVerifier = toBase64Url(rawB64);

        unsigned char hash[EVP_MAX_MD_SIZE];
        unsigned int hashLen = 0;
        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        if (ctx)
        {
            if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) == 1 &&
                EVP_DigestUpdate(ctx, pair.codeVerifier.data(), pair.codeVerifier.size()) == 1 &&
                EVP_DigestFinal_ex(ctx, hash, &hashLen) == 1)
            {
                std::string hashB64 = base64::to_base64(std::string_view(reinterpret_cast<const char*>(hash), hashLen));
                pair.codeChallenge = toBase64Url(hashB64);
            }
            EVP_MD_CTX_free(ctx);
        }

        return pair;
    }

    std::string generateOAuthStateHex(size_t byteCount)
    {
        if (byteCount == 0) byteCount = 32;
        std::vector<uint8_t> randBytes(byteCount);
        if (RAND_bytes(randBytes.data(), static_cast<int>(randBytes.size())) != 1)
        {
            for (size_t i = 0; i < randBytes.size(); ++i)
            {
                randBytes[i] = static_cast<uint8_t>(rand() & 0xFF);
            }
        }

        std::ostringstream ss;
        ss << std::hex << std::setfill('0');
        for (uint8_t b : randBytes)
        {
            ss << std::setw(2) << static_cast<int>(b);
        }
        return ss.str();
    }

    std::string resolveGDriveClientId(const std::string& customClientId)
    {
        auto isBlank = [](const std::string& s) {
            for (char c : s)
            {
                if (!std::isspace(static_cast<unsigned char>(c)))
                {
                    return false;
                }
            }
            return true;
        };

        if (!customClientId.empty() && !isBlank(customClientId))
        {
            return customClientId;
        }

        const char* envId = std::getenv("NAVI_GDRIVE_CLIENT_ID");
        if (envId && *envId && !isBlank(envId))
        {
            return std::string(envId);
        }

        return k_DefaultGDriveClientId;
    }

    std::string resolveGDriveClientSecret(const std::string& customClientSecret, const std::string& clientId)
    {
        auto isBlank = [](const std::string& s) {
            for (char c : s)
            {
                if (!std::isspace(static_cast<unsigned char>(c)))
                {
                    return false;
                }
            }
            return true;
        };

        if (!customClientSecret.empty() && !isBlank(customClientSecret))
        {
            return customClientSecret;
        }

        const char* envSec = std::getenv("NAVI_GDRIVE_CLIENT_SECRET");
        if (envSec && *envSec && !isBlank(envSec))
        {
            return std::string(envSec);
        }

        std::string resolvedId = resolveGDriveClientId(clientId);
        if (resolvedId == k_DefaultGDriveClientId)
        {
            return k_DefaultGDriveClientSecret;
        }

        return "";
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // GoogleDriveAppDataProvider
    // ─────────────────────────────────────────────────────────────────────────────

    GoogleDriveAppDataProvider::GoogleDriveAppDataProvider(CloudProviderConfig_t cfg)
        : m_cfg(std::move(cfg))
    {
        if (m_cfg.oauthClientId.empty())
        {
            m_cfg.oauthClientId = resolveGDriveClientId("");
        }
        if (m_cfg.oauthClientSecret.empty())
        {
            m_cfg.oauthClientSecret = resolveGDriveClientSecret("", m_cfg.oauthClientId);
        }
    }

    ECloudError GoogleDriveAppDataProvider::refreshAuthTokenIfNeeded()
    {
        if (m_cfg.refreshToken.empty())
        {
            return ECloudError::k_AuthExpired;
        }

        int64_t now = currentUnixTime();
        if (!m_cfg.accessToken.empty() && m_cfg.tokenExpiresAtUnixSec > (now + 60))
        {
            return ECloudError::k_Ok;
        }

        Curl::HttpRequest_t req;
        req.method = "POST";
        req.url = m_cfg.oauthTokenEndpoint;
        req.headers.push_back({"Content-Type", "application/x-www-form-urlencoded"});

        const std::string clientId = m_cfg.oauthClientId.empty() ? resolveGDriveClientId("") : m_cfg.oauthClientId;
        const std::string clientSecret = m_cfg.oauthClientSecret.empty() ? resolveGDriveClientSecret("", clientId) : m_cfg.oauthClientSecret;
        std::string form = "client_id=" + urlEncode(clientId);
        if (!clientSecret.empty())
        {
            form += "&client_secret=" + urlEncode(clientSecret);
        }
        form += "&refresh_token=" + urlEncode(m_cfg.refreshToken) +
                "&grant_type=refresh_token";
        req.body.assign(form.begin(), form.end());

        Curl::HttpResponse_t resp;
        if (!Curl::request(req, resp))
        {
            return ECloudError::k_NetworkError;
        }

        if (resp.statusCode == 400 || resp.statusCode == 401)
        {
            return ECloudError::k_AuthExpired;
        }

        if (resp.statusCode != 200)
        {
            return ECloudError::k_NetworkError;
        }

        std::string bodyStr(resp.body.begin(), resp.body.end());
        std::string access = jsonGetString(bodyStr, "access_token");
        if (access.empty())
        {
            return ECloudError::k_AuthExpired;
        }

        m_cfg.accessToken = access;
        int exp = jsonGetInt(bodyStr, "expires_in");
        m_cfg.tokenExpiresAtUnixSec = now + (exp > 0 ? exp : 3600);

        std::string newRefresh = jsonGetString(bodyStr, "refresh_token");
        if (!newRefresh.empty())
        {
            m_cfg.refreshToken = newRefresh;
        }

        return ECloudError::k_Ok;
    }

    bool GoogleDriveAppDataProvider::executeWithAuthAndRetry(Curl::HttpRequest_t req,
                                                            Curl::HttpResponse_t& outResp,
                                                            ECloudError& outErr)
    {
        refreshAuthTokenIfNeeded();

        bool refreshed = false;
        int maxRetries = std::max(0, m_cfg.maxRetries);

        for (int attempt = 0; attempt <= maxRetries; ++attempt)
        {
            // Update Authorization header
            bool hasAuth = false;
            for (auto& [k, v] : req.headers)
            {
                if (k == "Authorization")
                {
                    v = "Bearer " + m_cfg.accessToken;
                    hasAuth = true;
                    break;
                }
            }
            if (!hasAuth && !m_cfg.accessToken.empty())
            {
                req.headers.push_back({"Authorization", "Bearer " + m_cfg.accessToken});
            }

            if (!Curl::request(req, outResp))
            {
                if (attempt < maxRetries)
                {
                    if (m_cfg.baseRetryDelayMs > 0)
                        std::this_thread::sleep_for(std::chrono::milliseconds(m_cfg.baseRetryDelayMs * (1 << attempt)));
                    continue;
                }
                outErr = ECloudError::k_NetworkError;
                return false;
            }

            if (outResp.statusCode == 401)
            {
                if (!refreshed)
                {
                    refreshed = true;
                    m_cfg.tokenExpiresAtUnixSec = 0; // force refresh
                    if (refreshAuthTokenIfNeeded() == ECloudError::k_Ok)
                    {
                        // Retry with new token
                        continue;
                    }
                }
                outErr = ECloudError::k_AuthExpired;
                return false;
            }

            if (outResp.statusCode == 429)
            {
                int delayMs = m_cfg.baseRetryDelayMs * (1 << attempt);
                auto it = outResp.headers.find("retry-after");
                if (it != outResp.headers.end())
                {
                    try {
                        int sec = std::stoi(it->second);
                        if (sec > 0) delayMs = std::max(delayMs, sec * 1000);
                    } catch (...) {}
                }

                if (attempt < maxRetries)
                {
                    if (delayMs > 0)
                        std::this_thread::sleep_for(std::chrono::milliseconds(std::min(delayMs, 5000)));
                    continue;
                }
                outErr = ECloudError::k_RateLimited;
                return false;
            }

            if (outResp.statusCode >= 500 && outResp.statusCode <= 504 && outResp.statusCode != 507)
            {
                if (attempt < maxRetries)
                {
                    if (m_cfg.baseRetryDelayMs > 0)
                        std::this_thread::sleep_for(std::chrono::milliseconds(m_cfg.baseRetryDelayMs * (1 << attempt)));
                    continue;
                }
                outErr = ECloudError::k_NetworkError;
                return false;
            }

            if (outResp.statusCode == 403)
            {
                std::string bodyStr(outResp.body.begin(), outResp.body.end());
                if (bodyStr.find("storageQuotaExceeded") != std::string::npos ||
                    bodyStr.find("quotaExceeded") != std::string::npos)
                {
                    outErr = ECloudError::k_QuotaExceeded;
                    return false;
                }
                if (bodyStr.find("insufficientScopes") != std::string::npos ||
                    bodyStr.find("insufficientPermissions") != std::string::npos)
                {
                    outErr = ECloudError::k_AuthExpired;
                    return false;
                }
                outErr = ECloudError::k_NetworkError;
                return false;
            }

            if (outResp.statusCode == 507)
            {
                outErr = ECloudError::k_QuotaExceeded;
                return false;
            }

            if (outResp.statusCode == 412)
            {
                outErr = ECloudError::k_PreconditionFailed;
                return false;
            }

            if (outResp.statusCode == 404)
            {
                outErr = ECloudError::k_NotFound;
                return false;
            }

            if (outResp.statusCode >= 200 && outResp.statusCode < 300)
            {
                outErr = ECloudError::k_Ok;
                return true;
            }

            outErr = ECloudError::k_NetworkError;
            return false;
        }

        outErr = ECloudError::k_NetworkError;
        return false;
    }

    ECloudError GoogleDriveAppDataProvider::getOrCreateNaviFolderId(std::string& outFolderId)
    {
        if (!m_naviFolderId.empty())
        {
            outFolderId = m_naviFolderId;
            return ECloudError::k_Ok;
        }

        // Search for existing folder named "Navi" in Google Drive
        Curl::HttpRequest_t req;
        req.method = "GET";
        req.url = m_cfg.gdriveApiBaseUrl +
                  "/files?spaces=drive&q=mimeType+%3D+%27application%2Fvnd.google-apps.folder%27+and+name+%3D+%27Navi%27+and+trashed+%3D+false&fields=files(id%2Cname)&pageSize=1";

        Curl::HttpResponse_t resp;
        ECloudError err = ECloudError::k_Ok;
        if (!executeWithAuthAndRetry(req, resp, err))
        {
            return err;
        }

        std::string bodyStr(resp.body.begin(), resp.body.end());
        auto filesPos = bodyStr.find("\"files\"");
        if (filesPos != std::string::npos)
        {
            std::string sub = bodyStr.substr(filesPos);
            std::string id = jsonGetString(sub, "id");
            if (!id.empty())
            {
                m_naviFolderId = id;
                outFolderId = id;
                return ECloudError::k_Ok;
            }
        }

        // Folder not found - create /Navi folder with parent root
        Curl::HttpRequest_t createReq;
        createReq.method = "POST";
        createReq.url = m_cfg.gdriveApiBaseUrl + "/files";
        createReq.headers.push_back({"Content-Type", "application/json; charset=UTF-8"});
        std::string createJson = "{\"name\":\"Navi\",\"mimeType\":\"application/vnd.google-apps.folder\",\"parents\":[\"root\"]}";
        createReq.body.assign(createJson.begin(), createJson.end());

        Curl::HttpResponse_t createResp;
        if (!executeWithAuthAndRetry(createReq, createResp, err))
        {
            return err;
        }

        std::string createStr(createResp.body.begin(), createResp.body.end());
        std::string newId = jsonGetString(createStr, "id");
        if (newId.empty())
        {
            return ECloudError::k_NetworkError;
        }

        m_naviFolderId = newId;
        outFolderId = newId;
        return ECloudError::k_Ok;
    }

    ECloudError GoogleDriveAppDataProvider::getOrCreateFolderPath(const std::string& folderPath, std::string& outFolderId)
    {
        if (folderPath.empty() || folderPath == "." || folderPath == "/")
        {
            return getOrCreateNaviFolderId(outFolderId);
        }

        auto it = m_folderIdCache.find(folderPath);
        if (it != m_folderIdCache.end() && !it->second.empty())
        {
            outFolderId = it->second;
            return ECloudError::k_Ok;
        }

        std::string currentParentId;
        ECloudError err = getOrCreateNaviFolderId(currentParentId);
        if (err != ECloudError::k_Ok) return err;

        std::stringstream ss(folderPath);
        std::string segment;
        std::string accumulatedPath;

        while (std::getline(ss, segment, '/'))
        {
            if (segment.empty() || segment == ".") continue;

            if (!accumulatedPath.empty()) accumulatedPath += "/";
            accumulatedPath += segment;

            auto cacheIt = m_folderIdCache.find(accumulatedPath);
            if (cacheIt != m_folderIdCache.end() && !cacheIt->second.empty())
            {
                currentParentId = cacheIt->second;
                continue;
            }

            // Query for folder named 'segment' inside 'currentParentId'
            Curl::HttpRequest_t req;
            req.method = "GET";
            req.url = m_cfg.gdriveApiBaseUrl +
                      "/files?spaces=drive&q=mimeType+%3D+%27application%2Fvnd.google-apps.folder%27+and+name+%3D+%27" +
                      urlEncode(segment) + "%27+and+trashed+%3D+false+and+%27" + currentParentId +
                      "%27+in+parents&fields=files(id%2Cname)&pageSize=1";

            Curl::HttpResponse_t resp;
            if (!executeWithAuthAndRetry(req, resp, err)) return err;

            std::string bodyStr(resp.body.begin(), resp.body.end());
            std::string segmentId;
            auto filesPos = bodyStr.find("\"files\"");
            if (filesPos != std::string::npos)
            {
                std::string sub = bodyStr.substr(filesPos);
                segmentId = jsonGetString(sub, "id");
            }

            if (segmentId.empty())
            {
                // Create folder
                Curl::HttpRequest_t createReq;
                createReq.method = "POST";
                createReq.url = m_cfg.gdriveApiBaseUrl + "/files";
                createReq.headers.push_back({"Content-Type", "application/json; charset=UTF-8"});
                std::string createJson = "{\"name\":\"" + segment + "\",\"mimeType\":\"application/vnd.google-apps.folder\",\"parents\":[\"" + currentParentId + "\"]}";
                createReq.body.assign(createJson.begin(), createJson.end());

                Curl::HttpResponse_t createResp;
                if (!executeWithAuthAndRetry(createReq, createResp, err)) return err;

                std::string createStr(createResp.body.begin(), createResp.body.end());
                segmentId = jsonGetString(createStr, "id");
                if (segmentId.empty()) return ECloudError::k_NetworkError;
            }

            m_folderIdCache[accumulatedPath] = segmentId;
            currentParentId = segmentId;
        }

        outFolderId = currentParentId;
        return ECloudError::k_Ok;
    }

    ECloudError GoogleDriveAppDataProvider::findFileIdByName(const std::string& name,
                                                            std::string& outFileId,
                                                            std::string& outETag)
    {
        std::filesystem::path p(name);
        std::string folderPath = p.has_parent_path() ? p.parent_path().string() : "";
        std::string fileName = p.filename().string();

        std::string targetFolderId;
        ECloudError fErr = getOrCreateFolderPath(folderPath, targetFolderId);
        if (fErr != ECloudError::k_Ok)
        {
            return fErr;
        }

        // Search within the target folder
        Curl::HttpRequest_t req;
        req.method = "GET";
        req.url = m_cfg.gdriveApiBaseUrl + "/files?spaces=drive&q=%27" + targetFolderId +
                  "%27+in+parents+and+name+%3D+%27" + urlEncode(fileName) +
                  "%27+and+trashed+%3D+false&fields=files(id%2Cname%2CheadRevisionId%2Cversion)&pageSize=10";

        Curl::HttpResponse_t resp;
        ECloudError err = ECloudError::k_Ok;
        if (!executeWithAuthAndRetry(req, resp, err))
        {
            return err;
        }

        std::string bodyStr(resp.body.begin(), resp.body.end());
        auto filesPos = bodyStr.find("\"files\"");
        std::string id;
        std::string headRev;
        if (filesPos != std::string::npos)
        {
            std::string sub = bodyStr.substr(filesPos);
            id = jsonGetString(sub, "id");
            headRev = jsonGetString(sub, "headRevisionId");
            if (headRev.empty()) headRev = jsonGetString(sub, "version");
        }

        // Seamless migration: If not found in target folder, check if it exists in /Navi/ root or drive root
        if (id.empty())
        {
            std::string naviRootId;
            if (getOrCreateNaviFolderId(naviRootId) == ECloudError::k_Ok && naviRootId != targetFolderId)
            {
                Curl::HttpRequest_t naviReq;
                naviReq.method = "GET";
                naviReq.url = m_cfg.gdriveApiBaseUrl + "/files?spaces=drive&q=%27" + naviRootId +
                              "%27+in+parents+and+name+%3D+%27" + urlEncode(fileName) +
                              "%27+and+trashed+%3D+false&fields=files(id%2Cname%2CheadRevisionId%2Cversion)&pageSize=1";
                Curl::HttpResponse_t naviResp;
                ECloudError naviErr = ECloudError::k_Ok;
                if (executeWithAuthAndRetry(naviReq, naviResp, naviErr))
                {
                    std::string naviBody(naviResp.body.begin(), naviResp.body.end());
                    auto nPos = naviBody.find("\"files\"");
                    if (nPos != std::string::npos)
                    {
                        std::string nSub = naviBody.substr(nPos);
                        std::string legacyId = jsonGetString(nSub, "id");
                        if (!legacyId.empty())
                        {
                            // Move from /Navi/ root into target subfolder
                            Curl::HttpRequest_t moveReq;
                            moveReq.method = "PATCH";
                            moveReq.url = m_cfg.gdriveApiBaseUrl + "/files/" + legacyId +
                                          "?addParents=" + targetFolderId + "&removeParents=" + naviRootId +
                                          "&fields=id%2CheadRevisionId%2Cversion";
                            Curl::HttpResponse_t moveResp;
                            ECloudError moveErr = ECloudError::k_Ok;
                            if (executeWithAuthAndRetry(moveReq, moveResp, moveErr))
                            {
                                id = legacyId;
                                headRev = jsonGetString(nSub, "headRevisionId");
                                if (headRev.empty()) headRev = jsonGetString(nSub, "version");
                            }
                        }
                    }
                }
            }

            if (id.empty())
            {
                Curl::HttpRequest_t rootReq;
                rootReq.method = "GET";
                rootReq.url = m_cfg.gdriveApiBaseUrl + "/files?spaces=drive&q=%27root%27+in+parents+and+name+%3D+%27" +
                              urlEncode(fileName) + "%27+and+trashed+%3D+false&fields=files(id%2Cname%2CheadRevisionId%2Cversion)&pageSize=1";
                Curl::HttpResponse_t rootResp;
                ECloudError rootErr = ECloudError::k_Ok;
                if (executeWithAuthAndRetry(rootReq, rootResp, rootErr))
                {
                    std::string rootBody(rootResp.body.begin(), rootResp.body.end());
                    auto rPos = rootBody.find("\"files\"");
                    if (rPos != std::string::npos)
                    {
                        std::string rSub = rootBody.substr(rPos);
                        std::string rootId = jsonGetString(rSub, "id");
                        if (!rootId.empty())
                        {
                            Curl::HttpRequest_t moveReq;
                            moveReq.method = "PATCH";
                            moveReq.url = m_cfg.gdriveApiBaseUrl + "/files/" + rootId +
                                          "?addParents=" + targetFolderId + "&removeParents=root&fields=id%2CheadRevisionId%2Cversion";
                            Curl::HttpResponse_t moveResp;
                            ECloudError moveErr = ECloudError::k_Ok;
                            if (executeWithAuthAndRetry(moveReq, moveResp, moveErr))
                            {
                                id = rootId;
                                headRev = jsonGetString(rSub, "headRevisionId");
                                if (headRev.empty()) headRev = jsonGetString(rSub, "version");
                            }
                        }
                    }
                }
            }
        }

        if (id.empty())
        {
            return ECloudError::k_NotFound;
        }

        outFileId = id;
        if (headRev.empty()) headRev = id;
        outETag = headRev;

        return ECloudError::k_Ok;
    }

    ECloudError GoogleDriveAppDataProvider::queryQuota(CloudQuotaInfo_t& outQuota)
    {
        Curl::HttpRequest_t req;
        req.method = "GET";
        req.url = m_cfg.gdriveApiBaseUrl + "/about?fields=storageQuota";

        Curl::HttpResponse_t resp;
        ECloudError err = ECloudError::k_Ok;
        if (!executeWithAuthAndRetry(req, resp, err))
        {
            return err;
        }

        std::string bodyStr(resp.body.begin(), resp.body.end());
        auto qPos = bodyStr.find("\"storageQuota\"");
        if (qPos != std::string::npos)
        {
            std::string sub = bodyStr.substr(qPos);
            std::string usageStr = jsonGetString(sub, "usage");
            std::string limitStr = jsonGetString(sub, "limit");
            if (!usageStr.empty())
            {
                try { outQuota.usedBytes = std::stoull(usageStr); } catch (...) {}
            }
            if (!limitStr.empty())
            {
                try { outQuota.limitBytes = std::stoull(limitStr); } catch (...) {}
            }
        }

        return ECloudError::k_Ok;
    }

    ECloudError GoogleDriveAppDataProvider::startDeviceAuthFlow(DeviceCodePrompt_t& outPrompt)
    {
        Curl::HttpRequest_t req;
        req.method = "POST";
        req.url = m_cfg.oauthDeviceAuthEndpoint;
        req.headers.push_back({"Content-Type", "application/x-www-form-urlencoded"});

        std::string form = "client_id=" + urlEncode(m_cfg.oauthClientId) +
                           "&scope=" + urlEncode("https://www.googleapis.com/auth/drive.file");
        req.body.assign(form.begin(), form.end());

        Curl::HttpResponse_t resp;
        if (!Curl::request(req, resp) || resp.statusCode != 200)
        {
            return ECloudError::k_NetworkError;
        }

        std::string bodyStr(resp.body.begin(), resp.body.end());
        outPrompt.deviceCode = jsonGetString(bodyStr, "device_code");
        outPrompt.userCode = jsonGetString(bodyStr, "user_code");
        outPrompt.verificationUrl = jsonGetString(bodyStr, "verification_url");
        if (outPrompt.verificationUrl.empty())
        {
            outPrompt.verificationUrl = jsonGetString(bodyStr, "verification_uri");
        }

        int exp = jsonGetInt(bodyStr, "expires_in");
        if (exp > 0) outPrompt.expiresInSec = exp;

        int iv = jsonGetInt(bodyStr, "interval");
        if (iv > 0) outPrompt.intervalSec = iv;

        return ECloudError::k_Ok;
    }

    ECloudError GoogleDriveAppDataProvider::pollDeviceAuthToken(const std::string& deviceCode)
    {
        Curl::HttpRequest_t req;
        req.method = "POST";
        req.url = m_cfg.oauthTokenEndpoint;
        req.headers.push_back({"Content-Type", "application/x-www-form-urlencoded"});

        const std::string clientId = m_cfg.oauthClientId.empty() ? resolveGDriveClientId("") : m_cfg.oauthClientId;
        const std::string clientSecret = m_cfg.oauthClientSecret.empty() ? resolveGDriveClientSecret("", clientId) : m_cfg.oauthClientSecret;
        std::string form = "client_id=" + urlEncode(clientId);
        if (!clientSecret.empty())
        {
            form += "&client_secret=" + urlEncode(clientSecret);
        }
        form += "&device_code=" + urlEncode(deviceCode) +
                "&grant_type=urn:ietf:params:oauth:grant-type:device_code";
        req.body.assign(form.begin(), form.end());

        Curl::HttpResponse_t resp;
        if (!Curl::request(req, resp))
        {
            return ECloudError::k_NetworkError;
        }

        std::string bodyStr(resp.body.begin(), resp.body.end());
        if (resp.statusCode == 200)
        {
            std::string access = jsonGetString(bodyStr, "access_token");
            if (access.empty()) return ECloudError::k_AuthExpired;

            m_cfg.accessToken = access;
            std::string refresh = jsonGetString(bodyStr, "refresh_token");
            if (!refresh.empty()) m_cfg.refreshToken = refresh;

            int exp = jsonGetInt(bodyStr, "expires_in");
            m_cfg.tokenExpiresAtUnixSec = currentUnixTime() + (exp > 0 ? exp : 3600);
            return ECloudError::k_Ok;
        }

        std::string err = jsonGetString(bodyStr, "error");
        if (err == "authorization_pending" || err == "slow_down")
        {
            return ECloudError::k_RateLimited;
        }
        if (err == "expired_token" || err == "access_denied")
        {
            return ECloudError::k_AuthExpired;
        }

        return ECloudError::k_NetworkError;
    }

    std::string GoogleDriveAppDataProvider::buildBrowserAuthUrl(const std::string& redirectUri,
                                                                const std::string& state,
                                                                const std::string& codeChallenge) const
    {
        const std::string clientId = m_cfg.oauthClientId.empty() ? resolveGDriveClientId("") : m_cfg.oauthClientId;
        std::ostringstream ss;
        ss << m_cfg.oauthAuthEndpoint
           << "?client_id=" << urlEncode(clientId)
           << "&redirect_uri=" << urlEncode(redirectUri)
           << "&response_type=code"
           << "&scope=" << urlEncode("https://www.googleapis.com/auth/drive.file")
           << "&access_type=offline"
           << "&prompt=select_account+consent"
           << "&state=" << urlEncode(state)
           << "&code_challenge=" << urlEncode(codeChallenge)
           << "&code_challenge_method=S256";
        return ss.str();
    }

    ECloudError GoogleDriveAppDataProvider::exchangeAuthCodeForToken(const std::string& authCode,
                                                                     const std::string& codeVerifier,
                                                                     const std::string& redirectUri)
    {
        Curl::HttpRequest_t req;
        req.method = "POST";
        req.url = m_cfg.oauthTokenEndpoint;
        req.headers.push_back({"Content-Type", "application/x-www-form-urlencoded"});

        const std::string clientId = m_cfg.oauthClientId.empty() ? resolveGDriveClientId("") : m_cfg.oauthClientId;
        const std::string clientSecret = m_cfg.oauthClientSecret.empty() ? resolveGDriveClientSecret("", clientId) : m_cfg.oauthClientSecret;
        std::string form = "client_id=" + urlEncode(clientId);
        if (!clientSecret.empty())
        {
            form += "&client_secret=" + urlEncode(clientSecret);
        }
        form += "&code=" + urlEncode(authCode) +
                "&code_verifier=" + urlEncode(codeVerifier) +
                "&grant_type=authorization_code" +
                "&redirect_uri=" + urlEncode(redirectUri);

        req.body.assign(form.begin(), form.end());

        Curl::HttpResponse_t resp;
        if (!Curl::request(req, resp))
        {
            return ECloudError::k_NetworkError;
        }

        std::string bodyStr(resp.body.begin(), resp.body.end());
        if (resp.statusCode == 200)
        {
            std::string access = jsonGetString(bodyStr, "access_token");
            if (access.empty())
            {
                return ECloudError::k_AuthExpired;
            }

            m_cfg.accessToken = access;
            std::string refresh = jsonGetString(bodyStr, "refresh_token");
            if (!refresh.empty())
            {
                m_cfg.refreshToken = refresh;
            }

            int exp = jsonGetInt(bodyStr, "expires_in");
            m_cfg.tokenExpiresAtUnixSec = currentUnixTime() + (exp > 0 ? exp : 3600);
            return ECloudError::k_Ok;
        }

        if (resp.statusCode == 400 || resp.statusCode == 401)
        {
            return ECloudError::k_AuthExpired;
        }

        return ECloudError::k_NetworkError;
    }

    ECloudError GoogleDriveAppDataProvider::pushEncryptedObject(uint64_t /*steamId64*/,
                                                               const std::string& objectKey,
                                                               const std::vector<uint8_t>& blob)
    {
        CloudQuotaInfo_t quota;
        if (queryQuota(quota) == ECloudError::k_Ok && !quota.hasSpaceFor(blob.size()))
        {
            return ECloudError::k_QuotaExceeded;
        }

        std::filesystem::path p(objectKey);
        std::string folderPath = p.has_parent_path() ? p.parent_path().string() : "";
        std::string fileName = p.filename().string();

        std::string fileId, oldETag;
        ECloudError fErr = findFileIdByName(objectKey, fileId, oldETag);

        if (fErr == ECloudError::k_Ok)
        {
            Curl::HttpRequest_t req;
            req.method = "PATCH";
            req.url = m_cfg.gdriveUploadBaseUrl + "/files/" + fileId + "?uploadType=media";
            req.headers.push_back({"Content-Type", "application/octet-stream"});
            req.body = blob;

            Curl::HttpResponse_t resp;
            ECloudError err = ECloudError::k_Ok;
            if (!executeWithAuthAndRetry(req, resp, err)) return err;
            return ECloudError::k_Ok;
        }
        else if (fErr == ECloudError::k_NotFound)
        {
            std::string folderId;
            ECloudError folderErr = getOrCreateFolderPath(folderPath, folderId);
            if (folderErr != ECloudError::k_Ok) return folderErr;

            Curl::HttpRequest_t createReq;
            createReq.method = "POST";
            createReq.url = m_cfg.gdriveApiBaseUrl + "/files";
            createReq.headers.push_back({"Content-Type", "application/json; charset=UTF-8"});
            std::string metaJson = "{\"name\":\"" + fileName + "\",\"parents\":[\"" + folderId + "\"]}";
            createReq.body.assign(metaJson.begin(), metaJson.end());

            Curl::HttpResponse_t createResp;
            ECloudError cErr = ECloudError::k_Ok;
            if (!executeWithAuthAndRetry(createReq, createResp, cErr)) return cErr;

            std::string createStr(createResp.body.begin(), createResp.body.end());
            std::string newId = jsonGetString(createStr, "id");
            if (newId.empty()) return ECloudError::k_NetworkError;

            Curl::HttpRequest_t upReq;
            upReq.method = "PATCH";
            upReq.url = m_cfg.gdriveUploadBaseUrl + "/files/" + newId + "?uploadType=media";
            upReq.headers.push_back({"Content-Type", "application/octet-stream"});
            upReq.body = blob;

            Curl::HttpResponse_t upResp;
            ECloudError upErr = ECloudError::k_Ok;
            if (!executeWithAuthAndRetry(upReq, upResp, upErr)) return upErr;
            return ECloudError::k_Ok;
        }

        return fErr;
    }

    ECloudError GoogleDriveAppDataProvider::pullEncryptedObject(uint64_t /*steamId64*/,
                                                               const std::string& objectKey,
                                                               std::vector<uint8_t>& outBlob)
    {
        std::string fileName = objectKey;
        std::string fileId, etag;
        ECloudError fErr = findFileIdByName(fileName, fileId, etag);
        if (fErr != ECloudError::k_Ok) return fErr;

        Curl::HttpRequest_t req;
        req.method = "GET";
        req.url = m_cfg.gdriveApiBaseUrl + "/files/" + fileId + "?alt=media";

        Curl::HttpResponse_t resp;
        ECloudError err = ECloudError::k_Ok;
        if (!executeWithAuthAndRetry(req, resp, err)) return err;

        outBlob = std::move(resp.body);
        return ECloudError::k_Ok;
    }

    ECloudError GoogleDriveAppDataProvider::deleteRemoteObject(uint64_t /*steamId64*/,
                                                              const std::string& objectKey)
    {
        std::string fileName = objectKey;
        std::string fileId, etag;
        ECloudError fErr = findFileIdByName(fileName, fileId, etag);
        if (fErr == ECloudError::k_NotFound) return ECloudError::k_Ok;
        if (fErr != ECloudError::k_Ok) return fErr;

        Curl::HttpRequest_t req;
        req.method = "DELETE";
        req.url = m_cfg.gdriveApiBaseUrl + "/files/" + fileId;

        Curl::HttpResponse_t resp;
        ECloudError err = ECloudError::k_Ok;
        if (!executeWithAuthAndRetry(req, resp, err)) return err;
        return ECloudError::k_Ok;
    }

    ECloudError GoogleDriveAppDataProvider::commitRemoteManifest(uint64_t /*steamId64*/,
                                                                const SyncStateManifest_t& manifest,
                                                                const std::string& expectedETag,
                                                                std::string& outNewETag)
    {
        std::string yaml = serializeManifestToYaml(manifest);
        std::vector<uint8_t> blob(yaml.begin(), yaml.end());

        std::string fileName = "navi_sync_manifest.yaml";
        std::string fileId, currentETag;
        ECloudError fErr = findFileIdByName(fileName, fileId, currentETag);

        if (!expectedETag.empty())
        {
            if (fErr == ECloudError::k_NotFound)
            {
                return ECloudError::k_PreconditionFailed;
            }
            if (fErr == ECloudError::k_Ok && currentETag != expectedETag)
            {
                return ECloudError::k_PreconditionFailed;
            }
        }

        if (fErr == ECloudError::k_Ok)
        {
            Curl::HttpRequest_t req;
            req.method = "PATCH";
            req.url = m_cfg.gdriveUploadBaseUrl + "/files/" + fileId + "?uploadType=media";
            req.headers.push_back({"Content-Type", "text/yaml; charset=UTF-8"});
            if (!expectedETag.empty())
            {
                req.headers.push_back({"If-Match", expectedETag});
            }
            req.body = blob;

            Curl::HttpResponse_t resp;
            ECloudError err = ECloudError::k_Ok;
            if (!executeWithAuthAndRetry(req, resp, err)) return err;

            auto it = resp.headers.find("etag");
            if (it != resp.headers.end()) outNewETag = it->second;
            else
            {
                std::string bodyStr(resp.body.begin(), resp.body.end());
                outNewETag = jsonGetString(bodyStr, "headRevisionId");
                if (outNewETag.empty()) outNewETag = jsonGetString(bodyStr, "version");
                if (outNewETag.empty()) outNewETag = fileId;
            }
            return ECloudError::k_Ok;
        }
        else if (fErr == ECloudError::k_NotFound)
        {
            std::string folderId;
            ECloudError folderErr = getOrCreateNaviFolderId(folderId);
            if (folderErr != ECloudError::k_Ok) return folderErr;

            Curl::HttpRequest_t createReq;
            createReq.method = "POST";
            createReq.url = m_cfg.gdriveApiBaseUrl + "/files";
            createReq.headers.push_back({"Content-Type", "application/json; charset=UTF-8"});
            std::string metaJson = "{\"name\":\"" + fileName + "\",\"parents\":[\"" + folderId + "\"]}";
            createReq.body.assign(metaJson.begin(), metaJson.end());

            Curl::HttpResponse_t createResp;
            ECloudError cErr = ECloudError::k_Ok;
            if (!executeWithAuthAndRetry(createReq, createResp, cErr)) return cErr;

            std::string createStr(createResp.body.begin(), createResp.body.end());
            std::string newId = jsonGetString(createStr, "id");
            if (newId.empty()) return ECloudError::k_NetworkError;

            Curl::HttpRequest_t upReq;
            upReq.method = "PATCH";
            upReq.url = m_cfg.gdriveUploadBaseUrl + "/files/" + newId + "?uploadType=media";
            upReq.headers.push_back({"Content-Type", "text/yaml; charset=UTF-8"});
            upReq.body = blob;

            Curl::HttpResponse_t upResp;
            ECloudError upErr = ECloudError::k_Ok;
            if (!executeWithAuthAndRetry(upReq, upResp, upErr)) return upErr;

            auto it = upResp.headers.find("etag");
            if (it != upResp.headers.end()) outNewETag = it->second;
            else
            {
                std::string bodyStr(upResp.body.begin(), upResp.body.end());
                outNewETag = jsonGetString(bodyStr, "headRevisionId");
                if (outNewETag.empty()) outNewETag = jsonGetString(bodyStr, "version");
                if (outNewETag.empty()) outNewETag = newId;
            }
            return ECloudError::k_Ok;
        }

        return fErr;
    }

    ECloudError GoogleDriveAppDataProvider::fetchRemoteManifest(uint64_t steamId64,
                                                               SyncStateManifest_t& outManifest,
                                                               std::string& outETag)
    {
        std::string fileName = "navi_sync_manifest.yaml";
        std::string fileId, currentETag;
        ECloudError fErr = findFileIdByName(fileName, fileId, currentETag);
        if (fErr != ECloudError::k_Ok) return fErr;

        Curl::HttpRequest_t req;
        req.method = "GET";
        req.url = m_cfg.gdriveApiBaseUrl + "/files/" + fileId + "?alt=media";

        Curl::HttpResponse_t resp;
        ECloudError err = ECloudError::k_Ok;
        if (!executeWithAuthAndRetry(req, resp, err)) return err;

        auto it = resp.headers.find("etag");
        if (it != resp.headers.end()) outETag = it->second;
        else outETag = currentETag;

        std::string yamlStr(resp.body.begin(), resp.body.end());
        if (!deserializeManifestFromYaml(yamlStr, outManifest))
        {
            EncryptedEnvelopeMetadata_t expectedMeta;
            expectedMeta.steamId64 = steamId64;
            expectedMeta.keyEpoch = m_cfg.keyEpoch;
            expectedMeta.category = ESyncCategory::k_ManifestsAndLua;
            expectedMeta.logicalPath = "manifest.enc";
            expectedMeta.generation = 0;

            std::vector<uint8_t> plain;
            if (!decryptBlob(m_cfg.masterKey, expectedMeta, resp.body, plain))
            {
                MasterKey_t legacyZeroKey{};
                if (!decryptBlob(legacyZeroKey, expectedMeta, resp.body, plain))
                {
                    return ECloudError::k_CorruptPayload;
                }
            }
            yamlStr.assign(plain.begin(), plain.end());
            if (!deserializeManifestFromYaml(yamlStr, outManifest))
            {
                return ECloudError::k_CorruptPayload;
            }
        }

        if (outManifest.steamId64 != steamId64)
        {
            return ECloudError::k_CorruptPayload;
        }

        return ECloudError::k_Ok;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // WebDavCloudProvider
    // ─────────────────────────────────────────────────────────────────────────────

    WebDavCloudProvider::WebDavCloudProvider(CloudProviderConfig_t cfg)
        : m_cfg(std::move(cfg))
    {
    }

    std::string WebDavCloudProvider::normalizedBaseUrl() const
    {
        std::string base = m_cfg.webDavBaseUrl;
        while (!base.empty() && base.back() == '/')
        {
            base.pop_back();
        }
        return base;
    }

    std::string WebDavCloudProvider::getAuthHeader() const
    {
        if (!m_cfg.webDavUsername.empty())
        {
            std::string creds = m_cfg.webDavUsername + ":" + m_cfg.webDavPasswordOrToken;
            return "Basic " + base64::to_base64(creds);
        }
        else if (!m_cfg.webDavPasswordOrToken.empty())
        {
            return "Bearer " + m_cfg.webDavPasswordOrToken;
        }
        return "";
    }

    ECloudError WebDavCloudProvider::refreshAuthTokenIfNeeded()
    {
        return ECloudError::k_Ok;
    }

    bool WebDavCloudProvider::executeWithRetry(Curl::HttpRequest_t req,
                                              Curl::HttpResponse_t& outResp,
                                              ECloudError& outErr)
    {
        std::string auth = getAuthHeader();
        if (!auth.empty())
        {
            bool hasAuth = false;
            for (auto& [k, v] : req.headers)
            {
                if (k == "Authorization")
                {
                    v = auth;
                    hasAuth = true;
                    break;
                }
            }
            if (!hasAuth)
            {
                req.headers.push_back({"Authorization", auth});
            }
        }

        int maxRetries = std::max(0, m_cfg.maxRetries);
        for (int attempt = 0; attempt <= maxRetries; ++attempt)
        {
            if (!Curl::request(req, outResp))
            {
                if (attempt < maxRetries)
                {
                    if (m_cfg.baseRetryDelayMs > 0)
                        std::this_thread::sleep_for(std::chrono::milliseconds(m_cfg.baseRetryDelayMs * (1 << attempt)));
                    continue;
                }
                outErr = ECloudError::k_NetworkError;
                return false;
            }

            if (outResp.statusCode == 401)
            {
                outErr = ECloudError::k_AuthExpired;
                return false;
            }

            if (outResp.statusCode == 429)
            {
                int delayMs = m_cfg.baseRetryDelayMs * (1 << attempt);
                auto it = outResp.headers.find("retry-after");
                if (it != outResp.headers.end())
                {
                    try {
                        int sec = std::stoi(it->second);
                        if (sec > 0) delayMs = std::max(delayMs, sec * 1000);
                    } catch (...) {}
                }

                if (attempt < maxRetries)
                {
                    if (delayMs > 0)
                        std::this_thread::sleep_for(std::chrono::milliseconds(std::min(delayMs, 5000)));
                    continue;
                }
                outErr = ECloudError::k_RateLimited;
                return false;
            }

            if (outResp.statusCode >= 500 && outResp.statusCode <= 504 && outResp.statusCode != 507)
            {
                if (attempt < maxRetries)
                {
                    if (m_cfg.baseRetryDelayMs > 0)
                        std::this_thread::sleep_for(std::chrono::milliseconds(m_cfg.baseRetryDelayMs * (1 << attempt)));
                    continue;
                }
                outErr = ECloudError::k_NetworkError;
                return false;
            }

            if (outResp.statusCode == 403 || outResp.statusCode == 507)
            {
                outErr = ECloudError::k_QuotaExceeded;
                return false;
            }

            if (outResp.statusCode == 412)
            {
                outErr = ECloudError::k_PreconditionFailed;
                return false;
            }

            if (outResp.statusCode == 404)
            {
                outErr = ECloudError::k_NotFound;
                return false;
            }

            if (outResp.statusCode >= 200 && outResp.statusCode < 300)
            {
                outErr = ECloudError::k_Ok;
                return true;
            }

            outErr = ECloudError::k_NetworkError;
            return false;
        }

        outErr = ECloudError::k_NetworkError;
        return false;
    }

    ECloudError WebDavCloudProvider::ensureDirectories(uint64_t steamId64)
    {
        std::string base = normalizedBaseUrl();

        // MKCOL /<steamId64>
        Curl::HttpRequest_t req1;
        req1.method = "MKCOL";
        req1.url = base + "/" + std::to_string(steamId64);
        Curl::HttpResponse_t resp1;
        ECloudError err = ECloudError::k_Ok;
        executeWithRetry(req1, resp1, err);
        // Ignore 405 (Method Not Allowed - already exists) or 201 Created

        // MKCOL /<steamId64>/blobs
        Curl::HttpRequest_t req2;
        req2.method = "MKCOL";
        req2.url = base + "/" + std::to_string(steamId64) + "/blobs";
        Curl::HttpResponse_t resp2;
        executeWithRetry(req2, resp2, err);

        return ECloudError::k_Ok;
    }

    ECloudError WebDavCloudProvider::queryQuota(CloudQuotaInfo_t& outQuota)
    {
        std::string base = normalizedBaseUrl();

        Curl::HttpRequest_t req;
        req.method = "PROPFIND";
        req.url = base + "/";
        req.headers.push_back({"Depth", "0"});
        req.headers.push_back({"Content-Type", "application/xml; charset=utf-8"});

        std::string bodyXml =
            "<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n"
            "<d:propfind xmlns:d=\"DAV:\">\n"
            "  <d:prop>\n"
            "    <d:quota-available-bytes/>\n"
            "    <d:quota-used-bytes/>\n"
            "  </d:prop>\n"
            "</d:propfind>\n";
        req.body.assign(bodyXml.begin(), bodyXml.end());

        Curl::HttpResponse_t resp;
        ECloudError err = ECloudError::k_Ok;
        if (!executeWithRetry(req, resp, err))
        {
            // If PROPFIND fails with 401 or quota error, propagate
            return err;
        }

        std::string xml(resp.body.begin(), resp.body.end());

        // Parse XML tags: quota-available-bytes and quota-used-bytes
        std::regex reAvail(R"(<[^>]*quota-available-bytes[^>]*>([0-9]+)</[^>]*quota-available-bytes>)");
        std::regex reUsed(R"(<[^>]*quota-used-bytes[^>]*>([0-9]+)</[^>]*quota-used-bytes>)");

        std::smatch matchAvail, matchUsed;
        uint64_t avail = 0;
        uint64_t used = 0;
        bool hasAvail = false;
        bool hasUsed = false;

        if (std::regex_search(xml, matchAvail, reAvail) && matchAvail.size() > 1)
        {
            try {
                avail = std::stoull(matchAvail[1].str());
                hasAvail = true;
            } catch (...) {}
        }

        if (std::regex_search(xml, matchUsed, reUsed) && matchUsed.size() > 1)
        {
            try {
                used = std::stoull(matchUsed[1].str());
                hasUsed = true;
            } catch (...) {}
        }

        if (hasAvail || hasUsed)
        {
            outQuota.usedBytes = used;
            outQuota.limitBytes = hasAvail ? (used + avail) : 0;
        }
        else
        {
            // Server did not return quota XML: unlimited
            outQuota.usedBytes = 0;
            outQuota.limitBytes = 0;
        }

        return ECloudError::k_Ok;
    }

    ECloudError WebDavCloudProvider::pushEncryptedObject(uint64_t steamId64,
                                                        const std::string& objectKey,
                                                        const std::vector<uint8_t>& blob)
    {
        CloudQuotaInfo_t quota;
        if (queryQuota(quota) == ECloudError::k_Ok)
        {
            if (!quota.hasSpaceFor(blob.size()))
            {
                return ECloudError::k_QuotaExceeded;
            }
        }

        ensureDirectories(steamId64);

        std::string base = normalizedBaseUrl();
        Curl::HttpRequest_t req;
        req.method = "PUT";
        req.url = base + "/" + std::to_string(steamId64) + "/blobs/" + objectKey;
        req.headers.push_back({"Content-Type", "application/octet-stream"});
        req.body = blob;

        Curl::HttpResponse_t resp;
        ECloudError err = ECloudError::k_Ok;
        if (!executeWithRetry(req, resp, err))
        {
            return err;
        }

        return ECloudError::k_Ok;
    }

    ECloudError WebDavCloudProvider::pullEncryptedObject(uint64_t steamId64,
                                                        const std::string& objectKey,
                                                        std::vector<uint8_t>& outBlob)
    {
        std::string base = normalizedBaseUrl();
        Curl::HttpRequest_t req;
        req.method = "GET";
        req.url = base + "/" + std::to_string(steamId64) + "/blobs/" + objectKey;

        Curl::HttpResponse_t resp;
        ECloudError err = ECloudError::k_Ok;
        if (!executeWithRetry(req, resp, err))
        {
            return err;
        }

        outBlob = std::move(resp.body);
        return ECloudError::k_Ok;
    }

    ECloudError WebDavCloudProvider::commitRemoteManifest(uint64_t steamId64,
                                                         const SyncStateManifest_t& manifest,
                                                         const std::string& expectedETag,
                                                         std::string& outNewETag)
    {
        std::string yaml = serializeManifestToYaml(manifest);

        EncryptedEnvelopeMetadata_t meta;
        meta.steamId64 = manifest.steamId64;
        meta.keyEpoch = manifest.keyEpoch;
        meta.category = ESyncCategory::k_ManifestsAndLua;
        meta.logicalPath = "manifest.enc";
        meta.generation = 0;

        std::vector<uint8_t> plain(yaml.begin(), yaml.end());
        std::vector<uint8_t> blob;
        if (!encryptBlob(m_cfg.masterKey, meta, plain, blob))
        {
            return ECloudError::k_CorruptPayload;
        }

        ensureDirectories(steamId64);

        std::string base = normalizedBaseUrl();
        Curl::HttpRequest_t req;
        req.method = "PUT";
        req.url = base + "/" + std::to_string(steamId64) + "/manifest.enc";
        req.headers.push_back({"Content-Type", "application/octet-stream"});

        if (!expectedETag.empty())
        {
            req.headers.push_back({"If-Match", expectedETag});
        }
        req.body = blob;

        Curl::HttpResponse_t resp;
        ECloudError err = ECloudError::k_Ok;
        if (!executeWithRetry(req, resp, err))
        {
            return err;
        }

        auto it = resp.headers.find("etag");
        if (it != resp.headers.end())
        {
            outNewETag = it->second;
        }
        else
        {
            outNewETag = "\"gen-" + std::to_string(manifest.globalGeneration) + "\"";
        }

        return ECloudError::k_Ok;
    }

    ECloudError WebDavCloudProvider::fetchRemoteManifest(uint64_t steamId64,
                                                        SyncStateManifest_t& outManifest,
                                                        std::string& outETag)
    {
        std::string base = normalizedBaseUrl();
        Curl::HttpRequest_t req;
        req.method = "GET";
        req.url = base + "/" + std::to_string(steamId64) + "/manifest.enc";

        Curl::HttpResponse_t resp;
        ECloudError err = ECloudError::k_Ok;
        if (!executeWithRetry(req, resp, err))
        {
            return err;
        }

        auto it = resp.headers.find("etag");
        if (it != resp.headers.end())
        {
            outETag = it->second;
        }

        EncryptedEnvelopeMetadata_t expectedMeta;
        expectedMeta.steamId64 = steamId64;
        expectedMeta.keyEpoch = m_cfg.keyEpoch;
        expectedMeta.category = ESyncCategory::k_ManifestsAndLua;
        expectedMeta.logicalPath = "manifest.enc";
        expectedMeta.generation = 0;

        std::vector<uint8_t> plain;
        if (!decryptBlob(m_cfg.masterKey, expectedMeta, resp.body, plain))
        {
            MasterKey_t legacyZeroKey{};
            if (!decryptBlob(legacyZeroKey, expectedMeta, resp.body, plain))
            {
                return ECloudError::k_CorruptPayload;
            }
        }

        std::string yamlStr(plain.begin(), plain.end());
        if (!deserializeManifestFromYaml(yamlStr, outManifest))
        {
            return ECloudError::k_CorruptPayload;
        }

        if (outManifest.steamId64 != steamId64)
        {
            return ECloudError::k_CorruptPayload;
        }

        return ECloudError::k_Ok;
    }

    std::unique_ptr<ICloudStorageProvider> createCloudProvider(const CloudProviderConfig_t& cfg)
    {
        if (cfg.type == ECloudProviderType::k_GoogleDrive)
        {
            return std::make_unique<GoogleDriveAppDataProvider>(cfg);
        }
        if (cfg.type == ECloudProviderType::k_WebDav)
        {
            return std::make_unique<WebDavCloudProvider>(cfg);
        }
        return nullptr;
    }

} // namespace Sync
