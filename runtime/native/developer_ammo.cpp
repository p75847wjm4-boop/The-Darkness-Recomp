#include "developer_ammo.h"
#include "stall_profiler.h"
#include "runtime.h"
#include "ppc_recomp_shared.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>

namespace {
// CRPG_Object_Ammo (factory 82332550, vtable 8206FAC8): +540 total rounds,
// +728 magazine size, +732 loaded rounds, +736 maximum. Item::Activate 822C2C08
// and the weapon fire paths draw rounds through vtable+288, 82332428.
constexpr uint32_t kAmmoVtable = 0x8206FAC8;
constexpr uint32_t kLoadedOffset = 732;
constexpr uint32_t kInventoryOffset = 656;
std::atomic<uint8_t*> activeBase{nullptr};
std::atomic<uint32_t> activeInventory{0};
std::atomic<uint64_t> skipped{0};

bool probeEnabled() noexcept {
    static const bool enabled = [] {
        const char* value = std::getenv("DARK_DEVELOPER_PROBE");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return enabled;
}

bool span(uint32_t address, uint32_t size) noexcept {
    return address && !(address & 3) && uint64_t(address) + size <= PPC_MEMORY_SIZE;
}

// Original TArray: +4 count, +24 data. 82324800 indexes inventory+24 categories,
// each holding its items in the same layout at +24.
bool array(uint8_t* base, uint32_t address, uint32_t limit, uint32_t& data, uint32_t& count) noexcept {
    data = count = 0;
    if (!address) return true;
    if (!span(address, 28)) return false;
    count = PPC_LOAD_U32(address + 4);
    data = PPC_LOAD_U32(address + 24);
    return count <= limit && (!count || span(data, count * 4));
}

bool playerAmmo(uint8_t* base, uint32_t ammo) noexcept {
    const auto inventory = activeInventory.load(std::memory_order_acquire);
    if (!inventory || !DarkRecomp::Native::memory || base != DarkRecomp::Native::memory->base() ||
        activeBase.load(std::memory_order_acquire) != base || !span(ammo, kLoadedOffset + 4) ||
        PPC_LOAD_U32(ammo) != kAmmoVtable || !span(inventory, 28)) return false;
    uint32_t categories, categoryCount;
    if (!array(base, PPC_LOAD_U32(inventory + 24), 16, categories, categoryCount)) return false;
    for (uint32_t c = 0; c < categoryCount; ++c) {
        uint32_t items, itemCount;
        const auto category = PPC_LOAD_U32(categories + c * 4);
        if (!span(category, 28) || !array(base, PPC_LOAD_U32(category + 24), 4096, items, itemCount)) continue;
        for (uint32_t i = 0; i < itemCount; ++i)
            if (PPC_LOAD_U32(items + i * 4) == ammo) return true;
    }
    return false;
}
}

namespace DarkRecomp::Native {
void configureDeveloperInfiniteAmmo(uint8_t* base, uint32_t actor, bool infinite, bool enabled) noexcept {
    if (!enabled || !infinite || !memory || base != memory->base() || !span(actor, kInventoryOffset + 4)) {
        activeInventory.store(0, std::memory_order_release);
        return;
    }
    activeBase.store(base, std::memory_order_release);
    activeInventory.store(PPC_LOAD_U32(actor + kInventoryOffset), std::memory_order_release);
}
}

// Original 82332428(ammo, rounds): take = min(loaded, rounds), draw take from the
// total through vtable+304, loaded -= rounds, return take. For the player's own
// ammo with a sufficient magazine, return the same take without either draw.
// An empty magazine still runs the original so the normal reload path starts.
extern "C" PPC_FUNC(__imp__sub_82332428);
PPC_FUNC(sub_82332428) {
    DarkRecomp::Native::StallProfiler::Scope stallProfile(DarkRecomp::Native::StallProfiler::Section::Guest, __func__, 0x82332428u, uint32_t(ctx.lr));
    const auto ammo = ctx.r3.u32;
    const auto rounds = ctx.r4.s32;
    if (rounds > 0 && playerAmmo(base, ammo) && int32_t(PPC_LOAD_U32(ammo + kLoadedOffset)) >= rounds) {
        ctx.r3.u64 = uint32_t(rounds);
        if (probeEnabled() && skipped.fetch_add(1, std::memory_order_relaxed) < 16) {
            const auto savedMath = _mm_getcsr();
            std::fprintf(stderr, "[InfiniteAmmo] ammo=%08X rounds=%d loaded=%u caller=%08X\n",
                         ammo, rounds, PPC_LOAD_U32(ammo + kLoadedOffset), uint32_t(ctx.lr));
            _mm_setcsr(savedMath);
        }
        return;
    }
    __imp__sub_82332428(ctx, base);
}
