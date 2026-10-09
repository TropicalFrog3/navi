#include "../src/decompiler.hpp"
#include "../src/vftableinfo.hpp"
#include "../src/log.hpp"
#include "../src/globals.hpp"
#include <iostream>
#include <cstring>
#include <dlfcn.h>

#include <cstdarg>
#include <cstdio>

void CLog::trace(const char*, const char*, const int, const char* fmt, ...) { va_list args; va_start(args, fmt); vprintf(fmt, args); va_end(args); }
void CLog::traceOnce(const char*, const char*, const int, const char* fmt, ...) { va_list args; va_start(args, fmt); vprintf(fmt, args); va_end(args); }
void CLog::once(const char*, const char*, const int, const char* fmt, ...) { va_list args; va_start(args, fmt); vprintf(fmt, args); va_end(args); }
void CLog::debug(const char*, const char*, const int, const char* fmt, ...) { va_list args; va_start(args, fmt); vprintf(fmt, args); va_end(args); }
void CLog::debugOnce(const char*, const char*, const int, const char* fmt, ...) { va_list args; va_start(args, fmt); vprintf(fmt, args); va_end(args); }
void CLog::warn(const char*, const char*, const int, const char* fmt, ...) { va_list args; va_start(args, fmt); vprintf(fmt, args); va_end(args); }
void CLog::error(const char*, const char*, const int, const char* fmt, ...) { va_list args; va_start(args, fmt); vprintf(fmt, args); va_end(args); }
void CLog::info(const char*, const char*, const int, const char* fmt, ...) { va_list args; va_start(args, fmt); vprintf(fmt, args); va_end(args); }
void CLog::notify(const char*, const char*, const int, const char* fmt, ...) { va_list args; va_start(args, fmt); vprintf(fmt, args); va_end(args); }
void CLog::notifyLong(const char*, const char*, const int, const char* fmt, ...) { va_list args; va_start(args, fmt); vprintf(fmt, args); va_end(args); }
void CLog::notifyWarn(const char*, const char*, const int, const char* fmt, ...) { va_list args; va_start(args, fmt); vprintf(fmt, args); va_end(args); }
void CLog::notifyError(const char*, const char*, const int, const char* fmt, ...) { va_list args; va_start(args, fmt); vprintf(fmt, args); va_end(args); }
void CLog::api(const char*, const char*, const int, const char* fmt, ...) { va_list args; va_start(args, fmt); vprintf(fmt, args); va_end(args); }
void CLog::custom(const unsigned int, const char*, const char*, const int, const char* fmt, ...) { va_list args; va_start(args, fmt); vprintf(fmt, args); va_end(args); }

CLog::CLog(const char* p) : path(p ? p : "") {}
CLog::~CLog() {}
CLog* CLog::createDefaultLog() { return new CLog("/dev/null"); }
std::unique_ptr<CLog> g_pLog = std::make_unique<CLog>("/dev/null");

lm_module_t g_modSteamClient{};
lm_module_t g_modSteamUI{};
lm_module_t g_modTier0{};

int main()
{
    std::cout << "Loading steamui.so\n";
    if (!LM_LoadModule("/home/nicolas/.local/share/Steam/ubuntu12_32/steamui.so", &g_modSteamUI)) {
        std::cout << "Failed to load steamui.so, error: " << dlerror() << "\n";
        return 1;
    }
    std::cout << "Successfully loaded! Base=" << std::hex << g_modSteamUI.base << "\n";
    
    std::cout << "Parsing module...\n";
    Decompiler::parseModule(g_modSteamUI);
    
    std::cout << "Dumping interfaces...\n";
    auto tbl = Decompiler::parseInterfaceMapBase("21CSteamUIAppController");
    for (const auto& kv : tbl) {
        std::cout << "  " << kv.first << " = " << kv.second << "\n";
    }
    
    return 0;
}
