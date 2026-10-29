#pragma once

#include "sync_manifest.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Sync
{
    using MasterKey_t = std::array<uint8_t, 32>;

    // Master Key Generation & File Persistence
    bool generateMasterKey(MasterKey_t& outKey);
    bool ensureMasterKeyFile(const std::string& keyFilePath, MasterKey_t& outKey);
    bool saveMasterKeyFile(const std::string& keyFilePath, const MasterKey_t& key);

    // Crockford Base32 Recovery Key (Cloud-Only Setup UX)
    uint16_t computeCrc16Ccitt(const uint8_t* data, size_t len);
    std::string encodeRecoveryKey(const MasterKey_t& key);
    bool decodeRecoveryKey(std::string_view recoveryKeyStr, MasterKey_t& outKey);

    // HKDF-SHA256 Subkey Derivation
    bool deriveSubkey(const MasterKey_t& masterKey,
                      const uint8_t* salt,
                      size_t saltLen,
                      std::string_view info,
                      std::array<uint8_t, 32>& outSubkey);

    // AEAD Envelope Encryption (AES-256-GCM)
    struct EncryptedEnvelopeMetadata_t
    {
        uint64_t steamId64 = 0;
        uint32_t keyEpoch = 1;
        ESyncCategory category = ESyncCategory::k_ManifestsAndLua;
        std::string logicalPath;
        uint64_t generation = 0;
    };

    bool encryptBlob(const MasterKey_t& masterKey,
                     const EncryptedEnvelopeMetadata_t& meta,
                     const std::vector<uint8_t>& plaintext,
                     std::vector<uint8_t>& outBlob);

    bool decryptBlob(const MasterKey_t& masterKey,
                     const EncryptedEnvelopeMetadata_t& expectedMeta,
                     const std::vector<uint8_t>& blob,
                     std::vector<uint8_t>& outPlaintext);

    // Convenience Helpers for API Keys
    bool encryptApiKey(const MasterKey_t& masterKey,
                       uint64_t steamId64,
                       uint32_t keyEpoch,
                       const std::string& providerName,
                       uint64_t generation,
                       const std::string& plaintextApiKey,
                       std::vector<uint8_t>& outBlob);

    bool decryptApiKey(const MasterKey_t& masterKey,
                       uint64_t steamId64,
                       uint32_t keyEpoch,
                       const std::string& providerName,
                       uint64_t generation,
                       const std::vector<uint8_t>& blob,
                       std::string& outPlaintextApiKey);

} // namespace Sync
