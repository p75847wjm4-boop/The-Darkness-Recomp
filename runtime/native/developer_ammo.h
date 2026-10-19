#pragma once
#include <cstdint>

namespace DarkRecomp::Native {
// Resolve the active server player on the engine thread before configuring.
// Only the selected player's own CRPG_Object_Ammo items keep their loaded rounds;
// NPC ammo and saved inventory counts are never changed.
void configureDeveloperInfiniteAmmo(uint8_t* base, uint32_t actor, bool infinite, bool enabled) noexcept;
// Rapid fire clears the player's weapon refire delay; no recoil zeroes the two
// kick values each shot sends to the shooter (both through 822C26C8).
void configureDeveloperWeaponMods(uint8_t* base, uint32_t actor, bool rapidFire, bool noRecoil,
                                  bool enabled) noexcept;
}
