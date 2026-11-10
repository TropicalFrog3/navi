/**
 * @file  test_depotkeys.cpp
 * @brief Unit tests for depot key decryption and storage lookup
 */
#include <gtest/gtest.h>
#include "binarykv.hpp"
#include "depotkeys.hpp"
#include "luagenerator.hpp"
#include "config.hpp"
#include "paths.hpp"
#include "test_stubs.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <cstring>

namespace fs = std::filesystem;

static void appendString(std::vector<char>& buf, const std::string& str)
{
    buf.insert(buf.end(), str.begin(), str.end());
    buf.push_back('\0');
}

class DepotKeysTest : public ::testing::Test
{
protected:
    std::string m_tempDir;
    std::string m_origHome;
    std::string m_origXdg;
    bool m_hadXdg = false;

    void SetUp() override
    {
        m_tempDir = "/tmp/unit_test_depotkeys_" + std::to_string(getpid());
        fs::remove_all(m_tempDir);
        fs::create_directories(m_tempDir);

        const char* h = getenv("HOME");
        m_origHome = h ? h : "";

        const char* x = getenv("XDG_CONFIG_HOME");
        if (x)
        {
            m_hadXdg = true;
            m_origXdg = x;
        }

        setenv("HOME", m_tempDir.c_str(), 1);
        unsetenv("XDG_CONFIG_HOME");

        // Create standard SteamRoot and stplug-in directory
        std::string steamDir = m_tempDir + "/.local/share/Steam";
        fs::create_directories(steamDir + "/steamapps");
        fs::create_directories(steamDir + "/config/stplug-in");
        fs::create_directories(m_tempDir + "/.config/navi");
    }

    void TearDown() override
    {
        clearMockDepotsSection();
        if (!m_origHome.empty())
        {
            setenv("HOME", m_origHome.c_str(), 1);
        }
        if (m_hadXdg)
        {
            setenv("XDG_CONFIG_HOME", m_origXdg.c_str(), 1);
        }
        else
        {
            unsetenv("XDG_CONFIG_HOME");
        }
        fs::remove_all(m_tempDir);
    }
};

TEST_F(DepotKeysTest, BinaryKVParser)
{
    std::vector<char> buf;

    // Root node: 0x00 "depots"
    buf.push_back(0x00);
    appendString(buf, "depots");

        // Depot 3962421 node: 0x00 "3962421"
        buf.push_back(0x00);
        appendString(buf, "3962421");

            // Name: 0x01 "name" "Clair Obscur Content"
            buf.push_back(0x01);
            appendString(buf, "name");
            appendString(buf, "Clair Obscur Content");

            // Maxsize: 0x07 "maxsize" (8 bytes uint64)
            buf.push_back(0x07);
            appendString(buf, "maxsize");
            uint64_t maxsize = 45812938472ULL;
            char szBytes[8];
            std::memcpy(szBytes, &maxsize, sizeof(maxsize));
            buf.insert(buf.end(), szBytes, szBytes + 8);

            // Manifests: 0x00 "manifests"
            buf.push_back(0x00);
            appendString(buf, "manifests");

                // Public: 0x00 "public"
                buf.push_back(0x00);
                appendString(buf, "public");

                    // GID: 0x01 "gid" "2530054926824748916"
                    buf.push_back(0x01);
                    appendString(buf, "gid");
                    appendString(buf, "2530054926824748916");

                // End of public
                buf.push_back(0x08);

            // End of manifests
            buf.push_back(0x08);

        // End of depot 3962421
        buf.push_back(0x08);

    // End of depots
    buf.push_back(0x08);

    auto depots = AppInfoHelper::parseDepotsSection(buf.data(), buf.size());
    ASSERT_EQ(depots.size(), 1u);
    EXPECT_EQ(depots[0].depotId, 3962421u);
    EXPECT_EQ(depots[0].manifestGid, 2530054926824748916ULL);
    EXPECT_EQ(depots[0].maxsize, 45812938472ULL);
    EXPECT_EQ(depots[0].name, "Clair Obscur Content");
}

