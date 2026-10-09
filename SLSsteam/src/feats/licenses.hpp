#pragma once

#include "../sdk/sdk.hpp"
#include <atomic>
#include <unordered_set>
#include <vector>

namespace Licenses
{
	void init();

	void setUser(CUser* pUser);
	void setPackageInfoCache(void* pCache);

	bool isInitialized();
	bool tryInitPackage0();

	void queueRefresh();
	bool hasPendingRefresh();
	void drainPendingRefresh();

	bool appendAppId(CUtlVector<AppId_t>* vec, AppId_t id);
	bool removeAppId(CUtlVector<AppId_t>* vec, AppId_t id);

	bool hasPendingUIRefresh();
	void clearPendingUIRefresh();

	bool wasInjected(AppId_t id);
}
