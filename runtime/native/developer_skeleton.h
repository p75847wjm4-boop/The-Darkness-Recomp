#pragma once
#include <cstdint>

namespace DarkRecomp::Native {
// Captured while the original character animation path (8212B238) evaluates a
// skeleton (824220E8). Only characters the game animates this frame appear.
struct CapturedSkeleton {
    uint32_t skeleton = 0, instance = 0;
    uint64_t tick = 0;
};
void setSkeletonCapture(bool enabled) noexcept;
bool capturedSkeleton(uint32_t character, CapturedSkeleton& result);
// Diagnostics: up to max captured character addresses.
unsigned capturedCharacters(uint32_t* out, unsigned max);
}
