#include <gtest/gtest.h>

#include "sync_crypto.hpp"
#include "sync_manifest.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

class SyncCryptoTest : public ::testing::Test
{
protected:
    std::string m_testDir;

    void SetUp() override
    {
        m_testDir = "/tmp/unit_test_crypto_" + std::to_string(getpid()) + "_" +
                    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        fs::remove_all(m_testDir);
        fs::create_directories(m_testDir);
    }

    void TearDown() override
    {
        fs::remove_all(m_testDir);
    }
};

// ── Test 1: MasterKey generation & 0600 file persistence ─────────────────────

TEST_F(SyncCryptoTest, MasterKeyGenerationAndFilePersistence)
{
    Sync::MasterKey_t key1{};
    Sync::MasterKey_t key2{};

    EXPECT_TRUE(Sync::generateMasterKey(key1));
    EXPECT_TRUE(Sync::generateMasterKey(key2));

    // Must be non-zero and random (not equal)
    EXPECT_NE(key1, key2);

    std::string keyPath = m_testDir + "/master.key";

    // Save key
    EXPECT_TRUE(Sync::saveMasterKeyFile(keyPath, key1));

    // Verify 0600 permissions
    struct stat st{};
    ASSERT_EQ(stat(keyPath.c_str(), &st), 0);
    EXPECT_EQ(st.st_mode & 0777, 0600);

    // Ensure loads existing key
    Sync::MasterKey_t loadedKey{};
    EXPECT_TRUE(Sync::ensureMasterKeyFile(keyPath, loadedKey));
    EXPECT_EQ(loadedKey, key1);

    // Ensure on new file generates and creates with 0600
    std::string newKeyPath = m_testDir + "/new_master.key";
    Sync::MasterKey_t genKey{};
    EXPECT_TRUE(Sync::ensureMasterKeyFile(newKeyPath, genKey));
    EXPECT_NE(genKey, Sync::MasterKey_t{});

    struct stat st2{};
    ASSERT_EQ(stat(newKeyPath.c_str(), &st2), 0);
    EXPECT_EQ(st2.st_mode & 0777, 0600);
}

// ── Test 2: Crockford Base32 Recovery Key ─────────────────────────────────────

TEST_F(SyncCryptoTest, CrockfordBase32EncodingAndDecoding)
{
    Sync::MasterKey_t originalKey{};
    EXPECT_TRUE(Sync::generateMasterKey(originalKey));

    std::string recoveryStr = Sync::encodeRecoveryKey(originalKey);

    // 11 groups of 5 separated by 10 hyphens = 65 characters total
    EXPECT_EQ(recoveryStr.size(), 65);
    EXPECT_EQ(recoveryStr.find('-'), 5);

    // Decode round trip
    Sync::MasterKey_t decodedKey{};
    EXPECT_TRUE(Sync::decodeRecoveryKey(recoveryStr, decodedKey));
    EXPECT_EQ(decodedKey, originalKey);

    // Case-insensitivity (lowercase)
    std::string lowerStr = recoveryStr;
    for (char& c : lowerStr) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    Sync::MasterKey_t lowerDecoded{};
    EXPECT_TRUE(Sync::decodeRecoveryKey(lowerStr, lowerDecoded));
    EXPECT_EQ(lowerDecoded, originalKey);

    // Lookalike normalization: 'O'/'o' -> '0', 'I'/'i'/'L'/'l' -> '1'
    std::string modified = recoveryStr;
    // Replace any '0' with 'O' and any '1' with 'L' or 'I'
    for (char& c : modified)
    {
        if (c == '0') c = 'O';
        else if (c == '1') c = 'L';
    }
    Sync::MasterKey_t normDecoded{};
    EXPECT_TRUE(Sync::decodeRecoveryKey(modified, normDecoded));
    EXPECT_EQ(normDecoded, originalKey);

    // Rejection of disallowed 'U' / 'u'
    std::string withU = recoveryStr;
    // Replace first character with 'U'
    withU[0] = 'U';
    Sync::MasterKey_t uKey{};
    EXPECT_FALSE(Sync::decodeRecoveryKey(withU, uKey));
    EXPECT_EQ(uKey, Sync::MasterKey_t{});

    // Rejection of invalid length
    std::string truncated = recoveryStr.substr(0, 60);
    Sync::MasterKey_t truncKey{};
    EXPECT_FALSE(Sync::decodeRecoveryKey(truncated, truncKey));

    // CRC-16 error detection: single-char typo
    std::string typoStr = recoveryStr;
    // Change a non-hyphen character
    for (size_t i = 0; i < typoStr.size(); ++i)
    {
        if (typoStr[i] != '-')
        {
            typoStr[i] = (typoStr[i] == 'A') ? 'B' : 'A';
            break;
        }
    }
    Sync::MasterKey_t typoKey{};
    EXPECT_FALSE(Sync::decodeRecoveryKey(typoStr, typoKey));
    EXPECT_EQ(typoKey, Sync::MasterKey_t{});

    // CRC-16 error detection: transposition of adjacent characters
    std::string transpStr = recoveryStr;
    std::swap(transpStr[0], transpStr[1]);
    Sync::MasterKey_t transpKey{};
    EXPECT_FALSE(Sync::decodeRecoveryKey(transpStr, transpKey));
    EXPECT_EQ(transpKey, Sync::MasterKey_t{});
}

