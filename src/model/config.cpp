/**
 * @file  config.cpp
 * @brief Configuration loader, YAML parser, and in-memory setting manager
 */
#include "config.hpp"
#include "config_default.hpp"

#include "config_migration.hpp"
#include "cache_migration.hpp"
#include "cloudredirect.hpp"
#include "depotkeys.hpp"
#include "licenses.hpp"
#include "filewatcher.hpp"
#include "log.hpp"
#include "utils.hpp"
#include "atomic_file.hpp"

#include "yaml-cpp/yaml.h"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_set>
#include <sys/inotify.h>
#include <vector>


std::string CConfig::getDir() const
{
	std::ostringstream path;

	const char* configDir = getenv("XDG_CONFIG_HOME"); //Most users should have this set iirc
	if (configDir)
	{
		path << configDir;
	}
	else
	{
		LOG_CUSTOM(k_ELogLevelWarn | k_ELogLevelOnce, "XDG_CONFIG_HOME not set! Falling back to HOME\n");

		const char* home = getenv("HOME");
		path << home << "/.config";
	}

	path << "/navi";

	return path.str();
}

std::string CConfig::getPath() const
{
	return getDir() + "/config.yaml";
}

std::string CConfig::getPluginDir()
{
	const char* home = std::getenv("HOME");
	if (!home) return "";

	const std::string candidatePaths[] = {
		std::string(home) + "/.steam/steam",
		std::string(home) + "/.local/share/Steam"
	};

	for (const auto& candidate : candidatePaths)
	{
		if (std::filesystem::exists(candidate))
		{
			return candidate + "/config/stplug-in";
		}
	}
	return "";
}

bool CConfig::createFile() const
{
	const std::string path = getPath();
	if (!std::filesystem::exists(path))
	{
		const std::string dir = getDir();
		if (!std::filesystem::exists(dir))
		{
			if (!std::filesystem::create_directory(dir))
			{
				LOG_NOTIFY("Unable to create config directory at %s!\n", dir.c_str());
				return false;
			}

			LOG_DEBUG("Created config directory at %s\n", dir.c_str());
		}

		if (!AtomicFile::write(path, defaultConfig))
		{
			LOG_NOTIFY("Unable to create %s!", path.c_str());
			return false;
		}
	}

	return true;
}

static void onFileChange(const char* filename)
{
	if (filename)
	{
		std::string s(filename);
		if (!s.ends_with(".lua") && !s.ends_with(".manifest") && s != "config.yaml" && s != "depot_keys.yaml")
		{
			return;
		}
		LOG_DEBUG("onFileChange triggered by %s\n", filename);
	}

	DepotKeys::loadDepotKeysYaml();
	g_config.loadSettings();
	scanLuaPluginsAndUpdateConfig();
	DepotKeys::scanLuaPluginsForDepotKeys();
	if (g_config.cloudSavesEnabled.get())
	{
		Navi::CloudRedirect::CCloudRedirectBridge::Instance().SyncSlsSteamConfig(g_config.GetEffectiveCloudRedirectedApps());
	}
	Licenses::queueRefresh();
	LOG_NOTIFY("Config reloaded!");
}

bool CConfig::init()
{
	if(createFile())
	{
		const char* home = std::getenv("HOME");
		if (home)
		{
			std::string userConfigPath = getPath();
			std::string diffYamlPath = std::string(home) + "/.local/share/navi/res/config.diff.yaml";
			std::string templatePath = std::string(home) + "/.local/share/navi/res/config.yaml";
			
			ConfigMigration::migrate(userConfigPath, diffYamlPath, templatePath);

			std::string cacheDiffPath = std::string(home) + "/.local/share/navi/res/cache.diff.yaml";
			std::string cacheVerPath = std::string(home) + "/.local/share/navi/cache_version";
			CacheMigration::migrate(cacheDiffPath, cacheVerPath);
		}

		watcher = new CFileWatcher(onFileChange);
		watcher->addWatch(getPath().c_str());

		std::string pluginDir = getPluginDir();
		if (!pluginDir.empty())
		{
			if (!std::filesystem::exists(pluginDir))
			{
				std::filesystem::create_directories(pluginDir);
			}
			// IN_MODIFY | IN_CREATE | IN_DELETE | IN_MOVED_TO | IN_MOVED_FROM
			watcher->addWatch(pluginDir.c_str(), IN_MODIFY | IN_CREATE | IN_DELETE | IN_MOVED_TO | IN_MOVED_FROM);
		}

		watcher->start();
	}

	loadSettings();
	return true;
}

void CConfig::shutdown()
{
	if (watcher)
	{
		watcher->stop();
		delete watcher;
		watcher = nullptr;
	}
}

CConfig::~CConfig()
{
	shutdown();
}

void CConfig::setError(const ELoadError err, const char* keyName)
{
	const auto prev = __loadErrors.get();
	std::ostringstream msg;

	if (!prev.size())
	{
		msg << "Config loading issues encountered:\n";
	}
	else
	{
		msg << prev << "\n";
	}

	switch(err)
	{
		case ELoadError::MissingKey:
			msg << "Missing " << keyName;
			break;
	
		case ELoadError::ParsingException:
			msg << "Failed to parse " << keyName;
			break;

		default:
			break;
	}

	__loadErrors = msg.str();
}

