#include "IClientCompat.hpp"

#include "CUtl.hpp"
#include "steam.hpp"

#include "log.hpp"
#include "memhlp.hpp"
#include "vdf.hpp"
#include "vftableinfo.hpp"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

static std::unordered_map<std::string, std::string> s_fallbackDisplayNames;
static std::vector<std::string> s_cachedCompatTools;
static std::unordered_map<std::string, time_t> s_watchedDirMtimes;
static bool s_hasCachedTools = false;

static bool appendCompatTool(CUtlVector<CUtlString>* vec, const char* name)
{
	if (!vec || !name)
	{
		return false;
	}
	if (static_cast<int32_t>(vec->mem.growSize) < 0)
	{
		return false;
	}

	if (vec->size >= vec->mem.alloc)
	{
		const uint32_t newAlloc = vec->mem.alloc ? vec->mem.alloc * 2 : 8;
		const size_t bytes = static_cast<size_t>(newAlloc) * sizeof(CUtlString);
		void* mem = vec->mem.base
			? Steam::Plat_Realloc(vec->mem.base, bytes)
			: Steam::Plat_Alloc(bytes);
		if (!mem)
		{
			LOG_ERROR("appendCompatTool: allocation of %zu bytes failed\n", bytes);
			return false;
		}

		const size_t oldBytes = static_cast<size_t>(vec->mem.alloc) * sizeof(CUtlString);
		std::memset(reinterpret_cast<uint8_t*>(mem) + oldBytes, 0, bytes - oldBytes);

		vec->mem.base = reinterpret_cast<CUtlString*>(mem);
		vec->mem.alloc = newAlloc;
	}

	vec->mem.base[vec->size].setValue(name);
	vec->size++;
	return true;
}

static time_t getDirMtime(const std::string& path)
{
	struct stat st;
	if (stat(path.c_str(), &st) == 0)
	{
		return st.st_mtime;
	}
	return 0;
}

static std::vector<std::string> getSteamSearchPaths()
{
	std::vector<std::string> paths;
	const char* steamBase = getenv("STEAM_BASE_FOLDER");
	if (steamBase && *steamBase)
	{
		paths.emplace_back(steamBase);
	}
	const char* home = getenv("HOME");
	if (home && *home)
	{
		paths.push_back(std::string(home) + "/.local/share/Steam");
		paths.push_back(std::string(home) + "/.steam/root");
		paths.push_back(std::string(home) + "/.steam/steam");
	}
	return paths;
}

static std::vector<std::string> getSteamLibraryFolders()
{
	std::vector<std::string> libraryFolders;
	std::unordered_set<std::string> seen;

	auto steamPaths = getSteamSearchPaths();
	std::string vdfPath;
	for (const auto& sp : steamPaths)
	{
		std::string candidate = sp + "/steamapps/libraryfolders.vdf";
		if (fs::exists(candidate))
		{
			vdfPath = candidate;
			break;
		}
	}

	if (!vdfPath.empty())
	{
		VDF::Node root;
		if (VDF::parseFile(vdfPath, root))
		{
			const auto* libNode = root.getChild("libraryfolders");
			if (!libNode)
			{
				libNode = &root;
			}

			for (const auto& folder : libNode->children)
			{
				std::string p = folder.getString("path");
				if (!p.empty() && fs::exists(p) && !seen.contains(p))
				{
					seen.insert(p);
					libraryFolders.push_back(p);
				}
			}
		}
	}

	// Always fallback to at least the primary steam directories if none found
	for (const auto& sp : steamPaths)
	{
		if (fs::exists(sp) && !seen.contains(sp))
		{
			seen.insert(sp);
			libraryFolders.push_back(sp);
		}
	}

	return libraryFolders;
}

static std::string resolveProtonInternalName(const std::string& installdir, const std::string& appName, const std::string& toolDir)
{
	std::string nameLower = appName;
	for (char& c : nameLower)
	{
		c = std::tolower(static_cast<unsigned char>(c));
	}

	if (nameLower.find("experimental") != std::string::npos || installdir.find("Experimental") != std::string::npos)
	{
		return "proton_experimental";
	}
	if (nameLower.find("hotfix") != std::string::npos || installdir.find("Hotfix") != std::string::npos)
	{
		return "proton_hotfix";
	}

	auto extractMajor = [](const std::string& str) -> std::string
	{
		size_t pos = 0;
		while (pos < str.size())
		{
			size_t p1 = str.find("Proton", pos);
			size_t p2 = str.find("proton", pos);
			if (p1 == std::string::npos) pos = p2;
			else if (p2 == std::string::npos) pos = p1;
			else pos = std::min(p1, p2);

			if (pos == std::string::npos)
			{
				break;
			}

			pos += 6;
			while (pos < str.size() && (str[pos] == ' ' || str[pos] == '-' || str[pos] == '_'))
			{
				pos++;
			}
			std::string digits;
			while (pos < str.size() && std::isdigit(static_cast<unsigned char>(str[pos])))
			{
				digits += str[pos++];
			}
			if (!digits.empty())
			{
				return digits;
			}
		}
		return "";
	};

	std::string major = extractMajor(appName);
	if (major.empty())
	{
		major = extractMajor(installdir);
	}
	if (major.empty())
	{
		std::string dirName = fs::path(toolDir).filename().string();
		major = extractMajor(dirName);
	}

	if (!major.empty())
	{
		return "proton_" + major;
	}

	std::string versionFile = toolDir + "/version";
	if (fs::exists(versionFile))
	{
		std::ifstream vf(versionFile);
		std::string vline;
		if (std::getline(vf, vline))
		{
			if (vline.find("experimental") != std::string::npos)
			{
				return "proton_experimental";
			}
			if (vline.find("hotfix") != std::string::npos)
			{
				return "proton_hotfix";
			}
			std::string vMajor = extractMajor(vline);
			if (!vMajor.empty())
			{
				return "proton_" + vMajor;
			}
		}
	}

	return "";
}

