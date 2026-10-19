#include "developer_esp.h"
#include "developer_player_lookup.h"
#include "developer_skeleton.h"
#include "runtime.h"
#include "ppc_recomp_shared.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <mutex>
#include <unordered_map>

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

// Original client actor lookup (both client tables' virtual +284), including
// predicted objects. Null for objects the client has not spawned.
uint32_t clientObject(PPCContext& ctx, uint8_t* base, uint32_t client, uint32_t id) {
    auto call = ctx;
    call.r3.u64 = client; call.r4.u64 = id;
    sub_8249D618(call, base);
    return call.r3.u32;
}

bool clientClient(uint8_t* base, uint32_t client) {
    if (!committed(base, client, 8980)) return false;
    const auto table = PPC_LOAD_U32(client);
    return (table == 0x820807E0 || table == 0x82081BF0) && !(PPC_LOAD_U32(client + 516) & 0x20);
}

bool matrix(uint8_t* base, uint32_t object, float (&rows)[4][3]) {
    if (!committed(base, object, kMatrix + 64)) return false;
    for (int row = 0; row < 4; ++row)
        for (int i = 0; i < 3; ++i) {
            rows[row][i] = f32(base, object + kMatrix + row * 16 + i * 4);
            if (!std::isfinite(rows[row][i])) return false;
        }
    const float length = rows[0][0] * rows[0][0] + rows[0][1] * rows[0][1] + rows[0][2] * rows[0][2];
    return length > 0.8f && length < 1.2f;
}

bool clientView(PPCContext& ctx, uint8_t* base, uint32_t client, EspSnapshot& view) {
    if (!clientClient(base, client)) return false;
    const auto playerId = PPC_LOAD_U32(client + 536);
    if (!playerId || playerId > 0xffff) return false;
    float rows[4][3];
    if (!matrix(base, clientObject(ctx, base, client, playerId), rows)) return false;
    for (int i = 0; i < 3; ++i) {
        view.forward[i] = rows[0][i]; view.left[i] = rows[1][i];
        view.up[i] = rows[2][i]; view.eye[i] = rows[3][i];
    }
    view.eye[2] += kEyeHeight;
    const float fov = std::bit_cast<float>(fovBits.load(std::memory_order_relaxed));
    view.tanHalfVertical = std::tan(fov * 3.14159265f / 360) * 0.75f;
    return true;
}
std::atomic<uint32_t> lockedTarget{0};

// CXR_Skeleton (+12 -> count, nodes at +16, 48 bytes each): rest-pose center
// at +0, parent index (int16, FFFF = none) at +28. A CXR_SkeletonInstance
// keeps this frame's world bone matrices at +36 (64-byte rows, count +60);
// a joint is its rest center transformed by its bone matrix.
struct Pose {
    float root[3]{};
    uint32_t skeleton = 0, instance = 0;
};

bool joint(uint8_t* base, uint32_t nodes, uint32_t matrices, uint32_t index, float (&out)[3]) {
    const auto node = nodes + 16 + index * 48, m = matrices + index * 64;
    const float c[3]{f32(base, node), f32(base, node + 4), f32(base, node + 8)};
    for (int k = 0; k < 3; ++k) {
        out[k] = c[0] * f32(base, m + k * 4) + c[1] * f32(base, m + 16 + k * 4) +
                 c[2] * f32(base, m + 32 + k * 4) + f32(base, m + 48 + k * 4);
        if (!std::isfinite(out[k])) return false;
    }
    return true;
}

unsigned attachSkeleton(uint8_t* base, const Pose& pose, EspTarget& target) {
    const auto nodes = PPC_LOAD_U32(pose.skeleton + 12);
    if (!committed(base, nodes, 16)) return 0;
    const auto count = (std::min)(PPC_LOAD_U32(nodes), uint32_t(PPC_LOAD_U16(pose.instance + 60)));
    const auto matrices = PPC_LOAD_U32(pose.instance + 36);
    if (!count || count > 256 || !committed(base, nodes + 16, count * 48) || !committed(base, matrices, count * 64)) return 0;
    for (uint32_t i = 1; i < count; ++i) {
        const auto parent = PPC_LOAD_U16(nodes + 16 + i * 48 + 28);
        if (parent >= count) continue;
        float a[3], b[3];
        if (!joint(base, nodes, matrices, i, a) || !joint(base, nodes, matrices, parent, b)) continue;
        const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
        const float length = dx * dx + dy * dy + dz * dz;
        // Skip unevaluated bones (zero matrices) and finger-sized links.
        if (length < 4 || length > 40 * 40 || std::abs(a[0] - target.x) > 120 || std::abs(a[1] - target.y) > 120) continue;
        target.bones.push_back({a[0], a[1], a[2], b[0], b[1], b[2]});
        if (!target.hasHead || a[2] > target.head[2]) {
            target.hasHead = true;
            target.head[0] = a[0]; target.head[1] = a[1]; target.head[2] = a[2];
        }
    }
    return unsigned(target.bones.size());
}