bool CConfig::loadSettings(bool firstLoad)
{
	YAML::Node node;
	try
	{
		node = YAML::LoadFile(getPath());
	}
	catch (YAML::BadFile& bf)
	{
		LOG_NOTIFYLONG("Can not read config.yaml! %s\nUsing defaults", bf.msg.c_str());
		node = YAML::Node(); //Create empty node and let defaults kick in
	}
	catch (YAML::ParserException& pe)
	{
		LOG_NOTIFYLONG("Error parsing config.yaml! %s\nUsing defaults", pe.msg.c_str());
		node = YAML::Node(); //Create empty node and let defaults kick in
	}

	__loadErrors = std::string("");
	
	//Parse logLevels first, otherwise settings won't get logged
	logLevels = getSetting<uint32_t>(node, "LogLevels", 0xff, true);
	api = getSetting<bool>(node, "API", true);
	if (api.get())
	{
		logLevels = logLevels.get() | k_ELogLevelAPI;
	}

	//This is shitty, but to do it properly have to do something even shittier
	LOG_CUSTOM
	(
		k_ELogLevelInfo | k_ELogLevelOnce,
		"LogLevels is \"%s\"\n",

		ELogLevel_ToString(logLevels.get()).c_str()
	);

	disableFamilyLock = getSetting<bool>(node, "DisableFamilyShareLock", true);
	useWhiteList = getSetting<bool>(node, "UseWhitelist", false);
	maxSchemaTries = getSetting<uint32_t>(node, "MaxSchemaTries", 10);
	safeMode = getSetting<bool>(node, "SafeMode", false);
	warnHashMissmatch = getSetting<bool>(node, "WarnHashMissmatch", false);
	notifyInit = getSetting<bool>(node, "NotifyInit", true);
	fakeName = getSetting<std::string>(node, "FakeName", "");
	fakeEmail = getSetting<std::string>(node, "FakeEmail", "");
	fakeWalletBalance = getSetting<int32_t>(node, "FakeWalletBalance", 0);
	disableCloud = getSetting<bool>(node, "DisableCloud", true);
	disableUpdates = getSetting<bool>(node, "DisableUpdates", true);
	dumpInterfaceMaps = getSetting<bool>(node, "DumpClientInterfaces", false);
	extendedLogging = getSetting<bool>(node, "ExtendedLogging", false);
	
	ryuuKey = getSetting<std::string>(node, "RyuuKey", "");
	depotBoxKey = getSetting<std::string>(node, "DepotBoxKey", "");
	hubcapKey = getSetting<std::string>(node, "HubcapKey", "");

	const std::lock_guard appsChanged(appsChangedMutex);
	const auto prevAppIds = addedAppIds.get();
	const auto _addedAppIds = getList<AppId_t>(node, "AdditionalApps");

	if (!firstLoad)
	{
		for (const auto& appId : prevAppIds)
		{
			if (_addedAppIds.contains(appId))
			{
				continue;
			}

			removedApps.emplace(appId);
			LOG_DEBUG("AppId %u removed from AdditionalApps\n", appId);
		}
		for (const auto& appId : _addedAppIds)
		{
			if (prevAppIds.contains(appId))
			{
				continue;
			}

			newApps.emplace(appId);
			// LOG_DEBUG("AppId %u added to AdditionalApps\n", appId);
		}
	}

	addedAppIds = _addedAppIds;

	appIds = getList<AppId_t>(node, "AppIds");
	fakeOffline = getList<AppId_t>(node, "FakeOffline");
	depotBlacklist = getList<AppId_t>(node, "DepotBlacklist");

	fakeAppIds = getMap<AppId_t, AppId_t>(node, "FakeAppIds");
	manifestIds = getMap<AppId_t, uint64_t>(node, "ManifestIds");
	appTokens = getMap<AppId_t, uint64_t>(node, "AppTokens");
	cdKeys = getMap<AppId_t, std::string>(node, "CDKeys", true);
	gameTitles = getMap<AppId_t, std::string>(node, "GameTitles");
	subscriptionTimestamps = getMap<AppId_t, uint32_t>(node, "SubscriptionTimestamps");
	steamIdOverride = getMap<AppId_t, uint64_t>(node, "SteamIdOverride");

	//Do not log the keys themself
	for (const auto& key : cdKeys.get())
	{
		LOG_CUSTOM(k_ELogLevelInfo | k_ELogLevelOnce, "Added CDKey for %u\n", key.first);
	}

	//Do not warn for these (yet?)
	const auto idleStatusNode = node["IdleStatus"];
	if (idleStatusNode)
	{
		try
		{
			const auto appId = idleStatusNode["AppId"].as<AppId_t>();
			const auto title = idleStatusNode["Title"].as<std::string>();

			idleStatus = FakeGame_t
			{
				appId,
				title
			};

			LOG_CUSTOM(k_ELogLevelInfo | k_ELogLevelOnce, "Idle status %s with AppId %u\n", title.c_str(), appId);
		}
		catch(...)
		{
			//LOG_NOTIFYWARN("Failed to parse IdleStatus!");A
			setError(ELoadError::ParsingException, "IdleStatus");
		}
	}
	const auto unownedStatusNode = node["UnownedStatus"];
	if (unownedStatusNode)
	{
		try
		{
			auto appId = unownedStatusNode["AppId"].as<uint32_t>();
			auto title = unownedStatusNode["Title"].as<std::string>();

			unownedStatus = FakeGame_t
			{
				appId,
				title
			};

			LOG_INFO("Unowned status %s with AppId %u\n", title.c_str(), appId);
		}
		catch(...)
		{
			//LOG_WARN("Failed to parse UnownedStatus");
			setError(ELoadError::ParsingException, "UnownedStatus");
		}
	}


	const auto dlcDataNode = node["DlcData"];
	if (dlcDataNode)
	{
		auto _dlcData = dlcData.empty();

		for (auto& app : dlcDataNode)
		{
			try
			{
				const AppId_t parentId = app.first.as<AppId_t>();
				LOG_CUSTOM(k_ELogLevelInfo | k_ELogLevelOnce, "Parsing DlcData for %u\n", parentId);
				const auto dlcIds = getMap<AppId_t, std::string>(dlcDataNode, std::to_string(parentId).c_str());

				CDlcData& data = _dlcData[parentId];
				data.parentId = parentId;
				data.dlcIds = dlcIds;
			}
			catch(...)
			{
				//LOG_NOTIFY("Failed to parse DlcData!");
				setError(ELoadError::ParsingException, "DlcData");
				break;
			}
		}

		dlcData = _dlcData;
	}
	else
	{
		//LOG_NOTIFY("Missing DlcData entry in config!");
		setError(ELoadError::MissingKey, "DlcData");
	}

	const auto denuvoGamesNode = node["DenuvoGames"];
	if (denuvoGamesNode)
	{
		auto _denuvoGames = denuvoGames.empty();

		for (auto& steamIdNode : denuvoGamesNode)
		{
			try
			{
				const uint64_t steamId = steamIdNode.first.as<uint64_t>();
				_denuvoGames[steamId] = std::unordered_set<AppId_t>();

				for (auto& appIdNode : steamIdNode.second)
				{
					const AppId_t appId = appIdNode.as<AppId_t>();
					_denuvoGames[steamId].emplace(appId);

					//Again, not loggin SteamId because of privacy
					LOG_CUSTOM(k_ELogLevelInfo | k_ELogLevelOnce, "Added DenuvoGame %u\n", appId);
				}
			}
			catch (...)
			{
				//LOG_NOTIFY("Failed to parse DenuvoGames!");
				setError(ELoadError::ParsingException, "DenuvoGames");
			}
		}

		denuvoGames.set(_denuvoGames);
	}
	else
	{
		//LOG_NOTIFY("Missing DenuvoGames entry in config!");
		setError(ELoadError::MissingKey, "DenuvoGames");
	}

	const auto cloudSavesNode = node["CloudSaves"];
	if (cloudSavesNode)
	{
		cloudSavesEnabled.set(getSetting<bool>(cloudSavesNode, "Enabled", true));
		cloudSavesProvider.set(getSetting<std::string>(cloudSavesNode, "Provider", "local-only"));
		if (cloudSavesNode["NotificationsEnabled"])
		{
			cloudNotificationsEnabled.set(getSetting<bool>(cloudSavesNode, "NotificationsEnabled", true));
		}
		else
		{
			cloudNotificationsEnabled.set(true);
		}
		cloudAutoBackupBeforeReset.set(getSetting<bool>(cloudSavesNode, "AutoBackupBeforeReset", true));
		cloudBackupRetentionCount.set(getSetting<uint32_t>(cloudSavesNode, "BackupRetentionCount", 5));
		cloudSavesManagedApps.set(getList<AppId_t>(cloudSavesNode, "ManagedApps"));
		if (cloudSavesNode["DisabledApps"])
		{
			cloudSavesDisabledApps.set(getList<AppId_t>(cloudSavesNode, "DisabledApps", true));
		}
		else
		{
			cloudSavesDisabledApps.set({});
		}

		const auto prefixSyncNode = cloudSavesNode["PrefixSync"];
		if (prefixSyncNode)
		{
			cloudPrefixSyncEnabled.set(getSetting<bool>(prefixSyncNode, "Enabled", true));
			cloudPrefixSyncAtStartUp.set(getSetting<bool>(prefixSyncNode, "SyncAtStartup", true));
			cloudPrefixSyncOnLaunch.set(getSetting<bool>(prefixSyncNode, "SyncOnLaunch", true));
			cloudPrefixSyncOnExit.set(getSetting<bool>(prefixSyncNode, "SyncOnExit", true));
			cloudPrefixSyncMaxFileMb.set(getSetting<uint32_t>(prefixSyncNode, "MaxFileMb", 64));
		}
		else
		{
			cloudPrefixSyncEnabled.set(true);
			cloudPrefixSyncAtStartUp.set(true);
			cloudPrefixSyncOnLaunch.set(true);
			cloudPrefixSyncOnExit.set(true);
			cloudPrefixSyncMaxFileMb.set(64);
		}

		const auto statsSyncNode = cloudSavesNode["StatsSync"];
		if (statsSyncNode)
		{
			cloudStatsSyncEnabled.set(getSetting<bool>(statsSyncNode, "Enabled", false));
			cloudSyncAchievements.set(getSetting<bool>(statsSyncNode, "SyncAchievements", true));
			cloudSyncPlaytime.set(getSetting<bool>(statsSyncNode, "SyncPlaytime", true));
		}
		else
		{
			cloudStatsSyncEnabled.set(false);
			cloudSyncAchievements.set(true);
			cloudSyncPlaytime.set(true);
		}

		bool syncKeysMaster = cloudSavesNode["SyncProviderKeys"]
			? getSetting<bool>(cloudSavesNode, "SyncProviderKeys", true)
			: true;
		bool syncRyuu = cloudSavesNode["SyncRyuuKey"]
			? getSetting<bool>(cloudSavesNode, "SyncRyuuKey", true)
			: true;
		bool syncDepotBox = cloudSavesNode["SyncDepotBoxKey"]
			? getSetting<bool>(cloudSavesNode, "SyncDepotBoxKey", true)
			: true;
		bool syncHubcap = cloudSavesNode["SyncHubcapKey"]
			? getSetting<bool>(cloudSavesNode, "SyncHubcapKey", true)
			: true;

		const auto providerKeysSyncNode = cloudSavesNode["ProviderKeysSync"];
		if (providerKeysSyncNode && providerKeysSyncNode.IsMap())
		{
			if (providerKeysSyncNode["Enabled"])
			{
				syncKeysMaster = getSetting<bool>(providerKeysSyncNode, "Enabled", syncKeysMaster);
			}
			if (providerKeysSyncNode["SyncRyuuKey"])
			{
				syncRyuu = getSetting<bool>(providerKeysSyncNode, "SyncRyuuKey", syncRyuu);
			}
			if (providerKeysSyncNode["SyncDepotBoxKey"])
			{
				syncDepotBox = getSetting<bool>(providerKeysSyncNode, "SyncDepotBoxKey", syncDepotBox);
			}
			if (providerKeysSyncNode["SyncHubcapKey"])
			{
				syncHubcap = getSetting<bool>(providerKeysSyncNode, "SyncHubcapKey", syncHubcap);
			}
		}
		cloudSyncProviderKeys.set(syncKeysMaster);
		cloudSyncRyuuKey.set(syncRyuu);
		cloudSyncDepotBoxKey.set(syncDepotBox);
		cloudSyncHubcapKey.set(syncHubcap);
	}
	else
	{
		cloudSavesEnabled.set(true);
		cloudSyncProviderKeys.set(true);
		cloudSyncRyuuKey.set(true);
		cloudSyncDepotBoxKey.set(true);
		cloudSyncHubcapKey.set(true);
		cloudSavesProvider.set("local-only");
		cloudNotificationsEnabled.set(true);
		cloudAutoBackupBeforeReset.set(true);
		cloudBackupRetentionCount.set(5);
		cloudSavesManagedApps.set({});
		cloudSavesDisabledApps.set({});

		cloudPrefixSyncEnabled.set(true);
		cloudPrefixSyncAtStartUp.set(true);
		cloudPrefixSyncOnLaunch.set(true);
		cloudPrefixSyncOnExit.set(true);
		cloudPrefixSyncMaxFileMb.set(64);

		cloudStatsSyncEnabled.set(false);
		cloudSyncAchievements.set(true);
		cloudSyncPlaytime.set(true);
	}

	const auto autoCrackNode = node["autocrack"];
	if (autoCrackNode)
	{
		autoCrackEnabled.set(getSetting<bool>(autoCrackNode, "enabled", true));
		autoCrackAuto.set(getSetting<bool>(autoCrackNode, "auto", false));
		autoCrackAutoAllowlist.set(getList<AppId_t>(autoCrackNode, "autoAllowlist"));
		autoCrackGameInfoApi.set(getSetting<std::string>(autoCrackNode, "gameInfoApi", "steamclient"));
		autoCrackWebApiKey.set(getSetting<std::string>(autoCrackNode, "webApiKey", ""));
		autoCrackExperimentalEmu.set(getSetting<bool>(autoCrackNode, "experimentalEmu", false));
		autoCrackUnpackSteamStub.set(getSetting<bool>(autoCrackNode, "unpackSteamStub", true));
		autoCrackInactivityTimeoutSec.set(getSetting<uint32_t>(autoCrackNode, "inactivityTimeoutSec", 120));
	}
	else
	{
		autoCrackEnabled.set(true);
		autoCrackAuto.set(false);
		autoCrackAutoAllowlist.set({});
		autoCrackGameInfoApi.set("steamclient");
		autoCrackWebApiKey.set("");
		autoCrackExperimentalEmu.set(false);
		autoCrackUnpackSteamStub.set(true);
		autoCrackInactivityTimeoutSec.set(120);
	}

	const auto errors = __loadErrors.get();
	if (errors.size())
	{
		//We know this isn't build by user input, so disabling the warning is fine for this line
		#pragma GCC diagnostic push
		#pragma GCC diagnostic ignored "-Wformat-security"

		LOG_NOTIFYWARN(errors.c_str());

		#pragma GCC diagnostic pop
	}

	Navi::CloudRedirect::EngineConfig_t engCfg;
	engCfg.provider = cloudSavesProvider.get();
	engCfg.notificationsEnabled = cloudNotificationsEnabled.get();
	engCfg.statsSyncEnabled = cloudStatsSyncEnabled.get();
	engCfg.syncAchievements = cloudSyncAchievements.get();
	engCfg.syncPlaytime = cloudSyncPlaytime.get();
	Navi::CloudRedirect::CCloudRedirectBridge::Instance().SyncEngineConfig(engCfg);

	return true;
}

