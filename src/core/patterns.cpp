/**
 * @file  patterns.cpp
 * @brief Signature pattern definitions for steamclient.so and steamui.so hooks
 */
#include "patterns.hpp"

#include "globals.hpp"
#include "memhlp.hpp"

#include "libmem/libmem.h"

using SigFollowMode = MemHlp::SigFollowMode;


Pattern_t::Pattern_t(const char* name, const char* pattern, MemHlp::SigFollowMode followMode, lm_module_t* module, bool optional)
	:
	Pattern_t(name, pattern, followMode, std::vector<int16_t>(), module, optional)
{
}

Pattern_t::Pattern_t(const char* name, const char* pattern, MemHlp::SigFollowMode followMode, std::vector<int16_t> prologue, lm_module_t* module, bool optional)
	:
	name(name),
	pattern(pattern),
	followMode(followMode),
	prologue(prologue),
	optional(optional),
	address(0),
	module(module)
{
	Patterns::patterns.emplace_back(this);
}

bool Pattern_t::find()
{
	const int16_t* pPrologue = prologue.empty() ? nullptr : prologue.data();
	address = MemHlp::searchSignature(name.c_str(), pattern.c_str(), module ? *module : g_modSteamClient, followMode, pPrologue, prologue.size());
	if (address == LM_ADDRESS_BAD && name == "CAPIJob::GetPlayerStats")
	{
		LOG_INFO("Trying fallback pattern for CAPIJob::GetPlayerStats\n");
		address = MemHlp::searchSignature(name.c_str(), "E8 ? ? ? ? 83 C4 10 89 C5 E9 ? ? ? ? C7 86 ? ? ? ? 00 00 00 00", module ? *module : g_modSteamClient, followMode, pPrologue, prologue.size());
	}
	else if (address == LM_ADDRESS_BAD && name == "CSteamUIAppController::FillInAppOverview")
	{
		LOG_INFO("Trying fallback patterns for CSteamUIAppController::FillInAppOverview\n");
		address = MemHlp::searchSignature(name.c_str(), "55 89 E5 57 56 E8 ? ? ? ? 81 C6 ? ? ? ? 53 81 EC AC 00 00 00", module ? *module : g_modSteamClient, SigFollowMode::None);
		if (address == LM_ADDRESS_BAD)
		{
			address = MemHlp::searchSignature(name.c_str(), "55 89 E5 57 E8 ? ? ? ? 81 C7 ? ? ? ? 56 53 81 EC AC 00 00 00", module ? *module : g_modSteamClient, SigFollowMode::None);
		}
	}
	else if (address == LM_ADDRESS_BAD && name == "CSteamUIAppController::BuildCompleteAppOverviewChange")
	{
		LOG_INFO("Trying fallback patterns for CSteamUIAppController::BuildCompleteAppOverviewChange\n");
		address = MemHlp::searchSignature(name.c_str(), "8B 86 ? ? 03 00 89 F3 8B 40 44 C6 ? 2C 01 83 4A 08 01 83 C2 10", module ? *module : g_modSteamClient, SigFollowMode::PrologueUpwards, pPrologue, prologue.size());
		if (address == LM_ADDRESS_BAD)
		{
			address = MemHlp::searchSignature(name.c_str(), "8B 86 78 84 03 00 89 F3", module ? *module : g_modSteamClient, SigFollowMode::PrologueUpwards, pPrologue, prologue.size());
		}
		if (address == LM_ADDRESS_BAD)
		{
			address = MemHlp::searchSignature(name.c_str(), "8B 86 38 88 03 00 89 F3", module ? *module : g_modSteamClient, SigFollowMode::PrologueUpwards, pPrologue, prologue.size());
		}
	}
	else if (address == LM_ADDRESS_BAD && name == "CUser::GetSubscribedApps")
	{
		LOG_INFO("Trying fallback patterns for CUser::GetSubscribedApps\n");
		address = MemHlp::searchSignature(name.c_str(), "E8 ? ? ? ? 89 C6 83 C4 ? 85 C0 0F 84 ? ? ? ? 8B 9D ? ? ? ? 39 D8", module ? *module : g_modSteamClient, SigFollowMode::Relative);
		if (address == LM_ADDRESS_BAD)
		{
			address = MemHlp::searchSignature(name.c_str(), "E8 ? ? ? ? 89 C6 83 C4 ? 85 C0 0F 85 ? ? ? ? 8B 9D ? ? ? ? 39 D8", module ? *module : g_modSteamClient, SigFollowMode::Relative);
		}
	}
	return address != LM_ADDRESS_BAD;
}

