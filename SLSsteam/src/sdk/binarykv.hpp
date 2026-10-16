#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class BinaryKVNode
{
public:
    enum class Type : uint8_t
    {
        SubKey          = 0x00,
        String          = 0x01,
        Int32           = 0x02,
        Float           = 0x03,
        Ptr             = 0x04,
        WString         = 0x05,
        Color           = 0x06,
        Uint64          = 0x07,
        End             = 0x08,
        Int64           = 0x0a,
        AlternateUint64 = 0x0b,
        Unknown         = 0xff
    };

    std::string name;
    Type type = Type::Unknown;
    std::string stringVal;
    uint64_t intVal = 0;
    std::vector<std::shared_ptr<BinaryKVNode>> children;

    const BinaryKVNode* findChild(const std::string& childName) const;
    std::string getString(const std::string& key, const std::string& defVal = "") const;
    uint64_t getUint64(const std::string& key, uint64_t defVal = 0) const;
    uint32_t getUint32(const std::string& key, uint32_t defVal = 0) const;

    static std::shared_ptr<BinaryKVNode> parse(const char* data, size_t len);
};

namespace AppInfoHelper
{
    struct DepotInfo
    {
        uint32_t depotId = 0;
        uint64_t manifestGid = 0;
        uint64_t maxsize = 0;
        std::string name;
        uint32_t dlcAppId = 0;
    };

    // Parse the binary KeyValues returned by IClientApps::getAppDataSection(..., k_EAppInfoSectionDepots)
    std::vector<DepotInfo> parseDepotsSection(const char* data, size_t len);
}