bool Navi::CloudRedirect::CCloudRedirectBridge::SyncEngineConfigFromGlobal()
{
	Navi::CloudRedirect::EngineConfig_t cfg;
	cfg.provider = g_config.cloudSavesProvider.get();
	cfg.notificationsEnabled = g_config.cloudNotificationsEnabled.get();
	cfg.statsSyncEnabled = g_config.cloudStatsSyncEnabled.get();
	cfg.syncAchievements = g_config.cloudSyncAchievements.get();
	cfg.syncPlaytime = g_config.cloudSyncPlaytime.get();
	return SyncEngineConfig(cfg);
}

bool CConfig::isAddedAppId(const AppId_t appId)
{
	return addedAppIds.get().contains(appId);
}

std::unordered_set<AppId_t> CConfig::GetEffectiveCloudRedirectedApps() const
{
	auto* self = const_cast<CConfig*>(this);
	std::unordered_set<AppId_t> effective;
	if (!self->cloudSavesEnabled.get())
	{
		return effective;
	}

	const auto disabled = self->cloudSavesDisabledApps.get();
	auto addIfEnabled = [&](AppId_t id) {
		if (id != 0 && !disabled.contains(id))
		{
			effective.insert(id);
		}
	};

	for (AppId_t id : self->cloudSavesManagedApps.get()) addIfEnabled(id);
	for (AppId_t id : self->addedAppIds.get()) addIfEnabled(id);
	for (const auto& [realId, fakeId] : self->fakeAppIds.get())
	{
		addIfEnabled(realId);
		addIfEnabled(fakeId);
	}
	if (self->useWhiteList.get())
	{
		for (AppId_t id : self->appIds.get()) addIfEnabled(id);
	}

	return effective;
}

