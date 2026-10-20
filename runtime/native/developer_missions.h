#pragma once
#include "developer_tools.h"

namespace DarkRecomp::Native {
// Empty for unknown IDs. Only authored retail destinations can form commands.
std::string developerMissionCommand(std::string_view id, bool initializeSession = false);
bool canLoadDeveloperMission(uint8_t* base, uint32_t client);
// Runs any original console script (e.g. "showFPSToggle()") through CConsole::ExecuteString.
bool executeDeveloperScript(PPCContext& ctx, uint8_t* base, std::string_view script, std::string& status);
bool loadDeveloperMission(PPCContext& ctx, uint8_t* base, uint32_t client,
                          std::string_view id, std::string& status);
}
