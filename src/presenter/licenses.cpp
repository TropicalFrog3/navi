#include "licenses.hpp"

#include "config.hpp"
#include "log.hpp"
#include "patterns.hpp"
#include "hooks.hpp"
#include "steam.hpp"
#include "cdpinject.hpp"

#include <algorithm>
#include <mutex>

namespace
{
	typedef int (*CUser_MarkLicenseAsChanged_t)(CUser*, uint32_t, bool);
	typedef bool (*CUser_ProcessPendingLicenseUpdates_t)(CUser*);

	CUser_MarkLicenseAsChanged_t oMarkLicenseAsChanged = nullptr;
	CUser_ProcessPendingLicenseUpdates_t oProcessPendingLicenseUpdates = nullptr;

	void* g_pPackageInfoCache = nullptr;
	CUser* g_pCUser = nullptr;
	PackageInfo* g_pInjectedPackageInfo = nullptr;

	std::recursive_mutex g_licenseMutex;
	std::unordered_set<AppId_t> g_injectedAppIds;
	std::unordered_set<AppId_t> g_everInjectedAppIds;
	std::atomic<bool> g_pendingRefresh{false};
	std::atomic<bool> g_pendingUIRefresh{false};
	bool g_package0Initialized = false;

	constexpr uint32_t kInjectedPackageId = 0;
	constexpr uint64_t kInjectedPkgAccessToken = 10660652434190618804ull;
}

namespace Licenses
{
	void init()
	{
		if (Patterns::CUser::MarkLicenseAsChanged.address != LM_ADDRESS_BAD)
		{
			oMarkLicenseAsChanged = reinterpret_cast<CUser_MarkLicenseAsChanged_t>(
				Patterns::CUser::MarkLicenseAsChanged.address);
			LOG_INFO("Licenses: Resolved MarkLicenseAsChanged at 0x%x\n",
			         Patterns::CUser::MarkLicenseAsChanged.address);
		}
		else
		{
			LOG_WARN("Licenses: Failed to resolve MarkLicenseAsChanged\n");
		}

		if (Patterns::CUser::ProcessPendingLicenseUpdates.address != LM_ADDRESS_BAD)
		{
			oProcessPendingLicenseUpdates = reinterpret_cast<CUser_ProcessPendingLicenseUpdates_t>(
				Patterns::CUser::ProcessPendingLicenseUpdates.address);
			LOG_INFO("Licenses: Resolved ProcessPendingLicenseUpdates at 0x%x\n",
			         Patterns::CUser::ProcessPendingLicenseUpdates.address);
		}
		else
		{
			LOG_WARN("Licenses: Failed to resolve ProcessPendingLicenseUpdates\n");
		}
	}

	void setUser(CUser* pUser)
	{
		if (pUser && !g_pCUser)
		{
			std::lock_guard<std::recursive_mutex> lock(g_licenseMutex);
			g_pCUser = pUser;
			LOG_INFO("Licenses: Captured CUser at %p\n", reinterpret_cast<void*>(g_pCUser));
			queueRefresh();
		}
	}

	void setPackageInfoCache(void* pCache)
	{
		if (pCache && !g_pPackageInfoCache)
		{
			std::lock_guard<std::recursive_mutex> lock(g_licenseMutex);
			g_pPackageInfoCache = pCache;
			LOG_INFO("Licenses: Captured CPackageInfoCache at %p\n", g_pPackageInfoCache);
			queueRefresh();
		}
	}

	bool isInitialized()
	{
		std::lock_guard<std::recursive_mutex> lock(g_licenseMutex);
		return g_package0Initialized;
	}

	bool appendAppId(CUtlVector<AppId_t>* vec, AppId_t id)
	{
		if (!vec)
			return false;

		for (uint32_t i = 0; i < vec->size; ++i)
		{
			if (vec->mem.base && vec->mem.base[i] == id)
				return true;
		}

		if (static_cast<int32_t>(vec->mem.growSize) < 0)
		{
			LOG_WARN("appendAppId: vector uses external buffer, cannot grow\n");
			return false;
		}

		if (vec->size >= vec->mem.alloc)
		{
			const uint32_t newAlloc = vec->mem.alloc ? vec->mem.alloc * 2 : 16;
			const size_t bytes = static_cast<size_t>(newAlloc) * sizeof(AppId_t);
			void* mem = vec->mem.base
				? Steam::Plat_Realloc(vec->mem.base, bytes)
				: Steam::Plat_Alloc(bytes);
			if (!mem)
			{
				LOG_ERROR("appendAppId: allocation failed for %u elements\n", newAlloc);
				return false;
			}
			vec->mem.base = reinterpret_cast<AppId_t*>(mem);
			vec->mem.alloc = newAlloc;
		}

		vec->mem.base[vec->size++] = id;
		return true;
	}

	bool removeAppId(CUtlVector<AppId_t>* vec, AppId_t id)
	{
		if (!vec || !vec->mem.base)
			return false;

		for (uint32_t i = 0; i < vec->size; ++i)
		{
			if (vec->mem.base[i] == id)
			{
				if (i != vec->size - 1)
				{
					vec->mem.base[i] = vec->mem.base[vec->size - 1];
				}
				vec->size--;
				return true;
			}
		}
		return false;
	}