static void scanCompatToolsFallback()
{
	s_fallbackDisplayNames.clear();
	s_cachedCompatTools.clear();
	std::unordered_set<std::string> added;

	// Defaults
	s_fallbackDisplayNames["proton_experimental"] = "Proton Experimental";
	s_fallbackDisplayNames["proton_hotfix"] = "Proton Hotfix";
	s_fallbackDisplayNames["proton_11"] = "Proton 11.0";
	s_fallbackDisplayNames["proton_10"] = "Proton 10.0";
	s_fallbackDisplayNames["proton_9"] = "Proton 9.0";
	s_fallbackDisplayNames["proton_8"] = "Proton 8.0";
	s_fallbackDisplayNames["proton_7"] = "Proton 7.0";

	auto libraries = getSteamLibraryFolders();

	// 1. Scan official Valve Proton tools in each library
	for (const auto& lib : libraries)
	{
		fs::path commonPath = fs::path(lib) / "steamapps" / "common";
		if (!fs::exists(commonPath))
		{
			continue;
		}

		std::unordered_map<std::string, std::pair<std::string, std::string>> installdirToApp;
		fs::path steamappsPath = fs::path(lib) / "steamapps";
		try
		{
			for (const auto& entry : fs::directory_iterator(steamappsPath))
			{
				if (!entry.is_regular_file())
				{
					continue;
				}
				std::string fname = entry.path().filename().string();
				if (fname.rfind("appmanifest_", 0) == 0 && fname.ends_with(".acf"))
				{
					VDF::Node acfRoot;
					if (VDF::parseFile(entry.path().string(), acfRoot))
					{
						const auto* appState = acfRoot.getChild("AppState");
						if (!appState)
						{
							appState = &acfRoot;
						}
						std::string aid = appState->getString("appid");
						std::string name = appState->getString("name");
						std::string idir = appState->getString("installdir");
						if (!idir.empty())
						{
							installdirToApp[idir] = {aid, name};
						}
					}
				}
			}
		}
		catch (const std::exception& e)
		{
			LOG_DEBUG("Exception reading appmanifests in %s: %s\n", steamappsPath.c_str(), e.what());
		}

		try
		{
			for (const auto& entry : fs::directory_iterator(commonPath))
			{
				if (!entry.is_directory())
				{
					continue;
				}
				fs::path toolManifest = entry.path() / "toolmanifest.vdf";
				if (!fs::exists(toolManifest))
				{
					continue;
				}

				VDF::Node tmRoot;
				if (!VDF::parseFile(toolManifest.string(), tmRoot))
				{
					continue;
				}

				const auto* manifest = tmRoot.getChild("manifest");
				if (!manifest)
				{
					manifest = &tmRoot;
				}

				std::string layer = manifest->getString("compatmanager_layer_name");
				if (layer != "proton")
				{
					continue;
				}

				bool hasFiles = fs::exists(entry.path() / "proton") ||
				                fs::exists(entry.path() / "version") ||
				                fs::exists(entry.path() / "dist.lock");
				if (!hasFiles)
				{
					continue;
				}

				std::string dirName = entry.path().filename().string();
				std::string appName;
				if (installdirToApp.contains(dirName))
				{
					appName = installdirToApp[dirName].second;
				}

				std::string internalName = resolveProtonInternalName(dirName, appName, entry.path().string());
				if (!internalName.empty() && !added.contains(internalName))
				{
					added.insert(internalName);
					s_cachedCompatTools.push_back(internalName);
					if (!appName.empty())
					{
						s_fallbackDisplayNames[internalName] = appName;
					}
				}
			}
		}
		catch (const std::exception& e)
		{
			LOG_DEBUG("Exception scanning common in %s: %s\n", commonPath.c_str(), e.what());
		}
	}

	// 2. Scan custom compatibility tools in compatibilitytools.d
	std::vector<std::string> compatDirs;
	const char* home = getenv("HOME");
	if (home && *home)
	{
		compatDirs.push_back(std::string(home) + "/.local/share/Steam/compatibilitytools.d");
		compatDirs.push_back(std::string(home) + "/.steam/root/compatibilitytools.d");
		compatDirs.push_back(std::string(home) + "/.steam/steam/compatibilitytools.d");
	}
	compatDirs.push_back("/usr/share/steam/compatibilitytools.d");

	for (const auto& cdir : compatDirs)
	{
		if (!fs::exists(cdir))
		{
			continue;
		}
		s_watchedDirMtimes[cdir] = getDirMtime(cdir);

		try
		{
			for (const auto& entry : fs::directory_iterator(cdir))
			{
				if (!entry.is_directory())
				{
					continue;
				}
				fs::path toolVdf = entry.path() / "compatibilitytool.vdf";
				if (!fs::exists(toolVdf))
				{
					continue;
				}

				VDF::Node ctRoot;
				if (!VDF::parseFile(toolVdf.string(), ctRoot))
				{
					continue;
				}

				const auto* compNode = ctRoot.getChild("compatibilitytools");
				if (!compNode)
				{
					compNode = &ctRoot;
				}
				const auto* toolsNode = compNode->getChild("compat_tools");
				if (!toolsNode)
				{
					continue;
				}

				for (const auto& toolItem : toolsNode->children)
				{
					std::string toolName = toolItem.key;
					std::string dispName = toolItem.getString("display_name", toolName);
					std::string fromOs = toolItem.getString("from_oslist");
					std::string toOs = toolItem.getString("to_oslist");

					if (fromOs == "windows" && toOs == "linux")
					{
						if (!toolName.empty() && !added.contains(toolName))
						{
							added.insert(toolName);
							s_cachedCompatTools.push_back(toolName);
							s_fallbackDisplayNames[toolName] = dispName;
						}
					}
				}
			}
		}
		catch (const std::exception& e)
		{
			LOG_DEBUG("Exception scanning compat tools in %s: %s\n", cdir.c_str(), e.what());
		}
	}

	s_hasCachedTools = true;
	LOG_INFO("Compat fallback: scanned %zu compatibility tools\n", s_cachedCompatTools.size());
}

