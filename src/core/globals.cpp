/**
 * @file  globals.cpp
 * @brief Global runtime state variables and module handle definitions
 */
#include "globals.hpp"


lm_module_t g_modSteam { };
lm_module_t g_modSteamClient { };
lm_module_t g_modSteamUI { };
lm_module_t g_modTier0 { };

CSteamId g_currentSteamId;