// Last good skeleton per NPC, re-used (moved with the NPC) for a short time so
// frames where the game skips or rewrites an evaluation do not blink.
struct HeldPose {
    EspTarget target;
    uint64_t tick = 0;
};
std::unordered_map<uint32_t, HeldPose> held;
constexpr uint64_t kHoldMs = 300;

std::vector<Pose> currentPoses(uint8_t* base) {
    std::vector<Pose> poses;
    for (const auto& captured : capturedSkeletons()) {
        if (!committed(base, captured.instance, 64) || !committed(base, captured.skeleton, 16)) continue;
        const auto matrices = PPC_LOAD_U32(captured.instance + 36);
        if (!committed(base, matrices, 64)) continue;
        Pose pose{{f32(base, matrices + 48), f32(base, matrices + 52), f32(base, matrices + 56)},
                  captured.skeleton, captured.instance};
        poses.push_back(pose);
    }
    return poses;
}
}

void rememberGameplayFov(float degrees) noexcept {
    if (std::isfinite(degrees) && degrees > 1 && degrees < 179)
        fovBits.store(std::bit_cast<uint32_t>(degrees), std::memory_order_relaxed);
}

void collectDeveloperEsp(PPCContext& ctx, uint8_t* base, uint32_t client,
                         const DeveloperPlayerHandles& player, bool box, bool skeleton, bool aimbot) {
    const bool esp = box || skeleton;
    const auto now = GetTickCount64();
    aimEnabled.store(aimbot, std::memory_order_release);
    regions = {}; // Allocations may have been released since the last update.
    EspSnapshot next;
    if ((esp || aimbot) && player.actor && player.server && clientView(ctx, base, client, next)) {
        next.box = box;
        next.skeleton = skeleton;
        const auto poses = esp ? currentPoses(base) : std::vector<Pose>{};
        const auto objects = PPC_LOAD_U32(player.server + 1460);
        if (committed(base, objects, 28)) {
            const auto count = PPC_LOAD_U32(objects + 4), items = PPC_LOAD_U32(objects + 24);
            if (count <= 16384 && committed(base, items, count * 4)) {
                for (uint32_t id = 1; id < count; ++id) {
                    const auto object = PPC_LOAD_U32(items + id * 4);
                    if (object == player.actor || !committed(base, object, 660) ||
                        PPC_LOAD_U32(object) != kNpcVtable) continue;
                    int hp = 0;
                    if (!health(base, object, hp) || hp <= 0) continue;
                    float rows[4][3];
                    if (!matrix(base, clientObject(ctx, base, client, id), rows)) continue;
                    EspTarget target{rows[3][0], rows[3][1], rows[3][2], hp, id};
                    // With ESP on, show only characters the game animated this
                    // frame: their skeleton root sits exactly at the origin.
                    if (esp) {
                        // Several instances can share one character (blending,
                        // attachments); keep the one with the most valid bones.
                        EspTarget best = target;
                        unsigned bestBones = 0;
                        bool animated = false;
                        for (const auto& candidate : poses) {
                            const float dx = candidate.root[0] - target.x, dy = candidate.root[1] - target.y,
                                        dz = candidate.root[2] - target.z;
                            if (dx * dx + dy * dy + dz * dz >= 36) continue;
                            animated = true;
                            EspTarget trial = target;
                            const auto bones = attachSkeleton(base, candidate, trial);
                            if (bones > bestBones) { bestBones = bones; best = std::move(trial); }
                        }
                        auto found = held.find(id);
                        if (bestBones >= 6) {
                            held[id] = {best, now};
                            target = std::move(best);
                        } else if (found != held.end() && now - found->second.tick < kHoldMs) {
                            const auto& last = found->second.target;
                            const float mx = target.x - last.x, my = target.y - last.y, mz = target.z - last.z;
                            target.bones = last.bones;
                            for (auto& bone : target.bones) {
                                bone[0] += mx; bone[1] += my; bone[2] += mz;
                                bone[3] += mx; bone[4] += my; bone[5] += mz;
                            }
                            target.hasHead = last.hasHead;
                            target.head[0] = last.head[0] + mx; target.head[1] = last.head[1] + my;
                            target.head[2] = last.head[2] + mz;
                        } else if (!animated) continue;
                    }
                    next.targets.push_back(std::move(target));
                }
            }
        }
        next.valid = true;
    }
    for (auto it = held.begin(); it != held.end();)
        it = now - it->second.tick > kHoldMs ? held.erase(it) : std::next(it);
    std::lock_guard lock(snapshotMutex);
    current = std::move(next);
}

