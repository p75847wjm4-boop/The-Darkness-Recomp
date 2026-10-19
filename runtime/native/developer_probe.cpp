#include "developer_probe.h"
#include "developer_player_lookup.h"
#include "runtime.h"
#include "ppc_recomp_shared.h"
#include <atomic>
#include <bit>
#include <cstdio>
#include <string>

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
    std::fprintf(stderr, "[Probe %u] end\n", dumpCount);
}
}
