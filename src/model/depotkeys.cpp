#include "depotkeys.hpp"
#include "luagenerator.hpp"

#include "atomic_file.hpp"
#include "config.hpp"
#include "curl.hpp"
#include "globals.hpp"
#include "log.hpp"
#include "utils.hpp"
#include "CNetPacket.hpp"
#include "protobufs/steammessages_clientserver_2.pb.h"

#include <filesystem>
#include <fstream>
#include <mutex>
#include <regex>
#include <sstream>

#include <yaml-cpp/yaml.h>

static DepotKeys::KeyMap g_depotKeys;
static DepotKeys::KeyMap g_yamlDepotKeys;
static DepotKeys::AppDepotsMap g_appDepots;
static std::mutex g_depotKeysMutex;

static std::string hexToBytes(const std::string& hex)
{
	std::string bytes;
	bytes.reserve(hex.size() / 2);
	for (size_t i = 0; i + 1 < hex.size(); i += 2)
	{
		unsigned int byte;
		std::sscanf(hex.c_str() + i, "%02x", &byte);
		bytes.push_back(static_cast<char>(byte));
	}
	return bytes;
}

std::string DepotKeys::getDepotKeysYamlPath()
{
	return g_config.getDir() + "/depot_keys.yaml";
}

void DepotKeys::loadDepotKeysYaml()
{
	const std::string path = getDepotKeysYamlPath();
	if (!std::filesystem::exists(path))
	{
		LOG_INFO("DepotKeys: %s does not exist, skipping YAML load\n", path.c_str());
		return;
	}

	try
	{
		YAML::Node root = YAML::LoadFile(path);
		if (!root || !root.IsMap())
		{
			LOG_WARN("DepotKeys: %s is not a valid YAML map\n", path.c_str());
			return;
		}

		KeyMap newYamlKeys;
		AppDepotsMap newAppDepots;

		for (const auto& entry : root)
		{
			std::string keyStr = entry.first.as<std::string>();
			char* pEnd = nullptr;
			unsigned long parsedAppId = std::strtoul(keyStr.c_str(), &pEnd, 10);
			if (pEnd == keyStr.c_str() || parsedAppId == 0)
			{
				continue; // Skip non-numeric top-level keys
			}

			const uint32_t appId = static_cast<uint32_t>(parsedAppId);
			const YAML::Node& valNode = entry.second;

			if (!valNode.IsMap())
			{
				continue;
			}

			// Support both nested `depots:` map and direct `depotId: key` map
			const YAML::Node depotsNode = valNode["depots"] ? valNode["depots"] : valNode;

			if (depotsNode.IsMap())
			{
				for (const auto& dEntry : depotsNode)
				{
					std::string dKeyStr = dEntry.first.as<std::string>();
					char* pEndD = nullptr;
					unsigned long parsedDepotId = std::strtoul(dKeyStr.c_str(), &pEndD, 10);
					if (pEndD == dKeyStr.c_str() || parsedDepotId == 0)
					{
						continue; // Not a numeric depot ID (e.g. title or comment)
					}

					const uint32_t depotId = static_cast<uint32_t>(parsedDepotId);
					if (!dEntry.second.IsScalar())
					{
						continue;
					}

					std::string hexKey = dEntry.second.as<std::string>();
					while (!hexKey.empty() && isspace(static_cast<unsigned char>(hexKey.front()))) hexKey.erase(hexKey.begin());
					while (!hexKey.empty() && isspace(static_cast<unsigned char>(hexKey.back()))) hexKey.pop_back();

					if (hexKey.size() == 64)
					{
						newYamlKeys[depotId] = hexKey;
						newAppDepots[appId].insert(depotId);
					}
					else
					{
						LOG_WARN("DepotKeys: Invalid key length (%zu != 64) for depot %u under app %u in %s\n",
						         hexKey.size(), depotId, appId, path.c_str());
					}
				}
			}
		}

		{
			std::lock_guard<std::mutex> lock(g_depotKeysMutex);
			g_yamlDepotKeys = std::move(newYamlKeys);
			g_appDepots = std::move(newAppDepots);
			// Merge YAML keys into g_depotKeys with priority
			for (const auto& [depotId, hexKey] : g_yamlDepotKeys)
			{
				g_depotKeys[depotId] = hexKey;
			}
		}

		LOG_INFO("DepotKeys: Loaded %zu depot key(s) for %zu app(s) from %s\n",
		         g_yamlDepotKeys.size(), g_appDepots.size(), path.c_str());
	}
	catch (const std::exception& e)
	{
		LOG_ERROR("DepotKeys: Exception loading %s: %s\n", path.c_str(), e.what());
	}
	catch (...)
	{
		LOG_ERROR("DepotKeys: Unknown error loading %s\n", path.c_str());
	}
}

