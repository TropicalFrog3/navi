/**
 * @file  binarykv.cpp
 * @brief Binary KeyValues format parser and serializer
 */
#include "binarykv.hpp"

#include <cstdlib>
#include <cstring>
#include <algorithm>

namespace
{
    bool readNullTerminatedString(const char* data, size_t len, size_t& offset, std::string& out)
    {
        size_t start = offset;
        while (offset < len && data[offset] != '\0')
        {
            offset++;
        }
        if (offset >= len) return false;
        out.assign(data + start, offset - start);
        offset++; // consume '\0'
        return true;
    }

    std::shared_ptr<BinaryKVNode> parseNodeInternal(const char* data, size_t len, size_t& offset)
    {
        if (offset >= len) return nullptr;

        uint8_t typeByte = static_cast<uint8_t>(data[offset++]);
        if (typeByte == static_cast<uint8_t>(BinaryKVNode::Type::End))
        {
            return nullptr;
        }

        auto node = std::make_shared<BinaryKVNode>();
        node->type = static_cast<BinaryKVNode::Type>(typeByte);

        if (!readNullTerminatedString(data, len, offset, node->name))
        {
            return nullptr;
        }

        switch (node->type)
        {
            case BinaryKVNode::Type::SubKey:
            {
                while (offset < len)
                {
                    if (static_cast<uint8_t>(data[offset]) == static_cast<uint8_t>(BinaryKVNode::Type::End))
                    {
                        offset++; // Consume end token
                        break;
                    }
                    auto child = parseNodeInternal(data, len, offset);
                    if (child)
                    {
                        node->children.push_back(std::move(child));
                    }
                    else
                    {
                        break;
                    }
                }
                break;
            }

            case BinaryKVNode::Type::String:
            {
                if (readNullTerminatedString(data, len, offset, node->stringVal))
                {
                    char* pEnd = nullptr;
                    unsigned long long v = std::strtoull(node->stringVal.c_str(), &pEnd, 10);
                    if (pEnd != node->stringVal.c_str() && *pEnd == '\0')
                    {
                        node->intVal = v;
                    }
                }
                break;
            }

            case BinaryKVNode::Type::Int32:
            case BinaryKVNode::Type::Color:
            {
                if (offset + sizeof(uint32_t) <= len)
                {
                    uint32_t val = 0;
                    std::memcpy(&val, data + offset, sizeof(val));
                    node->intVal = val;
                    node->stringVal = std::to_string(val);
                    offset += sizeof(val);
                }
                break;
            }

            case BinaryKVNode::Type::Float:
            case BinaryKVNode::Type::Ptr:
            {
                if (offset + 4 <= len)
                {
                    offset += 4;
                }
                break;
            }

            case BinaryKVNode::Type::WString:
            {
                // Wide string is 2-byte null-terminated
                while (offset + 1 < len)
                {
                    if (data[offset] == 0 && data[offset + 1] == 0)
                    {
                        offset += 2;
                        break;
                    }
                    offset += 2;
                }
                break;
            }

            case BinaryKVNode::Type::Uint64:
            case BinaryKVNode::Type::Int64:
            case BinaryKVNode::Type::AlternateUint64:
            {
                if (offset + sizeof(uint64_t) <= len)
                {
                    uint64_t val = 0;
                    std::memcpy(&val, data + offset, sizeof(val));
                    node->intVal = val;
                    node->stringVal = std::to_string(val);
                    offset += sizeof(val);
                }
                break;
            }

            default:
                // Unrecognized type byte
                return nullptr;
        }

        return node;
    }
}

const BinaryKVNode* BinaryKVNode::findChild(const std::string& childName) const
{
    for (const auto& child : children)
    {
        if (strcasecmp(child->name.c_str(), childName.c_str()) == 0)
        {
            return child.get();
        }
    }
    return nullptr;
}

std::string BinaryKVNode::getString(const std::string& key, const std::string& defVal) const
{
    const BinaryKVNode* child = findChild(key);
    if (child && !child->stringVal.empty())
    {
        return child->stringVal;
    }
    return defVal;
}

uint64_t BinaryKVNode::getUint64(const std::string& key, uint64_t defVal) const
{
    const BinaryKVNode* child = findChild(key);
    if (child)
    {
        if (child->intVal != 0) return child->intVal;
        if (!child->stringVal.empty())
        {
            char* pEnd = nullptr;
            unsigned long long v = std::strtoull(child->stringVal.c_str(), &pEnd, 10);
            if (pEnd != child->stringVal.c_str()) return v;
        }
    }
    return defVal;
}

uint32_t BinaryKVNode::getUint32(const std::string& key, uint32_t defVal) const
{
    return static_cast<uint32_t>(getUint64(key, defVal));
}

std::shared_ptr<BinaryKVNode> BinaryKVNode::parse(const char* data, size_t len)
{
    if (!data || len == 0) return nullptr;

    auto root = std::make_shared<BinaryKVNode>();
    root->type = BinaryKVNode::Type::SubKey;
    root->name = "root";

    size_t offset = 0;
    while (offset < len)
    {
        if (static_cast<uint8_t>(data[offset]) == static_cast<uint8_t>(BinaryKVNode::Type::End))
        {
            offset++;
            continue;
        }

        auto node = parseNodeInternal(data, len, offset);
        if (node)
        {
            root->children.push_back(std::move(node));
        }
        else
        {
            break;
        }
    }

    return root;
}

std::vector<AppInfoHelper::DepotInfo> AppInfoHelper::parseDepotsSection(const char* data, size_t len)
{
    std::vector<DepotInfo> result;
    auto root = BinaryKVNode::parse(data, len);
    if (!root) return result;

    // Check if there is an explicit "depots" node
    const BinaryKVNode* depotsNode = root->findChild("depots");
    if (!depotsNode)
    {
        depotsNode = root.get();
    }

    for (const auto& child : depotsNode->children)
    {
        char* pEnd = nullptr;
        unsigned long parsedDepotId = std::strtoul(child->name.c_str(), &pEnd, 10);
        if (pEnd == child->name.c_str() || parsedDepotId == 0)
        {
            continue; // Not a depot ID
        }

        DepotInfo info;
        info.depotId = static_cast<uint32_t>(parsedDepotId);
        info.name = child->getString("name");
        info.dlcAppId = child->getUint32("dlcappid");

        // Extract manifest GID: manifests -> public -> gid
        const BinaryKVNode* manifests = child->findChild("manifests");
        if (manifests)
        {
            const BinaryKVNode* pub = manifests->findChild("public");
            if (pub)
            {
                info.manifestGid = pub->getUint64("gid");
            }
        }

        // Fallback: encryptedmanifests -> public -> gid
        if (info.manifestGid == 0)
        {
            const BinaryKVNode* enc = child->findChild("encryptedmanifests");
            if (enc)
            {
                const BinaryKVNode* pub = enc->findChild("public");
                if (pub)
                {
                    info.manifestGid = pub->getUint64("gid");
                }
            }
        }

        // Size
        info.maxsize = child->getUint64("maxsize");
        if (info.maxsize == 0)
        {
            info.maxsize = child->getUint64("size");
        }

        result.push_back(std::move(info));
    }

    return result;
}
