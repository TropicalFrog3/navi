/**
 * @file  test_patterns_and_utils.cpp
 * @brief Unit tests for pattern matching, string utilities, and hashing
 */
#include <gtest/gtest.h>
#include "utils.hpp"
#include "patterns.hpp"
#include "memhlp.hpp"
#include "decompiler.hpp"
#include "vftableinfo.hpp"
#include "atomic_file.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// ==========================================
// Utils Tests
// ==========================================

TEST(UtilsTest, IsNumber)
{
    EXPECT_TRUE(Utils::isNumber("123456"));
    EXPECT_TRUE(Utils::isNumber("0"));
    EXPECT_FALSE(Utils::isNumber(""));
    EXPECT_FALSE(Utils::isNumber("123a45"));
    EXPECT_FALSE(Utils::isNumber("-10"));
    EXPECT_FALSE(Utils::isNumber(" 123"));
}

TEST(UtilsTest, TryConvertToNumber)
{
    int32_t iVal = 0;
    EXPECT_TRUE(Utils::tryConvertToNumber("12345", iVal));
    EXPECT_EQ(iVal, 12345);

    EXPECT_FALSE(Utils::tryConvertToNumber("abc", iVal));

    uint32_t uVal = 0;
    EXPECT_TRUE(Utils::tryConvertToNumber("4294967295", uVal));
    EXPECT_EQ(uVal, 4294967295u);

    EXPECT_FALSE(Utils::tryConvertToNumber("", uVal));
}

TEST(UtilsTest, Strsplit)
{
    char str[] = "apple,banana,orange,grape";
    auto tokens = Utils::strsplit(str, ",");
    ASSERT_EQ(tokens.size(), 4u);
    EXPECT_EQ(tokens[0], "apple");
    EXPECT_EQ(tokens[1], "banana");
    EXPECT_EQ(tokens[2], "orange");
    EXPECT_EQ(tokens[3], "grape");
}

TEST(UtilsTest, GetFileSHA256)
{
    const std::string tempDir = "/tmp/unit_test_sha_" + std::to_string(getpid());
    fs::remove_all(tempDir);
    fs::create_directories(tempDir);

    const std::string filePath = tempDir + "/test_sha.txt";
    // echo -n "hello world" | sha256sum -> b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9
    ASSERT_TRUE(AtomicFile::write(filePath, "hello world"));

    std::string hash = Utils::getFileSHA256(filePath.c_str());
    EXPECT_EQ(hash, "b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9");

    fs::remove_all(tempDir);
}

// ==========================================
// Patterns & Memory Helpers Tests
// ==========================================

TEST(PatternsTest, PatternToBytes)
{
    auto bytes = MemHlp::patternToBytes("55 89 E5 ? ? 83 C4 10");
    ASSERT_EQ(bytes.size(), 8u);
    EXPECT_EQ(bytes[0], 0x55);
    EXPECT_EQ(bytes[1], 0x89);
    EXPECT_EQ(bytes[2], 0xE5);
    EXPECT_EQ(bytes[3], -1);
    EXPECT_EQ(bytes[4], -1);
    EXPECT_EQ(bytes[5], 0x83);
    EXPECT_EQ(bytes[6], 0xC4);
    EXPECT_EQ(bytes[7], 0x10);
}

TEST(PatternsTest, HexdumpOutput)
{
    uint8_t buffer[16] = {
        0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x20, 0x57, 0x6f,
        0x72, 0x6c, 0x64, 0x21, 0x00, 0x01, 0x02, 0x03
    };

    std::string dump = MemHlp::hexdump(buffer, sizeof(buffer));
    EXPECT_FALSE(dump.empty());
    EXPECT_NE(dump.find("Hello World!"), std::string::npos);
}

// ==========================================
// Decompiler & VFTable Fallback Tests
// ==========================================

TEST(DecompilerTest, GetStringAndExtractHex)
{
    char syntheticBuffer[] = "SampleStringIdentifier\0TrailingJunk";
    std::string extracted;
    unsigned int readCount = Decompiler::getString(reinterpret_cast<lm_address_t>(syntheticBuffer), &extracted);
    EXPECT_EQ(extracted, "SampleStringIdentifier");
    EXPECT_EQ(readCount, 23u); // 22 chars + 1 null terminator

    lm_address_t hexVal = Decompiler::extractHexNum("prefix_0x1A2B_suffix");
    EXPECT_EQ(hexVal, 0x1A2Bu);
}

TEST(VFTableInfoTest, FallbackAndOptionalHandling)
{
    // VFTableInfo with hasFallback = true should succeed even if table is missing
    VFTableInfo_t fallbackInfo("NonExistentTable", "NonExistentFunc", VFTableInfo_t::NO_INDEX, false, true);
    EXPECT_TRUE(fallbackInfo.init());
    EXPECT_EQ(fallbackInfo.address, 0u);
    EXPECT_EQ(fallbackInfo.index, VFTableInfo_t::NO_INDEX);

    // VFTableInfo with optional = true should succeed when missing
    VFTableInfo_t optionalInfo("MissingOptionalTable", "MissingFunc", VFTableInfo_t::NO_INDEX, true, false);
    EXPECT_TRUE(optionalInfo.init());

    // VFTableInfo required (non-optional, no fallback) should fail when table is absent
    VFTableInfo_t requiredInfo("MissingRequiredTable", "MissingFunc", VFTableInfo_t::NO_INDEX, false, false);
    EXPECT_FALSE(requiredInfo.init());
}