bool CConfig::IsAppCloudRedirected(AppId_t appId) const
{
	auto* self = const_cast<CConfig*>(this);
	if (!appId || !self->cloudSavesEnabled.get())
	{
		return false;
	}

	if (self->cloudSavesDisabledApps.get().contains(appId))
	{
		return false;
	}

	if (self->cloudSavesManagedApps.get().contains(appId) ||
	    self->addedAppIds.get().contains(appId))
	{
		return true;
	}

	const auto fakeMap = self->fakeAppIds.get();
	if (fakeMap.contains(appId))
	{
		return true;
	}
	for (const auto& [realId, fakeId] : fakeMap)
	{
		(void)realId;
		if (fakeId == appId) return true;
	}

	if (self->useWhiteList.get() && self->appIds.get().contains(appId))
	{
		return true;
	}

	return false;
}

bool CConfig::shouldExcludeAppId(const AppId_t appId, const bool ignoreAdditionalApps)
{
	bool exclude = false;
	//Proper way would be with getAppType, but that seems broken so we need to do this instead
	constexpr AppId_t ONE_BILLION = 1E9; //Implicit cast from double to unsigned int, hopefully this does not break anything
	if (appId >= ONE_BILLION) //Higher and equal to 10^9 gets used by Steam Internally
	{
		exclude = true;
	}
	else
	{
		const bool whitelist = useWhiteList.get();
		const bool found = appIds.get().contains(appId);
		exclude = (!isAddedAppId(appId) || ignoreAdditionalApps) && ((whitelist && !found) || (!whitelist && found));

		if (!ignoreAdditionalApps && g_pSteamEngine)
		{
			const auto usr = g_pSteamEngine->getUser();
			const auto appInfo = usr ? usr->getClientApps() : nullptr;

			//Might be worth to check for APPTYPE_DLC, but knowing Valve & individual gamedevs
			//surely not every DLC will be tagged as such
			char chParent[16] { };
			const int len = appInfo ? appInfo->getAppData(appId, "parent", chParent, sizeof(chParent)) : 0;
			//Do not blindly trust len, nor the str included. Some devs just like to mess with Valve or something (for example appId 221300)
			if (len > 0 && Utils::isNumber(chParent))
			{
				//LOG_DEBUG("AppId %i, parent %s (%i)\n", appId, chParent, len);
				AppId_t parentId = std::stoul(chParent);

				if (whitelist && !shouldExcludeAppId(parentId, true))
				{
					//LOG_DEBUG("Override exclude %i with false, because parent %u isn't excluded\n", exclude, parentId);
					exclude = false;
				}
				else if (!whitelist && shouldExcludeAppId(parentId, true))
				{
					//LOG_DEBUG("Override exclude %i with true, because parent %u is excluded\n", exclude, parentId);
					exclude = true;
				}
			}
		}
	}

	LOG_ONCE("shouldExcludeAppId(%u) -> %i\n", appId, exclude);
	return exclude;
}