void DepotKeys::scanLuaPluginsForDepotKeys()
{
	std::string pluginDir = g_config.getPluginDir();
	if (pluginDir.empty())
	{
		LOG_INFO("DepotKeys: Cannot locate plugin directory\n");
		return;
	}

	if (!std::filesystem::exists(pluginDir))
	{
		return;
	}

	// Match: addappid(depot_id, 0, "hex_key")
	// The depot key lines look like: addappid(3618391,0,"af524ae28cb634f861e43a8dc0f9dbc6002bb303ec8e8167e02a8c92fa57d1e9")
	const std::regex depotKeyRegex(R"re(addappid\(\s*(\d+)\s*,\s*\d+\s*,\s*"([0-9a-fA-F]+)"\s*\))re");

	KeyMap newKeys;

	for (const auto& entry : std::filesystem::directory_iterator(pluginDir))
	{
		if (!entry.is_regular_file())
			continue;

		if (entry.path().extension() != ".lua")
			continue;

		std::ifstream file(entry.path());
		if (!file.is_open())
			continue;

		std::string content((std::istreambuf_iterator<char>(file)),
							 std::istreambuf_iterator<char>());
		file.close();

		// Find all depot key entries in this lua file
		auto begin = std::sregex_iterator(content.begin(), content.end(), depotKeyRegex);
		auto end = std::sregex_iterator();

		for (auto it = begin; it != end; ++it)
		{
			const std::smatch& match = *it;
			try
			{
				const uint32_t depotId = static_cast<uint32_t>(std::stoul(match[1].str()));
				const std::string hexKey = match[2].str();

				if (hexKey.size() == 64) // 32 bytes = 64 hex chars (AES-256)
				{
					newKeys[depotId] = hexKey;
				}
				else
				{
					LOG_INFO("DepotKeys: Unexpected key length %zu for depot %u\n",
								 hexKey.size(), depotId);
				}
			}
			catch (...)
			{
				LOG_INFO("DepotKeys: Failed to parse depot key in %s\n",
							 entry.path().c_str());
			}
		}
	}

	{
		std::lock_guard<std::mutex> lock(g_depotKeysMutex);
		g_depotKeys = std::move(newKeys);
		// Prioritize keys from depot_keys.yaml
		for (const auto& [depotId, hexKey] : g_yamlDepotKeys)
		{
			g_depotKeys[depotId] = hexKey;
		}
	}

	LOG_INFO("DepotKeys: Scan complete, total loaded %zu depot key(s)\n", g_depotKeys.size());
}

std::string DepotKeys::getDepotKeysJsonPath()
{
	return g_config.getDir() + "/depotkeys.json";
}

std::string DepotKeys::getAppAccessTokensJsonPath()
{
	return g_config.getDir() + "/appaccesstokens.json";
}

std::string DepotKeys::findAndGetDepotKey(uint32_t depotId)
{
	if (depotId == 0) return "";

	// Check in-memory map first
	{
		std::lock_guard<std::mutex> lock(g_depotKeysMutex);
		auto it = g_depotKeys.find(depotId);
		if (it != g_depotKeys.end() && !it->second.empty())
		{
			return it->second;
		}
	}

	const std::string jsonPath = getDepotKeysJsonPath();

	// If depotkeys.json is missing, fetch from Archive.org ManifestHub-repo.zip
	if (!std::filesystem::exists(jsonPath))
	{
		LOG_INFO("DepotKeys: depotkeys.json not found locally. Fetching from Archive.org ManifestHub-repo.zip...\n");
		Curl::HttpRequest_t req;
		req.method = "GET";
		req.url = "https://ia600607.us.archive.org/view_archive.php?archive=/33/items/manifest-hub-repo/ManifestHub-repo.zip&file=ManifestHub%2Fdepotkeys.json";
		req.timeoutSec = 20;
		req.headers.push_back({"User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"});

		Curl::HttpResponse_t resp;
		if (Curl::request(req, resp) && resp.statusCode == 200 && resp.body.size() > 1024)
		{
			std::string bodyStr(reinterpret_cast<const char*>(resp.body.data()), resp.body.size());
			AtomicFile::write(jsonPath, bodyStr, true);
			LOG_INFO("DepotKeys: Successfully saved depotkeys.json to %s (size %zu bytes)\n", jsonPath.c_str(), resp.body.size());
		}
		else
		{
			LOG_WARN("DepotKeys: Failed to fetch depotkeys.json from Archive.org (status=%d)\n", resp.statusCode);
		}
	}

	// Search depotkeys.json for `"depotId": "hexKey"`
	if (std::filesystem::exists(jsonPath))
	{
		std::ifstream file(jsonPath);
		if (file.is_open())
		{
			std::string line;
			const std::string target = "\"" + std::to_string(depotId) + "\":";
			while (std::getline(file, line))
			{
				size_t pos = line.find(target);
				if (pos != std::string::npos)
				{
					size_t q1 = line.find('"', pos + target.size());
					if (q1 != std::string::npos)
					{
						size_t q2 = line.find('"', q1 + 1);
						if (q2 != std::string::npos && q2 - q1 - 1 == 64)
						{
							std::string key = line.substr(q1 + 1, 64);
							std::lock_guard<std::mutex> lock(g_depotKeysMutex);
							g_depotKeys[depotId] = key;
							LOG_INFO("DepotKeys: Found and cached depot key for depot %u from %s\n", depotId, jsonPath.c_str());
							return key;
						}
					}
				}
			}
		}
	}

	return "";
}

