/**
 * @file  achievement_merger.hpp
 * @brief Declarations for achievement schema caching and stat merging
 */
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "sdk/binarykv.hpp"

namespace Sync
{
    struct AchievementEntry_t
    {
        std::string id;
        bool unlocked = false;
        uint32_t unlockTimestamp = 0;

        bool operator==(const AchievementEntry_t&) const = default;
    };

    struct AchievementStatBlob_t
    {
        uint32_t appId = 0;
        std::unordered_map<std::string, AchievementEntry_t> achievements;
        std::unordered_map<std::string, int64_t> intStats;

        bool operator==(const AchievementStatBlob_t&) const = default;
    };

    /**
     * Performs a mathematical set union (A ∪ B) between local and remote achievement/stat states:
     * - Merged unlocked = local.unlocked || remote.unlocked (strictly monotonic).
     * - Merged unlockTimestamp = std::min(loc, rem) if both unlocked and > 0, else non-zero timestamp, else 0.
     * - Merged intStats[k] = std::max(localVal, remoteVal).
     */
    AchievementStatBlob_t mergeAchievementStateUnion(
        const AchievementStatBlob_t& localState,
        const AchievementStatBlob_t& remoteState);

    /**
     * Serializes a BinaryKVNode tree into Valve Binary KeyValues format:
     * type byte + null-terminated name + value/children + 0x08 End byte, plus trailing 0x08 root terminator.
     */
    std::vector<uint8_t> serializeBinaryKV(const BinaryKVNode& root);

    /**
     * Parses BinaryKV or YAML fallback achievements into AchievementStatBlob_t.
     */
    bool parseAchievementBinaryKV(
        const std::vector<uint8_t>& rawBytes,
        AchievementStatBlob_t& outState);

    /**
     * Serializes AchievementStatBlob_t into BinaryKV UserGameStats binary buffer.
     */
    std::vector<uint8_t> serializeAchievementBinaryKV(
        const AchievementStatBlob_t& state);

    /**
     * Merges two achievement buffers directly via set union.
     */
    bool mergeAchievementBuffersUnion(
        const std::vector<uint8_t>& localBytes,
        const std::vector<uint8_t>& remoteBytes,
        std::vector<uint8_t>& outMergedBytes);
} // namespace Sync
