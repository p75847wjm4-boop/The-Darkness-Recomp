#include "developer_ammo.h"
#include "stall_profiler.h"
#include "runtime.h"
#include "ppc_recomp_shared.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>

namespace {
// Guns keep their magazine in the weapon: 82325CA8 (CRPG_Object_Summon::Activate,
// reached from the Gun/Rifle/ShotGun activate overrides) subtracts its rounds
// argument from int16 +396 unless +398 (magazine size) is FFFF or +584 bit 0x40
// marks an authored unlimited weapon. Reloads move rounds from CRPG_Object_Ammo
// (factory 82332550, vtable 8206FAC8: +540 total, +728 magazine size, +732
// loaded, +736 maximum); item and minigun paths draw those through 82332428.
constexpr uint32_t kAmmoVtable = 0x8206FAC8;
constexpr uint32_t kLoadedOffset = 732;
constexpr uint32_t kMagazineOffset = 396;
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

bool inventoryItem(uint8_t* base, uint32_t inventory, uint32_t item, uint32_t size) noexcept {
    if (!inventory || !DarkRecomp::Native::memory || base != DarkRecomp::Native::memory->base() ||
        activeBase.load(std::memory_order_acquire) != base || !span(item, size) || !span(inventory, 28)) return false;
    uint32_t categories, categoryCount;
    if (!array(base, PPC_LOAD_U32(inventory + 24), 16, categories, categoryCount)) return false;
    for (uint32_t c = 0; c < categoryCount; ++c) {
        uint32_t items, itemCount;
        const auto category = PPC_LOAD_U32(categories + c * 4);
        if (!span(category, 28) || !array(base, PPC_LOAD_U32(category + 24), 4096, items, itemCount)) continue;
        for (uint32_t i = 0; i < itemCount; ++i)
            if (PPC_LOAD_U32(items + i * 4) == item) return true;
    }
    return false;
}
bool playerItem(uint8_t* base, uint32_t item, uint32_t size) noexcept {
    return inventoryItem(base, activeInventory.load(std::memory_order_acquire), item, size);
}
std::atomic<uint32_t> modInventory{0};
std::atomic<bool> rapidFire{false}, noRecoil{false};
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
void configureDeveloperWeaponMods(uint8_t* base, uint32_t actor, bool rapid, bool steady, bool enabled) noexcept {
    if (!enabled || !(rapid || steady) || !memory || base != memory->base() || !span(actor, kInventoryOffset + 4)) {
        modInventory.store(0, std::memory_order_release);
        return;
    }
    activeBase.store(base, std::memory_order_release);
    rapidFire.store(rapid, std::memory_order_release);
    noRecoil.store(steady, std::memory_order_release);
    modInventory.store(PPC_LOAD_U32(actor + kInventoryOffset), std::memory_order_release);
}
}
// Original 822C26C8(weapon, shooter, delay, f1, f2) runs as each shot fires:
// it sends f1 and f2 to the shooter as messages 101D/101C (the shot's kick),
// then sets the next permitted fire tick at +556 to now + delay (weapon +308).
// A zero delay stores tick 0, so the next trigger pull fires at once.
extern "C" PPC_FUNC(__imp__sub_822C26C8);
PPC_FUNC(sub_822C26C8) {
    const auto inventory = modInventory.load(std::memory_order_acquire);
    if (inventory && activeBase.load(std::memory_order_acquire) == base &&
        inventoryItem(base, inventory, ctx.r3.u32, 560)) {
        if (rapidFire.load(std::memory_order_relaxed)) ctx.r5.u64 = 0;
        if (noRecoil.load(std::memory_order_relaxed)) { ctx.f1.f64 = 0; ctx.f2.f64 = 0; }
    }
    __imp__sub_822C26C8(ctx, base);
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
    if (rounds > 0 && playerItem(base, ammo, kLoadedOffset + 4) && PPC_LOAD_U32(ammo) == kAmmoVtable &&
        int32_t(PPC_LOAD_U32(ammo + kLoadedOffset)) >= rounds) {
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

// Fire with zero rounds for the player's own weapons, as the original does for
// an authored unlimited weapon. Shot counting, effects and projectiles still run.
extern "C" PPC_FUNC(__imp__sub_82325CA8);
PPC_FUNC(sub_82325CA8) {
    DarkRecomp::Native::StallProfiler::Scope stallProfile(DarkRecomp::Native::StallProfiler::Section::Guest, __func__, 0x82325CA8u, uint32_t(ctx.lr));
    if (ctx.r4.s32 > 0 && playerItem(base, ctx.r3.u32, kMagazineOffset + 4)) {
        if (probeEnabled() && skipped.fetch_add(1, std::memory_order_relaxed) < 16) {
            const auto savedMath = _mm_getcsr();
            std::fprintf(stderr, "[InfiniteAmmo] weapon=%08X rounds=%d magazine=%d caller=%08X\n",
                         ctx.r3.u32, ctx.r4.s32, int(int16_t(PPC_LOAD_U16(ctx.r3.u32 + kMagazineOffset))),
                         uint32_t(ctx.lr));
            _mm_setcsr(savedMath);
        }
        ctx.r4.u64 = 0;
    }
    __imp__sub_82325CA8(ctx, base);
}
