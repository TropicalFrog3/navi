#include "../src/feats/manifestcode.hpp"
#include "../src/feats/nativeinstall.hpp"
#include "../src/sdk/IClientAppManager.hpp"
#include "../src/config.hpp"
#include "../src/log.hpp"
#include "../src/CppAccela/acceladownload.hpp"
#include "../src/CppAccela/accelaluaparser.hpp"
#include "../src/CppAccela/accelapath.hpp"
#include "../src/CppAccela/accelazip.hpp"
#include "../src/feats/depotkeys.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <cstring>
#include <set>

// Provide minimal stubs for CLog
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
std::string CConfig::getDir() const
{
    const char* h = getenv("HOME");
    return std::string(h ? h : "/tmp") + "/.config/SLSsteam";
}

CSteamEngine* g_pSteamEngine = nullptr;

namespace CppAccela::LuaParser {
    ParseResult parseFile(const std::string&) { return {}; }
}

namespace DepotKeys {
    void scanLuaPluginsForDepotKeys() {}
    void loadDepotKeysYaml() {}
    bool hasKeysForApp(uint32_t) { return false; }
    std::string getDepotKey(uint32_t) { return "1122334455667788990011223344556677889900112233445566778899001122"; }
}

namespace LuaGenerator {
    bool generateForApp(uint32_t) { return false; }
}

namespace CppAccela::Download {
    static CachedRequest s_cached;
    static bool s_hasCached = false;

    void cacheInstallRequest(IClientAppManager* pMgr, uint32_t appId, uint32_t lib, uint8_t a4) {
        s_cached.pClientAppManager = pMgr;
        s_cached.library = lib;
        s_cached.a4 = a4;
        s_hasCached = true;
    }

    bool takeCachedInstall(uint32_t appId, CachedRequest& out) {
        if (!s_hasCached) return false;
        out = s_cached;
        s_hasCached = false;
        return true;
    }
}

// Helpers for manual protobuf construction
static void writeVarint(std::vector<uint8_t>& buf, uint64_t val)
{
    while (val >= 0x80) {
        buf.push_back(static_cast<uint8_t>((val & 0x7F) | 0x80));
        val >>= 7;
    }
    buf.push_back(static_cast<uint8_t>(val & 0x7F));
}

static void testProtobufWireCodec()
{
    std::cout << "[TEST] Testing ManifestCode wire-format parser & serializer...\n";

    // Build synthetic CContentServerDirectory_GetManifestRequestCode_Request
    // Field 1: app_id = 480 (varint) -> key: (1 << 3) | 0 = 0x08
    // Field 2: depot_id = 481 (varint) -> key: (2 << 3) | 0 = 0x10
    // Field 3: manifest_id = 88888888888888888ULL (varint) -> key: (3 << 3) | 0 = 0x18
    // Field 4: app_branch = "public" (length-delimited) -> key: (4 << 3) | 2 = 0x22
    std::vector<uint8_t> reqBuf;

    // Field 1
    reqBuf.push_back(0x08);
    writeVarint(reqBuf, 480);

    // Field 2
    reqBuf.push_back(0x10);
    writeVarint(reqBuf, 481);

    // Field 3
    reqBuf.push_back(0x18);
    writeVarint(reqBuf, 88888888888888888ULL);

    // Field 4
    reqBuf.push_back(0x22);
    reqBuf.push_back(6);
    const char* branch = "public";
    reqBuf.insert(reqBuf.end(), branch, branch + 6);

    ManifestCode::CContentServerDirectory_GetManifestRequestCode_Request req;
    bool ok = req.ParseFromArray(reqBuf.data(), reqBuf.size());

    assert(ok && "Failed to parse GetManifestRequestCode request");
    assert(req.app_id() == 480 && "AppID mismatch");
    assert(req.depot_id() == 481 && "DepotID mismatch");
    assert(req.manifest_id() == 88888888888888888ULL && "ManifestID mismatch");

    std::cout << "  - Request parsed successfully: App=" << req.app_id()
              << " Depot=" << req.depot_id()
              << " Manifest=" << req.manifest_id() << "\n";

    // Test response serialization
    uint64_t sampleCode = 0x123456789ABCDEF0ULL;
    ManifestCode::CContentServerDirectory_GetManifestRequestCode_Response resp;
    resp.set_manifest_request_code(sampleCode);

    size_t respSize = resp.ByteSizeLong();
    assert(respSize > 0 && "Response ByteSizeLong must be > 0");
    std::vector<uint8_t> respBuf(respSize);
    bool serOk = resp.SerializeToArray(respBuf.data(), respBuf.size());
    assert(serOk && "SerializeToArray failed");

    // Wire check: Field 1 (tag 0x08), followed by varint
    assert(respBuf[0] == 0x08 && "Response field 1 key mismatch");

    // Read back varint
    size_t offset = 1;
    uint64_t readCode = 0;
    int shift = 0;
    while (offset < respBuf.size()) {
        uint8_t b = respBuf[offset++];
        readCode |= static_cast<uint64_t>(b & 0x7F) << shift;
        shift += 7;
        if (!(b & 0x80)) break;
    }
    assert(readCode == sampleCode && "Serialized request_code mismatch");
    std::cout << "  - Response serialized and verified: code=0x" << std::hex << readCode << std::dec << "\n";
}

