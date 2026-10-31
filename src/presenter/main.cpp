#include "sdk/sdk.hpp"

#include "api.hpp"
#include "config.hpp"
#include "decompiler.hpp"
#include "globals.hpp"
#include "hooks.hpp"
#include "log.hpp"
#include "patterns.hpp"
#include "update.hpp"
#include "utils.hpp"
#include "cefsizefix.hpp"
#include "depotkeys.hpp"
#include "storeinject.hpp"
#include "apps.hpp"
#include "removelua.hpp"
#include "tier0hook.hpp"
#include "autoupdate.hpp"
#include "nativeinstall.hpp"
#include "vftableinfo.hpp"
#include "legacy_migration.hpp"
#include "cloudredirect.hpp"

#include "libmem/libmem.h"
#include <curl/curl.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <link.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>


static bool cleanEnvVar(const char* varName, const char* endsWith)
{
	char* var = getenv(varName);
	if (var == NULL)
		return false;

	const auto splits = Utils::strsplit(var, ":");
	auto newEnv = std::string();

	for (unsigned int i = 0; i < splits.size(); i++)
	{
		const auto split = splits.at(i);
		if (split.ends_with(endsWith))
		{
			LOG_DEBUG("Removed %s from $%s\n", endsWith, varName);
			continue;
		}

		if (newEnv.size() > 0)
		{
			newEnv.append(":");
		}
		newEnv.append(split);
	}

	if (newEnv.size())
	{
		setenv(varName, newEnv.c_str(), true);
	}
	else
	{
		unsetenv(varName);
	}
	//LOG_DEBUG("Set %s to %s\n", varName, newEnv.c_str());

	return true;
}

//Looking at /proc/self/maps it seems like this isn't needed for processes that aren't steam
//__attribute__((noreturn))
static void unload()
{
	static std::atomic<bool> s_unloaded{false};
	if (s_unloaded.exchange(true))
	{
		return;
	}

	StoreInject::shutdown();
	Tier0Hook::remove();
	CefSizeFix::removeSizeFixScript();
	Hooks::remove();
	NaviAPI::shutdown();
	g_config.shutdown();
	curl_global_cleanup();
}

//TODO: Remove when unload() works properly since it should not be needed anymore after that
static bool setupSuccess = false;

static void runPendingReleaseInstaller()
{
	const char* home = getenv("HOME");
	if (!home)
	{
		return;
	}

	const std::string installDirs[] = {
		std::string(home) + "/.local/share/navi",
		std::string(home) + "/.var/app/com.valvesoftware.Steam/.local/share/navi"
	};

	for (const auto& installDir : installDirs)
	{
		const auto marker = std::filesystem::path(installDir) / ".pending-full-update";
		if (std::filesystem::exists(marker))
		{
			std::error_code ec;
			std::filesystem::remove(marker, ec);
		}
	}
}

static void setup()
{
	lm_process_t proc {};
	if (!LM_GetProcess(&proc))
	{
		unload();
		return;
	}

	//Do not do anything in other processes
	if (strcmp(proc.name, "steam") != 0)
	{
		unload();
		return;
	}

	g_pLog = std::unique_ptr<CLog>(CLog::createDefaultLog());
	curl_global_init(CURL_GLOBAL_ALL);
	//Won't happen, log throws a runtime exception when creation fails.
	//But just in case I decide to refactor one day
	if (!g_pLog)
	{
		unload();
		return;
	}

	runPendingReleaseInstaller();

	LOG_DEBUG("Navi loading in %s\n", proc.name);

	if (LM_FindModule(proc.name, &g_modSteam))
	{
		LOG_INFO("steam module found at 0x%x to 0x%x, path: %s\n", g_modSteam.base, g_modSteam.end, g_modSteam.path);
		Decompiler::parseModule(g_modSteam);
	}
	else if (LM_FindModule("steam", &g_modSteam))
	{
		LOG_INFO("steam module found at 0x%x to 0x%x, path: %s\n", g_modSteam.base, g_modSteam.end, g_modSteam.path);
		Decompiler::parseModule(g_modSteam);
	}
	else
	{
		LOG_WARN("Failed to find steam module!\n");
	}

	//Any release
	cleanEnvVar("LD_AUDIT", "libnavi.so");
	cleanEnvVar("LD_AUDIT", "library-inject.so");

	//Arch release
	cleanEnvVar("LD_AUDIT", "libnavi.so");
	cleanEnvVar("LD_AUDIT", "libnavi-library-inject.so");
	//TODO: Investigate weird logging. Not like it's necessary anymore
	//cleanEnvVar("LD_PRELOAD");

	LegacyMigration::migrateIfNeeded();

	if (!g_config.init())
	{
		unload();
		return;
	}

	if (g_config.cloudSavesEnabled.get())
	{
		auto& bridge = Navi::CloudRedirect::CCloudRedirectBridge::Instance();
		bridge.EnsureLocalOnlyConfig();
		std::error_code ec;
		if (!std::filesystem::exists(bridge.GetPaths().crKillswitchFile, ec))
		{
			bridge.SetEnabledState(true);
		}
		bridge.SyncSlsSteamConfig(g_config.cloudSavesManagedApps.get());
	}

	DepotKeys::loadDepotKeysYaml();
	scanLuaPluginsAndUpdateConfig();
	DepotKeys::scanLuaPluginsForDepotKeys();

	//Since we can't statically link everything and some distros seem to respect LD_LIBRARY_PATH
	//more or less than mine does we just force append those
	//Hopefully this won't mess anything else up
	const char* env_ldLibPath = getenv("LD_LIBRARY_PATH");
	auto ldLibPath = std::string(env_ldLibPath ? env_ldLibPath : "");
	if (!ldLibPath.empty()) {
		ldLibPath.append(":");
	}
	ldLibPath.append("/usr/lib:/usr/lib32");
	setenv("LD_LIBRARY_PATH", ldLibPath.c_str(), true);

	Updater::init();
	AutoUpdate::checkAndPrompt();

	setupSuccess = true;
}

