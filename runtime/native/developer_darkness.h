#pragma once
#include <cstdint>
#include <string>
#include <string_view>

struct PPCContext;

namespace DarkRecomp::Native {
struct DeveloperPlayerHandles;

struct DeveloperDarknessResult {
    bool applied = false;
    std::string status;
};

// One-shot progression grants, on the engine thread with freshly resolved
// campaign-player handles. Unlock includes the original Ancient gun pair and
// repairs a missing partner without adding duplicates. The save system may
// retain these grants, including a partial inventory grant if creation fails.
DeveloperDarknessResult applyDeveloperDarkness(PPCContext& ctx, uint8_t* base,
    const DeveloperPlayerHandles& player, bool unlock, bool maximum, uint32_t client = 0);

// One-shot grant of a single weapon template through the same original helper
// as giveall. Only templates the current level has loaded are granted; the
// resource registry stays frozen, so nothing is added that the client lacks.
DeveloperDarknessResult grantDeveloperWeapon(PPCContext& ctx, uint8_t* base,
    const DeveloperPlayerHandles& player, std::string_view name, std::string_view title);

// Spawns an object template (for example an NPC) 96 units in front of the
// player through the server's original object factory, as the retail summon
// command does. Templates the level never loaded fail and are reported.
DeveloperDarknessResult spawnDeveloperObject(PPCContext& ctx, uint8_t* base,
    const DeveloperPlayerHandles& player, std::string_view name, uint32_t client);
// Removes every object spawnDeveloperObject created that still exists.
DeveloperDarknessResult removeDeveloperSpawns(PPCContext& ctx, uint8_t* base,
    const DeveloperPlayerHandles& player);
// Remembers the player's position, or moves the player back to it.
DeveloperDarknessResult saveDeveloperPosition(uint8_t* base, const DeveloperPlayerHandles& player);
DeveloperDarknessResult loadDeveloperPosition(uint8_t* base, const DeveloperPlayerHandles& player);
}
