#include "developer_probe.h"
#include "developer_player_lookup.h"
#include "runtime.h"
#include "ppc_recomp_shared.h"
#include <atomic>
#include <bit>
#include <cstdio>
#include <string>
#include <cmath>
#include <functional>
#include <set>

namespace DarkRecomp::Native {
namespace {
std::atomic<uint64_t> lastViewport{0};
unsigned dumpCount = 0;

bool readable(uint8_t* base, uint32_t address, size_t bytes) {
    if (!memory || base != memory->base() || !address || (address & 3) ||
        uint64_t(address) + bytes > PPC_MEMORY_SIZE) return false;
    auto* cursor = base + address;
    const auto* end = cursor + bytes;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(cursor, &info, sizeof(info)) || info.State != MEM_COMMIT ||
            (info.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
        cursor = static_cast<uint8_t*>(info.BaseAddress) + info.RegionSize;
    }
    return true;
}

float f32(uint8_t* base, uint32_t address) { return std::bit_cast<float>(PPC_LOAD_U32(address)); }

// Rows of four: offset, raw words, floats. Offsets are decimal for grep.
void block(uint8_t* base, const char* label, uint32_t address, uint32_t bytes) {
    if (!readable(base, address, bytes)) {
        std::fprintf(stderr, "[Probe %u] %s %08X unreadable\n", dumpCount, label, address);
        return;
    }
    for (uint32_t offset = 0; offset < bytes; offset += 16) {
        const auto at = address + offset;
        std::fprintf(stderr, "[Probe %u] %s +%u: %08X %08X %08X %08X | %g %g %g %g\n", dumpCount, label, offset,
            PPC_LOAD_U32(at), PPC_LOAD_U32(at + 4), PPC_LOAD_U32(at + 8), PPC_LOAD_U32(at + 12),
            f32(base, at), f32(base, at + 4), f32(base, at + 8), f32(base, at + 12));
    }
}
}

void rememberGameplayViewport(uint32_t client, uint32_t viewport) noexcept {
    lastViewport.store((uint64_t(client) << 32) | viewport, std::memory_order_release);
}

void dumpDeveloperProbe(PPCContext& ctx, uint8_t* base, uint32_t client) {
    ++dumpCount;
    const auto player = resolveDeveloperPlayer(ctx, base, client);
    std::fprintf(stderr, "[Probe %u] begin client=%08X server=%08X actor=%08X state=%08X\n",
                 dumpCount, client, player.server, player.actor, player.state);
    if (!player.actor) return;
    block(base, "actor", player.actor, 1024);
    const auto camera = lastViewport.load(std::memory_order_acquire);
    if (uint32_t(camera >> 32) == client) block(base, "viewport", uint32_t(camera), 416);
    const auto objects = PPC_LOAD_U32(player.server + 1460);
    if (!readable(base, objects, 28)) return;
    const auto count = PPC_LOAD_U32(objects + 4), items = PPC_LOAD_U32(objects + 24);
    if (count > 8192 || !readable(base, items, count * 4)) return;
    std::fprintf(stderr, "[Probe %u] objects=%u\n", dumpCount, count);
    for (uint32_t id = 0; id < count; ++id) {
        const auto object = PPC_LOAD_U32(items + id * 4);
        if (!readable(base, object, 640)) continue;
        std::fprintf(stderr, "[Probe %u] object id=%u at=%08X vtable=%08X ids=%u/%u state=%08X pos80=%g %g %g\n",
            dumpCount, id, object, PPC_LOAD_U32(object), PPC_LOAD_U16(object + 368), PPC_LOAD_U16(object + 370),
            PPC_LOAD_U32(object + 396), f32(base, object + 128), f32(base, object + 132), f32(base, object + 136));
    }
    // Skeleton research: the nearest spawned NPC's client object, plus the
    // objects its first pointers reference (bone matrices live off-object).
    uint32_t nearest = 0; float best = 1e30f;
    const float px = f32(base, player.actor + 64), py = f32(base, player.actor + 68);
    for (uint32_t id = 1; id < count; ++id) {
        const auto object = PPC_LOAD_U32(items + id * 4);
        if (!readable(base, object, 640) || PPC_LOAD_U32(object) != 0x820626C8) continue;
        const float dx = f32(base, object + 64) - px, dy = f32(base, object + 68) - py;
        auto call = ctx; call.r3.u64 = client; call.r4.u64 = id;
        sub_8249D618(call, base);
        if (call.r3.u32 && dx * dx + dy * dy < best) { best = dx * dx + dy * dy; nearest = call.r3.u32; }
    }
    if (nearest) {
        const float nx = f32(base, nearest + 64), ny = f32(base, nearest + 68), nz = f32(base, nearest + 72);
        std::fprintf(stderr, "[Probe %u] skeleton search npc=%08X state=%08X pos=%g %g %g\n",
                     dumpCount, nearest, PPC_LOAD_U32(nearest + 396), nx, ny, nz);
        // Bone arrays: runs of matrices (64-byte 4x4 or 48-byte 3x4) whose 3x3 part
        // is orthonormal and whose translation is near the NPC (world space) or
        // near the origin (model space). Follow pointers two levels deep.
        const auto rotation = [&](uint32_t at, uint32_t stride) {
            for (int row = 0; row < 3; ++row) {
                const uint32_t r = at + row * (stride == 64 ? 16 : 16);
                float length = 0;
                for (int i = 0; i < 3; ++i) {
                    const float v = f32(base, r + i * 4);
                    if (!std::isfinite(v)) return false;
                    length += v * v;
                }
                if (std::abs(length - 1) > 0.02f) return false;
            }
            return true;
        };
        std::set<uint32_t> seen;
        unsigned reported = 0;
        const std::function<void(uint32_t, uint32_t, int, const std::string&)> search =
            [&](uint32_t block, uint32_t bytes, int depth, const std::string& path) {
            // A one-shot diagnostic may pause the game briefly; bound the walk.
            if (seen.size() >= 6000 || !seen.insert(block).second || !readable(base, block, bytes) ||
                reported >= 24) return;
            for (const uint32_t stride : {64u, 48u}) {
                for (uint32_t start = 0; start + stride * 8 <= bytes; start += 16) {
                    uint32_t run = 0;
                    while (start + (run + 1) * stride <= bytes && rotation(block + start + run * stride, stride)) ++run;
                    if (run < 8) continue;
                    std::fprintf(stderr, "[Probe %u] bones path=%s block=%08X start=%u stride=%u count=%u\n",
                                 dumpCount, path.c_str(), block, start, stride, run);
                    for (uint32_t bone = 0; bone < run && bone < 96; ++bone) {
                        const auto at = block + start + bone * stride;
                        const float tx = stride == 64 ? f32(base, at + 48) : f32(base, at + 12);
                        const float ty = stride == 64 ? f32(base, at + 52) : f32(base, at + 28);
                        const float tz = stride == 64 ? f32(base, at + 56) : f32(base, at + 44);
                        std::fprintf(stderr, "[Probe %u] bone %u: %g %g %g | rel %g %g %g\n", dumpCount, bone,
                                     tx, ty, tz, tx - nx, ty - ny, tz - nz);
                    }
                    ++reported;
                    start += run * stride;
                }
            }
            if (depth >= 3) return;
            for (uint32_t offset = 0; offset + 4 <= bytes; offset += 4) {
                const auto pointer = PPC_LOAD_U32(block + offset);
                if (pointer >= 0x40000000 && pointer < 0xF0000000 && !(pointer & 15))
                    search(pointer, depth == 0 ? 8192 : 4096, depth + 1, path + "+" + std::to_string(offset) + ">");
            }
        };
        search(nearest, 2048, 0, "npc");
        const auto state = PPC_LOAD_U32(nearest + 396);
        if (state) search(state, 12288, 0, "state");
        std::fprintf(stderr, "[Probe %u] skeleton search reported=%u visited=%zu\n", dumpCount, reported, seen.size());
    }
    std::fprintf(stderr, "[Probe %u] end\n", dumpCount);
}
}