bool Patterns::init()
{
	bool found = true;

	for (auto& pattern : patterns)
	{
		if (!pattern->find())
		{
			if (pattern->optional)
			{
				LOG_WARN("Optional pattern not found: %s\n", pattern->name.c_str());
			}
			else
			{
				LOG_WARN("Failed to find pattern: %s\n", pattern->name.c_str());
				found = false;
			}
		}
	}

	return found;
}

namespace Patterns
{
	Pattern_t TraceIPC
	{
		"TraceIPC",
		"0F 45 F8 85 ED",
		SigFollowMode::PrologueUpwards,
		std::vector<int16_t> { 0x53, 0x56, 0x57, 0x55 }
	};

	namespace CAPIJob
	{
		Pattern_t SendAndRecv
		{
			"CAPIJob::SendAndRecv",
			"8B 7C 24 ? FF 76 ? FF 76",
			SigFollowMode::PrologueUpwards,
			std::vector<int16_t> { 0x53, 0x56, 0x57, 0x55 }
		};
	}

	namespace CAppDataCache
	{
		Pattern_t BParseResponseMessage
		{
			"CAppDataCache::BParseResponseMessage",
			"8B 77 ? 39 46",
			SigFollowMode::PrologueUpwards,
			std::vector<int16_t> { 0x53, 0x56, 0x57, 0xE5, 0x89, 0x55, -1, -1, -1, -1, 05, -1, -1, -1, -1, 0xE8 }
		};
	}

	namespace CWebSocketConnection
	{
		Pattern_t BBuildAndAsyncSendFrame
		{
			"CWebSocketConnection::BBuildAndAsyncSendFrame",
			"89 C6 85 D2 78",
			SigFollowMode::PrologueUpwards,
			std::vector<int16_t> { 0xE8, 0x57, 0xE5, 0x89, 0x55 }
		};
	}

	namespace CSteamUIAppController
	{
		//NOTE: GCC emitted this as a local regparm(3) clone (this=eax, pAppOverview=edx, pApp=ecx).
		//Verified via VPROF xref "CSteamUIAppController::FillInAppOverview" and the call site inside
		//BuildCompleteAppOverviewChange.
		//Anchor: 0xac stack frame allocation + saving (this=eax, pAppOverview=edx, pApp=ecx).
		//PrologueUpwards with { 0x57, 0xE5, 0x89, 0x55 } lands on 55 89 E5 57 (function entry) across all GCC variations.
		Pattern_t FillInAppOverview
		{
			"CSteamUIAppController::FillInAppOverview",
			"81 EC AC 00 00 00 89 45 90 89 55 9C 89 4D 98",
			SigFollowMode::PrologueUpwards,
			std::vector<int16_t> { 0x57, 0xE5, 0x89, 0x55 },
			&g_modSteamUI
		};
		//NOTE: Entry of BuildCompleteAppOverviewChange matching both Desktop and Steam Deck builds.
		Pattern_t BuildCompleteAppOverviewChange
		{
			"CSteamUIAppController::BuildCompleteAppOverviewChange",
			"55 89 E5 57 56 53 E8 ? ? ? ? 81 C3 ? ? ? ? 81 EC 8C 00 00 00 8B 7D 08 89 5D A4",
			SigFollowMode::None,
			&g_modSteamUI
		};
		Pattern_t RunFrame
		{
			"CSteamUIAppController::RunFrame",
			"55 89 E5 56 53 E8 ? ? ? ? 81 C3 ? ? ? ? 83 EC 40 E8",
			SigFollowMode::None,
			&g_modSteamUI
		};
	}

	namespace SteamApp
	{
		Pattern_t Shutdown
		{
			"CSteamApp::Shutdown",
			"C6 46 4C 01 8D 86 94 00 00 00 89 04 24",
			SigFollowMode::PrologueUpwards,
			std::vector<int16_t> { 0x10, 0x24, 0x74, 0x8B, 0x53, 0x56, 0x57 },
			&g_modSteam,
			true
		};
	}

	namespace CPackageInfoCache
	{
		Pattern_t GetPackageInfo
		{
			"CPackageInfoCache::GetPackageInfo",
			"E8 ? ? ? ? 83 C4 10 83 78 18 03",
			SigFollowMode::Relative
		};
	}

