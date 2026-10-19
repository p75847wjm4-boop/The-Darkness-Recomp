#pragma once
#include <cstdint>
#include <vector>

namespace DarkRecomp::Native {
// Every skeleton evaluation (824220E8, reached from about 17 original callers)
// while capture is on, keyed by its CXR_SkeletonInstance. Matching to a
// character is by position: the bone matrices sit at the character's origin.
struct CapturedSkeleton {
    uint32_t skeleton = 0, instance = 0;
    uint64_t tick = 0;
};
void setSkeletonCapture(bool enabled) noexcept;
// Skeletons evaluated in the last 250 ms.
std::vector<CapturedSkeleton> capturedSkeletons();
}