static void load()
{
	if (!setupSuccess)
	{
		return;
	}

	if (!g_modSteamClient.base || !g_modSteamUI.base || !g_modTier0.base)
	{
		return;
	}

	if (!g_modSteam.base)
	{
		if (LM_FindModule("steam", &g_modSteam))
		{
			Decompiler::parseModule(g_modSteam);
		}
	}

	const auto path = std::filesystem::path(g_modSteamClient.path);
	const auto dir = path.parent_path();

	LOG_INFO
	(
		"steamclient.so loaded from %s/%s at 0x%x to 0x%x\n",
		dir.filename().c_str(),
		path.filename().c_str(),
		g_modSteamClient.base,
		g_modSteamClient.end
	);
	LOG_INFO
	(
		"steamui.so loaded at 0x%x to 0x%x\n",
		g_modSteamUI.base,
		g_modSteamUI.end
	);

	if (!Updater::verifySafeModeHash())
	{
		if (g_config.safeMode.get())
		{
			LOG_NOTIFYERROR("Unknown steamclient.so hash! Aborting...");
			unload();
			return;
		}
		else if (g_config.warnHashMissmatch.get())
		{
			LOG_NOTIFYWARN("steamclient.so hash missmatch! Please update :)");
		}
	}

	if (!Steam::init())
	{
		LOG_NOTIFYERROR("Failed to find steam exports!\n");
		return;
	}

	if (!VFTIndexes::init())
	{
		LOG_NOTIFYERROR("Failed to parse VFTables! Aborting...");
		return;
	}

	if (!Patterns::init())
	{
		LOG_NOTIFYERROR("Failed to find all patterns! Aborting...");
		return;
	}

	if (!Hooks::setup())
	{
		unload();
		return;
	}

	NaviAPI::init();
	NativeInstall::init();

	// Install the tier0 hook to intercept steamwebhelper launch
	// This replaces --cef-enable-debugging with --remote-debugging-pipe
	// Must be done early so it catches the next steamwebhelper spawn
	if (Tier0Hook::install()) {
		LOG_INFO("Tier0 hook installed - steamwebhelper will use pipe-based CDP\n");
	} else {
		LOG_WARN("Tier0 hook failed - falling back to port-based CDP\n");
	}

	if (g_config.cloudSavesEnabled.get())
	{
		Navi::CloudRedirect::CCloudRedirectBridge::Instance().LoadEngine();
	}

	// Inject the CEF size fix script into Steam's UI
	CefSizeFix::injectSizeFixScript();

	// Start the Store injection background thread (callback server on port 9001)
	// MUST start before RemoveLua injection, because the JS depends on the /check API
	StoreInject::init();

	// Inject the Remove Lua button script (needs port 9001 to be listening)
	RemoveLua::injectRemoveLuaScript();

	Decompiler::cleanUp();

	if (g_config.notifyInit.get())
	{
		const auto now = std::chrono::time_point { std::chrono::system_clock::now() };
		const auto ymd = std::chrono::year_month_day { std::chrono::floor<std::chrono::days>(now) };

		//Funsy easter egg :)
		if (static_cast<unsigned int>(ymd.month()) == 2 && static_cast<unsigned int>(ymd.day()) == 22)
		{
			LOG_NOTIFY("Happy birthday Navi!");
		}
		else
		{
			LOG_NOTIFY("Loaded successfully");
		}
	}
}

unsigned int la_version(unsigned int)
{
	return LAV_CURRENT;
}

static uintptr_t *g_steamclientCookie = nullptr;
static uintptr_t *g_steamuiCookie = nullptr;

