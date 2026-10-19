#include "developer_tools.h"
#include "developer_missions.h"
#include "developer_player.h"
#include "developer_invincibility.h"
#include "developer_darkness.h"
#include "developer_player_lookup.h"
#include "ppc_recomp_shared.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <mutex>
#include <optional>
#include <utility>

namespace DarkRecomp::Native {
namespace {
std::mutex stateMutex;
DeveloperSnapshot state;
std::optional<std::string> pendingMission;
std::optional<std::string> pendingWeapon;
bool playerToolsEnabled = false;
bool pendingDarknessUnlock = false;
bool pendingDarknessLevel = false;
uint64_t requestRevision = 0;
bool panelVisible = false;
std::atomic<bool> needsUpdate{false};

void refreshWorkLocked() {
    needsUpdate.store(panelVisible || pendingMission.has_value() || pendingWeapon.has_value() || playerToolsEnabled ||
        pendingDarknessUnlock || pendingDarknessLevel, std::memory_order_release);
}

void requested(std::string_view status) {
    state.status = status;
    ++state.revision;
    ++requestRevision;
    refreshWorkLocked();
}

void publish(uint64_t request, bool canLoad, bool hasPlayer, std::string_view status,
             bool playerApplied = false, bool playerProcessed = false) {
    std::lock_guard lock(stateMutex);
    const bool availabilityChanged = state.canLoadMission != canLoad || state.hasActivePlayer != hasPlayer;
    state.canLoadMission = canLoad;
    state.hasActivePlayer = hasPlayer;
    if (playerApplied && status.empty() &&
        (state.status == "Player speed queued." || state.status == "Invincibility queued." ||
         state.status == "Noclip queued." || state.status == "Infinite ammo queued."))
        status = "Player settings applied.";
    const bool statusChanged = requestRevision == request && !status.empty() && state.status != status;
    if (statusChanged) state.status = status;
    if (availabilityChanged || statusChanged) ++state.revision;
    // Returning to defaults still needs one update to clear previously applied
    // hooks. A request arriving during that update must keep its next update.
    if (playerProcessed && requestRevision == request && state.playerSpeed == 1 &&
        !state.invincible && !state.noclip && !state.infiniteAmmo) playerToolsEnabled = false;
    refreshWorkLocked();
}

// Template names from the retail level archives (Content/Xdf/*_Common.XDF).
// NPC, multiplayer, scripted-scene and prop variants are omitted.
constexpr std::array<DeveloperWeapon, 13> weapons{{
    {"weapon_gun1911", "1911 pistol"},
    {"weapon_revolver", "Revolver"},
    {"weapon_gun_50cal", ".50 cal pistol"},
    {"weapon_so2", "SO2 pistol"},
    {"weapon_smg", "SMG"},
    {"weapon_automatic", "Assault rifle"},
    {"weapon_tactical", "Tactical rifle"},
    {"weapon_shgcombat", "Combat shotgun"},
    {"weapon_shgriot", "Riot shotgun"},
    {"weapon_minigun", "Minigun"},
    {"weapon_owrifle", "Otherworld rifle"},
    {"weapon_ww1_rifle_01", "WW1 rifle"},
    {"weapon_ww1_gun_mauser_01", "Mauser pistol"},
}};

const DeveloperWeapon* weaponFor(std::string_view id) {
    const auto found = std::find_if(weapons.begin(), weapons.end(), [id](const auto& weapon) { return weapon.id == id; });
    return found == weapons.end() ? nullptr : &*found;
}
}

std::span<const DeveloperWeapon> developerWeapons() noexcept { return weapons; }

bool developerToolsNeedsUpdate() noexcept {
    return needsUpdate.load(std::memory_order_acquire);
}

void setDeveloperToolsVisible(bool visible) {
    std::lock_guard lock(stateMutex);
    panelVisible = visible;
    refreshWorkLocked();
}

DeveloperSnapshot developerSnapshot() {
    std::lock_guard lock(stateMutex);
    return state;
}

bool requestDeveloperMission(std::string_view id) {
    const auto missions = developerMissions();
    if (std::none_of(missions.begin(), missions.end(), [id](const auto& mission) { return mission.id == id; })) return false;
    std::lock_guard lock(stateMutex);
    // One pending transition. Replacing it avoids loading a succession of
    // worlds if the user changes their selection before the next engine tick.
    pendingMission = id;
    requested("Mission load queued.");
    return true;
}

bool requestDeveloperSpeed(float multiplier) {
    if (!std::isfinite(multiplier) || multiplier < 0.25f || multiplier > 4.0f) return false;
    std::lock_guard lock(stateMutex);
    state.playerSpeed = multiplier;
    playerToolsEnabled = true;
    requested("Player speed queued.");
    return true;
}

void requestDeveloperInvincibility(bool enabled) {
    std::lock_guard lock(stateMutex);
    state.invincible = enabled;
    playerToolsEnabled = true;
    requested("Invincibility queued.");
}

void requestDeveloperNoclip(bool enabled) {
    std::lock_guard lock(stateMutex);
    state.noclip = enabled;
    playerToolsEnabled = true;
    requested("Noclip queued.");
}

void requestDeveloperInfiniteAmmo(bool enabled) {
    std::lock_guard lock(stateMutex);
    state.infiniteAmmo = enabled;
    playerToolsEnabled = true;
    requested("Infinite ammo queued.");
}

bool requestDeveloperWeapon(std::string_view id) {
    if (!weaponFor(id)) return false;
    std::lock_guard lock(stateMutex);
    pendingWeapon = id;
    requested("Weapon grant queued.");
    return true;
}

void requestDeveloperUnlockDarkness() {
    std::lock_guard lock(stateMutex);
    pendingDarknessUnlock = true;
    requested("Darkness ability unlock queued.");
}

void requestDeveloperMaxDarkness() {
    std::lock_guard lock(stateMutex);
    pendingDarknessLevel = true;
    requested("Maximum Darkness level queued.");
}

void resetDeveloperTools() {
    std::lock_guard lock(stateMutex);
    state = {};
    pendingMission.reset();
    pendingWeapon.reset();
    playerToolsEnabled = false;
    pendingDarknessUnlock = pendingDarknessLevel = false;
    ++requestRevision;
    resetDeveloperPlayer();
    refreshWorkLocked();
}

bool processDeveloperTools(PPCContext& ctx, uint8_t* base, uint32_t client, bool missionReady) {
    std::optional<std::string> mission, weapon;
    float speed;
    bool invincible, noclip, infiniteAmmo, enabled, unlockDarkness, maxDarkness;
    uint64_t request;
    {
        std::lock_guard lock(stateMutex);
        mission = std::move(pendingMission);
        pendingMission.reset();
        weapon = std::move(pendingWeapon);
        pendingWeapon.reset();
        speed = state.playerSpeed;
        invincible = state.invincible;
        noclip = state.noclip;
        infiniteAmmo = state.infiniteAmmo;
        enabled = playerToolsEnabled;
        unlockDarkness = std::exchange(pendingDarknessUnlock, false);
        maxDarkness = std::exchange(pendingDarknessLevel, false);
        request = requestRevision;
    }
    const bool canLoad = missionReady && canLoadDeveloperMission(base, client);
    // Original console calls get an independent register context and the same
    // floating-point mode as an ordinary guest function invocation.
    auto call = ctx;
    const unsigned savedMath = _mm_getcsr();
    struct RestoreMath {
        unsigned saved;
        ~RestoreMath() { _mm_setcsr(saved); }
    } restoreMath{savedMath};
    _mm_setcsr(savedMath & ~(_MM_ROUND_MASK | _MM_FLUSH_ZERO_MASK | _MM_DENORMALS_ZERO_MASK));
    if (mission) {
        updateDeveloperPlayer(call, base, 0, 1, false, false);
        std::string status;
        const bool loaded = canLoad && loadDeveloperMission(call, base, client, *mission, status);
        if (!canLoad) status = "Mission loading is unavailable. Try again when the main menu or game is ready.";
        publish(request, loaded ? false : canLoad, false, status);
        // Even a failed engine command may have changed transition state.
        return canLoad;
    }
    const auto player = updateDeveloperPlayer(call, base, client, speed, invincible, enabled, noclip, infiniteAmmo);
    if (weapon) {
        const auto handles = resolveDeveloperPlayer(call, base, client);
        const auto* entry = weaponFor(*weapon);
        const auto granted = grantDeveloperWeapon(call, base, handles, entry->id, entry->title);
        publish(request, canLoad, player.hasActivePlayer, granted.status, false, true);
    } else if (unlockDarkness || maxDarkness) {
        const auto handles = resolveDeveloperPlayer(call, base, client);
        const auto darkness = applyDeveloperDarkness(call, base, handles, unlockDarkness, maxDarkness, client);
        publish(request, canLoad, player.hasActivePlayer, darkness.status, false, true);
    } else publish(request, canLoad, player.hasActivePlayer, player.status, player.applied, true);
    return false;
}
}
