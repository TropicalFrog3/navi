#include "sync_crypto.hpp"
#include "atomic_file.hpp"

#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/rand.h>

namespace fs = std::filesystem;

namespace Sync
{
    struct CleanseGuard
    {
        void* ptr;
        size_t len;
        CleanseGuard(void* p, size_t l) : ptr(p), len(l) {}
        ~CleanseGuard()
        {
            if (ptr && len > 0)
            {
                OPENSSL_cleanse(ptr, len);
            }
        }
    };

    static const char CROCKFORD_ALPHABET[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

    bool generateMasterKey(MasterKey_t& outKey)
    {
        return RAND_bytes(outKey.data(), static_cast<int>(outKey.size())) == 1;
    }

    bool saveMasterKeyFile(const std::string& keyFilePath, const MasterKey_t& key)
    {
        if (keyFilePath.empty()) return false;
        std::string_view sv(reinterpret_cast<const char*>(key.data()), key.size());
        if (!AtomicFile::write(keyFilePath, sv, true)) return false;
        chmod(keyFilePath.c_str(), 0600);
        return true;
    }

    bool ensureMasterKeyFile(const std::string& keyFilePath, MasterKey_t& outKey)
    {
        if (keyFilePath.empty()) return false;

        std::error_code ec;
        if (fs::exists(keyFilePath, ec))
        {
            std::ifstream ifs(keyFilePath, std::ios::binary);
            if (ifs.is_open())
            {
                std::string content((std::istreambuf_iterator<char>(ifs)),
                                    std::istreambuf_iterator<char>());
                if (content.size() == 32)
                {
                    std::memcpy(outKey.data(), content.data(), 32);
                    chmod(keyFilePath.c_str(), 0600);
                    return true;
                }
                if (decodeRecoveryKey(content, outKey))
                {
                    chmod(keyFilePath.c_str(), 0600);
                    return true;
                }
            }
        }

        if (!generateMasterKey(outKey))
        {
            return false;
        }

        return saveMasterKeyFile(keyFilePath, outKey);
    }

    uint16_t computeCrc16Ccitt(const uint8_t* data, size_t len)
    {
        uint16_t crc = 0xFFFF;
        if (!data) return crc;
        for (size_t i = 0; i < len; ++i)
        {
            crc ^= static_cast<uint16_t>(data[i]) << 8;
            for (int b = 0; b < 8; ++b)
            {
                if (crc & 0x8000)
                    crc = static_cast<uint16_t>((crc << 1) ^ 0x1021);
                else
                    crc = static_cast<uint16_t>(crc << 1);
            }
        }
        return crc;
    }

    std::string encodeRecoveryKey(const MasterKey_t& key)
    {
        uint8_t raw[35] = {0};
        CleanseGuard rawGuard(raw, sizeof(raw));
        std::memcpy(raw, key.data(), 32);

        uint16_t crc = computeCrc16Ccitt(key.data(), 32);
        raw[32] = static_cast<uint8_t>((crc >> 8) & 0xFF);
        raw[33] = static_cast<uint8_t>(crc & 0xFF);
        raw[34] = 0;

        std::string result;
        result.reserve(65);

        for (size_t i = 0; i < 55; ++i)
        {
            if (i > 0 && i % 5 == 0)
            {
                result.push_back('-');
            }

            size_t bitOffset = i * 5;
            size_t byteIdx = bitOffset / 8;
            size_t bitInByte = bitOffset % 8;

            uint16_t w = static_cast<uint16_t>(raw[byteIdx]) << 8;
            w |= static_cast<uint16_t>(raw[byteIdx + 1]);

            uint8_t sym = static_cast<uint8_t>((w >> (11 - bitInByte)) & 0x1F);
            result.push_back(CROCKFORD_ALPHABET[sym]);
        }

        return result;
    }

    static uint8_t crockfordCharToVal(char c, bool& ok)
    {
        ok = true;
        switch (c)
        {
            case '0': case 'o': case 'O': return 0;
            case '1': case 'i': case 'I': case 'l': case 'L': return 1;
            case '2': return 2;
            case '3': return 3;
            case '4': return 4;
            case '5': return 5;
            case '6': return 6;
            case '7': return 7;
            case '8': return 8;
            case '9': return 9;
            case 'a': case 'A': return 10;
            case 'b': case 'B': return 11;
            case 'c': case 'C': return 12;
            case 'd': case 'D': return 13;
            case 'e': case 'E': return 14;
            case 'f': case 'F': return 15;
            case 'g': case 'G': return 16;
            case 'h': case 'H': return 17;
            case 'j': case 'J': return 18;
            case 'k': case 'K': return 19;
            case 'm': case 'M': return 20;
            case 'n': case 'N': return 21;
            case 'p': case 'P': return 22;
            case 'q': case 'Q': return 23;
            case 'r': case 'R': return 24;
            case 's': case 'S': return 25;
            case 't': case 'T': return 26;
            case 'v': case 'V': return 27;
            case 'w': case 'W': return 28;
            case 'x': case 'X': return 29;
            case 'y': case 'Y': return 30;
            case 'z': case 'Z': return 31;
            // 'u'/'U' or other characters are disallowed
            default:
                ok = false;
                return 0;
        }
    }

    bool decodeRecoveryKey(std::string_view recoveryKeyStr, MasterKey_t& outKey)
    {
        std::string clean;
        clean.reserve(55);
        for (char c : recoveryKeyStr)
        {
            if (c == '-' || std::isspace(static_cast<unsigned char>(c)))
            {
                continue;
            }
            clean.push_back(c);
        }

        if (clean.size() != 55)
        {
            outKey.fill(0);
            return false;
        }

        uint8_t raw[35] = {0};
        CleanseGuard rawGuard(raw, sizeof(raw));
        for (size_t i = 0; i < 55; ++i)
        {
            bool ok = false;
            uint8_t val = crockfordCharToVal(clean[i], ok);
            if (!ok)
            {
                outKey.fill(0);
                return false;
            }

            size_t bitOffset = i * 5;
            size_t byteIdx = bitOffset / 8;
            size_t bitInByte = bitOffset % 8;

            uint16_t w = static_cast<uint16_t>(val) << (11 - bitInByte);
            raw[byteIdx] |= static_cast<uint8_t>(w >> 8);
            raw[byteIdx + 1] |= static_cast<uint8_t>(w & 0xFF);
        }

        // Verify the 3 trailing pad bits are 0
        if ((raw[34] & 0xE0) != 0)
        {
            outKey.fill(0);
            return false;
        }

        uint16_t expectedCrc = (static_cast<uint16_t>(raw[32]) << 8) | static_cast<uint16_t>(raw[33]);
        uint16_t computedCrc = computeCrc16Ccitt(raw, 32);
        if (expectedCrc != computedCrc)
        {
            outKey.fill(0);
            return false;
        }

        std::memcpy(outKey.data(), raw, 32);
        return true;
    }

    bool deriveSubkey(const MasterKey_t& masterKey,
                      const uint8_t* salt,
                      size_t saltLen,
                      std::string_view info,
                      std::array<uint8_t, 32>& outSubkey)
    {
        EVP_PKEY_CTX* pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);
        if (!pctx) return false;

        if (EVP_PKEY_derive_init(pctx) <= 0 ||
            EVP_PKEY_CTX_set_hkdf_md(pctx, EVP_sha256()) <= 0 ||
            EVP_PKEY_CTX_set1_hkdf_key(pctx, masterKey.data(), static_cast<int>(masterKey.size())) <= 0)
        {
            EVP_PKEY_CTX_free(pctx);
            return false;
        }

        if (salt && saltLen > 0)
        {
            if (EVP_PKEY_CTX_set1_hkdf_salt(pctx, salt, static_cast<int>(saltLen)) <= 0)
            {
                EVP_PKEY_CTX_free(pctx);
                return false;
            }
        }

        if (!info.empty())
        {
            if (EVP_PKEY_CTX_add1_hkdf_info(pctx,
                                            reinterpret_cast<const unsigned char*>(info.data()),
                                            static_cast<int>(info.size())) <= 0)
            {
                EVP_PKEY_CTX_free(pctx);
                return false;
            }
        }

        size_t outLen = outSubkey.size();
        if (EVP_PKEY_derive(pctx, outSubkey.data(), &outLen) <= 0 || outLen != 32)
        {
            EVP_PKEY_CTX_free(pctx);
            return false;
        }

        EVP_PKEY_CTX_free(pctx);
        return true;
    }