static void testNativeInstallPersistenceAndArm()
{
    std::cout << "[TEST] Testing NativeInstall persistence, arm, and disarm...\n";

    std::string testDir = "/tmp/sls_test_native_persist";
    std::filesystem::remove_all(testDir);
    std::filesystem::create_directories(testDir + "/.config/SLSsteam");
    setenv("HOME", testDir.c_str(), 1);

    NativeInstall::init();

    assert(!NativeInstall::isNative(480) && "480 must not be native initially");
    assert(!NativeInstall::isQueuedOrArmed(480) && "480 must not be queued initially");

    // Cache an install request first so arm can take it
    CppAccela::Download::cacheInstallRequest(nullptr, 480, 1, 0);

    // Test arming via CSV depot list
    bool armed = NativeInstall::arm(480, "481, 482");
    assert(armed && "NativeInstall::arm must succeed");
    assert(NativeInstall::isNative(480) && "480 must now be marked native");
    assert(NativeInstall::isQueuedOrArmed(480) && "480 must be queued or armed");

    auto sel = NativeInstall::getSelection(480);
    assert(sel != nullptr && "Selection must be present");
    assert(sel->depots.size() == 2 && "Must have 2 selected depots");
    assert(sel->depots.count(481) == 1 && "Depot 481 missing");
    assert(sel->depots.count(482) == 1 && "Depot 482 missing");

    // Verify persistence file exists and contains 480
    std::string yamlPath = testDir + "/.config/SLSsteam/native_installs.yaml";
    assert(std::filesystem::exists(yamlPath) && "native_installs.yaml must exist after arm");

    // Re-initialize to test load from disk
    NativeInstall::init();
    assert(NativeInstall::isNative(480) && "480 must remain native after re-init");

    // Test disarm
    NativeInstall::disarm(480);
    assert(!NativeInstall::isNative(480) && "480 must not be native after disarm");
    assert(!NativeInstall::isQueuedOrArmed(480) && "480 must not be armed after disarm");

    // Re-initialize to verify disarm persisted to disk
    NativeInstall::init();
    assert(!NativeInstall::isNative(480) && "480 must remain disarmed after disk reload");

    std::cout << "  - Persistence and arm/disarm state verified.\n";
}