// ── Test 3: HKDF-SHA256 Subkey Derivation ────────────────────────────────────

TEST_F(SyncCryptoTest, HKDFSubkeyDerivation)
{
    Sync::MasterKey_t masterKey{};
    EXPECT_TRUE(Sync::generateMasterKey(masterKey));

    uint8_t salt[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    std::string info1 = "navi-sync-v1-test1";
    std::string info2 = "navi-sync-v1-test2";

    std::array<uint8_t, 32> subkey1a{};
    std::array<uint8_t, 32> subkey1b{};
    std::array<uint8_t, 32> subkey2{};

    EXPECT_TRUE(Sync::deriveSubkey(masterKey, salt, sizeof(salt), info1, subkey1a));
    EXPECT_TRUE(Sync::deriveSubkey(masterKey, salt, sizeof(salt), info1, subkey1b));
    EXPECT_TRUE(Sync::deriveSubkey(masterKey, salt, sizeof(salt), info2, subkey2));

    // Determinism
    EXPECT_EQ(subkey1a, subkey1b);

    // Domain separation
    EXPECT_NE(subkey1a, subkey2);
}

// ── Test 4: AEAD Envelope Encryption Round-Trip ──────────────────────────────

TEST_F(SyncCryptoTest, AEADEnvelopeRoundTripAndPayloadPreservation)
{
    Sync::MasterKey_t masterKey{};
    EXPECT_TRUE(Sync::generateMasterKey(masterKey));

    Sync::EncryptedEnvelopeMetadata_t meta;
    meta.steamId64 = 76561198000000001ULL;
    meta.keyEpoch = 2;
    meta.category = Sync::ESyncCategory::k_ManifestsAndLua;
    meta.logicalPath = "steamapps://appmanifest_440.acf";
    meta.generation = 10;

    std::string text = "AppState { appid: 440, name: Team Fortress 2, installed: 1 }";
    std::vector<uint8_t> plaintext(text.begin(), text.end());

    std::vector<uint8_t> blob;
    EXPECT_TRUE(Sync::encryptBlob(masterKey, meta, plaintext, blob));

    // Envelope header: 50 bytes + plaintext + 16 bytes tag
    EXPECT_EQ(blob.size(), 50 + plaintext.size() + 16);
    // Wire magic check
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(blob.data()), 8), "NAVIENC1");

    // Decrypt
    std::vector<uint8_t> decrypted;
    EXPECT_TRUE(Sync::decryptBlob(masterKey, meta, blob, decrypted));
    EXPECT_EQ(decrypted, plaintext);

    // Empty plaintext round trip
    std::vector<uint8_t> emptyPlain;
    std::vector<uint8_t> emptyBlob;
    EXPECT_TRUE(Sync::encryptBlob(masterKey, meta, emptyPlain, emptyBlob));
    EXPECT_EQ(emptyBlob.size(), 66);

    std::vector<uint8_t> emptyDecrypted;
    EXPECT_TRUE(Sync::decryptBlob(masterKey, meta, emptyBlob, emptyDecrypted));
    EXPECT_TRUE(emptyDecrypted.empty());
}

// ── Test 5: AEAD Envelope Tamper & Mismatch Rejection ─────────────────────────

