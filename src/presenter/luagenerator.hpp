/**
 * @file  luagenerator.hpp
 * @brief Declarations for procedural Lua game manifest generation
 */
#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

namespace LuaGenerator
{
    // Generate or update stplug-in/<appId>.lua for an app using keys from depot_keys.yaml
    // and depot/manifest metadata from Steam's appinfo cache (k_EAppInfoSectionDepots).
    // Always overwrites to keep manifest GIDs and sizes up to date with Steam branch updates.
    bool generateForApp(uint32_t appId);

    // Sync and generate .lua plugins for all AppIDs currently registered in depot_keys.yaml
    size_t syncAllAppsWithKeys();
}