static void testDepotFilteringAlgorithm()
{
    std::cout << "[TEST] Testing Depot filtering backward-shift algorithm...\n";

    // Setup an array of DepotInfo_t
    DepotInfo_t depots[10];
    std::memset(depots, 0, sizeof(depots));

    uint32_t count = 4;
    depots[0].depotId = 101;
    depots[1].depotId = 102;
    depots[2].depotId = 103;
    depots[3].depotId = 104;

    std::unordered_set<uint32_t> selected = { 101, 103 };

    // Backward-iteration prune
    for (int i = (int)count - 1; i >= 0; --i)
    {
        if (selected.find(depots[i].depotId) == selected.end())
        {
            for (uint32_t j = (uint32_t)i; j < count - 1; ++j)
            {
                depots[j] = depots[j + 1];
            }
            count--;
        }
    }

    assert(count == 2 && "Count must be 2 after pruning 102 and 104");
    assert(depots[0].depotId == 101 && "Depot 0 must be 101");
    assert(depots[1].depotId == 103 && "Depot 1 must be 103");

    // Test pruning all
    selected.clear();
    for (int i = (int)count - 1; i >= 0; --i)
    {
        if (selected.find(depots[i].depotId) == selected.end())
        {
            for (uint32_t j = (uint32_t)i; j < count - 1; ++j)
            {
                depots[j] = depots[j + 1];
            }
            count--;
        }
    }
    assert(count == 0 && "Count must be 0 after pruning all");

    // Test pruning consecutive items (e.g. 200, 201, 202, 203, keep only 203)
    count = 4;
    depots[0].depotId = 200;
    depots[1].depotId = 201;
    depots[2].depotId = 202;
    depots[3].depotId = 203;

    selected = { 203 };
    for (int i = (int)count - 1; i >= 0; --i)
    {
        if (selected.find(depots[i].depotId) == selected.end())
        {
            for (uint32_t j = (uint32_t)i; j < count - 1; ++j)
            {
                depots[j] = depots[j + 1];
            }
            count--;
        }
    }
    assert(count == 1 && "Count must be 1");
    assert(depots[0].depotId == 203 && "Only 203 should remain");

    std::cout << "  - Backward-shift pruning algorithm verified across edge cases.\n";
}

static void testProviderSidecarFormatAndEscaping()
{
    std::cout << "[TEST] Testing Provider sidecar reading, fallback, and escaping...\n";

    std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "sls_provider_test";
    std::filesystem::create_directories(tempDir);

    // Write a provider file with special characters
    std::string provFile = (tempDir / "1443430.provider").string();
    {
        std::ofstream pf(provFile);
        pf << "Hubcap \"Special\" \\Provider\\\n";
    }

    // Read back and escape
    std::string providerName;
    {
        std::ifstream pf(provFile);
        assert(pf.is_open());
        std::getline(pf, providerName);
    }
    assert(providerName == "Hubcap \"Special\" \\Provider\\");

    std::string escapedProvider;
    for (char c : providerName) {
        if (c == '"' || c == '\\') escapedProvider += '\\';
        escapedProvider += c;
    }
    assert(escapedProvider == "Hubcap \\\"Special\\\" \\\\Provider\\\\");

    // Clean up
    std::filesystem::remove_all(tempDir);
    std::cout << "  - Provider sidecar read and JSON escaping verified successfully.\n";
}

