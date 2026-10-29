#include "achievement_merger.hpp"

#include "yaml-cpp/yaml.h"

#include <algorithm>
#include <climits>
#include <cstring>
#include <memory>
#include <string_view>
#include <unordered_set>

namespace Sync
{
    namespace
    {
        void serializeNodeInternal(const BinaryKVNode& node, std::vector<uint8_t>& buf)
        {
            buf.push_back(static_cast<uint8_t>(node.type));
            buf.insert(buf.end(), node.name.begin(), node.name.end());
            buf.push_back('\0');

            switch (node.type)
            {
                case BinaryKVNode::Type::SubKey:
                {
                    for (const auto& child : node.children)
                    {
                        if (child)
                        {
                            serializeNodeInternal(*child, buf);
                        }
                    }
                    buf.push_back(static_cast<uint8_t>(BinaryKVNode::Type::End));
                    break;
                }
                case BinaryKVNode::Type::String:
                {
                    buf.insert(buf.end(), node.stringVal.begin(), node.stringVal.end());
                    buf.push_back('\0');
                    break;
                }
                case BinaryKVNode::Type::Int32:
                case BinaryKVNode::Type::Color:
                {
                    uint32_t v = static_cast<uint32_t>(node.intVal);
                    const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
                    buf.insert(buf.end(), p, p + sizeof(v));
                    break;
                }
                case BinaryKVNode::Type::Float:
                case BinaryKVNode::Type::Ptr:
                {
                    uint32_t v = static_cast<uint32_t>(node.intVal);
                    const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
                    buf.insert(buf.end(), p, p + sizeof(v));
                    break;
                }
                case BinaryKVNode::Type::WString:
                {
                    buf.push_back('\0');
                    buf.push_back('\0');
                    break;
                }
                case BinaryKVNode::Type::Uint64:
                case BinaryKVNode::Type::Int64:
                case BinaryKVNode::Type::AlternateUint64:
                {
                    uint64_t v = node.intVal;
                    const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
                    buf.insert(buf.end(), p, p + sizeof(v));
                    break;
                }
                default:
                    break;
            }
        }
    } // anonymous namespace

    AchievementStatBlob_t mergeAchievementStateUnion(
        const AchievementStatBlob_t& localState,
        const AchievementStatBlob_t& remoteState)
    {
        AchievementStatBlob_t merged;
        merged.appId = (localState.appId != 0) ? localState.appId : remoteState.appId;

        // Collect all achievement IDs
        std::unordered_set<std::string> allIds;
        for (const auto& [id, _] : localState.achievements) allIds.insert(id);
        for (const auto& [id, _] : remoteState.achievements) allIds.insert(id);

        for (const auto& id : allIds)
        {
            AchievementEntry_t entry;
            entry.id = id;

            auto itLoc = localState.achievements.find(id);
            auto itRem = remoteState.achievements.find(id);

            bool locUnlocked = (itLoc != localState.achievements.end()) ? itLoc->second.unlocked : false;
            uint32_t locTime = (itLoc != localState.achievements.end()) ? itLoc->second.unlockTimestamp : 0;

            bool remUnlocked = (itRem != remoteState.achievements.end()) ? itRem->second.unlocked : false;
            uint32_t remTime = (itRem != remoteState.achievements.end()) ? itRem->second.unlockTimestamp : 0;

            entry.unlocked = locUnlocked || remUnlocked;

            if (!entry.unlocked)
            {
                entry.unlockTimestamp = 0;
            }
            else if (locUnlocked && remUnlocked && locTime > 0 && remTime > 0)
            {
                entry.unlockTimestamp = std::min(locTime, remTime);
            }
            else if (locTime > 0)
            {
                entry.unlockTimestamp = locTime;
            }
            else if (remTime > 0)
            {
                entry.unlockTimestamp = remTime;
            }
            else
            {
                entry.unlockTimestamp = 0;
            }

            merged.achievements[id] = entry;
        }

        // Collect all stat keys
        std::unordered_set<std::string> allStats;
        for (const auto& [k, _] : localState.intStats) allStats.insert(k);
        for (const auto& [k, _] : remoteState.intStats) allStats.insert(k);

        for (const auto& k : allStats)
        {
            auto itLoc = localState.intStats.find(k);
            auto itRem = remoteState.intStats.find(k);

            if (itLoc != localState.intStats.end() && itRem != remoteState.intStats.end())
            {
                merged.intStats[k] = std::max(itLoc->second, itRem->second);
            }
            else if (itLoc != localState.intStats.end())
            {
                merged.intStats[k] = itLoc->second;
            }
            else
            {
                merged.intStats[k] = itRem->second;
            }
        }

        return merged;
    }

