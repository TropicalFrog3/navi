#pragma once

#include <vector>
#include <cstdint>
#include <string>

void setMockDepotsSection(const std::vector<char>& data);
void clearMockDepotsSection();
int getWarnCount();
void resetWarnCount();