CSteamId CConfig::getDenuvoGameOwner(const AppId_t appId)
{
	for (const auto& tpl : denuvoGames.get())
	{
		if (tpl.second.contains(appId))
		{
			//LOG_ONCE("%u is DenuvoGame\n", appId);
			return CSteamId(tpl.first);
		}
	}

	return CSteamId();
}

bool CConfig::addAdditionalAppId(uint32_t appId)
{
	const std::string configPath = getPath();

	// 1. Read config.yaml line by line
	std::ifstream inFile(configPath);
	if (!inFile.is_open())
	{
		LOG_INFO("addAdditionalAppId: Cannot open config at %s\n", configPath.c_str());
		return false;
	}

	std::vector<std::string> lines;
	std::string line;
	while (std::getline(inFile, line))
	{
		lines.push_back(line);
	}
	inFile.close();

	// 2. Check if appId already exists under AdditionalApps (avoid duplicates)
	bool inAdditionalApps = false;
	for (const auto& l : lines)
	{
		if (l.find("AdditionalApps:") != std::string::npos
			&& !l.empty() && l[0] != '#' && l[0] != ' ' && l[0] != '\t')
		{
			inAdditionalApps = true;
			continue;
		}

		if (inAdditionalApps)
		{
			if (!l.empty() && l[0] != ' ' && l[0] != '\t' && l[0] != '#')
			{
				inAdditionalApps = false;
				continue;
			}

			size_t dashPos = l.find("- ");
			if (dashPos != std::string::npos)
			{
				bool leadingWS = true;
				for (size_t i = 0; i < dashPos; ++i)
				{
					if (l[i] != ' ' && l[i] != '\t') { leadingWS = false; break; }
				}
				if (leadingWS)
				{
					std::string val = l.substr(dashPos + 2);
					size_t cmt = val.find('#');
					if (cmt != std::string::npos) val = val.substr(0, cmt);
					while (!val.empty() && (val.back() == ' ' || val.back() == '\t')) val.pop_back();
					try
					{
						if (static_cast<uint32_t>(std::stoul(val)) == appId)
						{
							LOG_INFO("addAdditionalAppId: AppID %u already in AdditionalApps\n", appId);
							auto current = addedAppIds.get();
							current.insert(appId);
							addedAppIds = current;
							return true;
						}
					}
					catch (...) {}
				}
			}
		}
	}

	// 3. Find the AdditionalApps: section header and the insertion point
	int sectionHeaderIdx = -1;
	int lastEntryIdx = -1;
	inAdditionalApps = false;

	for (int i = 0; i < static_cast<int>(lines.size()); ++i)
	{
		const auto& l = lines[i];

		if (!inAdditionalApps)
		{
			if (l.find("AdditionalApps:") != std::string::npos
				&& !l.empty() && l[0] != '#' && l[0] != ' ' && l[0] != '\t')
			{
				sectionHeaderIdx = i;
				inAdditionalApps = true;
			}
		}
		else
		{
			if (!l.empty() && l[0] != ' ' && l[0] != '\t' && l[0] != '#')
				break;

			size_t dashPos = l.find("- ");
			if (dashPos != std::string::npos)
			{
				bool leadingWS = true;
				for (size_t j = 0; j < dashPos; ++j)
				{
					if (l[j] != ' ' && l[j] != '\t') { leadingWS = false; break; }
				}
				if (leadingWS) lastEntryIdx = i;
			}
		}
	}

	if (sectionHeaderIdx == -1)
	{
		LOG_INFO("addAdditionalAppId: AdditionalApps section not found in %s\n", configPath.c_str());
		return false;
	}

	// 4. Build the new entry and insert it
	std::string newEntry = "  - " + std::to_string(appId);
	int insertAfter = (lastEntryIdx != -1) ? lastEntryIdx : sectionHeaderIdx;
	lines.insert(lines.begin() + insertAfter + 1, newEntry);

	// 5. Write atomically using AtomicFile::writeStream
	bool written = AtomicFile::writeStream(configPath, [&](std::ostream& outFile) {
		for (size_t i = 0; i < lines.size(); ++i)
		{
			outFile << lines[i];
			if (i + 1 < lines.size())
				outFile << '\n';
		}
		return true;
	});

	if (!written)
	{
		LOG_INFO("addAdditionalAppId: Failed to atomically write %s\n", configPath.c_str());
		return false;
	}

	// LOG_INFO("addAdditionalAppId: Appended AppID %u to AdditionalApps in %s\n", appId, configPath.c_str());
	
	// Update memory set immediately for hooks
	auto current = addedAppIds.get();
	current.insert(appId);
	addedAppIds = current;

	if (cloudSavesEnabled.get())
	{
		Navi::CloudRedirect::CCloudRedirectBridge::Instance().SyncSlsSteamConfig(GetEffectiveCloudRedirectedApps());
	}

	{
		const std::lock_guard lock(appsChangedMutex);
		newApps.emplace(appId);
	}
	Licenses::queueRefresh();
	
	return true;
}