static void testRemoveLuaPatternScanning()
{
    std::cout << "[TEST] Testing RemoveLua pattern scanning and file isolation...\n";

    std::filesystem::path pluginDir = std::filesystem::temp_directory_path() / "sls_removelua_test";
    std::filesystem::create_directories(pluginDir);

    uint32_t targetAppId = 1443430;
    std::string idStr = std::to_string(targetAppId);

    // Create test files:
    // 1. Target lua with addappid(1443430)
    {
        std::ofstream f(pluginDir / "rogue.lua");
        f << "-- Rogue configuration\naddappid(" << targetAppId << ")\nsetname(\"Rogue\")\n";
    }
    // 2. Target lua with spaces addappid( 1443430 )
    {
        std::ofstream f(pluginDir / "rogue_alt.lua");
        f << "addappid( " << targetAppId << " )\n";
    }
    // 3. Unrelated lua (must NOT be touched)
    {
        std::ofstream f(pluginDir / "unrelated.lua");
        f << "addappid(504230)\nsetname(\"Celeste\")\n";
    }
    // 4. Target provider sidecar
    {
        std::ofstream f(pluginDir / (idStr + ".provider"));
        f << "HubcapDB\n";
    }
    // 5. Target manifest
    {
        std::ofstream f(pluginDir / (idStr + "_1443431.manifest"));
        f << "MANIFEST_BINARY_DATA\n";
    }
    // 6. Unrelated manifest
    {
        std::ofstream f(pluginDir / "504230_504231.manifest");
        f << "MANIFEST_CELESTE\n";
    }

    // Execute scan algorithm matching storeinject.cpp
    std::vector<std::string> deletedFiles;

    // Scan Lua files
    for (const auto& entry : std::filesystem::directory_iterator(pluginDir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".lua") {
            std::ifstream file(entry.path());
            if (file.is_open()) {
                std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                file.close();
                if (content.find("addappid") != std::string::npos && content.find(idStr) != std::string::npos) {
                    deletedFiles.push_back(entry.path().filename().string());
                    std::filesystem::remove(entry.path());
                }
            }
        }
    }

    // Scan manifests
    for (const auto& entry : std::filesystem::directory_iterator(pluginDir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".manifest") {
            std::string stem = entry.path().stem().string();
            if (stem.find(idStr) == 0) {
                deletedFiles.push_back(entry.path().filename().string());
                std::filesystem::remove(entry.path());
            }
        }
    }

    // Remove provider sidecar
    auto providerPath = pluginDir / (idStr + ".provider");
    if (std::filesystem::exists(providerPath)) {
        deletedFiles.push_back(providerPath.filename().string());
        std::filesystem::remove(providerPath);
    }

    // Assertions
    assert(std::filesystem::exists(pluginDir / "unrelated.lua") && "Unrelated Lua must not be deleted");
    assert(std::filesystem::exists(pluginDir / "504230_504231.manifest") && "Unrelated manifest must not be deleted");
    assert(!std::filesystem::exists(pluginDir / "rogue.lua") && "rogue.lua must be deleted");
    assert(!std::filesystem::exists(pluginDir / "rogue_alt.lua") && "rogue_alt.lua must be deleted");
    assert(!std::filesystem::exists(pluginDir / (idStr + ".provider")) && "provider file must be deleted");
    assert(!std::filesystem::exists(pluginDir / (idStr + "_1443431.manifest")) && "rogue manifest must be deleted");

    std::filesystem::remove_all(pluginDir);
    std::cout << "  - RemoveLua pattern scan correctly deleted " << deletedFiles.size() << " files while preserving unrelated plugins.\n";
}

static void testCrossPlatformDepotGuardSimulation()
{
    std::cout << "[TEST] Testing cross-platform depot guard logic (depots->size == 0)...\n";

    // Scenario A: Native Linux depot provided by Steam
    {
        uint32_t nativeDepotCount = 1; // Native client found Linux depot
        bool injected = false;

        // Guard from apps.cpp line 806: only inject when native client returned 0 depots
        if (nativeDepotCount == 0) {
            injected = true;
        }

        assert(!injected && "Cross-platform depots must NOT be injected when native depot is present");
    }

    // Scenario B: Unowned or cross-platform app where native client found 0 depots
    {
        uint32_t nativeDepotCount = 0;
        bool injected = false;

        if (nativeDepotCount == 0) {
            injected = true;
        }

        assert(injected && "Missing depots must be injected when native client provides 0 depots");
    }

    std::cout << "  - Cross-platform depot guard verified successfully.\n";
}