void clearDeveloperEsp() noexcept {
    aimEnabled.store(false, std::memory_order_release);
    held.clear();
    lockedTarget.store(0, std::memory_order_relaxed);
    std::lock_guard lock(snapshotMutex);
    current = {};
}

EspSnapshot developerEspSnapshot() {
    std::lock_guard lock(snapshotMutex);
    return current;
}

bool developerAimCorrection(PPCContext& ctx, uint8_t* base, uint32_t client,
                            int64_t& yawUnits, int64_t& pitchUnits) {
    if (!aimEnabled.load(std::memory_order_acquire) || !foreground() ||
        !((GetAsyncKeyState('V') | GetAsyncKeyState(VK_XBUTTON1)) & 0x8000)) {
        lockedTarget.store(0, std::memory_order_relaxed);
        return false;
    }
    const auto targets = developerEspSnapshot().targets;
    regions = {};
    EspSnapshot view;
    if (targets.empty() || !clientView(ctx, base, client, view)) return false;
    const float* f = view.forward;
    const auto direction = [&](const EspTarget& target, float& x, float& y, float& z) {
        const float aimZ = target.hasHead ? target.head[2] - 2 : target.z + kAimHeight;
        x = (target.hasHead ? target.head[0] : target.x) - view.eye[0];
        y = (target.hasHead ? target.head[1] : target.y) - view.eye[1];
        z = aimZ - view.eye[2];
        const float length = std::sqrt(x * x + y * y + z * z);
        if (length < 1 || length > kAimRange) return 10.0f;
        return std::acos(std::clamp((x * f[0] + y * f[1] + z * f[2]) / length, -1.0f, 1.0f));
    };
    // Keep the current target while it stays alive and in the cone, so nearby
    // NPCs cannot pull the view back and forth between frames.
    const EspTarget* chosen = nullptr;
    float best = kAimCone, dx = 0, dy = 0, dz = 0;
    const auto locked = lockedTarget.load(std::memory_order_relaxed);
    for (const auto& target : targets) {
        float x, y, z;
        const float angle = direction(target, x, y, z);
        if (target.id == locked && angle < kAimCone * 1.5f) { chosen = &target; dx = x; dy = y; dz = z; break; }
        if (angle < best) { best = angle; chosen = &target; dx = x; dy = y; dz = z; }
    }
    if (!chosen) { lockedTarget.store(0, std::memory_order_relaxed); return false; }
    lockedTarget.store(chosen->id, std::memory_order_relaxed);
    const double yawError = std::remainder(std::atan2(dy, dx) - std::atan2(f[1], f[0]), 2 * 3.14159265358979);
    const double pitchError = std::atan2(dz, std::hypot(dx, dy)) - std::asin(std::clamp(f[2], -1.0f, 1.0f));
    // The client applies a look packet on its next update, so correct only part
    // of the error per call and stop inside a small dead zone.
    constexpr double gain = 0.2, limit = 1500, deadZone = 0.004;
    const auto step = [&](double error) {
        if (std::abs(error) < deadZone) return int64_t(0);
        return int64_t(std::clamp(error * kUnitsPerRadian * gain, -limit, limit));
    };
    yawUnits = -step(yawError); // Positive guest yaw turns right (clockwise, Z up).
    pitchUnits = step(pitchError);
    return yawUnits || pitchUnits;
}
}
