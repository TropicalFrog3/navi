/**
 * @file  apps.hpp
 * @brief Declarations for application lifecycle presenters
 */
#pragma once

#include "sdk.hpp"
#include <filesystem>
#include <vector>

#include <cstdint>
#include <map>
#include <set>
#include <mutex>
#include <unordered_set>


namespace Apps
{
	void init();
	extern bool applistRequested;
	extern std::unordered_set<AppId_t> privateApps;

	extern std::mutex pendingLicenseChangesMutex;
	extern std::unordered_set<AppId_t> pendingLicenseChanges;

	bool unlockApp(const AppId_t appId, AppOwnershipInfo_t* info, const CSteamId& ownerId);
	bool unlockApp(const AppId_t appId, AppOwnershipInfo_t* info);

	void buildDepotDependency(AppId_t appId, CUtlVector<DepotInfo_t>* depots, CUtlVector<DepotInfo_t>* sharedDepots);
	bool checkAppOwnership(const AppId_t appId, AppOwnershipInfo_t* info);
	void getLegacyCDKey(const AppId_t appId);
	void getSubscribedApps(AppId_t* appList, const uint32_t size, uint32_t& count);
	void parseProductInfoFromResponse(CMsgClientPICSProductInfoResponse* msg);
	void runIPCFrame();

	extern std::map<uint32_t, int> appIdOwnerOverride;
	extern std::set<uint32_t> installedApps;
	extern std::set<uint32_t> onlineFixApps;
	extern std::set<uint32_t> autoCrackApps;

	bool isNonSteamApp(uint32_t appId);

	bool isInstalled(uint32_t appId);
	void setInstalled(uint32_t appId);
	void removeInstalled(uint32_t appId);
	void deleteGameFiles(uint32_t appId);
	bool gameFilesExist(uint32_t appId);
	std::filesystem::path resolveInstallDir(uint32_t appId);
	std::vector<std::filesystem::path> getSteamLibraryPaths();

	bool isOnlineFixInstalled(uint32_t appId);
	void setOnlineFixInstalled(uint32_t appId, bool installed);

	bool isAutoCrackInstalled(uint32_t appId);
	void setAutoCrackInstalled(uint32_t appId, bool installed);

	void postAppLicensesChanged(const std::unordered_set<AppId_t>& apps);

	bool shouldDisableCloud(const AppId_t appId);
	bool shouldDisableCDKey(const AppId_t appId);
	bool shouldDisableUpdates(const AppId_t appId);

	void sendAndRecvLastPlayedTimes(const char* name, CPlayer_GetLastPlayedTimes_Response* recv);
	void sendGamesPlayed(CNetPacket* pkt);
	void sendPICSInfoRequest(CNetPacket* pkt);
	void sendMsg(CNetPacket* pkt);
	bool shouldChokeRecvPacket(const CNetPacket* pkt);
	uint32_t resolveAppOwnerId(CUser* usr, const AppId_t appId);

	//True when the app is only usable through Family Sharing (legacy or New Steam
	//Families) without a directly owned license. Used to remap presence packets.
	bool isFamilySharedApp(CUser* usr, const AppId_t appId, const uint32_t packetOwnerId = 0);

	void setConfigStoreString(const char* key, const char* value);
};