    std::vector<uint8_t> serializeBinaryKV(const BinaryKVNode& root)
    {
        std::vector<uint8_t> buf;
        if (root.name == "root" && root.type == BinaryKVNode::Type::SubKey)
        {
            for (const auto& child : root.children)
            {
                if (child)
                {
                    serializeNodeInternal(*child, buf);
                }
            }
            buf.push_back(static_cast<uint8_t>(BinaryKVNode::Type::End));
        }
        else
        {
            serializeNodeInternal(root, buf);
            buf.push_back(static_cast<uint8_t>(BinaryKVNode::Type::End));
        }
        return buf;
    }

    bool parseAchievementBinaryKV(
        const std::vector<uint8_t>& rawBytes,
        AchievementStatBlob_t& outState)
    {
        if (rawBytes.empty()) return false;

        // Check YAML fallback
        const std::string yamlPrefix = "schema: navi_achievements_v1";
        if (rawBytes.size() >= yamlPrefix.size() &&
            std::string_view(reinterpret_cast<const char*>(rawBytes.data()), yamlPrefix.size()) == yamlPrefix)
        {
            try
            {
                std::string yamlStr(rawBytes.begin(), rawBytes.end());
                YAML::Node doc = YAML::Load(yamlStr);
                outState.achievements.clear();
                outState.intStats.clear();
                if (doc["appid"]) outState.appId = doc["appid"].as<uint32_t>();

                if (doc["achievements"] && doc["achievements"].IsMap())
                {
                    for (const auto& item : doc["achievements"])
                    {
                        AchievementEntry_t entry;
                        entry.id = item.first.as<std::string>();
                        if (item.second.IsMap())
                        {
                            if (item.second["unlocked"]) entry.unlocked = item.second["unlocked"].as<bool>();
                            if (item.second["unlock_time"]) entry.unlockTimestamp = item.second["unlock_time"].as<uint32_t>();
                            else if (item.second["time"]) entry.unlockTimestamp = item.second["time"].as<uint32_t>();
                        }
                        else if (item.second.IsScalar())
                        {
                            entry.unlocked = item.second.as<bool>();
                        }
                        outState.achievements[entry.id] = entry;
                    }
                }

                if (doc["stats"] && doc["stats"].IsMap())
                {
                    for (const auto& item : doc["stats"])
                    {
                        outState.intStats[item.first.as<std::string>()] = item.second.as<int64_t>();
                    }
                }
                return true;
            }
            catch (...)
            {
                return false;
            }
        }

        // BinaryKV parsing
        auto parsedRoot = BinaryKVNode::parse(reinterpret_cast<const char*>(rawBytes.data()), rawBytes.size());
        if (!parsedRoot) return false;

        const BinaryKVNode* statsRoot = parsedRoot->findChild("UserGameStats");
        if (!statsRoot)
        {
            statsRoot = parsedRoot.get();
        }

        outState.achievements.clear();
        outState.intStats.clear();

        outState.appId = statsRoot->getUint32("appid", 0);

        // Achievements subkey
        const BinaryKVNode* achNode = statsRoot->findChild("Achievements");
        if (!achNode) achNode = statsRoot->findChild("achievements");
        if (achNode)
        {
            for (const auto& child : achNode->children)
            {
                if (!child) continue;
                AchievementEntry_t entry;
                entry.id = child->name;
                if (child->type == BinaryKVNode::Type::SubKey)
                {
                    uint32_t unl = child->getUint32("unlocked", 0);
                    entry.unlocked = (unl != 0);
                    entry.unlockTimestamp = child->getUint32("unlock_time", 0);
                    if (entry.unlockTimestamp == 0)
                    {
                        entry.unlockTimestamp = child->getUint32("time", 0);
                    }
                }
                else
                {
                    entry.unlocked = (child->intVal != 0);
                    entry.unlockTimestamp = 0;
                }
                outState.achievements[entry.id] = entry;
            }
        }

        // Stats subkey
        const BinaryKVNode* sNode = statsRoot->findChild("Stats");
        if (!sNode) sNode = statsRoot->findChild("stats");
        if (sNode)
        {
            for (const auto& child : sNode->children)
            {
                if (!child) continue;
                int64_t val = 0;
                if (child->type == BinaryKVNode::Type::Int32 ||
                    child->type == BinaryKVNode::Type::Uint64 ||
                    child->type == BinaryKVNode::Type::Int64 ||
                    child->type == BinaryKVNode::Type::AlternateUint64)
                {
                    val = static_cast<int64_t>(child->intVal);
                }
                else if (child->type == BinaryKVNode::Type::String)
                {
                    try { val = std::stoll(child->stringVal); } catch (...) {}
                }
                outState.intStats[child->name] = val;
            }
        }

        return true;
    }

