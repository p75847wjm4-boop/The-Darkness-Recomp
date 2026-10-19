#pragma once
#include <cstdint>

struct PPCContext;

namespace DarkRecomp::Native {
// Diagnostic dump for ESP/aimbot research: writes the player actor, the last
// gameplay camera viewport and every server object to the runtime log.
// Engine thread only; reads guest memory and changes nothing.
void dumpDeveloperProbe(PPCContext& ctx, uint8_t* base, uint32_t client);
// Recorded by the 8249A030 gameplay camera query wrapper.
void rememberGameplayViewport(uint32_t client, uint32_t viewport) noexcept;
}
