#pragma once
#include <cstdint>

union PPCRegister;
namespace DarkRecomp::Native {
// The actor and state must be the active server player, resolved on the engine
// thread. Configuration contains host values only; no cheat bit is saved.
void configureDeveloperInvincibility(uint8_t* base, uint32_t actor, uint32_t state,
                                    bool invincible, bool enabled) noexcept;
// The player's summoned darklings (CWObject_CharDarkling) take the same god-mode path.
void configureDeveloperDarklingGod(bool enabled) noexcept;
}
void ApplyDeveloperInvincibilityMidAsmHook(PPCRegister& actor, PPCRegister& state, PPCRegister& flags);