TEST_F(DepotKeysTest, DepotKeysYamlLoadingAndQuerying)
{
    std::string yamlPath = g_config.getDir() + "/depot_keys.yaml";
    std::string yamlContent =
        "3962420:\n"
        "  title: \"Clair Obscur: Expedition 33\"\n"
        "  depots:\n"
        "    3962421: \"af524ae28cb634f861e43a8dc0f9dbc6002bb303ec8e8167e02a8c92fa57d1e9\"\n"
        "    3962422: \"112233445566778899aabbccddeeff00112233445566778899aabbccddeeff00\"\n"
        "4090260:\n"
        "  depots:\n"
        "    4090261: \"abcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcd\"\n";

    {
        std::ofstream out(yamlPath);
        out << yamlContent;
    }

    DepotKeys::loadDepotKeysYaml();

    EXPECT_TRUE(DepotKeys::hasKeysForApp(3962420));
    EXPECT_TRUE(DepotKeys::hasKeysForApp(4090260));
    EXPECT_FALSE(DepotKeys::hasKeysForApp(999999));

    EXPECT_TRUE(DepotKeys::isKeyManagedApp(3962420));
    EXPECT_TRUE(DepotKeys::isKeyManagedApp(4090260));

    std::string key1 = DepotKeys::getDepotKey(3962421);
    EXPECT_EQ(key1, "af524ae28cb634f861e43a8dc0f9dbc6002bb303ec8e8167e02a8c92fa57d1e9");

    std::string key2 = DepotKeys::getDepotKey(3962422);
    EXPECT_EQ(key2, "112233445566778899aabbccddeeff00112233445566778899aabbccddeeff00");

    auto depots = DepotKeys::getDepotsForApp(3962420);
    EXPECT_EQ(depots.size(), 2u);
    EXPECT_EQ(depots.count(3962421), 1u);
    EXPECT_EQ(depots.count(3962422), 1u);

    auto allApps = DepotKeys::getAllAppIdsWithKeys();
    EXPECT_EQ(allApps.size(), 2u);
    EXPECT_EQ(allApps.count(3962420), 1u);
    EXPECT_EQ(allApps.count(4090260), 1u);
}

TEST_F(DepotKeysTest, LuaGenerationKeyFirst)
{
    // Write depot_keys.yaml
    std::string yamlPath = g_config.getDir() + "/depot_keys.yaml";
    std::string yamlContent =
        "3962420:\n"
        "  title: \"Clair Obscur: Expedition 33\"\n"
        "  depots:\n"
        "    3962421: \"af524ae28cb634f861e43a8dc0f9dbc6002bb303ec8e8167e02a8c92fa57d1e9\"\n";
    {
        std::ofstream out(yamlPath);
        out << yamlContent;
    }
    DepotKeys::loadDepotKeysYaml();

    // Prepare mock binary appinfo for app 3962420
    std::vector<char> buf;
    buf.push_back(0x00);
    appendString(buf, "depots");
        buf.push_back(0x00);
        appendString(buf, "3962421");
            buf.push_back(0x07);
            appendString(buf, "maxsize");
            uint64_t maxsize = 45812938472ULL;
            char szBytes[8];
            std::memcpy(szBytes, &maxsize, sizeof(maxsize));
            buf.insert(buf.end(), szBytes, szBytes + 8);

            buf.push_back(0x00);
            appendString(buf, "manifests");
                buf.push_back(0x00);
                appendString(buf, "public");
                    buf.push_back(0x01);
                    appendString(buf, "gid");
                    appendString(buf, "2530054926824748916");
                buf.push_back(0x08);
            buf.push_back(0x08);
        buf.push_back(0x08);
    buf.push_back(0x08);

    setMockDepotsSection(buf);

    bool ok = LuaGenerator::generateForApp(3962420);
    ASSERT_TRUE(ok);

    std::string genLuaPath = m_tempDir + "/.local/share/Steam/config/stplug-in/3962420.lua";
    ASSERT_TRUE(fs::exists(genLuaPath));

    std::ifstream in(genLuaPath);
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    EXPECT_NE(content.find("addappid(3962420, 1, \"0\")"), std::string::npos);
    EXPECT_NE(content.find("addappid(3962421, 0, \"af524ae28cb634f861e43a8dc0f9dbc6002bb303ec8e8167e02a8c92fa57d1e9\")"), std::string::npos);
    EXPECT_NE(content.find("setManifestid(3962421, \"2530054926824748916\", 45812938472)"), std::string::npos);
}