std::string DepotKeys::findAndGetAppAccessToken(uint32_t appId)
{
	if (appId == 0) return "";

	const std::string jsonPath = getAppAccessTokensJsonPath();

	// If appaccesstokens.json is missing, fetch from Archive.org ManifestHub-repo.zip
	if (!std::filesystem::exists(jsonPath))
	{
		LOG_INFO("DepotKeys: appaccesstokens.json not found locally. Fetching from Archive.org ManifestHub-repo.zip...\n");
		Curl::HttpRequest_t req;
		req.method = "GET";
		req.url = "https://ia600607.us.archive.org/view_archive.php?archive=/33/items/manifest-hub-repo/ManifestHub-repo.zip&file=ManifestHub%2Fappaccesstokens.json";
		req.timeoutSec = 10;
		req.headers.push_back({"User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"});

		Curl::HttpResponse_t resp;
		if (Curl::request(req, resp) && resp.statusCode == 200 && resp.body.size() > 100)
		{
			std::string bodyStr(reinterpret_cast<const char*>(resp.body.data()), resp.body.size());
			AtomicFile::write(jsonPath, bodyStr, true);
			LOG_INFO("DepotKeys: Successfully saved appaccesstokens.json to %s (size %zu bytes)\n", jsonPath.c_str(), resp.body.size());
		}
		else
		{
			LOG_WARN("DepotKeys: Failed to fetch appaccesstokens.json from Archive.org (status=%d)\n", resp.statusCode);
		}
	}

	// Search appaccesstokens.json for `"appId": "token"`
	if (std::filesystem::exists(jsonPath))
	{
		std::ifstream file(jsonPath);
		if (file.is_open())
		{
			std::string line;
			const std::string target = "\"" + std::to_string(appId) + "\":";
			while (std::getline(file, line))
			{
				size_t pos = line.find(target);
				if (pos != std::string::npos)
				{
					size_t q1 = line.find('"', pos + target.size());
					if (q1 != std::string::npos)
					{
						size_t q2 = line.find('"', q1 + 1);
						if (q2 != std::string::npos)
						{
							std::string token = line.substr(q1 + 1, q2 - q1 - 1);
							LOG_INFO("DepotKeys: Found and loaded app access token for app %u from %s\n", appId, jsonPath.c_str());
							return token;
						}
					}
				}
			}
		}
	}

	return "";
}

std::string DepotKeys::getDepotKey(uint32_t depotId)
{
	{
		std::lock_guard<std::mutex> lock(g_depotKeysMutex);
		auto it = g_depotKeys.find(depotId);
		if (it != g_depotKeys.end() && !it->second.empty())
		{
			return it->second;
		}
	}
	return findAndGetDepotKey(depotId);
}

bool DepotKeys::isKeyManagedApp(uint32_t appId)
{
	std::lock_guard<std::mutex> lock(g_depotKeysMutex);
	if (g_appDepots.count(appId) > 0)
	{
		return true;
	}
	// Fallback check: does the Lua plugin exist and have a valid depot decryption key?
	std::string pluginDir = g_config.getPluginDir();
	if (pluginDir.empty()) return false;
	std::string luaPath = pluginDir + "/" + std::to_string(appId) + ".lua";
	if (!std::filesystem::exists(luaPath)) return false;

	std::ifstream file(luaPath);
	if (!file.is_open()) return false;
	std::string content((std::istreambuf_iterator<char>(file)),
	                     std::istreambuf_iterator<char>());
	const std::regex depotKeyRegex(R"re(addappid\(\s*\d+\s*,\s*\d+\s*,\s*"([0-9a-fA-F]{64})"\s*\))re");
	return std::regex_search(content, depotKeyRegex);
}

bool DepotKeys::hasKeysForApp(uint32_t appId)
{
	std::lock_guard<std::mutex> lock(g_depotKeysMutex);
	return g_appDepots.count(appId) > 0;
}

std::unordered_set<uint32_t> DepotKeys::getDepotsForApp(uint32_t appId)
{
	std::lock_guard<std::mutex> lock(g_depotKeysMutex);
	auto it = g_appDepots.find(appId);
	if (it != g_appDepots.end())
	{
		return it->second;
	}
	return {};
}

