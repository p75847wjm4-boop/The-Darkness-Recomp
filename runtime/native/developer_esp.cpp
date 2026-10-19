#include "developer_esp.h"
#include "developer_player_lookup.h"
#include "runtime.h"
#include "ppc_recomp_shared.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <mutex>

namespace DarkRecomp::Native {
namespace {
// Server object table (server+1460, TArray: +4 count, +24 data) holds objects by
// ID. CWObject_CharNPC (factory 8211AE68) installs vtable 820626C8. Every
// CWObject keeps its world matrix at +16 (row 3, +64, is the position).
// Health is the int16 the original death test 8214EC3C reads through the
// character's RPG object: actor+656 -> +32 -> +24 -> [0].
constexpr uint32_t kNpcVtable = 0x820626C8;
constexpr uint32_t kMatrix = 16;
constexpr float kEyeHeight = 52, kAimHeight = 46;
constexpr float kAimCone = 25.0f * 3.14159265f / 180, kAimRange = 4000;
constexpr double kUnitsPerRadian = 65536.0 / (2 * 3.14159265358979);

std::mutex snapshotMutex;
EspSnapshot current;
std::atomic<bool> aimEnabled{false};
std::atomic<uint32_t> fovBits{std::bit_cast<uint32_t>(70.0f)};

bool span(uint32_t address, uint32_t size) {
    return address && !(address & 3) && uint64_t(address) + size <= PPC_MEMORY_SIZE;
}

// Heap objects cluster in a few committed regions; remember the last ones so a
// full object-table pass costs a handful of VirtualQuery calls.
struct Regions {
    std::array<std::pair<uint8_t*, uint8_t*>, 8> known{};
    unsigned next = 0;
};
thread_local Regions regions;

bool committed(uint8_t* base, uint32_t address, uint32_t size) {
    if (!span(address, size)) return false;
    auto* first = base + address;
    auto* last = first + size;
    for (const auto& [begin, end] : regions.known)
        if (first >= begin && last <= end) return true;
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(first, &info, sizeof(info)) || info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    auto* begin = static_cast<uint8_t*>(info.BaseAddress);
    auto* end = begin + info.RegionSize;
    if (last > end) return false;
    regions.known[regions.next++ % regions.known.size()] = {begin, end};
    return true;
}

float f32(uint8_t* base, uint32_t address) { return std::bit_cast<float>(PPC_LOAD_U32(address)); }

bool health(uint8_t* base, uint32_t object, int& value) {
    const auto rpg = PPC_LOAD_U32(object + 656);
    if (!committed(base, rpg, 36)) return false;
    const auto values = PPC_LOAD_U32(rpg + 32);
    if (!committed(base, values, 28)) return false;
    const auto data = PPC_LOAD_U32(values + 24);
    if (!committed(base, data, 2)) return false;
    value = int16_t(PPC_LOAD_U16(data));
    return true;
}

bool foreground() {
    DWORD process = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &process);
    return process == GetCurrentProcessId();
}
}

void rememberGameplayFov(float degrees) noexcept {
    if (std::isfinite(degrees) && degrees > 1 && degrees < 179)
        fovBits.store(std::bit_cast<uint32_t>(degrees), std::memory_order_relaxed);
}

void collectDeveloperEsp(uint8_t* base, const DeveloperPlayerHandles& player, bool esp, bool aimbot) {
    aimEnabled.store(aimbot, std::memory_order_release);
    regions = {}; // Allocations may have been released since the last update.
    EspSnapshot next;
    if ((esp || aimbot) && player.actor && player.server && committed(base, player.actor, 1024)) {
        const auto m = player.actor + kMatrix;
        for (int i = 0; i < 3; ++i) {
            next.forward[i] = f32(base, m + i * 4);
            next.left[i] = f32(base, m + 16 + i * 4);
            next.up[i] = f32(base, m + 32 + i * 4);
            next.eye[i] = f32(base, m + 48 + i * 4);
        }
        next.eye[2] += kEyeHeight;
        const float fov = std::bit_cast<float>(fovBits.load(std::memory_order_relaxed));
        next.tanHalfVertical = std::tan(fov * 3.14159265f / 360) * 0.75f;
        next.esp = esp;
        const auto objects = PPC_LOAD_U32(player.server + 1460);
        if (committed(base, objects, 28)) {
            const auto count = PPC_LOAD_U32(objects + 4), items = PPC_LOAD_U32(objects + 24);
            if (count <= 16384 && committed(base, items, count * 4)) {
                for (uint32_t id = 0; id < count; ++id) {
                    const auto object = PPC_LOAD_U32(items + id * 4);
                    if (object == player.actor || !committed(base, object, 660) ||
                        PPC_LOAD_U32(object) != kNpcVtable) continue;
                    int hp = 0;
                    if (!health(base, object, hp) || hp <= 0) continue;
                    EspTarget target{f32(base, object + kMatrix + 48), f32(base, object + kMatrix + 52),
                                     f32(base, object + kMatrix + 56), hp};
                    if (std::isfinite(target.x) && std::isfinite(target.y) && std::isfinite(target.z))
                        next.targets.push_back(target);
                }
            }
        }
        next.valid = true;
    }
    std::lock_guard lock(snapshotMutex);
    current = std::move(next);
}

EspSnapshot developerEspSnapshot() {
    std::lock_guard lock(snapshotMutex);
    return current;
}

bool developerAimCorrection(int64_t& yawUnits, int64_t& pitchUnits) {
    if (!aimEnabled.load(std::memory_order_acquire) || !foreground() ||
        !((GetAsyncKeyState('V') | GetAsyncKeyState(VK_XBUTTON1)) & 0x8000)) return false;
    const auto view = developerEspSnapshot();
    if (!view.valid) return false;
    const float* f = view.forward;
    float best = kAimCone, dx = 0, dy = 0, dz = 0;
    for (const auto& target : view.targets) {
        const float x = target.x - view.eye[0], y = target.y - view.eye[1], z = target.z + kAimHeight - view.eye[2];
        const float length = std::sqrt(x * x + y * y + z * z);
        if (length < 1 || length > kAimRange) continue;
        const float angle = std::acos(std::clamp((x * f[0] + y * f[1] + z * f[2]) / length, -1.0f, 1.0f));
        if (angle < best) { best = angle; dx = x; dy = y; dz = z; }
    }
    if (best >= kAimCone) return false;
    const double yawError = std::remainder(std::atan2(dy, dx) - std::atan2(f[1], f[0]), 2 * 3.14159265358979);
    const double pitchError = std::atan2(dz, std::hypot(dx, dy)) - std::asin(std::clamp(f[2], -1.0f, 1.0f));
    // Ease in over several ticks; positive guest yaw turns right (clockwise, Z up).
    constexpr double smoothing = 0.4, limit = 3000;
    yawUnits = int64_t(std::clamp(-yawError * kUnitsPerRadian * smoothing, -limit, limit));
    pitchUnits = int64_t(std::clamp(pitchError * kUnitsPerRadian * smoothing, -limit, limit));
    return yawUnits || pitchUnits;
}
}
