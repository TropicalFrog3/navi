#include "../src/feats/manifestcode.hpp"
#include "../src/feats/nativeinstall.hpp"
#include "../src/sdk/IClientAppManager.hpp"
#include "../src/config.hpp"
#include "../src/log.hpp"
#include "../src/CppAccela/acceladownload.hpp"
#include "../src/CppAccela/accelaluaparser.hpp"
#include "../src/CppAccela/accelapath.hpp"
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

namespace CppAccela::LuaParser {
    ParseResult parseFile(const std::string&) { return {}; }
}

namespace DepotKeys {
    void scanLuaPluginsForDepotKeys() {}
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
    assert(sel.has_value() && "Selection must be present");
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

int main()
{
    std::cout << "========================================\n";
    std::cout << "Running Native Install & ManifestCode Unit Tests\n";
    std::cout << "========================================\n";

    testProtobufWireCodec();
    testNativeInstallPersistenceAndArm();
    testDepotFilteringAlgorithm();

    std::cout << "\n[ALL TESTS PASSED SUCCESSFULLY!]\n";
    return 0;
}