bool CConfig::removeAdditionalAppId(uint32_t appId)
{
	const std::string configPath = getPath();

	// 1. Read config.yaml line by line
	std::ifstream inFile(configPath);
	if (!inFile.is_open())
	{
		LOG_INFO("removeAdditionalAppId: Cannot open config at %s\n", configPath.c_str());
		return false;
	}

	std::vector<std::string> lines;
	std::string line;
	while (std::getline(inFile, line))
	{
		lines.push_back(line);
	}
	inFile.close();

	// 2. Find the AdditionalApps: section and remove the line
	bool inAdditionalApps = false;
	bool removed = false;
	std::vector<std::string> newLines;

	for (const auto& l : lines)
	{
		if (!inAdditionalApps)
		{
			if (l.find("AdditionalApps:") != std::string::npos
				&& !l.empty() && l[0] != '#' && l[0] != ' ' && l[0] != '\t')
			{
				inAdditionalApps = true;
			}
			newLines.push_back(l);
		}
		else
		{
			if (!l.empty() && l[0] != ' ' && l[0] != '\t' && l[0] != '#')
			{
				inAdditionalApps = false;
				newLines.push_back(l);
				continue;
			}

			size_t dashPos = l.find("- ");
			if (dashPos != std::string::npos)
			{
				bool leadingWS = true;
				for (size_t i = 0; i < dashPos; ++i)
				{
					if (l[i] != ' ' && l[i] != '\t') { leadingWS = false; break; }
				}
				if (leadingWS)
				{
					std::string val = l.substr(dashPos + 2);
					size_t cmt = val.find('#');
					if (cmt != std::string::npos) val = val.substr(0, cmt);
					while (!val.empty() && (val.back() == ' ' || val.back() == '\t')) val.pop_back();
					try
					{
						if (static_cast<uint32_t>(std::stoul(val)) == appId)
						{
							LOG_INFO("removeAdditionalAppId: Found AppID %u, removing line\n", appId);
							removed = true;
							continue; // Skip this line
						}
					}
					catch (...) {}
				}
			}
			newLines.push_back(l);
		}
	}

	if (!removed)
	{
		LOG_INFO("removeAdditionalAppId: AppID %u not found in AdditionalApps\n", appId);
		return false;
	}

	// 3. Write atomically using AtomicFile::writeStream
	bool written = AtomicFile::writeStream(configPath, [&](std::ostream& outFile) {
		for (size_t i = 0; i < newLines.size(); ++i)
		{
			outFile << newLines[i];
			if (i + 1 < newLines.size())
				outFile << '\n';
		}
		return true;
	});

	if (!written)
	{
		LOG_INFO("removeAdditionalAppId: Failed to atomically write %s\n", configPath.c_str());
		return false;
	}

	LOG_INFO("removeAdditionalAppId: Removed AppID %u from AdditionalApps in %s\n", appId, configPath.c_str());

	// Update memory set immediately for hooks
	auto current = addedAppIds.get();
	if (current.erase(appId) > 0)
	{
		addedAppIds = current;
		LOG_INFO("removeAdditionalAppId: Updated memory set for AppID %u\n", appId);

		{
			const std::lock_guard lock(appsChangedMutex);
			removedApps.emplace(appId);
		}
		Licenses::queueRefresh();
	}

	return true;
}

