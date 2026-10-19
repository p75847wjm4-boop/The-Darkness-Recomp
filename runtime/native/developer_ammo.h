#pragma once
#include <cstdint>

namespace DarkRecomp::Native {
// Resolve the active server player on the engine thread before configuring.
// Only the selected player's own CRPG_Object_Ammo items keep their loaded rounds;
// NPC ammo and saved inventory counts are never changed.
void configureDeveloperInfiniteAmmo(uint8_t* base, uint32_t actor, bool infinite, bool enabled) noexcept;
}
