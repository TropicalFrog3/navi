#include "../src/sdk/binarykv.hpp"
#include "../src/feats/depotkeys.hpp"
#include "../src/feats/luagenerator.hpp"
#include "../src/config.hpp"
#include "../src/log.hpp"
#include "../src/atomic_file.hpp"
#include "../src/sdk/IClientApps.hpp"

#include <cassert>
#include <iostream>
#include <vector>
#include <cstring>
#include <filesystem>
#include <fstream>

// Minimal logging stubs
void CLog::trace(const char*, const char*, const int, const char*, ...) {}
void CLog::traceOnce(const char*, const char*, const int, const char*, ...) {}
void CLog::once(const char*, const char*, const int, const char*, ...) {}
void CLog::debug(const char*, const char*, const int, const char*, ...) {}
void CLog::debugOnce(const char*, const char*, const int, const char*, ...) {}
void CLog::warn(const char*, const char*, const int, const char*, ...) {}
void CLog::error(const char*, const char*, const int, const char*, ...) {}
void CLog::info(const char*, const char*, const int, const char*, ...) {}
void CLog::notify(const char*, const char*, const int, const char*, ...) {}
void CLog::notifyLong(const char*, const char*, const int, const char*, ...) {}
void CLog::notifyWarn(const char*, const char*, const int, const char*, ...) {}
void CLog::notifyError(const char*, const char*, const int, const char*, ...) {}
void CLog::api(const char*, const char*, const int, const char*, ...) {}
void CLog::custom(const unsigned int, const char*, const char*, const int, const char*, ...) {}

CLog::CLog(const char* p) : path(p ? p : "") {}
CLog::~CLog() {}
CLog* CLog::createDefaultLog() { return new CLog("/dev/null"); }
std::unique_ptr<CLog> g_pLog = std::make_unique<CLog>("/dev/null");

// Minimal CConfig stub
CConfig g_config;
CConfig::~CConfig() {}

static std::string s_testDir;
std::string CConfig::getDir() const
{
    return s_testDir;
}

std::string CConfig::getPluginDir()
{
    return s_testDir + "/stplug-in";
}

void scanLuaPluginsAndUpdateConfig() {}

// Mock IClientApps
IClientApps* g_pClientApps = nullptr;
static std::vector<char> s_mockDepotsSection;

uint32_t IClientApps::getAppDataSection(const AppId_t appId, const EAppInfoSection section, const char* pChOut, const uint32_t outSize)
{
    if (section == k_EAppInfoSectionDepots && !s_mockDepotsSection.empty() && pChOut)
    {
        uint32_t toCopy = std::min(outSize, static_cast<uint32_t>(s_mockDepotsSection.size()));
        std::memcpy(const_cast<char*>(pChOut), s_mockDepotsSection.data(), toCopy);
        return toCopy;
    }
    return 0;
}

bool IClientApps::requestAppInfoUpdate(const AppId_t*, const uint32_t)
{
    return true;
}

int32_t IClientApps::getAppData(const AppId_t, const char*, const char*, const uint32_t)
{
    return 0;
}

EAppType IClientApps::getAppType(const AppId_t)
{
    return k_EAppTypeGame;
}

// Helper to append null-terminated string to buffer
static void appendString(std::vector<char>& buf, const std::string& str)
{
    buf.insert(buf.end(), str.begin(), str.end());
    buf.push_back('\0');
}

static void testBinaryKVParser()
{
    std::cout << "[TEST] Testing Valve Binary KeyValues and AppInfo parser..." << std::endl;

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
    assert(depots.size() == 1);
    assert(depots[0].depotId == 3962421);
    assert(depots[0].manifestGid == 2530054926824748916ULL);
    assert(depots[0].maxsize == 45812938472ULL);
    assert(depots[0].name == "Clair Obscur Content");

    std::cout << "  - BinaryKV parsed depot 3962421: GID=" << depots[0].manifestGid
              << ", size=" << depots[0].maxsize << ", name=" << depots[0].name << std::endl;
}