bool CConfig::updateApiAuth(const std::string& newRyuu, const std::string& newDepotBox, const std::string& newHubcap)
{
	const std::string configPath = getPath();
	std::ifstream inFile(configPath);
	if (!inFile.is_open()) return false;

	const auto quoteYaml = [](const std::string& value) {
		if (value == "\"\"")
			return std::string();

		std::string escaped;
		escaped.reserve(value.size());
		for (const char character : value)
		{
			if (character == '\\' || character == '"')
				escaped.push_back('\\');
			escaped.push_back(character);
		}
		return escaped;
	};

	const std::string ryuu = quoteYaml(newRyuu);
	const std::string depotBox = quoteYaml(newDepotBox);
	const std::string hubcap = quoteYaml(newHubcap);

	std::vector<std::string> lines;
	std::string line;
	bool foundRyuu = false;
	bool foundDepotBox = false;
	bool foundHubcap = false;

	while (std::getline(inFile, line))
	{
		if (line.find("MorrenusKey:") == 0)
		{
			continue; // Drop legacy MorrenusKey line
		}
		else if (line.find("RyuuKey:") == 0)
		{
			if (!foundRyuu)
			{
				lines.push_back("RyuuKey: \"" + ryuu + "\"");
				foundRyuu = true;
			}
			// else: drop duplicate line
		}
		else if (line.find("DepotBoxKey:") == 0)
		{
			if (!foundDepotBox)
			{
				lines.push_back("DepotBoxKey: \"" + depotBox + "\"");
				foundDepotBox = true;
			}
			// else: drop duplicate line
		}
		else if (line.find("HubcapKey:") == 0)
		{
			if (!foundHubcap)
			{
				lines.push_back("HubcapKey: \"" + hubcap + "\"");
				foundHubcap = true;
			}
			// else: drop duplicate line
		}
		else if (line.find("RyuuCookies:") == 0)
		{
			continue; // Skip old RyuuCookies line (deprecated)
		}
		else
		{
			lines.push_back(line);
		}
	}
	inFile.close();

	if (!foundRyuu) lines.push_back("RyuuKey: \"" + ryuu + "\"");
	if (!foundDepotBox) lines.push_back("DepotBoxKey: \"" + depotBox + "\"");
	if (!foundHubcap) lines.push_back("HubcapKey: \"" + hubcap + "\"");


	bool written = AtomicFile::writeStream(configPath, [&](std::ostream& outFile) {
		for (size_t i = 0; i < lines.size(); ++i)
		{
			outFile << lines[i] << '\n';
		}
		return true;
	});

	if (!written) return false;

	LOG_INFO("updateApiAuth: Updated API credentials in %s\n", configPath.c_str());
	return true;
}

