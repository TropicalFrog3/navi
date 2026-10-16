#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

class CNetPacket;

namespace DepotKeys
{
	// depot_id -> hex-encoded decryption key (64 hex characters / 32 bytes AES)
	using KeyMap = std::unordered_map<uint32_t, std::string>;

	// app_id -> set of depot_ids
	using AppDepotsMap = std::unordered_map<uint32_t, std::unordered_set<uint32_t>>;

	// Path to ~/.config/SLSsteam/depot_keys.yaml
	std::string getDepotKeysYamlPath();

	// Load ~/.config/SLSsteam/depot_keys.yaml into memory
	void loadDepotKeysYaml();

	// Parse all *.lua files in stplug-in/ and extract depot keys
	// from addappid(depot_id, 0, "hex_key") calls (secondary fallback)
	void scanLuaPluginsForDepotKeys();

	// Get the cached depot key for a given depot_id (returns empty string if not found)
	std::string getDepotKey(uint32_t depotId);

	// Check if an app is managed by ACCELA / depot_keys (has keys or .lua plugin file)
	bool isAccelaApp(uint32_t appId);

	// Check if an app has registered keys in depot_keys.yaml
	bool hasKeysForApp(uint32_t appId);

	// Get all depot IDs registered for appId in depot_keys.yaml
	std::unordered_set<uint32_t> getDepotsForApp(uint32_t appId);

	// Get all app IDs that have keys in depot_keys.yaml
	std::unordered_set<uint32_t> getAllAppIdsWithKeys();

	// Handle incoming protobuf messages — intercept failed depot decryption key responses
	void recvMsg(CNetPacket* pkt);
}