TEST_F(DepotKeysTest, BailWhenNoDecryptionKey)
{
    // App with no keys in YAML
    EXPECT_FALSE(DepotKeys::hasKeysForApp(999888));
    EXPECT_TRUE(DepotKeys::getDepotKey(999889).empty());

    // LuaGenerator returns false
    bool genOk = LuaGenerator::generateForApp(999888);
    EXPECT_FALSE(genOk);

    // Dummy keyless Lua file on disk
    std::string keylessLuaPath = m_tempDir + "/.local/share/Steam/config/stplug-in/999888.lua";
    {
        std::ofstream out(keylessLuaPath);
        out << "addappid(999888)\nsetManifestid(999889, \"1234567890\")\n";
    }
    ASSERT_TRUE(fs::exists(keylessLuaPath));
    EXPECT_FALSE(DepotKeys::isKeyManagedApp(999888));
    fs::remove(keylessLuaPath);

    // Keyless Lua with manifest ID but no depot key
    {
        std::ofstream out(keylessLuaPath);
        out << "addappid(999888)\nsetManifestid(999889, \"2530054926824748916\", 3248601047)\n";
    }
    ASSERT_TRUE(fs::exists(keylessLuaPath));
    EXPECT_FALSE(DepotKeys::isKeyManagedApp(999888));
    EXPECT_TRUE(DepotKeys::getDepotKey(999889).empty());
    fs::remove(keylessLuaPath);
}

TEST_F(DepotKeysTest, FindAndGetDepotKeyAndTokenFromJson)
{
    // Write mock depotkeys.json and appaccesstokens.json in m_tempDir + "/.config/navi"
    std::string keysJsonPath = m_tempDir + "/.config/navi/depotkeys.json";
    {
        std::ofstream out(keysJsonPath);
        out << "{\n";
        out << "  \"785481\": \"0856170e60687e3639654046517fab3060863565fca9f1df9deddaea2c1d32f4\",\n";
        out << "  \"999123\": \"1111111111111111111111111111111111111111111111111111111111111111\"\n";
        out << "}\n";
    }

    std::string tokensJsonPath = m_tempDir + "/.config/navi/appaccesstokens.json";
    {
        std::ofstream out(tokensJsonPath);
        out << "{\n";
        out << "  \"766\": \"6919353661818993790\",\n";
        out << "  \"999120\": \"12345678901234567890\"\n";
        out << "}\n";
    }

    // Lookup depot key
    EXPECT_EQ(DepotKeys::findAndGetDepotKey(785481), "0856170e60687e3639654046517fab3060863565fca9f1df9deddaea2c1d32f4");
    EXPECT_EQ(DepotKeys::findAndGetDepotKey(999123), "1111111111111111111111111111111111111111111111111111111111111111");

    // Lookup app access token
    EXPECT_EQ(DepotKeys::findAndGetAppAccessToken(766), "6919353661818993790");
    EXPECT_EQ(DepotKeys::findAndGetAppAccessToken(999120), "12345678901234567890");

    // Non-existent entries
    EXPECT_TRUE(DepotKeys::findAndGetDepotKey(0).empty());
    EXPECT_TRUE(DepotKeys::findAndGetAppAccessToken(0).empty());
}
