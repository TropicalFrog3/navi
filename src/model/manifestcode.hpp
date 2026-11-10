/**
 * @file  manifestcode.hpp
 * @brief Interface for managing manifest authorization codes
 */
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <future>
#include <mutex>

class CNetPacket;

namespace ManifestCode
{
    // Lightweight protobuf definitions matching Steam wire format for:
    // CContentServerDirectory_GetManifestRequestCode_Request
    // CContentServerDirectory_GetManifestRequestCode_Response

    class CContentServerDirectory_GetManifestRequestCode_Request
    {
    public:
        uint32_t m_app_id = 0;
        uint32_t m_depot_id = 0;
        uint64_t m_manifest_id = 0;
        std::string m_app_branch;
        std::string m_branch_password_hash;

        bool m_has_app_id = false;
        bool m_has_depot_id = false;
        bool m_has_manifest_id = false;

        bool has_app_id() const { return m_has_app_id; }
        uint32_t app_id() const { return m_app_id; }
        void set_app_id(uint32_t val) { m_app_id = val; m_has_app_id = true; }

        bool has_depot_id() const { return m_has_depot_id; }
        uint32_t depot_id() const { return m_depot_id; }
        void set_depot_id(uint32_t val) { m_depot_id = val; m_has_depot_id = true; }

        bool has_manifest_id() const { return m_has_manifest_id; }
        uint64_t manifest_id() const { return m_manifest_id; }
        void set_manifest_id(uint64_t val) { m_manifest_id = val; m_has_manifest_id = true; }

        bool ParseFromArray(const void* data, size_t size);
        size_t ByteSizeLong() const;
        bool SerializeToArray(void* data, size_t size) const;
    };

    class CContentServerDirectory_GetManifestRequestCode_Response
    {
    public:
        uint64_t m_manifest_request_code = 0;
        bool m_has_code = false;

        bool has_manifest_request_code() const { return m_has_code; }
        uint64_t manifest_request_code() const { return m_manifest_request_code; }
        void set_manifest_request_code(uint64_t val) { m_manifest_request_code = val; m_has_code = true; }

        bool ParseFromArray(const void* data, size_t size);
        size_t ByteSizeLong() const;
        bool SerializeToArray(void* data, size_t size) const;
    };

    // Public API
    bool fetchManifestRequestCode(uint64_t manifestGid, uint64_t* outCode, uint32_t appId = 0, uint32_t depotId = 0);
    bool ensureManifestFile(uint32_t appId, uint32_t depotId, uint64_t manifestGid);

    void sendMsg(CNetPacket* pkt);
    void recvMsg(CNetPacket* pkt);
}