	namespace CSteamEngine
	{
		Pattern_t GetServerPipe
		{
			"CSteamEngine::GetServerPipe",
			"0F B7 CA 31 C0 3B 4B",
			SigFollowMode::PrologueUpwards,
			std::vector<int16_t> { 0x53, 0x56, 0x57 }
		};
		Pattern_t SetAppIdForCurrentPipe
		{
			"CSteamEngine::SetAppIdForCurrentPipe",
			"E8 ? ? ? ? 83 C4 ? 8B 45 ? 85 C0 75 ? 31 FF",
			SigFollowMode::Relative
		};
		Pattern_t ProcessIPCFrame
		{
			"CSteamEngine::ProcessIPCFrame",
			"5E C3 FF 74 24",
			SigFollowMode::PrologueUpwards,
			std::vector<int16_t> { 0xC3, 0x81, -1, -1, -1, -1, 0xE8, 0x53, 0x56 }
		};
		Pattern_t Offset_ClientUtils
		{
			"CSteamEngine::m_ClientUtils",
			"89 86 ? ? ? ? 8D 86 ? ? ? ? 89 44 24 ? 50 E8 ? ? ? ? 83 C4",
			SigFollowMode::None
		};
		Pattern_t Offset_User
		{
			"CSteamEngine::m_pUser",
			"8B 80 ? ? ? ? FF 75 ? ? ? ? 56 FF 75",
			SigFollowMode::None
		};
	}

	namespace CUser
	{
		Pattern_t CheckAppOwnership
		{
			"CUser::CheckAppOwnership",
			"0F 94 C2 08 51",
			SigFollowMode::PrologueUpwards,
			std::vector<int16_t> { 0x53, 0x56, 0x57, 0xE5, 0x89, 0x55, -1, -1, -1, -1, 0x5, -1, -1, -1, -1, 0xE8 }
		};
		Pattern_t GetSubscribedApps
		{
			"CUser::GetSubscribedApps",
			"55 89 E5 57 56 E8 ? ? ? ? 81 C6 ? ? ? ? 53 83 C4 80 0F B6 45 14 6A 00 6A 00 88 45 8F 8D 45 D0",
			SigFollowMode::None
		};
		Pattern_t MarkLicenseAsChanged
		{
			"CUser::MarkLicenseAsChanged",
			"55 57 56 53 E8 ? ? ? ? 81 C3 ? ? ? ? 83 EC 2C 8B 74 24 40 8B 44 24 48 8B BE ? ? ? 00 88 44 24 1C",
			SigFollowMode::None,
			nullptr,
			true
		};
		Pattern_t ProcessPendingLicenseUpdates
		{
			"CUser::ProcessPendingLicenseUpdates",
			"55 57 56 53 E8 ? ? ? ? 81 C3 ? ? ? ? 83 EC 2C 8B 44 24 40 8B 88 ? ? ? 00 85 C9 0F 8E",
			SigFollowMode::None,
			nullptr,
			true
		};
		Pattern_t PostCallback
		{
			"CUser::PostCallback",
			"E8 ? ? ? ? 8B 75 ? 89 D8",
			SigFollowMode::Relative
		};
		Pattern_t PostCallbackToAppId
		{
			"CUser::PostCallbackToAppId",
			"84 C0 0F 45 F8 89 F8",
			MemHlp::SigFollowMode::PrologueUpwards,
			std::vector<int16_t> { 0xE8, 0x53, 0x56, 0x57, 0x55 }
		};
		Pattern_t UpdateAppOwnershipTicket
		{
			"CUser::UpdateAppOwnershipTicket",
			"52 57 89 DF FF 75",
			SigFollowMode::PrologueUpwards,
			std::vector<int16_t> { 0x53, 0x56, 0x57, 0xE5, 0x89, 0x55, -1, -1, -1, -1, 0x5, -1, -1, -1, -1, 0xE8 }
		};
		Pattern_t m_OffsetClientUser
		{
			"CUser::m_ClientUser",
			"2D ? ? ? ? C7 44 24 ? ? ? ? ? 81 E1",
			SigFollowMode::None
		};
		Pattern_t m_OffsetUserAppInfo
		{
			"CUser::m_UserAppInfo",
			"8D 90 ? ? ? ? 8B 80 ? ? ? ? 6A ? 8D 4C 24",
			SigFollowMode::None
		};
		Pattern_t m_OffsetUserAppManager
		{
			"CUser::m_UserAppmanager",
			"8D 90 ? ? ? ? 8B 80 ? ? ? ? 68 ? ? ? ? 56",
			SigFollowMode::None
		};
	}

	namespace CUserAppManager
	{
		Pattern_t BuildDepotDependency
		{
			"CUserAppManager::BuildDepotDependency",
			"E8 ? ? ? ? 83 C4 ? 84 C0 74 ? 8B 45 ? 85 C0 89 45",
			SigFollowMode::Relative
		};
	}

	namespace IClientUtils
	{
		Pattern_t Offset_GetPipeIndex
		{
			"IClientUtils::m_PipeIndex",
			"8B 91 ? ? ? ? 83 F8 FF 74 ? 8B 89 ? ? ? ? EB ? ? ? ? 8B 00 83 F8 FF 74 ? 8D 04 ? 8D 04 ? 3B 50",
			SigFollowMode::None,
		};
	}

	std::vector<Pattern_t*> patterns;
}