static void testDepotKeysYamlLoading()
{
    std::cout << "[TEST] Testing depot_keys.yaml loading and querying..." << std::endl;

    std::string yamlPath = s_testDir + "/depot_keys.yaml";
    std::string yamlContent =
        "3962420:\n"
        "  title: \"Clair Obscur: Expedition 33\"\n"
        "  depots:\n"
        "    3962421: \"af524ae28cb634f861e43a8dc0f9dbc6002bb303ec8e8167e02a8c92fa57d1e9\"\n"
        "    3962422: \"112233445566778899aabbccddeeff00112233445566778899aabbccddeeff00\"\n"
        "4090260:\n"
        "  depots:\n"
        "    4090261: \"abcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcd\"\n";

    std::ofstream out(yamlPath);
    out << yamlContent;
    out.close();

    DepotKeys::loadDepotKeysYaml();

    assert(DepotKeys::hasKeysForApp(3962420));
    assert(DepotKeys::hasKeysForApp(4090260));
    assert(!DepotKeys::hasKeysForApp(999999));

    assert(DepotKeys::isKeyManagedApp(3962420));
    assert(DepotKeys::isKeyManagedApp(4090260));

    std::string key1 = DepotKeys::getDepotKey(3962421);
    assert(key1 == "af524ae28cb634f861e43a8dc0f9dbc6002bb303ec8e8167e02a8c92fa57d1e9");

    std::string key2 = DepotKeys::getDepotKey(3962422);
    assert(key2 == "112233445566778899aabbccddeeff00112233445566778899aabbccddeeff00");

    auto depots = DepotKeys::getDepotsForApp(3962420);
    assert(depots.size() == 2);
    assert(depots.count(3962421) == 1);
    assert(depots.count(3962422) == 1);

    auto allApps = DepotKeys::getAllAppIdsWithKeys();
    assert(allApps.size() == 2);
    assert(allApps.count(3962420) == 1);
    assert(allApps.count(4090260) == 1);

    std::cout << "  - depot_keys.yaml loaded and verified successfully (2 apps, 3 depots)." << std::endl;
}

static void testLuaGeneration()
{
    std::cout << "[TEST] Testing LuaGenerator::generateForApp..." << std::endl;

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

    s_mockDepotsSection = buf;
    static IClientApps dummyApps;
    g_pClientApps = &dummyApps;

    bool ok = LuaGenerator::generateForApp(3962420);
    assert(ok);

    std::string genLuaPath = s_testDir + "/stplug-in/3962420.lua";
    assert(std::filesystem::exists(genLuaPath));

    std::ifstream in(genLuaPath);
    std::string content((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
    in.close();

    assert(content.find("addappid(3962420, 1, \"0\")") != std::string::npos);
    assert(content.find("addappid(3962421, 0, \"af524ae28cb634f861e43a8dc0f9dbc6002bb303ec8e8167e02a8c92fa57d1e9\")") != std::string::npos);
    assert(content.find("setManifestid(3962421, \"2530054926824748916\", 45812938472)") != std::string::npos);

    std::cout << "  - Lua generated at " << genLuaPath << " with correct addappid and setManifestid." << std::endl;
}

static void testBailWhenNoDecryptionKey()
{
    std::cout << "[TEST] Testing bailing when there is no decryption key..." << std::endl;

    // 1. App with no keys in YAML
    assert(!DepotKeys::hasKeysForApp(999888));
    assert(DepotKeys::getDepotKey(999889).empty());

    // 2. LuaGenerator should return false
    bool genOk = LuaGenerator::generateForApp(999888);
    assert(!genOk);

    // 3. Even if a dummy keyless Lua file exists on disk, isKeyManagedApp should return false
    std::string keylessLuaPath = s_testDir + "/stplug-in/999888.lua";
    {
        std::ofstream out(keylessLuaPath);
        out << "addappid(999888)\nsetManifestid(999889, \"1234567890\")\n";
    }
    assert(std::filesystem::exists(keylessLuaPath));
    assert(!DepotKeys::isKeyManagedApp(999888));
    std::filesystem::remove(keylessLuaPath);

    // 4. Test keyless Lua with manifest ID but no depot key (like 3962420 when unowned with no key)
    std::string testKeylessLua = s_testDir + "/stplug-in/999888.lua";
    {
        std::ofstream out(testKeylessLua);
        out << "addappid(999888)\nsetManifestid(999889, \"2530054926824748916\", 3248601047)\n";
    }
    assert(std::filesystem::exists(testKeylessLua));
    assert(!DepotKeys::isKeyManagedApp(999888));
    assert(DepotKeys::getDepotKey(999889).empty());
    std::filesystem::remove(testKeylessLua);

    std::cout << "  - Verified: App and depots without AES decryption key bail correctly." << std::endl;
}

int main()
{
    s_testDir = "/tmp/sls_test_key_first_" + std::to_string(getpid());
    std::filesystem::create_directories(s_testDir);
    std::filesystem::create_directories(s_testDir + "/stplug-in");

    std::cout << "========================================" << std::endl;
    std::cout << "Running Key-First & Lua Generation Tests" << std::endl;
    std::cout << "========================================" << std::endl;

    testBinaryKVParser();
    testDepotKeysYamlLoading();
    testLuaGeneration();
    testBailWhenNoDecryptionKey();

    std::filesystem::remove_all(s_testDir);

    std::cout << "\n[ALL KEY-FIRST TESTS PASSED SUCCESSFULLY!]" << std::endl;
    return 0;
}
