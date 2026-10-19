#include "developer_skeleton.h"
#include "stall_profiler.h"
#include "runtime.h"
#include "ppc_recomp_shared.h"
#include <atomic>
#include <mutex>
#include <unordered_map>

namespace {
std::atomic<bool> captureEnabled{false};
thread_local uint32_t animatingCharacter = 0;
std::mutex capturedMutex;
std::unordered_map<uint32_t, DarkRecomp::Native::CapturedSkeleton> captured;
}

namespace DarkRecomp::Native {
void setSkeletonCapture(bool enabled) noexcept {
    captureEnabled.store(enabled, std::memory_order_release);
    if (!enabled) {
        std::lock_guard lock(capturedMutex);
        captured.clear();
    }
}

unsigned capturedCharacters(uint32_t* out, unsigned max) {
    std::lock_guard lock(capturedMutex);
    unsigned count = 0;
    for (const auto& [character, skeleton] : captured) {
        if (count == max) break;
        out[count++] = character;
    }
    return count;
}

bool capturedSkeleton(uint32_t character, CapturedSkeleton& result) {
    std::lock_guard lock(capturedMutex);
    const auto found = captured.find(character);
    if (found == captured.end() || GetTickCount64() - found->second.tick > 250) return false;
    result = found->second;
    return true;
}
}

// 8212B238(character, ..., skeleton r5, ...) evaluates one character's pose.
extern "C" PPC_FUNC(__imp__sub_8212B238);
PPC_FUNC(sub_8212B238) {
    DarkRecomp::Native::StallProfiler::Scope stallProfile(DarkRecomp::Native::StallProfiler::Section::Guest, __func__, 0x8212B238u, uint32_t(ctx.lr));
    const auto previous = animatingCharacter;
    animatingCharacter = ctx.r3.u32;
    __imp__sub_8212B238(ctx, base);
    animatingCharacter = previous;
}

// 824220E8(skeleton r3, ..., CXR_SkeletonInstance r6): after it returns, the
// instance's 64-byte bone matrices (+32/+36, count +60) hold this frame's pose.
extern "C" PPC_FUNC(__imp__sub_824220E8);
PPC_FUNC(sub_824220E8) {
    DarkRecomp::Native::StallProfiler::Scope stallProfile(DarkRecomp::Native::StallProfiler::Section::Guest, __func__, 0x824220E8u, uint32_t(ctx.lr));
    const auto skeleton = ctx.r3.u32, instance = ctx.r6.u32, character = animatingCharacter;
    __imp__sub_824220E8(ctx, base);
    if (!character || !captureEnabled.load(std::memory_order_acquire)) return;
    std::lock_guard lock(capturedMutex);
    captured[character] = {skeleton, instance, GetTickCount64()};
}