void scanLuaPluginsAndUpdateConfig()
{
	// 1. Resolve $SteamRoot — try known paths, use whichever exists
	std::string pluginDir = g_config.getPluginDir();

	if (pluginDir.empty())
	{
		LOG_WARN("scanLuaPluginsAndUpdateConfig: Cannot locate SteamRoot or plugin directory\n");
		return;
	}

	// 3. Collect AppIDs from all *.lua files
	std::unordered_set<uint32_t> collectedAppIds;

	for (const auto& entry : std::filesystem::directory_iterator(pluginDir))
	{
		if (!entry.is_regular_file())
			continue;

		const auto& path = entry.path();
		if (path.extension() == ".lua")
		{
			// Read file content
			std::ifstream file(path);
			if (!file.is_open())
			{
				LOG_WARN("scanLuaPluginsAndUpdateConfig: Cannot open Lua plugin: %s\n", path.c_str());
				continue;
			}

			std::string content((std::istreambuf_iterator<char>(file)),
								 std::istreambuf_iterator<char>());
			file.close();

			// 4. Extract all AppIDs from addappid(\d+) matches
			size_t pos = 0;
			bool foundAny = false;
			
			while ((pos = content.find("addappid(", pos)) != std::string::npos)
			{
				pos += 9; // length of "addappid("
				size_t endPos = pos;
				while (endPos < content.length() && std::isdigit(content[endPos]))
				{
					endPos++;
				}
				
				if (endPos > pos)
				{
					foundAny = true;
					try
					{
						std::string appIdStr = content.substr(pos, endPos - pos);
						const uint32_t appId = static_cast<uint32_t>(std::stoul(appIdStr));
						collectedAppIds.emplace(appId);
						// LOG_DEBUG("scanLuaPluginsAndUpdateConfig: Found AppID %u in %s\n", appId, path.c_str());
					}
					catch (...)
					{
						LOG_WARN("Malformed AppID in Lua plugin: %s\n", path.c_str());
					}
				}
				pos = endPos;
			}
			
			if (!foundAny)
			{
				LOG_WARN("Malformed Lua plugin: %s\n", path.c_str());
				continue;
			}
		}
		else if (path.extension() == ".manifest")
		{
			// Extract AppID from filename (e.g. 293781_9207527406397102173.manifest)
			std::string stem = path.stem().string();
			size_t underscorePos = stem.find('_');
			std::string appIdStr = (underscorePos != std::string::npos) ? stem.substr(0, underscorePos) : stem;
			
			char* endPtr = nullptr;
			unsigned long parsedId = std::strtoul(appIdStr.c_str(), &endPtr, 10);
			if (endPtr && endPtr != appIdStr.c_str() && *endPtr == '\0' && parsedId > 0)
			{
				collectedAppIds.emplace(static_cast<uint32_t>(parsedId));
			}
		}
	}

	// Also include any AppIDs configured in depot_keys.yaml
	for (const uint32_t appId : DepotKeys::getAllAppIdsWithKeys())
	{
		collectedAppIds.emplace(appId);
	}

	// 5. For each collected AppID: if not already in addedAppIds, add it
	for (const uint32_t appId : collectedAppIds)
	{
		if (!g_config.isAddedAppId(appId))
		{
			// LOG_INFO("scanLuaPluginsAndUpdateConfig: Adding AppID %u to AdditionalApps\n", appId);
			g_config.addAdditionalAppId(appId);
		}
		else
		{
			// LOG_DEBUG("scanLuaPluginsAndUpdateConfig: AppID %u already in AdditionalApps, skipping\n", appId);
		}
	}

	// 6. Remove AppIDs that are in config but NO LONGER on disk
	std::vector<uint32_t> toRemove;
	{
		auto currentAdded = g_config.addedAppIds.get();
		for (uint32_t appId : currentAdded)
		{
			if (collectedAppIds.find(appId) == collectedAppIds.end())
			{
				toRemove.push_back(appId);
			}
		}
	}

	for (uint32_t appId : toRemove)
	{
		LOG_INFO("scanLuaPluginsAndUpdateConfig: Removing stale AppID %u from AdditionalApps\n", appId);
		g_config.removeAdditionalAppId(appId);
	}

	LOG_INFO("scanLuaPluginsAndUpdateConfig: Scan complete, processed %zu Lua plugin(s)\n", collectedAppIds.size());
}

CConfig g_config = CConfig();
