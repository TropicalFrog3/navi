/**
 * @file  manifestcode.cpp
 * @brief Steam manifest decryption code storage and retrieval
 */
#include "manifestcode.hpp"
#include "log.hpp"
#include "CNetPacket.hpp"
#include "paths.hpp"
#include "atomic_file.hpp"
#include "config.hpp"
#include "curl.hpp"
#include <charconv>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace ManifestCode
{
    // ── Protobuf wire helpers ──────────────────────────────────────────────

    static inline bool readVarint(const uint8_t*& p, const uint8_t* end, uint64_t& val)
    {
        val = 0;
        int shift = 0;
        while (p < end)
        {
            uint8_t byte = *p++;
            val |= static_cast<uint64_t>(byte & 0x7F) << shift;
            if ((byte & 0x80) == 0) return true;
            shift += 7;
            if (shift >= 64) return false;
        }
        return false;
    }

    static inline size_t writeVarint(uint8_t* p, uint64_t val)
    {
        size_t count = 0;
        while (val >= 0x80)
        {
            p[count++] = static_cast<uint8_t>((val & 0x7F) | 0x80);
            val >>= 7;
        }
        p[count++] = static_cast<uint8_t>(val);
        return count;
    }

    static inline size_t varintSize(uint64_t val)
    {
        size_t count = 1;
        while (val >= 0x80)
        {
            count++;
            val >>= 7;
        }
        return count;
    }

    // ── CContentServerDirectory_GetManifestRequestCode_Request ─────────────

    bool CContentServerDirectory_GetManifestRequestCode_Request::ParseFromArray(const void* data, size_t size)
    {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(data);
        const uint8_t* end = p + size;
        while (p < end)
        {
            uint64_t tag = 0;
            if (!readVarint(p, end, tag)) return false;
            uint32_t field = static_cast<uint32_t>(tag >> 3);
            uint32_t wireType = static_cast<uint32_t>(tag & 7);

            if (field == 1 && wireType == 0)
            {
                uint64_t val = 0;
                if (!readVarint(p, end, val)) return false;
                m_app_id = static_cast<uint32_t>(val);
                m_has_app_id = true;
            }
            else if (field == 2 && wireType == 0)
            {
                uint64_t val = 0;
                if (!readVarint(p, end, val)) return false;
                m_depot_id = static_cast<uint32_t>(val);
                m_has_depot_id = true;
            }
            else if (field == 3 && wireType == 0)
            {
                uint64_t val = 0;
                if (!readVarint(p, end, val)) return false;
                m_manifest_id = val;
                m_has_manifest_id = true;
            }
            else if (field == 4 && wireType == 2)
            {
                uint64_t len = 0;
                if (!readVarint(p, end, len) || p + len > end) return false;
                m_app_branch.assign(reinterpret_cast<const char*>(p), len);
                p += len;
            }
            else if (field == 5 && wireType == 2)
            {
                uint64_t len = 0;
                if (!readVarint(p, end, len) || p + len > end) return false;
                m_branch_password_hash.assign(reinterpret_cast<const char*>(p), len);
                p += len;
            }
            else
            {
                if (wireType == 0)
                {
                    uint64_t dummy;
                    if (!readVarint(p, end, dummy)) return false;
                }
                else if (wireType == 1)
                {
                    if (p + 8 > end) return false;
                    p += 8;
                }
                else if (wireType == 2)
                {
                    uint64_t len = 0;
                    if (!readVarint(p, end, len) || p + len > end) return false;
                    p += len;
                }
                else if (wireType == 5)
                {
                    if (p + 4 > end) return false;
                    p += 4;
                }
                else
                {
                    return false;
                }
            }
        }
        return true;
    }

    size_t CContentServerDirectory_GetManifestRequestCode_Request::ByteSizeLong() const
    {
        size_t sz = 0;
        if (m_has_app_id) sz += 1 + varintSize(m_app_id);
        if (m_has_depot_id) sz += 1 + varintSize(m_depot_id);
        if (m_has_manifest_id) sz += 1 + varintSize(m_manifest_id);
        if (!m_app_branch.empty()) sz += 1 + varintSize(m_app_branch.size()) + m_app_branch.size();
        if (!m_branch_password_hash.empty()) sz += 1 + varintSize(m_branch_password_hash.size()) + m_branch_password_hash.size();
        return sz;
    }

    bool CContentServerDirectory_GetManifestRequestCode_Request::SerializeToArray(void* data, size_t size) const
    {
        if (size < ByteSizeLong()) return false;
        uint8_t* p = reinterpret_cast<uint8_t*>(data);
        if (m_has_app_id)
        {
            *p++ = 0x08;
            p += writeVarint(p, m_app_id);
        }
        if (m_has_depot_id)
        {
            *p++ = 0x10;
            p += writeVarint(p, m_depot_id);
        }
        if (m_has_manifest_id)
        {
            *p++ = 0x18;
            p += writeVarint(p, m_manifest_id);
        }
        if (!m_app_branch.empty())
        {
            *p++ = 0x22;
            p += writeVarint(p, m_app_branch.size());
            memcpy(p, m_app_branch.data(), m_app_branch.size());
            p += m_app_branch.size();
        }
        if (!m_branch_password_hash.empty())
        {
            *p++ = 0x2A;
            p += writeVarint(p, m_branch_password_hash.size());
            memcpy(p, m_branch_password_hash.data(), m_branch_password_hash.size());
            p += m_branch_password_hash.size();
        }
        return true;
    }

    // ── CContentServerDirectory_GetManifestRequestCode_Response ────────────

    bool CContentServerDirectory_GetManifestRequestCode_Response::ParseFromArray(const void* data, size_t size)
    {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(data);
        const uint8_t* end = p + size;
        while (p < end)
        {
            uint64_t tag = 0;
            if (!readVarint(p, end, tag)) return false;
            uint32_t field = static_cast<uint32_t>(tag >> 3);
            uint32_t wireType = static_cast<uint32_t>(tag & 7);

            if (field == 1 && wireType == 0)
            {
                uint64_t val = 0;
                if (!readVarint(p, end, val)) return false;
                m_manifest_request_code = val;
                m_has_code = true;
            }
            else
            {
                if (wireType == 0)
                {
                    uint64_t dummy;
                    if (!readVarint(p, end, dummy)) return false;
                }
                else if (wireType == 1)
                {
                    if (p + 8 > end) return false;
                    p += 8;
                }
                else if (wireType == 2)
                {
                    uint64_t len = 0;
                    if (!readVarint(p, end, len) || p + len > end) return false;
                    p += len;
                }
                else if (wireType == 5)
                {
                    if (p + 4 > end) return false;
                    p += 4;
                }
                else
                {
                    return false;
                }
            }
        }
        return true;
    }

    size_t CContentServerDirectory_GetManifestRequestCode_Response::ByteSizeLong() const
    {
        size_t sz = 0;
        if (m_has_code)
        {
            sz += 1 + varintSize(m_manifest_request_code);
        }
        return sz;
    }

    bool CContentServerDirectory_GetManifestRequestCode_Response::SerializeToArray(void* data, size_t size) const
    {
        if (size < ByteSizeLong()) return false;
        uint8_t* p = reinterpret_cast<uint8_t*>(data);
        if (m_has_code)
        {
            *p++ = 0x08;
            p += writeVarint(p, m_manifest_request_code);
        }
        return true;
    }

    // ── HTTP Fetcher & Providers ──────────────────────────────────────────

    static bool httpGet(const std::string& url, std::string& response, long timeoutSec = 10)
    {
        Curl::HttpRequest_t req;
        req.method = "GET";
        req.url = url;
        req.timeoutSec = static_cast<int>(timeoutSec);
        req.headers.push_back({"User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"});

        Curl::HttpResponse_t resp;
        if (!Curl::request(req, resp))
        {
            return false;
        }

        if (resp.statusCode >= 200 && resp.statusCode < 300)
        {
            response.assign(reinterpret_cast<const char*>(resp.body.data()), resp.body.size());
            return true;
        }
        return false;
    }

    static bool parsePlainUint(std::string_view body, uint64_t* out)
    {
        while (!body.empty() && (body.front() == ' ' || body.front() == '\r' || body.front() == '\n' || body.front() == '\t'))
            body.remove_prefix(1);
        while (!body.empty() && (body.back() == ' ' || body.back() == '\r' || body.back() == '\n' || body.back() == '\t'))
            body.remove_suffix(1);

        if (body.empty()) return false;
        uint64_t code = 0;
        auto [_, ec] = std::from_chars(body.data(), body.data() + body.size(), code);
        if (ec != std::errc{}) return false;
        *out = code;
        return true;
    }

    static bool parseSteamRunJson(std::string_view body, uint64_t* out)
    {
        size_t key = body.find("\"content\"");
        if (key == std::string_view::npos) return false;
        size_t q1 = body.find('"', key + 9);
        if (q1 == std::string_view::npos) return false;
        size_t q2 = body.find('"', q1 + 1);
        if (q2 == std::string_view::npos) return false;
        return parsePlainUint(body.substr(q1 + 1, q2 - q1 - 1), out);
    }

    struct Provider
    {
        const char* name;
        const char* urlTemplate;
        bool (*parse)(std::string_view, uint64_t*);
    };

    static constexpr Provider kProviders[] = {
        { "opensteamtool", "https://manifest.opensteamtool.com/%llu", parsePlainUint },
        { "wudrm",         "http://gmrc.wudrm.com/manifest/%llu", parsePlainUint },
        { "steamrun",      "https://manifest.steam.run/api/manifest/%llu", parseSteamRunJson }
    };

    bool fetchManifestRequestCode(uint64_t manifestGid, uint64_t* outCode, uint32_t appId, uint32_t depotId)
    {
        if (!outCode || manifestGid == 0) return false;

        LOG_INFO("ManifestCode: Fetching manifest request code for gid=%llu (appId=%u, depotId=%u)\n",
                 manifestGid, appId, depotId);

        for (const auto& p : kProviders)
        {
            char url[256];
            std::snprintf(url, sizeof(url), p.urlTemplate, manifestGid);

            std::string resp;
            if (httpGet(url, resp, 8))
            {
                uint64_t code = 0;
                if (p.parse(resp, &code) && code != 0)
                {
                    LOG_INFO("ManifestCode: Resolved request code %llu via provider %s for gid=%llu\n",
                             code, p.name, manifestGid);
                    *outCode = code;
                    return true;
                }
            }
            LOG_DEBUG("ManifestCode: Provider %s failed or returned empty for gid=%llu\n", p.name, manifestGid);
        }

        LOG_WARN("ManifestCode: All providers failed to resolve manifest request code for gid=%llu\n", manifestGid);
        return false;
    }

    static bool isValidManifestContent(const std::string& data)
    {
        if (data.size() < 16) return false;
        // Reject HTML error pages, Cloudflare blocks, and JSON error bodies
        if (data.rfind("<!DOCTYPE", 0) == 0 || data.rfind("<html", 0) == 0 ||
            data.rfind("error code:", 0) == 0 || data.rfind("{\"error\"", 0) == 0 ||
            data.rfind("<html>", 0) == 0 || data.rfind("503 Service", 0) == 0)
        {
            return false;
        }
        return true;
    }

    static std::vector<std::string> getSteamContentServers()
    {
        static std::vector<std::string> s_cachedServers;
        static std::chrono::steady_clock::time_point s_lastFetch;
        static std::mutex s_mutex;

        std::lock_guard<std::mutex> lock(s_mutex);
        auto now = std::chrono::steady_clock::now();
        if (!s_cachedServers.empty() && (now - s_lastFetch < std::chrono::hours(1)))
        {
            return s_cachedServers;
        }

        std::string resp;
        std::vector<std::string> servers;
        if (httpGet("https://api.steampowered.com/IContentServerDirectoryService/GetServersForSteamPipe/v1/?cellId=0", resp, 4))
        {
            size_t pos = 0;
            while ((pos = resp.find("\"host\":\"", pos)) != std::string::npos)
            {
                pos += 8;
                size_t end = resp.find('"', pos);
                if (end == std::string::npos) break;
                std::string host = resp.substr(pos, end - pos);
                if (!host.empty() && std::find(servers.begin(), servers.end(), host) == servers.end())
                {
                    servers.push_back(host);
                    if (servers.size() >= 4) break;
                }
                pos = end + 1;
            }
        }

        if (std::find(servers.begin(), servers.end(), "cache1-ord1.steamcontent.com") == servers.end())
            servers.push_back("cache1-ord1.steamcontent.com");
        if (std::find(servers.begin(), servers.end(), "content.steampowered.com") == servers.end())
            servers.push_back("content.steampowered.com");
        if (std::find(servers.begin(), servers.end(), "client-download.steampowered.com") == servers.end())
            servers.push_back("client-download.steampowered.com");

        s_cachedServers = servers;
        s_lastFetch = now;
        return s_cachedServers;
    }

    static bool fetchManifestFromSteamServer(uint32_t depotId, uint64_t manifestGid, std::string& outContent, uint64_t requestCode = 0)
    {
        const auto servers = getSteamContentServers();
        for (const auto& server : servers)
        {
            std::vector<std::string> urls;
            if (requestCode != 0)
            {
                urls.push_back("https://" + server + "/depot/" + std::to_string(depotId) +
                               "/manifest/" + std::to_string(manifestGid) + "/5/" + std::to_string(requestCode));
            }
            urls.push_back("https://" + server + "/depot/" + std::to_string(depotId) +
                           "/manifest/" + std::to_string(manifestGid) + "/5");

            for (const auto& url : urls)
            {
                std::string resp;
                if (httpGet(url, resp, 8) && isValidManifestContent(resp))
                {
                    LOG_INFO("ManifestCode: Successfully fetched manifest for depot %u from Steam server %s (size %zu bytes)\n",
                             depotId, server.c_str(), resp.size());
                    outContent = std::move(resp);
                    return true;
                }
            }
        }
        return false;
    }

    bool ensureManifestFile(uint32_t appId, uint32_t depotId, uint64_t manifestGid)
    {
        if (depotId == 0 || manifestGid == 0) return false;

        std::string depotcacheDir = Paths::depotcacheDir();
        if (depotcacheDir.empty()) return false;

        std::string manifestFilename = std::to_string(depotId) + "_" + std::to_string(manifestGid) + ".manifest";
        std::string manifestPath = depotcacheDir + "/" + manifestFilename;
        std::string legacyPath = Paths::findSteamRoot() + "/config/depotcache/" + manifestFilename;

        // Primary Check: Already in local cache
        if (std::filesystem::exists(manifestPath))
        {
            std::error_code ec;
            auto sz = std::filesystem::file_size(manifestPath, ec);
            if (!ec && sz > 0)
            {
                LOG_INFO("ManifestCode: Found local manifest %s (size %llu bytes)\n", manifestPath.c_str(), static_cast<unsigned long long>(sz));
                return true;
            }
        }

        // Secondary Check: If in legacy config/depotcache, promote/copy to native depotcache
        if (!legacyPath.empty() && std::filesystem::exists(legacyPath))
        {
            std::error_code ec;
            auto sz = std::filesystem::file_size(legacyPath, ec);
            if (!ec && sz > 0)
            {
                if (AtomicFile::copy(legacyPath, manifestPath))
                {
                    LOG_INFO("ManifestCode: Promoted legacy manifest %s to %s (size %llu bytes)\n",
                             legacyPath.c_str(), manifestPath.c_str(), static_cast<unsigned long long>(sz));
                    return true;
                }
            }
        }

        LOG_INFO("ManifestCode: Local manifest %s not found. Attempting pre-fetch from Archive.org repository sources...\n", manifestFilename.c_str());

        std::string content;

        // 1. First Fallback: Archive.org ManifestHub repositories with encoded in-zip paths
        // - https://archive.org/download/manifest-hub-repo/NEW-depot-keys.zip/NEW-depot-keys%2F{appId main}%2F{appId depot}_{depot}.manifest
        // - https://archive.org/download/manifest-hub-repo/branches.zip/branches%2F{appid main}%2F{appid depot}_{depot}.manifest
        // - https://archive.org/download/manifest-hub-repo/depot-keys-2026-05-15.zip/depot-keys-2026-05-15%2F{appid main}%2F{appid depot}_{depot}.manifest
        static const char* const kArchiveBases[] = {
            "https://archive.org/download/manifest-hub-repo/NEW-depot-keys.zip/NEW-depot-keys%2F",
            "https://archive.org/download/manifest-hub-repo/branches.zip/branches%2F",
            "https://archive.org/download/manifest-hub-repo/depot-keys-2026-05-15.zip/depot-keys-2026-05-15%2F"
        };

        const uint32_t mainAppId = (appId != 0) ? appId : depotId;
        for (const char* base : kArchiveBases)
        {
            std::string aUrl = base + std::to_string(mainAppId) + "%2F" + manifestFilename;
            if (httpGet(aUrl, content, 4) && isValidManifestContent(content))
            {
                if (AtomicFile::write(manifestPath, content, true))
                {
                    LOG_INFO("ManifestCode: Successfully downloaded and cached manifest from archive.org (%s) to %s (size %zu bytes)\n",
                             aUrl.c_str(), manifestPath.c_str(), content.size());
                    if (!legacyPath.empty()) AtomicFile::write(legacyPath, content, true);
                    return true;
                }
            }
        }

        // 2. Second Fallback: Steam content server direct request (SteamPipe CDN)
        uint64_t reqCode = 0;
        fetchManifestRequestCode(manifestGid, &reqCode, appId, depotId);
        if (fetchManifestFromSteamServer(depotId, manifestGid, content, reqCode))
        {
            if (AtomicFile::write(manifestPath, content, true))
            {
                LOG_INFO("ManifestCode: Successfully downloaded and cached Steam server manifest to %s\n", manifestPath.c_str());
                if (!legacyPath.empty()) AtomicFile::write(legacyPath, content, true);
                return true;
            }
        }

        // 3. Third Fallback: steamrun download_manifest
        std::string steamrunUrl = "https://manifest.steam.run/api/download_manifest?depot_id=" +
                                  std::to_string(depotId) + "&manifest_id=" + std::to_string(manifestGid);
        if (httpGet(steamrunUrl, content, 10) && isValidManifestContent(content))
        {
            if (AtomicFile::write(manifestPath, content, true))
            {
                LOG_INFO("ManifestCode: Successfully downloaded and cached steamrun manifest to %s\n", manifestPath.c_str());
                if (!legacyPath.empty()) AtomicFile::write(legacyPath, content, true);
                return true;
            }
        }

        // 4. Fourth Fallback: depotbox
        std::string depotboxUrl = "https://depotbox.org/manifest/" + std::to_string(depotId) + "/" + std::to_string(manifestGid);
        if (httpGet(depotboxUrl, content, 10) && isValidManifestContent(content))
        {
            if (AtomicFile::write(manifestPath, content, true))
            {
                LOG_INFO("ManifestCode: Successfully downloaded and cached depotbox manifest to %s\n", manifestPath.c_str());
                if (!legacyPath.empty()) AtomicFile::write(legacyPath, content, true);
                return true;
            }
        }

        LOG_INFO("ManifestCode: Direct manifest file download not available for %s. Fallback to CM protobuf interception will handle it.\n",
                 manifestFilename.c_str());
        return false;
    }

    // ── CM Protobuf Interception ──────────────────────────────────────────

    static std::unordered_map<uint64_t, std::shared_future<uint64_t>> s_codeFutures;
    static std::mutex s_codeMutex;

    void sendMsg(CNetPacket* pkt)
    {
        if (!pkt || !pkt->isValid() || !pkt->isProtoBuf()) return;
        if (pkt->getProtoBufType() != k_EMsgServiceMethodCallFromClient) return;

        CMsgProtoBufHeader hdr = pkt->deserializeHeader();
        if (!hdr.has_target_job_name() || hdr.target_job_name() != "ContentServerDirectory.GetManifestRequestCode#1")
            return;

        if (!hdr.has_jobid_source())
        {
            LOG_WARN("ManifestCode: GetManifestRequestCode missing jobid_source in header\n");
            return;
        }

        auto req = pkt->deserializeBody<CContentServerDirectory_GetManifestRequestCode_Request>();
        if (!req.has_depot_id() || !req.has_manifest_id())
        {
            LOG_WARN("ManifestCode: GetManifestRequestCode request missing depot_id or manifest_id\n");
            return;
        }

        uint64_t jobId = hdr.jobid_source();
        uint32_t depotId = req.depot_id();
        uint64_t manifestGid = req.manifest_id();
        uint32_t appId = req.has_app_id() ? req.app_id() : 0;

        LOG_INFO("ManifestCode: Intercepted GetManifestRequestCode send: depot=%u gid=%llu jobId=%llu app=%u\n",
                 depotId, manifestGid, jobId, appId);

        auto task = std::async(std::launch::async, [manifestGid, appId, depotId]() -> uint64_t {
            ensureManifestFile(appId, depotId, manifestGid);
            uint64_t code = 0;
            fetchManifestRequestCode(manifestGid, &code, appId, depotId);
            return code;
        });

        {
            std::lock_guard<std::mutex> lock(s_codeMutex);
            s_codeFutures[jobId] = task.share();
        }
    }

    void recvMsg(CNetPacket* pkt)
    {
        if (!pkt || !pkt->isValid() || !pkt->isProtoBuf()) return;
        if (pkt->getProtoBufType() != k_EMsgServiceMethodResponse) return;

        CMsgProtoBufHeader hdr = pkt->deserializeHeader();
        if (!hdr.has_jobid_target()) return;

        uint64_t jobId = hdr.jobid_target();
        std::shared_future<uint64_t> future;

        {
            std::lock_guard<std::mutex> lock(s_codeMutex);
            auto it = s_codeFutures.find(jobId);
            if (it == s_codeFutures.end()) return;
            future = it->second;
            s_codeFutures.erase(it);
        }

        // If native Steam server already succeeded with OK and valid code, do not overwrite
        if (hdr.has_eresult() && hdr.eresult() == static_cast<int32_t>(k_EResultOK))
        {
            auto nativeResp = pkt->deserializeBody<CContentServerDirectory_GetManifestRequestCode_Response>();
            if (nativeResp.has_manifest_request_code() && nativeResp.manifest_request_code() != 0)
            {
                LOG_INFO("ManifestCode: Native Steam server returned OK with code=%llu for jobId=%llu, leaving untouched\n",
                         nativeResp.manifest_request_code(), jobId);
                return;
            }
        }

        auto status = future.wait_for(std::chrono::seconds(12));
        if (status != std::future_status::ready)
        {
            LOG_WARN("ManifestCode: GetManifestRequestCode recv: HTTP fetch timed out for jobId=%llu\n", jobId);
            return;
        }

        uint64_t code = future.get();
        if (!code)
        {
            LOG_WARN("ManifestCode: GetManifestRequestCode recv: HTTP fetch returned 0 for jobId=%llu\n", jobId);
            return;
        }

        LOG_INFO("ManifestCode: GetManifestRequestCode recv: injecting code=%llu for jobId=%llu\n", code, jobId);

        hdr.set_eresult(static_cast<int32_t>(k_EResultOK));

        CContentServerDirectory_GetManifestRequestCode_Response resp;
        resp.set_manifest_request_code(code);

        pkt->serialize(resp, &hdr);
    }
}