std::unordered_set<uint32_t> DepotKeys::getAllAppIdsWithKeys()
{
	std::lock_guard<std::mutex> lock(g_depotKeysMutex);
	std::unordered_set<uint32_t> apps;
	for (const auto& [appId, _] : g_appDepots)
	{
		apps.insert(appId);
	}
	return apps;
}

static void handleDepotDecryptionKeyResponse(CNetPacket* pkt)
{
	auto body = pkt->deserializeBody<CMsgClientGetDepotDecryptionKeyResponse>();

	const uint32_t depotId = body.depot_id();
	const int32_t result = body.eresult();

	LOG_INFO("DepotKeys: Received decryption key response for depot %u, eresult=%d\n",
			 depotId, result);

	if (result == k_EResultOK)
	{
		LOG_DEBUG_ONCE("DepotKeys: Depot %u key received from Steam (OK)\n", depotId);
		return;
	}

	std::string hexKey = DepotKeys::getDepotKey(depotId);
	if (hexKey.empty())
	{
		// Try reloading depot_keys.yaml in case it was updated on disk while Steam was running
		DepotKeys::loadDepotKeysYaml();
		hexKey = DepotKeys::getDepotKey(depotId);
	}
	if (hexKey.empty() && DepotKeys::isKeyManagedApp(depotId))
	{
		// Steam queries the App ID itself as a depot container during installation.
		// If depotId is an app managed by Navi with valid depot keys, provide a dummy key
		// so Steam does not fail the whole app before querying child depots.
		hexKey = "0000000000000000000000000000000000000000000000000000000000000000";
		LOG_INFO("DepotKeys: Injected dummy container key for managed app %u\n", depotId);
	}
	if (hexKey.empty())
	{
		LOG_INFO("DepotKeys: No cached key for depot %u (eresult=%d), cannot inject\n",
				 depotId, result);
		return;
	}

	const std::string keyBytes = hexToBytes(hexKey);

	CMsgProtoBufHeader hdr = pkt->deserializeHeader();
	hdr.set_eresult(static_cast<int32_t>(k_EResultOK));

	body.set_eresult(k_EResultOK);
	body.set_depot_encryption_key(keyBytes.data(), keyBytes.size());

	pkt->serialize(body, &hdr);

	LOG_INFO("DepotKeys: Injected cached key for depot %u (overriding eresult %d -> OK)\n",
			 depotId, result);
}

static void handleOwnershipTicketResponse(CNetPacket* pkt)
{
	auto body = pkt->deserializeBody<CMsgClientGetAppOwnershipTicketResponse>();

	const uint32_t appId = body.app_id();
	const uint32_t result = body.eresult();

	// If successful, let the existing ticket system handle caching
	if (result == k_EResultOK)
	{
		return;
	}

	// Only intercept for key-managed apps
	if (!DepotKeys::isKeyManagedApp(appId))
	{
		return;
	}

	// If app has keys configured in depot_keys.yaml, ensure Lua plugin is generated/fresh
	if (DepotKeys::hasKeysForApp(appId))
	{
		LuaGenerator::generateForApp(appId);
	}

	LOG_INFO("DepotKeys: Ownership ticket failed for app %u (eresult=%u), injecting OK\n",
			 appId, result);

	CMsgProtoBufHeader hdr = pkt->deserializeHeader();
	hdr.set_eresult(static_cast<int32_t>(k_EResultOK));

	body.set_eresult(k_EResultOK);
	// Set a 50-byte dummy ticket that is structurally valid so Steam doesn't fail parsing
	std::string dummyTicket(50, '\0');
	uint32_t* pU32 = reinterpret_cast<uint32_t*>(dummyTicket.data());
	pU32[0] = 50; // length
	pU32[1] = 4;  // version
	uint64_t steamId = 0x0110000100000000ULL | g_currentSteamId.steamId64;
	*reinterpret_cast<uint64_t*>(dummyTicket.data() + 8) = steamId;
	pU32[4] = appId; // offset 16 is appId

	body.set_ticket(dummyTicket);
	pkt->serialize(body, &hdr);

	LOG_INFO("DepotKeys: Injected ownership ticket OK (with dummy struct) for app %u\n", appId);
}

void DepotKeys::recvMsg(CNetPacket* pkt)
{
	if (!pkt || !pkt->isValid() || !pkt->isProtoBuf())
	{
		return;
	}

	switch (pkt->getProtoBufType())
	{
		case k_EMsgClientGetDepotDecryptionKeyResponse:
			handleDepotDecryptionKeyResponse(pkt);
			break;

		case k_EMsgClientGetAppOwnershipTicketResponse:
			handleOwnershipTicketResponse(pkt);
			break;

		default:
			break;
	}
}
