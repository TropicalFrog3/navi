#include "test_stubs.hpp"
#include "log.hpp"
#include "IClientApps.hpp"
#include "IClientUser.hpp"
#include "CSteamEngine.hpp"
#include "CUser.hpp"
#include "steam.hpp"
#include "types.hpp"
#include "globals.hpp"
#include "fakeappid.hpp"

#include <libmem/libmem.h>
#include <cstring>
#include <algorithm>
#include <sstream>


static std::vector<char> s_mockDepotsSection;
static int s_warnCount = 0;

void setMockDepotsSection(const std::vector<char>& data)
{
    s_mockDepotsSection = data;
}

void clearMockDepotsSection()
{
    s_mockDepotsSection.clear();
}

int getWarnCount()
{
    return s_warnCount;
}

void resetWarnCount()
{
    s_warnCount = 0;
}

// CLog stubs
void CLog::trace(const char*, const char*, const int, const char*, ...) {}
void CLog::traceOnce(const char*, const char*, const int, const char*, ...) {}
void CLog::once(const char*, const char*, const int, const char*, ...) {}
void CLog::debug(const char*, const char*, const int, const char*, ...) {}
void CLog::debugOnce(const char*, const char*, const int, const char*, ...) {}
void CLog::warn(const char*, const char*, const int, const char*, ...) { s_warnCount++; }
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

std::string ELogLevel_ToString(const unsigned int lvlFlags)
{
    constexpr static auto flagToString = [](const unsigned int flag)
    {
        switch(flag)
        {
            case k_ELogLevelTrace: return "Trace";
            case k_ELogLevelOnce: return "Once";
            case k_ELogLevelDebug: return "Debug";
            case k_ELogLevelWarn: return "Warn";
            case k_ELogLevelError: return "Error";
            case k_ELogLevelInfo: return "Info";
            case k_ELogLevelNotifyShort: return "Notify";
            case k_ELogLevelNotifyLong: return "Notify Long";
            case k_ELogLevelAPI: return "API";
            default: return "Unknown";
        }
    };

    std::ostringstream lvlStr;
    for (int i = ELogLevelCount - 1; i >= 0; i--)
    {
        const unsigned int flag = 1 << i;
        if (lvlFlags & flag)
        {
            if (lvlStr.str().size() > 0)
            {
                lvlStr << "|";
            }
            lvlStr << flagToString(flag);
        }
    }
    return lvlStr.str();
}

// Licenses stub
namespace Licenses {
    void queueRefresh() {}
}

// Steam identity and engine stubs
CSteamId g_currentSteamId{};
CSteamEngine* g_pSteamEngine = nullptr;

static CUser s_dummyUser;
CUser* CSteamEngine::getUser(const unsigned int)
{
    return &s_dummyUser;
}

// CUser stubs
IClientAppManager* CUser::getAppManager() { return nullptr; }
IClientApps* CUser::getClientApps() { return g_pClientApps; }
IClientUser* CUser::getClientUser() { return nullptr; }
bool CUser::checkAppOwnership(const AppId_t, AppOwnershipInfo_t*) { return false; }
bool CUser::isSubscribed(const AppId_t) { return false; }
void CUser::postCallback(const ECallbackType, void*, const uint32_t) {}
void CUser::updateAppOwnershipTicket(const AppId_t, void*, const uint32_t) {}

// IClientUser stubs
bool IClientUser::loggedOn() { return true; }
uint32_t IClientUser::getAppOwnershipTicketExtendeData(const AppId_t, void*, const uint32_t, uint32_t*, uint32_t*, uint32_t*, uint32_t*) { return 0; }
bool IClientUser::setLegacyCDKey(const AppId_t, const char*) { return true; }

// FakeAppIds stubs
namespace FakeAppIds {
    AppId_t getFakeAppId(const AppId_t appId) { return appId; }
}

// IClientApps stubs
static IClientApps s_defaultClientApps;
IClientApps* g_pClientApps = &s_defaultClientApps;

int32_t IClientApps::getAppData(const AppId_t, const char*, const char*, const uint32_t)
{
    return 0;
}

uint32_t IClientApps::getAppDataSection(const AppId_t, const EAppInfoSection section, const char* pChOut, const uint32_t outSize)
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

EAppType IClientApps::getAppType(const AppId_t)
{
    return k_EAppTypeGame;
}

// Dynamic module handles
lm_module_t g_modSteam{};
lm_module_t g_modSteamClient{};
lm_module_t g_modSteamUI{};
lm_module_t g_modTier0{};

// Weak stubs for libmem functions in case liblibmem.a is omitted from linker line
extern "C" {
    lm_bool_t LM_CALL LM_ProtMemory(lm_address_t, lm_size_t, lm_prot_t, lm_prot_t*) __attribute__((weak));
    lm_bool_t LM_CALL LM_ProtMemory(lm_address_t, lm_size_t, lm_prot_t, lm_prot_t*) { return LM_TRUE; }

    lm_bool_t LM_CALL LM_Disassemble(lm_address_t, lm_inst_t*) __attribute__((weak));
    lm_bool_t LM_CALL LM_Disassemble(lm_address_t, lm_inst_t*) { return LM_FALSE; }

    lm_bool_t LM_CALL LM_Assemble(lm_string_t, lm_inst_t*) __attribute__((weak));
    lm_bool_t LM_CALL LM_Assemble(lm_string_t, lm_inst_t*) { return LM_FALSE; }

    lm_size_t LM_CALL LM_WriteMemory(lm_address_t, lm_bytearray_t, lm_size_t) __attribute__((weak));
    lm_size_t LM_CALL LM_WriteMemory(lm_address_t, lm_bytearray_t, lm_size_t) { return 0; }
}