    static std::vector<uint8_t> buildCanonicalAad(const EncryptedEnvelopeMetadata_t& meta)
    {
        std::vector<uint8_t> aad;
        // Magic "NAVIENC1" (8 bytes)
        const uint8_t magic[8] = {'N', 'A', 'V', 'I', 'E', 'N', 'C', '1'};
        aad.insert(aad.end(), magic, magic + 8);

        // Version uint8_t(1)
        aad.push_back(1);

        // keyEpoch uint32_t little-endian (4 bytes)
        uint32_t epoch = meta.keyEpoch;
        for (int i = 0; i < 4; ++i)
        {
            aad.push_back(static_cast<uint8_t>(epoch & 0xFF));
            epoch >>= 8;
        }

        // category uint8_t (1 byte)
        aad.push_back(static_cast<uint8_t>(meta.category));

        // steamId64 uint64_t little-endian (8 bytes)
        uint64_t sid = meta.steamId64;
        for (int i = 0; i < 8; ++i)
        {
            aad.push_back(static_cast<uint8_t>(sid & 0xFF));
            sid >>= 8;
        }

        // generation uint64_t little-endian (8 bytes)
        uint64_t gen = meta.generation;
        for (int i = 0; i < 8; ++i)
        {
            aad.push_back(static_cast<uint8_t>(gen & 0xFF));
            gen >>= 8;
        }

        // logicalPath raw bytes
        aad.insert(aad.end(), meta.logicalPath.begin(), meta.logicalPath.end());

        return aad;
    }

