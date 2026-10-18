#pragma once

#include "types.hpp"
#include "CUtl.hpp"

#include <cstddef>
#include <cstdint>


class IClientAppManager;
class IClientApps;
class IClientUser;

enum class ECallbackType : uint32_t
{
	LicensesUpdate_t = 0x7d,
	AppOwnershipTicketReceived_t = 0xf907c,
	AppLicensesChanged_t = 0xf90be
};

enum EReleaseState
{
	k_EAppReleaseStateUnknown = 0x0,
	k_EAppReleaseStateUnavailable = 0x1,
	k_EAppReleaseStatePrerelease = 0x2,
	k_EAppReleaseStatePreloadonly = 0x3,
	k_EAppReleaseStateReleased = 0x4,
	k_EAppReleaseStateDisabled = 0x5
};

SDK_Struct AppOwnershipTicketReceived_t
{
	EResult result;
	AppId_t appId;
};

SDK_Struct AppLicensesChanged_t
{
	static constexpr unsigned int MAX_APPS_PER_CALLBACK = 0x40;

	bool reloadAll;							//0x0
	bool firstLoad;							//0x1
	uint8_t __pad_0x2[0x2];					//0x2
	uint32_t remainingPackets;				//0x4
	uint32_t count;							//0x8
	AppId_t apps[MAX_APPS_PER_CALLBACK];	//0xC
	uint64_t appsAdded;						//0x10C
}; //0x114

SDK_Struct PackageInfo
{
	AppId_t             packageId;              // +0x00
	uint32_t            changeNumber;           // +0x04
	uint64_t            picsToken;              // +0x08
	uint32_t            billingType;            // +0x10
	uint32_t            licenseType;            // +0x14
	uint32_t            status;                 // +0x18 (3 = Available)
	uint8_t             sha1Hash[20];           // +0x1C
	void*               pPackageInfoNode;       // +0x30
	void*               pExtendNode;            // +0x34
	CUtlVector<AppId_t> appIdVec;               // +0x38
	CUtlVector<AppId_t> depotIdVec;             // +0x48
};
static_assert(offsetof(PackageInfo, appIdVec) == 0x38, "PackageInfo::appIdVec offset must be 0x38");
static_assert(offsetof(PackageInfo, depotIdVec) == 0x48, "PackageInfo::depotIdVec offset must be 0x48");

SDK_Struct AppOwnershipInfo_t {
    int32_t subId;
    int32_t releaseState;
    uint32_t owner;
    int32_t masterSubscriptionAppId;
    uint32_t trialTime;
    uint32_t numLicenses;
    char region[4]; //Client copies this like a DWORD, so even though CountryCodes are only 2 bytes 4 seems to be correct
    uint32_t purchaseTime;
    uint32_t realOwner;
    bool ownsLicense;
    bool licenseExpired;
    bool field12_0x26;
    bool lowViolence;
    bool freeLicense;
    bool regionRestricted;
    bool fromFreeWeekend;
    bool licenseLocked;
    bool licensePending;
    bool retailLicense;
    bool autoGrant;
    bool licensePermanent;
    bool field21_0x30;
    bool field22_0x31;
    bool siteLicense;
    bool field24_0x33;
    bool field25_0x34;
    bool familyShared;
    bool field27_0x36;
    bool field28_0x37;
}; //0x38

SDK_Class CUser
{
public:
	IClientAppManager* getAppManager();
	IClientApps* getClientApps();
	IClientUser* getClientUser();

	bool checkAppOwnership(const AppId_t appId, AppOwnershipInfo_t* pInfo);
	bool isSubscribed(const AppId_t appId);

	void postCallback(const ECallbackType type, void* pCallback, const uint32_t callbackSize);
	void updateAppOwnershipTicket(const AppId_t appId, void* pTicket, const uint32_t len);
};
