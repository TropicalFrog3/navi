#pragma once

#include "CUser.hpp"

#include <vector>
#include <cstdint>
#include <string>

void setMockDepotsSection(const std::vector<char>& data);
void clearMockDepotsSection();
void setMockAppOwnership(AppId_t appId, const AppOwnershipInfo_t& info);
void setMockDirectOwnership(AppId_t appId, bool ownsDirectly);
void clearMockAppOwnership();
int getWarnCount();
void resetWarnCount();