    bool encryptBlob(const MasterKey_t& masterKey,
                     const EncryptedEnvelopeMetadata_t& meta,
                     const std::vector<uint8_t>& plaintext,
                     std::vector<uint8_t>& outBlob)
    {
        uint8_t salt[16];
        if (RAND_bytes(salt, sizeof(salt)) != 1)
        {
            return false;
        }

        uint8_t iv[12];
        if (RAND_bytes(iv, sizeof(iv)) != 1)
        {
            return false;
        }

        std::string info = "navi-sync-v1-" + std::to_string(meta.steamId64) +
                           "-" + std::to_string(meta.keyEpoch) +
                           "-" + std::to_string(static_cast<uint8_t>(meta.category));

        std::array<uint8_t, 32> subkey;
        CleanseGuard subkeyGuard(subkey.data(), subkey.size());
        if (!deriveSubkey(masterKey, salt, sizeof(salt), info, subkey))
        {
            return false;
        }

        std::vector<uint8_t> aad = buildCanonicalAad(meta);

        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx)
        {
            return false;
        }

        if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) <= 0 ||
            EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, sizeof(iv), nullptr) <= 0 ||
            EVP_EncryptInit_ex(ctx, nullptr, nullptr, subkey.data(), iv) <= 0)
        {
            EVP_CIPHER_CTX_free(ctx);
            return false;
        }

        int len = 0;
        if (!aad.empty())
        {
            if (EVP_EncryptUpdate(ctx, nullptr, &len, aad.data(), static_cast<int>(aad.size())) <= 0)
            {
                EVP_CIPHER_CTX_free(ctx);
                return false;
            }
        }

        std::vector<uint8_t> ciphertext(plaintext.size());
        int outlen = 0;
        if (!plaintext.empty())
        {
            if (EVP_EncryptUpdate(ctx, ciphertext.data(), &outlen, plaintext.data(), static_cast<int>(plaintext.size())) <= 0)
            {
                EVP_CIPHER_CTX_free(ctx);
                return false;
            }
        }

        int finalLen = 0;
        if (EVP_EncryptFinal_ex(ctx, ciphertext.data() + outlen, &finalLen) <= 0)
        {
            EVP_CIPHER_CTX_free(ctx);
            return false;
        }
        ciphertext.resize(outlen + finalLen);

        uint8_t gcmTag[16];
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, sizeof(gcmTag), gcmTag) <= 0)
        {
            EVP_CIPHER_CTX_free(ctx);
            return false;
        }

        EVP_CIPHER_CTX_free(ctx);

        // Assemble wire format:
        // Magic (8) + Version (1) + keyEpoch (4 LE) + category (1) + steamId64 (8 LE) +
        // salt (16) + iv (12) + ciphertext (N) + gcmTag (16)
        outBlob.clear();
        outBlob.reserve(50 + ciphertext.size() + 16);

        const uint8_t magic[8] = {'N', 'A', 'V', 'I', 'E', 'N', 'C', '1'};
        outBlob.insert(outBlob.end(), magic, magic + 8);
        outBlob.push_back(1);

        uint32_t epoch = meta.keyEpoch;
        for (int i = 0; i < 4; ++i)
        {
            outBlob.push_back(static_cast<uint8_t>(epoch & 0xFF));
            epoch >>= 8;
        }

        outBlob.push_back(static_cast<uint8_t>(meta.category));

        uint64_t sid = meta.steamId64;
        for (int i = 0; i < 8; ++i)
        {
            outBlob.push_back(static_cast<uint8_t>(sid & 0xFF));
            sid >>= 8;
        }

        outBlob.insert(outBlob.end(), salt, salt + sizeof(salt));
        outBlob.insert(outBlob.end(), iv, iv + sizeof(iv));
        outBlob.insert(outBlob.end(), ciphertext.begin(), ciphertext.end());
        outBlob.insert(outBlob.end(), gcmTag, gcmTag + sizeof(gcmTag));

        return true;
    }

    bool decryptBlob(const MasterKey_t& masterKey,
                     const EncryptedEnvelopeMetadata_t& expectedMeta,
                     const std::vector<uint8_t>& blob,
                     std::vector<uint8_t>& outPlaintext)
    {
        outPlaintext.clear();

        // Minimum blob size: 50 header + 0 ciphertext + 16 tag = 66 bytes
        if (blob.size() < 66)
        {
            return false;
        }

        // Verify Magic "NAVIENC1"
        const uint8_t expectedMagic[8] = {'N', 'A', 'V', 'I', 'E', 'N', 'C', '1'};
        if (std::memcmp(blob.data(), expectedMagic, 8) != 0)
        {
            return false;
        }

        // Verify Version 1
        if (blob[8] != 1)
        {
            return false;
        }

        // Extract header fields
        uint32_t blobEpoch = 0;
        for (int i = 0; i < 4; ++i)
        {
            blobEpoch |= (static_cast<uint32_t>(blob[9 + i]) << (i * 8));
        }

        uint8_t blobCategory = blob[13];

        uint64_t blobSteamId64 = 0;
        for (int i = 0; i < 8; ++i)
        {
            blobSteamId64 |= (static_cast<uint64_t>(blob[14 + i]) << (i * 8));
        }

        if (blobEpoch != expectedMeta.keyEpoch ||
            blobCategory != static_cast<uint8_t>(expectedMeta.category) ||
            blobSteamId64 != expectedMeta.steamId64)
        {
            return false;
        }

        const uint8_t* salt = blob.data() + 22;
        const uint8_t* iv = blob.data() + 38;

        size_t cipherLen = blob.size() - 50 - 16;
        const uint8_t* cipherPtr = blob.data() + 50;
        const uint8_t* tagPtr = blob.data() + blob.size() - 16;

        std::string info = "navi-sync-v1-" + std::to_string(expectedMeta.steamId64) +
                           "-" + std::to_string(expectedMeta.keyEpoch) +
                           "-" + std::to_string(static_cast<uint8_t>(expectedMeta.category));

        std::array<uint8_t, 32> subkey;
        CleanseGuard subkeyGuard(subkey.data(), subkey.size());
        if (!deriveSubkey(masterKey, salt, 16, info, subkey))
        {
            return false;
        }

        std::vector<uint8_t> aad = buildCanonicalAad(expectedMeta);

        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx)
        {
            return false;
        }

        if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) <= 0 ||
            EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, nullptr) <= 0 ||
            EVP_DecryptInit_ex(ctx, nullptr, nullptr, subkey.data(), iv) <= 0 ||
            EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, const_cast<uint8_t*>(tagPtr)) <= 0)
        {
            EVP_CIPHER_CTX_free(ctx);
            return false;
        }

        int len = 0;
        if (!aad.empty())
        {
            if (EVP_DecryptUpdate(ctx, nullptr, &len, aad.data(), static_cast<int>(aad.size())) <= 0)
            {
                EVP_CIPHER_CTX_free(ctx);
                return false;
            }
        }

        outPlaintext.resize(cipherLen);
        int outlen = 0;
        if (cipherLen > 0)
        {
            if (EVP_DecryptUpdate(ctx, outPlaintext.data(), &outlen, cipherPtr, static_cast<int>(cipherLen)) <= 0)
            {
                EVP_CIPHER_CTX_free(ctx);
                if (!outPlaintext.empty()) OPENSSL_cleanse(outPlaintext.data(), outPlaintext.size());
                outPlaintext.clear();
                return false;
            }
        }

        int finalLen = 0;
        int ret = EVP_DecryptFinal_ex(ctx, outPlaintext.data() + outlen, &finalLen);
        EVP_CIPHER_CTX_free(ctx);

        if (ret <= 0)
        {
            if (!outPlaintext.empty()) OPENSSL_cleanse(outPlaintext.data(), outPlaintext.size());
            outPlaintext.clear();
            return false;
        }

        outPlaintext.resize(outlen + finalLen);
        return true;
    }

    bool encryptApiKey(const MasterKey_t& masterKey,
                       uint64_t steamId64,
                       uint32_t keyEpoch,
                       const std::string& providerName,
                       uint64_t generation,
                       const std::string& plaintextApiKey,
                       std::vector<uint8_t>& outBlob)
    {
        EncryptedEnvelopeMetadata_t meta;
        meta.steamId64 = steamId64;
        meta.keyEpoch = keyEpoch;
        meta.category = ESyncCategory::k_ApiKeys;
        meta.logicalPath = "apikeys://" + providerName;
        meta.generation = generation;

        std::vector<uint8_t> plain(plaintextApiKey.begin(), plaintextApiKey.end());
        return encryptBlob(masterKey, meta, plain, outBlob);
    }

    bool decryptApiKey(const MasterKey_t& masterKey,
                       uint64_t steamId64,
                       uint32_t keyEpoch,
                       const std::string& providerName,
                       uint64_t generation,
                       const std::vector<uint8_t>& blob,
                       std::string& outPlaintextApiKey)
    {
        EncryptedEnvelopeMetadata_t meta;
        meta.steamId64 = steamId64;
        meta.keyEpoch = keyEpoch;
        meta.category = ESyncCategory::k_ApiKeys;
        meta.logicalPath = "apikeys://" + providerName;
        meta.generation = generation;

        std::vector<uint8_t> plain;
        if (!decryptBlob(masterKey, meta, blob, plain))
        {
            outPlaintextApiKey.clear();
            return false;
        }

        outPlaintextApiKey.assign(plain.begin(), plain.end());
        return true;
    }

} // namespace Sync
