#include "manifestcode.hpp"
#include "log.hpp"
#include "CNetPacket.hpp"
#include "paths.hpp"
#include "atomic_file.hpp"
#include "config.hpp"

#include <curl/curl.h>
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

    static size_t curlWriteCallback(void* ptr, size_t size, size_t nmemb, void* userdata)
    {
        std::string* s = reinterpret_cast<std::string*>(userdata);
        s->append(reinterpret_cast<char*>(ptr), size * nmemb);
        return size * nmemb;
    }

    static bool httpGet(const std::string& url, std::string& response, long timeoutSec = 10)
    {
        CURL* curl = curl_easy_init();
        if (!curl) return false;

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSec);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "SLSsteam-ManifestClient/1.0");

        CURLcode res = curl_easy_perform(curl);
        long httpCode = 0;
        if (res == CURLE_OK)
        {
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
        }
        curl_easy_cleanup(curl);

        return (res == CURLE_OK && httpCode >= 200 && httpCode < 300);
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

        LOG_INFO("ManifestCode: Local manifest %s not found. Attempting pre-fetch...\n", manifestFilename.c_str());

        // Fast-path Download attempt: check if binary manifest can be fetched directly
        // Try depotbox / manifest providers if available
        std::string url = "https://depotbox.org/manifest/" + std::to_string(depotId) + "/" + std::to_string(manifestGid);
        std::string content;
        if (httpGet(url, content, 10) && !content.empty())
        {
            if (AtomicFile::write(manifestPath, content, true))
            {
                LOG_INFO("ManifestCode: Successfully downloaded and cached manifest to %s\n", manifestPath.c_str());
                if (!legacyPath.empty())
                {
                    AtomicFile::write(legacyPath, content, true);
                }
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