	bool tryInitPackage0()
	{
		std::lock_guard<std::recursive_mutex> lock(g_licenseMutex);
		if (g_package0Initialized)
			return true;

		if (!g_pPackageInfoCache || !Hooks::CPackageInfoCache_GetPackageInfo.tramp.fn)
			return false;

		PackageInfo* pPkg = Hooks::CPackageInfoCache_GetPackageInfo.tramp.fn(
			g_pPackageInfoCache, kInjectedPackageId, kInjectedPkgAccessToken);
		if (!pPkg)
		{
			LOG_WARN("tryInitPackage0: GetPackageInfo returned null for package 0\n");
			return false;
		}

		g_pInjectedPackageInfo = pPkg;

		if (pPkg->status != 0) // EPackageStatus::Available is 0
		{
			LOG_WARN("tryInitPackage0: package 0 status is not Available (%u)\n", pPkg->status);
			return false;
		}

		// Populate all currently added apps
		const auto addedApps = g_config.addedAppIds.get();
		for (AppId_t id : addedApps)
		{
			if (appendAppId(&pPkg->appIdVec, id))
			{
				g_injectedAppIds.insert(id);
				g_everInjectedAppIds.insert(id);
			}
		}

		g_package0Initialized = true;
		LOG_INFO("Licenses: Package 0 initialized with %u apps (total in vector: %u)\n",
		         static_cast<uint32_t>(g_injectedAppIds.size()), pPkg->appIdVec.size);

		if (g_pCUser && oMarkLicenseAsChanged && oProcessPendingLicenseUpdates)
		{
			oMarkLicenseAsChanged(g_pCUser, kInjectedPackageId, true);
			oProcessPendingLicenseUpdates(g_pCUser);
			LOG_INFO("Licenses: Initial licenses processed for Package 0\n");
		}

		return true;
	}

	void queueRefresh()
	{
		g_pendingRefresh.store(true, std::memory_order_release);
		LOG_DEBUG("Licenses: Refresh queued\n");
	}

	bool hasPendingRefresh()
	{
		return g_pendingRefresh.load(std::memory_order_acquire);
	}

	void drainPendingRefresh()
	{
		if (!hasPendingRefresh())
			return;

		std::lock_guard<std::recursive_mutex> lock(g_licenseMutex);
		if (!g_pCUser || !g_pPackageInfoCache || !Hooks::CPackageInfoCache_GetPackageInfo.tramp.fn)
		{
			return;
		}

		if (!g_package0Initialized)
		{
			if (!tryInitPackage0())
			{
				return;
			}
			g_pendingRefresh.store(false, std::memory_order_release);
			return;
		}

		if (!oMarkLicenseAsChanged || !oProcessPendingLicenseUpdates)
		{
			return;
		}

		const auto currentConfigApps = g_config.addedAppIds.get();
		uint32_t additionsCount = 0;
		uint32_t removalsCount = 0;

		// 1. Process additions
		for (AppId_t id : currentConfigApps)
		{
			if (!g_injectedAppIds.contains(id))
			{
				if (appendAppId(&g_pInjectedPackageInfo->appIdVec, id))
				{
					g_injectedAppIds.insert(id);
					g_everInjectedAppIds.insert(id);
					additionsCount++;
					LOG_INFO("Licenses: Injected AppID %u into Package 0\n", id);
				}
			}
		}

		// 2. Process removals
		std::vector<AppId_t> toRemove;
		for (AppId_t id : g_injectedAppIds)
		{
			if (!currentConfigApps.contains(id))
			{
				toRemove.push_back(id);
			}
		}
		for (AppId_t id : toRemove)
		{
			if (removeAppId(&g_pInjectedPackageInfo->appIdVec, id))
			{
				g_injectedAppIds.erase(id);
				removalsCount++;
				CDPInject::notifyAppRemoved(id);
				LOG_INFO("Licenses: Removed AppID %u from Package 0\n", id);
			}
		}

		if (additionsCount > 0 || removalsCount > 0)
		{
			oMarkLicenseAsChanged(g_pCUser, kInjectedPackageId, true);
			bool res = oProcessPendingLicenseUpdates(g_pCUser);
			g_pendingUIRefresh.store(true, std::memory_order_release);
			LOG_INFO("Licenses: License update dispatched! (+%u / -%u, res=%d, total in vector=%u)\n",
			         additionsCount, removalsCount, res ? 1 : 0, g_pInjectedPackageInfo->appIdVec.size);
		}
		else
		{
			LOG_DEBUG("Licenses: drainPendingRefresh had no additions or removals\n");
		}

		g_pendingRefresh.store(false, std::memory_order_release);
	}

	bool hasPendingUIRefresh()
	{
		return g_pendingUIRefresh.load(std::memory_order_acquire);
	}

	void clearPendingUIRefresh()
	{
		g_pendingUIRefresh.store(false, std::memory_order_release);
	}

	bool wasInjected(AppId_t id)
	{
		std::lock_guard<std::recursive_mutex> lock(g_licenseMutex);
		return g_everInjectedAppIds.contains(id);
	}
}