unsigned int la_objopen(struct link_map *map, __attribute__((unused)) Lmid_t lmid, uintptr_t *cookie)
{
	// The loader may provide an unnamed link map during early startup.
	if (!map || !map->l_name)
	{
		return 0;
	}

	const std::string name(map->l_name);

	if (name.ends_with("/steamclient.so"))
	{
		g_steamclientCookie = cookie;
		//Analyse modules before any relocations get applied
		LM_FindModule("steamclient.so", &g_modSteamClient);
		Decompiler::parseModule(g_modSteamClient);
		//This is wasteful, but we have to analyse right away otherwise the offset get turned into
		//addresses messing up the analysis.
		//We could workaround it by only loading after a late module has been loaded
		for (auto& vft : Decompiler::vftables)
		{
			vft.second.analyze();
		}

		load();
	}
	if (name.ends_with("/steamui.so"))
	{
		g_steamuiCookie = cookie;
		//Analyse modules before any relocations get applied
		LM_FindModule("steamui.so", &g_modSteamUI);
		Decompiler::parseModule(g_modSteamUI);
		//This is wasteful, but we have to analyse right away otherwise the offset get turned into
		//addresses messing up the analysis.
		//We could workaround it by only loading after a late module has been loaded
		for (auto& vft : Decompiler::vftables)
		{
			vft.second.analyze();
		}

		load();
	}
	if (name.ends_with("/libtier0_s.so"))
	{
		LM_FindModule("libtier0_s.so", &g_modTier0);

		load();
	}
	// Retry tier0 hook when libtier0_s.so is loaded (it may load after steamclient.so)
	if (setupSuccess && name.ends_with("/libtier0_s.so"))
	{
		if (!Tier0Hook::isHookInstalled() && Tier0Hook::install())
		{
			LOG_INFO("Tier0 hook installed via la_objopen (libtier0_s.so just loaded)\n");
		}
	}

	return 0;
}

extern "C" unsigned int la_objclose(uintptr_t *cookie)
{
	if (cookie != nullptr && (cookie == g_steamclientCookie || cookie == g_steamuiCookie))
	{
		StoreInject::shutdown();
	}
	return 0;
}

static const void* g_registeredEhFrameBegin = nullptr;
alignas(8) static uint8_t g_ehFrameObjectStorage[64] = {};

__attribute__((constructor)) static void registerAuditEhFrame()
{
	// glibc's _dl_find_object only indexes LM_ID_BASE and returns -1 for libraries
	// loaded inside an LD_AUDIT (dlmopen) namespace, causing libgcc_s _Unwind_Find_FDE
	// to fail and std::terminate() on any C++ exception unless .eh_frame is registered.
	Dl_info dlInfo{};
	if (dladdr(reinterpret_cast<const void*>(&registerAuditEhFrame), &dlInfo) == 0 || !dlInfo.dli_fbase)
	{
		return;
	}

	const auto* base = static_cast<const uint8_t*>(dlInfo.dli_fbase);
	const auto* ehdr = reinterpret_cast<const Elf32_Ehdr*>(base);
	if (std::memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0)
	{
		return;
	}

	const auto* phdrs = reinterpret_cast<const Elf32_Phdr*>(base + ehdr->e_phoff);
	for (uint16_t i = 0; i < ehdr->e_phnum; ++i)
	{
		if (phdrs[i].p_type == PT_GNU_EH_FRAME)
		{
			const uint8_t* ehFrameHdr = base + phdrs[i].p_vaddr;
			if (ehFrameHdr[0] == 1 && ehFrameHdr[1] == 0x1b)
			{
				int32_t relOffset = 0;
				std::memcpy(&relOffset, ehFrameHdr + 4, sizeof(relOffset));
				const void* ehFrameBegin = (ehFrameHdr + 4) + relOffset;
				using RegisterFrameInfoFn = void (*)(const void*, void*);
				auto regFn = reinterpret_cast<RegisterFrameInfoFn>(dlsym(RTLD_DEFAULT, "__register_frame_info"));
				if (regFn)
				{
					g_registeredEhFrameBegin = ehFrameBegin;
					regFn(g_registeredEhFrameBegin, g_ehFrameObjectStorage);
				}
			}
			break;
		}
	}
}

__attribute__((destructor)) static void onLibraryUnload()
{
	StoreInject::shutdown();
	NaviAPI::shutdown();
	g_config.shutdown();
	if (g_registeredEhFrameBegin != nullptr)
	{
		using DeregisterFrameInfoFn = void* (*)(const void*);
		auto deregFn = reinterpret_cast<DeregisterFrameInfoFn>(dlsym(RTLD_DEFAULT, "__deregister_frame_info"));
		if (deregFn)
		{
			deregFn(g_registeredEhFrameBegin);
		}
		g_registeredEhFrameBegin = nullptr;
	}
}

void la_preinit(__attribute__((unused)) uintptr_t *cookie)
{
	setup();
}