static bool shouldRescanCompatTools()
{
	if (!s_hasCachedTools)
	{
		return true;
	}
	for (const auto& [dir, lastMtime] : s_watchedDirMtimes)
	{
		if (getDirMtime(dir) != lastMtime)
		{
			return true;
		}
	}
	return false;
}

bool IClientCompat::isCompatToolEnabled(const AppId_t appId)
{
	if (!VFTIndexes::IClientCompat::BIsCompatibilityToolEnabled.resolved())
	{
		return false;
	}

	return MemHlp::callVFunc<bool(*)(void*, AppId_t)>
	(
		VFTIndexes::IClientCompat::BIsCompatibilityToolEnabled.index,
		this,
		appId
	);
}

const char* IClientCompat::getDisplayName(const char* name)
{
	if (!name || !*name)
	{
		return "";
	}

	if (VFTIndexes::IClientCompat::GetCompatToolDisplayName.resolved())
	{
		const char* disp = MemHlp::callVFunc<const char*(*)(void*, const char*)>
		(
			VFTIndexes::IClientCompat::GetCompatToolDisplayName.index,
			this,
			name
		);
		if (disp && *disp)
		{
			return disp;
		}
	}

	if (s_fallbackDisplayNames.contains(name))
	{
		return s_fallbackDisplayNames[name].c_str();
	}

	return name;
}

void IClientCompat::getCompatToolsForApp(const AppId_t appId, CUtlVector<CUtlString>* pVecTools)
{
	if (VFTIndexes::IClientCompat::GetAvailableCompatToolsForApp.resolved())
	{
		MemHlp::callVFunc<void(*)(void*, CUtlVector<CUtlString>*, AppId_t)>
		(
			VFTIndexes::IClientCompat::GetAvailableCompatToolsForApp.index,
			this,
			pVecTools,
			appId
		);
		return;
	}

	if (shouldRescanCompatTools())
	{
		scanCompatToolsFallback();
	}

	if (!pVecTools)
	{
		return;
	}

	for (const auto& toolName : s_cachedCompatTools)
	{
		appendCompatTool(pVecTools, toolName.c_str());
	}
}

const char* IClientCompat::getCompatToolName(const AppId_t appId)
{
	if (!VFTIndexes::IClientCompat::GetCompatToolName.resolved())
	{
		return "";
	}

	return MemHlp::callVFunc<const char*(*)(void*, AppId_t)>
	(
		VFTIndexes::IClientCompat::GetCompatToolName.index,
		this,
		appId
	);
}

void IClientCompat::specifyCompatTool(const AppId_t appId, const char* name, const char* config, int32_t priority)
{
	if (!VFTIndexes::IClientCompat::SpecifyCompatTool.resolved())
	{
		return;
	}

	MemHlp::callVFunc<void(*)(void*, AppId_t, const char*, const char*, int32_t)>
	(
		VFTIndexes::IClientCompat::SpecifyCompatTool.index,
		this,
		appId,
		name,
		config,
		priority
	);
}

IClientCompat* g_pClientCompat = nullptr;
