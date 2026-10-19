#include "developer_skeleton.h"
#include "stall_profiler.h"
#include "runtime.h"
#include "ppc_recomp_shared.h"
#include <atomic>
#include <mutex>
#include <unordered_map>

namespace {
std::atomic<bool> captureEnabled{false};
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

std::vector<CapturedSkeleton> capturedSkeletons() {
    const auto now = GetTickCount64();
    std::vector<CapturedSkeleton> result;
    std::lock_guard lock(capturedMutex);
    for (auto it = captured.begin(); it != captured.end();) {
        if (now - it->second.tick > 250) { it = captured.erase(it); continue; }
        result.push_back(it->second);
        ++it;
    }
    return result;
}
}

// 824220E8(skeleton r3, ..., CXR_SkeletonInstance r6): after it returns, the
// instance's 64-byte bone matrices (+32/+36, count +60) hold this frame's pose.
extern "C" PPC_FUNC(__imp__sub_824220E8);
PPC_FUNC(sub_824220E8) {
    DarkRecomp::Native::StallProfiler::Scope stallProfile(DarkRecomp::Native::StallProfiler::Section::Guest, __func__, 0x824220E8u, uint32_t(ctx.lr));
    const auto skeleton = ctx.r3.u32, instance = ctx.r6.u32;
    __imp__sub_824220E8(ctx, base);
    if (!instance || !captureEnabled.load(std::memory_order_acquire)) return;
    std::lock_guard lock(capturedMutex);
    captured[instance] = {skeleton, instance, GetTickCount64()};
}