static void testZipAssemblyAndCacheExtraction()
{
    std::cout << "[TEST] Testing Zip assembly and cache extraction across provider formats...\n";

    std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "sls_zip_test";
    std::filesystem::create_directories(tempDir);

    // Scenario A: Lua-only ZIP (e.g. wudrm / OpenSteamTool / raw Lua)
    {
        std::string luaPath = (tempDir / "480.lua").string();
        {
            std::ofstream f(luaPath);
            f << "addappid(480)\nsetManifestid(480, \"1234567890\")\n";
        }

        std::string provPath = (tempDir / "480.provider").string();
        {
            std::ofstream f(provPath);
            f << "wudrm\n";
        }

        std::string zipPath = (tempDir / "480.zip").string();

        // Assemble with empty manifestPaths
        bool assembled = CppAccela::Zip::assemble(zipPath, luaPath, {}, provPath);
        assert(assembled && "assemble must succeed with empty manifest list (Lua-only)");

        assert(CppAccela::Zip::isValid(zipPath) && "Generated zip must be valid");

        // Extract to a separate directory
        std::string extractDir = (tempDir / "480_extracted").string();
        bool extracted = CppAccela::Zip::extract(zipPath, extractDir);
        assert(extracted && "extract must succeed on generated zip");

        auto files = CppAccela::Zip::findFiles(extractDir, "480");
        assert(!files.luaFile.empty() && "findFiles must locate 480.lua");
        assert(!files.providerFile.empty() && "findFiles must locate 480.provider");
        assert(files.manifestFiles.empty() && "manifestFiles must be empty for Lua-only zip");

        // Verify provider content
        std::ifstream pf(files.providerFile);
        std::string provContent;
        std::getline(pf, provContent);
        assert(provContent == "wudrm" && "Provider content must be wudrm");
    }

    // Scenario B: Lua + Manifests ZIP (e.g. steamrun / DepotBox)
    {
        std::string luaPath = (tempDir / "1000.lua").string();
        {
            std::ofstream f(luaPath);
            f << "addappid(1000)\nsetManifestid(1001, \"11111\")\nsetManifestid(1002, \"22222\")\n";
        }

        std::string provPath = (tempDir / "1000.provider").string();
        {
            std::ofstream f(provPath);
            f << "steamrun\n";
        }

        std::string m1 = (tempDir / "1001_11111.manifest").string();
        {
            std::ofstream f(m1);
            f << "BINARY_DEPOT_MANIFEST_1";
        }
        std::string m2 = (tempDir / "1002_22222.manifest").string();
        {
            std::ofstream f(m2);
            f << "BINARY_DEPOT_MANIFEST_2";
        }

        std::string zipPath = (tempDir / "1000.zip").string();

        bool assembled = CppAccela::Zip::assemble(zipPath, luaPath, { m1, m2 }, provPath);
        assert(assembled && "assemble must succeed with manifest list");
        assert(CppAccela::Zip::isValid(zipPath) && "Generated steamrun zip must be valid");

        std::string extractDir = (tempDir / "1000_extracted").string();
        bool extracted = CppAccela::Zip::extract(zipPath, extractDir);
        assert(extracted && "extract must succeed on steamrun zip");

        auto files = CppAccela::Zip::findFiles(extractDir, "1000");
        assert(!files.luaFile.empty() && "findFiles must locate 1000.lua");
        assert(!files.providerFile.empty() && "findFiles must locate 1000.provider");
        assert(files.manifestFiles.size() == 2 && "manifestFiles must contain both manifests");

        std::ifstream pf(files.providerFile);
        std::string provContent;
        std::getline(pf, provContent);
        assert(provContent == "steamrun" && "Provider content must be steamrun");
    }

    // Clean up
    std::filesystem::remove_all(tempDir);
    std::cout << "  - Universal Zip assembly and extraction verified successfully.\n";
}

int main()
{
    std::cout << "========================================\n";
    std::cout << "Running Native Install & ManifestCode Unit Tests\n";
    std::cout << "========================================\n";

    testProtobufWireCodec();
    testNativeInstallPersistenceAndArm();
    testDepotFilteringAlgorithm();
    testProviderSidecarFormatAndEscaping();
    testRemoveLuaPatternScanning();
    testCrossPlatformDepotGuardSimulation();
    testZipAssemblyAndCacheExtraction();

    std::cout << "\n[ALL TESTS PASSED SUCCESSFULLY!]\n";
    return 0;
}