    std::vector<uint8_t> serializeAchievementBinaryKV(const AchievementStatBlob_t& state)
    {
        BinaryKVNode root;
        root.name = "UserGameStats";
        root.type = BinaryKVNode::Type::SubKey;

        auto appNode = std::make_shared<BinaryKVNode>();
        appNode->name = "appid";
        appNode->type = BinaryKVNode::Type::Int32;
        appNode->intVal = state.appId;
        root.children.push_back(appNode);

        auto achNode = std::make_shared<BinaryKVNode>();
        achNode->name = "Achievements";
        achNode->type = BinaryKVNode::Type::SubKey;

        // Sort achievements by ID
        std::vector<std::string> sortedAchIds;
        sortedAchIds.reserve(state.achievements.size());
        for (const auto& [id, _] : state.achievements) sortedAchIds.push_back(id);
        std::sort(sortedAchIds.begin(), sortedAchIds.end());

        for (const auto& id : sortedAchIds)
        {
            const auto& entry = state.achievements.at(id);
            auto entryNode = std::make_shared<BinaryKVNode>();
            entryNode->name = id;
            entryNode->type = BinaryKVNode::Type::SubKey;

            auto unlNode = std::make_shared<BinaryKVNode>();
            unlNode->name = "unlocked";
            unlNode->type = BinaryKVNode::Type::Int32;
            unlNode->intVal = entry.unlocked ? 1 : 0;
            entryNode->children.push_back(unlNode);

            auto timeNode = std::make_shared<BinaryKVNode>();
            timeNode->name = "unlock_time";
            timeNode->type = BinaryKVNode::Type::Int32;
            timeNode->intVal = entry.unlockTimestamp;
            entryNode->children.push_back(timeNode);

            achNode->children.push_back(entryNode);
        }
        root.children.push_back(achNode);

        auto statsNode = std::make_shared<BinaryKVNode>();
        statsNode->name = "Stats";
        statsNode->type = BinaryKVNode::Type::SubKey;

        // Sort stats by key
        std::vector<std::string> sortedStatKeys;
        sortedStatKeys.reserve(state.intStats.size());
        for (const auto& [k, _] : state.intStats) sortedStatKeys.push_back(k);
        std::sort(sortedStatKeys.begin(), sortedStatKeys.end());

        for (const auto& k : sortedStatKeys)
        {
            int64_t v = state.intStats.at(k);
            auto statEntry = std::make_shared<BinaryKVNode>();
            statEntry->name = k;
            if (v >= INT32_MIN && v <= INT32_MAX)
            {
                statEntry->type = BinaryKVNode::Type::Int32;
                statEntry->intVal = static_cast<uint32_t>(v);
            }
            else
            {
                statEntry->type = BinaryKVNode::Type::Int64;
                statEntry->intVal = static_cast<uint64_t>(v);
            }
            statsNode->children.push_back(statEntry);
        }
        root.children.push_back(statsNode);

        return serializeBinaryKV(root);
    }

    bool mergeAchievementBuffersUnion(
        const std::vector<uint8_t>& localBytes,
        const std::vector<uint8_t>& remoteBytes,
        std::vector<uint8_t>& outMergedBytes)
    {
        if (localBytes.empty() && remoteBytes.empty())
        {
            outMergedBytes.clear();
            return true;
        }
        if (localBytes.empty())
        {
            outMergedBytes = remoteBytes;
            return true;
        }
        if (remoteBytes.empty())
        {
            outMergedBytes = localBytes;
            return true;
        }

        AchievementStatBlob_t localState;
        bool locOk = parseAchievementBinaryKV(localBytes, localState);

        AchievementStatBlob_t remoteState;
        bool remOk = parseAchievementBinaryKV(remoteBytes, remoteState);

        if (!locOk && !remOk) return false;
        if (!locOk)
        {
            outMergedBytes = remoteBytes;
            return true;
        }
        if (!remOk)
        {
            outMergedBytes = localBytes;
            return true;
        }

        AchievementStatBlob_t merged = mergeAchievementStateUnion(localState, remoteState);
        outMergedBytes = serializeAchievementBinaryKV(merged);
        return true;
    }
} // namespace Sync