TEST_F(SyncCryptoTest, AEADEnvelopeTamperRejection)
{
    Sync::MasterKey_t masterKey{};
    EXPECT_TRUE(Sync::generateMasterKey(masterKey));

    Sync::EncryptedEnvelopeMetadata_t meta;
    meta.steamId64 = 76561198000000002ULL;
    meta.keyEpoch = 1;
    meta.category = Sync::ESyncCategory::k_Configs;
    meta.logicalPath = "configs://settings.json";
    meta.generation = 5;

    std::string text = "Sensitive user configuration blob";
    std::vector<uint8_t> plaintext(text.begin(), text.end());

    std::vector<uint8_t> blob;
    ASSERT_TRUE(Sync::encryptBlob(masterKey, meta, plaintext, blob));

    // 1. Bit flip in ciphertext
    {
        std::vector<uint8_t> tampered = blob;
        tampered[52] ^= 0x40; // in ciphertext region
        std::vector<uint8_t> out;
        EXPECT_FALSE(Sync::decryptBlob(masterKey, meta, tampered, out));
        EXPECT_TRUE(out.empty());
    }

    // 2. Bit flip in GCM tag
    {
        std::vector<uint8_t> tampered = blob;
        tampered.back() ^= 0x01; // last byte of tag
        std::vector<uint8_t> out;
        EXPECT_FALSE(Sync::decryptBlob(masterKey, meta, tampered, out));
        EXPECT_TRUE(out.empty());
    }

    // 3. SteamID mismatch in expectedMeta (AAD)
    {
        Sync::EncryptedEnvelopeMetadata_t wrongMeta = meta;
        wrongMeta.steamId64 = 76561198000000999ULL;
        std::vector<uint8_t> out;
        EXPECT_FALSE(Sync::decryptBlob(masterKey, wrongMeta, blob, out));
        EXPECT_TRUE(out.empty());
    }

    // 4. LogicalPath mismatch in expectedMeta (AAD)
    {
        Sync::EncryptedEnvelopeMetadata_t wrongMeta = meta;
        wrongMeta.logicalPath = "configs://tampered.json";
        std::vector<uint8_t> out;
        EXPECT_FALSE(Sync::decryptBlob(masterKey, wrongMeta, blob, out));
        EXPECT_TRUE(out.empty());
    }

    // 5. Generation mismatch in expectedMeta (AAD)
    {
        Sync::EncryptedEnvelopeMetadata_t wrongMeta = meta;
        wrongMeta.generation = 999;
        std::vector<uint8_t> out;
        EXPECT_FALSE(Sync::decryptBlob(masterKey, wrongMeta, blob, out));
        EXPECT_TRUE(out.empty());
    }

    // 6. KeyEpoch mismatch in expectedMeta (AAD + Subkey)
    {
        Sync::EncryptedEnvelopeMetadata_t wrongMeta = meta;
        wrongMeta.keyEpoch = 2;
        std::vector<uint8_t> out;
        EXPECT_FALSE(Sync::decryptBlob(masterKey, wrongMeta, blob, out));
        EXPECT_TRUE(out.empty());
    }

    // 7. Wrong master key
    {
        Sync::MasterKey_t otherMasterKey{};
        EXPECT_TRUE(Sync::generateMasterKey(otherMasterKey));
        std::vector<uint8_t> out;
        EXPECT_FALSE(Sync::decryptBlob(otherMasterKey, meta, blob, out));
        EXPECT_TRUE(out.empty());
    }

    // 8. Truncated blob (< 66 bytes)
    {
        std::vector<uint8_t> truncated(blob.begin(), blob.begin() + 45);
        std::vector<uint8_t> out;
        EXPECT_FALSE(Sync::decryptBlob(masterKey, meta, truncated, out));
        EXPECT_TRUE(out.empty());
    }
}

// ── Test 6: API Key Convenience Helpers ──────────────────────────────────────

TEST_F(SyncCryptoTest, ApiKeyConvenienceHelpers)
{
    Sync::MasterKey_t masterKey{};
    EXPECT_TRUE(Sync::generateMasterKey(masterKey));

    uint64_t steamId64 = 76561198000000003ULL;
    uint32_t epoch = 1;
    std::string provider = "steamgriddb";
    uint64_t generation = 7;
    std::string secretKey = "sgdb_api_key_9876543210abcdef";

    std::vector<uint8_t> blob;
    EXPECT_TRUE(Sync::encryptApiKey(masterKey, steamId64, epoch, provider, generation, secretKey, blob));
    EXPECT_FALSE(blob.empty());

    std::string decryptedKey;
    EXPECT_TRUE(Sync::decryptApiKey(masterKey, steamId64, epoch, provider, generation, blob, decryptedKey));
    EXPECT_EQ(decryptedKey, secretKey);

    // Mismatched provider name fails closed
    std::string badKey;
    EXPECT_FALSE(Sync::decryptApiKey(masterKey, steamId64, epoch, "otherprovider", generation, blob, badKey));
    EXPECT_TRUE(badKey.empty());
}
