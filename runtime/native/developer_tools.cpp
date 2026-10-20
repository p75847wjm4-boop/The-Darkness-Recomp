#include "developer_tools.h"
#include <windows.h>
#include "developer_missions.h"
#include "developer_player.h"
#include "developer_invincibility.h"
#include "developer_darkness.h"
#include "developer_player_lookup.h"
#include "developer_probe.h"
#include "developer_esp.h"
#include "developer_skeleton.h"
#include "ppc_recomp_shared.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <map>
#include <mutex>
#include <optional>
#include <utility>

namespace DarkRecomp::Native {
namespace {
std::mutex stateMutex;
DeveloperSnapshot state;
std::optional<std::string> pendingMission;
std::optional<std::string> pendingWeapon, pendingSpawn;
bool pendingProbe = false, pendingRemoveSpawns = false, pendingSavePosition = false, pendingLoadPosition = false;
bool playerToolsEnabled = false;
bool pendingDarknessUnlock = false;
bool pendingDarknessLevel = false;
uint64_t requestRevision = 0;
bool panelVisible = false;
std::atomic<bool> needsUpdate{false};
std::atomic<bool> bunnyHopOn{false};
std::atomic<float> hopBoost{1};
std::atomic<long long> mouseTurn{0};
std::atomic<bool> airStrafe{false};
std::atomic<unsigned> aimKey{0};
std::atomic<int> aimMode{0};
std::atomic<float> aimSmoothing{0.36f};
std::atomic<uint64_t> hopStarted{0};
std::atomic<float> bunnyHopMax{2.5f};
std::atomic<float> playerSpeed{-1};

// Horizontal speed of the server player from successive positions (row 3 of
// the world matrix, +64), smoothed; 32 world units per meter.
void measurePlayerSpeed(uint8_t* base, uint32_t actor) {
    static LARGE_INTEGER frequency{};
    static LARGE_INTEGER last{};
    static float lastX = 0, lastY = 0, smoothed = 0;
    static uint32_t lastActor = 0;
    if (!frequency.QuadPart) QueryPerformanceFrequency(&frequency);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if (!actor) { playerSpeed.store(-1, std::memory_order_release); lastActor = 0; return; }
    const float x = std::bit_cast<float>(PPC_LOAD_U32(actor + 64));
    const float y = std::bit_cast<float>(PPC_LOAD_U32(actor + 68));
    const double seconds = double(now.QuadPart - last.QuadPart) / double(frequency.QuadPart);
    if (actor != lastActor || seconds > 0.5) {
        lastActor = actor; last = now; lastX = x; lastY = y; smoothed = 0;
    } else if (seconds >= 0.05) {
        const float dx = x - lastX, dy = y - lastY;
        const float metersPerSecond = float(std::sqrt(dx * dx + dy * dy) / seconds / 32);
        smoothed = smoothed * 0.6f + metersPerSecond * 0.4f;
        last = now; lastX = x; lastY = y;
    }
    playerSpeed.store(smoothed, std::memory_order_release);
}

void refreshWorkLocked() {
    needsUpdate.store(panelVisible || pendingMission.has_value() || pendingWeapon.has_value() || pendingSpawn.has_value() || pendingProbe || pendingRemoveSpawns || pendingSavePosition || pendingLoadPosition || playerToolsEnabled ||
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
         state.status == "Noclip queued." || state.status == "Infinite ammo queued." ||
         state.status == "Bunny hop queued." ||
         state.status == "Darkling god mode queued." ||
         state.status == "ESP queued." || state.status == "Aimbot queued."))
        status = "Player settings applied.";
    const bool statusChanged = requestRevision == request && !status.empty() && state.status != status;
    if (statusChanged) state.status = status;
    if (availabilityChanged || statusChanged) ++state.revision;
    // Returning to defaults still needs one update to clear previously applied
    // hooks. A request arriving during that update must keep its next update.
    if (playerProcessed && requestRevision == request && state.playerSpeed == 1 &&
        !state.invincible && !state.noclip && !state.infiniteAmmo &&
        !state.darklingGod && !state.bunnyHop && !state.speedometer &&
        !state.espBox && !state.espSkeleton && !state.espTracers &&
        !state.aimbot) playerToolsEnabled = false;
    refreshWorkLocked();
}

std::mutex spawnWatchMutex;
std::atomic<bool> spawnWatching{false};
std::vector<std::string> spawnMissing;
std::map<std::string, std::vector<std::string>> unavailableByLevel;
} // namespace

void noteDeveloperMissingFile(std::string_view relativePath) {
    if (!spawnWatching.load(std::memory_order_acquire)) return;
    constexpr std::string_view models = "content\\models\\";
    if (!relativePath.starts_with(models) || relativePath == "content\\models\\coordsys.xmd" ||
        relativePath == "content\\models\\.xmd") return;
    std::lock_guard lock(spawnWatchMutex);
    if (std::find(spawnMissing.begin(), spawnMissing.end(), relativePath) == spawnMissing.end())
        spawnMissing.emplace_back(relativePath);
}
void beginDeveloperSpawnWatch() {
    std::lock_guard lock(spawnWatchMutex);
    spawnMissing.clear();
    spawnWatching.store(true, std::memory_order_release);
}
std::vector<std::string> endDeveloperSpawnWatch() {
    std::lock_guard lock(spawnWatchMutex);
    spawnWatching.store(false, std::memory_order_release);
    return std::move(spawnMissing);
}
void markDeveloperSpawnUnavailable(std::string_view templateName) {
    std::lock_guard lock(stateMutex);
    auto& names = unavailableByLevel[state.level];
    if (std::find(names.begin(), names.end(), templateName) != names.end()) return;
    names.emplace_back(templateName);
    state.unavailableSpawns = names;
    ++state.revision;
}

void noteDeveloperArchive(std::string_view fileName) {
    constexpr std::string_view suffix = "_common.xdf";
    if (fileName.size() <= suffix.size()) return;
    const auto tail = fileName.substr(fileName.size() - suffix.size());
    if (!std::equal(tail.begin(), tail.end(), suffix.begin(),
                    [](char a, char b) { return (a >= 'A' && a <= 'Z' ? char(a + 32) : a) == b; })) return;
    const std::string level(fileName.substr(0, fileName.size() - suffix.size()));
    std::lock_guard lock(stateMutex);
    if (state.level == level) return;
    state.level = level;
    state.unavailableSpawns = unavailableByLevel[level];
    ++state.revision;
}

namespace {
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

namespace {
// Generic characters from the global GameContext_Create.XDF first, then the
// darklings the summon power uses (names from the level archives).
constexpr std::array<std::string_view, 30> spawnSuggestions{
    "ai_badguy_standard", "ai_badguy_leader", "ai_toughguy_standard", "ai_meatface_standard",
    "ai_police_swat", "ai_police_swat_weak", "ai_civ_standard",
    "ai_soldier_german", "ai_ow_soldier_leader", "ai_ow_soldier_allied", "ai_ow_soldier_standard_nightvision",
    "ai_darkling_berserker1", "ai_darkling_berserker2", "ai_darkling_berserker3", "ai_darkling_berserker4",
    "ai_darkling_gunner", "ai_darkling_kamikaze", "ai_darkling_lightkiller",
    "ai_darkling_berserker_axe", "ai_darkling_berserker_bat", "ai_darkling_berserker_brokensword",
    "ai_darkling_berserker_demolitionhammer", "ai_darkling_berserker_golfdriver",
    "ai_darkling_berserker_hammer", "ai_darkling_berserker_informer", "ai_darkling_berserker_katana",
    "ai_darkling_berserker_machete", "ai_darkling_berserker_pistol", "ai_darkling_berserker_saw",
    "ai_darklingguide"};
}

std::span<const std::string_view> developerSpawnSuggestions() noexcept { return spawnSuggestions; }

bool requestDeveloperSpawn(std::string_view templateName) {
    if (templateName.empty() || templateName.size() > 63 ||
        std::any_of(templateName.begin(), templateName.end(), [](char c) {
            return !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_');
        })) return false;
    std::lock_guard lock(stateMutex);
    pendingSpawn = std::string(templateName);
    requested("Spawn queued.");
    return true;
}

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

void requestDeveloperBunnyHop(bool enabled) {
    std::lock_guard lock(stateMutex);
    state.bunnyHop = enabled;
    bunnyHopOn.store(enabled, std::memory_order_release);
    playerToolsEnabled = true;
    requested("Bunny hop queued.");
}
bool developerBunnyHopEnabled() noexcept { return bunnyHopOn.load(std::memory_order_acquire); }
void noteDeveloperMouseTurn(long long dx) noexcept {
    if (bunnyHopOn.load(std::memory_order_relaxed)) mouseTurn.fetch_add(dx, std::memory_order_relaxed);
}
void updateDeveloperBunnyHop(bool hopping, int strafe) noexcept {
    static LARGE_INTEGER frequency{}, last{};
    if (!frequency.QuadPart) QueryPerformanceFrequency(&frequency);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    const float seconds = last.QuadPart ? (std::min)(0.1f, float(now.QuadPart - last.QuadPart) / float(frequency.QuadPart)) : 0;
    last = now;
    const auto turn = mouseTurn.exchange(0, std::memory_order_relaxed);
    float boost = hopBoost.load(std::memory_order_relaxed);
    if (!hopping) {
        boost = 1;
        hopStarted.store(0, std::memory_order_relaxed);
    } else if (!hopStarted.load(std::memory_order_relaxed)) {
        hopStarted.store(GetTickCount64(), std::memory_order_relaxed);
    }
    if (!hopping) {
    } else if (!airStrafe.load(std::memory_order_relaxed)) boost += 0.3f * seconds;
    else if (strafe && turn && (turn > 0) == (strafe > 0)) boost += 0.8f * seconds;
    else boost -= 0.15f * seconds;
    hopBoost.store(std::clamp(boost, 1.f, bunnyHopMax.load(std::memory_order_relaxed)), std::memory_order_relaxed);
}
float developerBunnyHopBoost() noexcept {
    return bunnyHopOn.load(std::memory_order_acquire) ? hopBoost.load(std::memory_order_relaxed) : 1;
}
void setDeveloperAirStrafe(bool enabled) noexcept { airStrafe.store(enabled, std::memory_order_relaxed); }
bool developerAirStrafe() noexcept { return airStrafe.load(std::memory_order_relaxed); }
void setDeveloperAimKey(unsigned virtualKey) noexcept {
    aimKey.store(virtualKey < 256 ? virtualKey : 0, std::memory_order_relaxed);
}
unsigned developerAimKey() noexcept { return aimKey.load(std::memory_order_relaxed); }
float developerBunnyHopSeconds() noexcept {
    const auto started = hopStarted.load(std::memory_order_relaxed);
    return started && bunnyHopOn.load(std::memory_order_relaxed) ? float(GetTickCount64() - started) / 1000 : 0;
}
void setDeveloperAimMode(int mode) noexcept { aimMode.store(mode == 1 ? 1 : 0, std::memory_order_relaxed); }
int developerAimMode() noexcept { return aimMode.load(std::memory_order_relaxed); }
void setDeveloperAimSmoothing(float gain) noexcept {
    if (std::isfinite(gain)) aimSmoothing.store(std::clamp(gain, 0.05f, 1.f), std::memory_order_relaxed);
}
float developerAimSmoothing() noexcept { return aimSmoothing.load(std::memory_order_relaxed); }
void setDeveloperBunnyHopMax(float multiplier) noexcept {
    if (std::isfinite(multiplier)) bunnyHopMax.store(std::clamp(multiplier, 1.5f, 10.f), std::memory_order_relaxed);
}
float developerBunnyHopMax() noexcept { return bunnyHopMax.load(std::memory_order_relaxed); }
void requestDeveloperSpeedometer(bool enabled) {
    std::lock_guard lock(stateMutex);
    state.speedometer = enabled;
    playerToolsEnabled = true;
    if (!enabled) playerSpeed.store(-1, std::memory_order_release);
    requested(enabled ? "Speedometer on." : "Speedometer off.");
}
float developerPlayerSpeed() noexcept { return playerSpeed.load(std::memory_order_acquire); }

void requestDeveloperDarklingGod(bool enabled) {
    std::lock_guard lock(stateMutex);
    state.darklingGod = enabled;
    playerToolsEnabled = true;
    requested("Darkling god mode queued.");
}

void requestDeveloperEspBox(bool enabled) {
    std::lock_guard lock(stateMutex);
    state.espBox = enabled;
    playerToolsEnabled = true;
    requested("ESP queued.");
}

void requestDeveloperEspTracers(bool enabled) {
    std::lock_guard lock(stateMutex);
    state.espTracers = enabled;
    playerToolsEnabled = true;
    requested("ESP queued.");
}

void requestDeveloperEspSkeleton(bool enabled) {
    std::lock_guard lock(stateMutex);
    state.espSkeleton = enabled;
    playerToolsEnabled = true;
    requested("ESP queued.");
}

void requestDeveloperAimbot(bool enabled) {
    std::lock_guard lock(stateMutex);
    state.aimbot = enabled;
    playerToolsEnabled = true;
    requested("Aimbot queued.");
}

bool requestDeveloperWeapon(std::string_view id) {
    if (!weaponFor(id)) return false;
    std::lock_guard lock(stateMutex);
    pendingWeapon = id;
    requested("Weapon grant queued.");
    return true;
}

void requestDeveloperProbe() {
    std::lock_guard lock(stateMutex);
    pendingProbe = true;
    requested("Probe dump queued.");
}

void requestDeveloperRemoveSpawns() {
    std::lock_guard lock(stateMutex);
    pendingRemoveSpawns = true;
    requested("Removing spawned characters.");
}

void requestDeveloperSavePosition() {
    std::lock_guard lock(stateMutex);
    pendingSavePosition = true;
    requested("Saving position.");
}

void requestDeveloperLoadPosition() {
    std::lock_guard lock(stateMutex);
    pendingLoadPosition = true;
    requested("Teleporting.");
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
    pendingSpawn.reset();
    playerToolsEnabled = false;
    pendingDarknessUnlock = pendingDarknessLevel = pendingProbe = false;
    pendingRemoveSpawns = pendingSavePosition = pendingLoadPosition = false;
    ++requestRevision;
    resetDeveloperPlayer();
    clearDeveloperEsp();
    refreshWorkLocked();
}

bool processDeveloperTools(PPCContext& ctx, uint8_t* base, uint32_t client, bool missionReady) {
    std::optional<std::string> mission, weapon, spawn;
    float speed;
    bool invincible, noclip, infiniteAmmo, darklingGod, bunnyHop, speedometer, espBox, espSkeleton, espTracers, aimbot, enabled, unlockDarkness, maxDarkness, probe, overlay, removeSpawns, savePosition, loadPosition;
    uint64_t request;
    {
        std::lock_guard lock(stateMutex);
        mission = std::move(pendingMission);
        pendingMission.reset();
        weapon = std::move(pendingWeapon);
        pendingWeapon.reset();
        spawn = std::move(pendingSpawn);
        pendingSpawn.reset();
        speed = state.playerSpeed;
        invincible = state.invincible;
        noclip = state.noclip;
        infiniteAmmo = state.infiniteAmmo;
        bunnyHop = state.bunnyHop;
        speedometer = state.speedometer;
        darklingGod = state.darklingGod;
        espBox = state.espBox;
        espSkeleton = state.espSkeleton;
        espTracers = state.espTracers;
        aimbot = state.aimbot;
        enabled = playerToolsEnabled;
        unlockDarkness = std::exchange(pendingDarknessUnlock, false);
        maxDarkness = std::exchange(pendingDarknessLevel, false);
        probe = std::exchange(pendingProbe, false);
        removeSpawns = std::exchange(pendingRemoveSpawns, false);
        savePosition = std::exchange(pendingSavePosition, false);
        loadPosition = std::exchange(pendingLoadPosition, false);
        overlay = panelVisible;
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
        clearDeveloperEsp();
        std::string status;
        const bool loaded = canLoad && loadDeveloperMission(call, base, client, *mission, status);
        if (!canLoad) status = "Mission loading is unavailable. Try again when the main menu or game is ready.";
        publish(request, loaded ? false : canLoad, false, status);
        // Even a failed engine command may have changed transition state.
        return canLoad;
    }
    const auto player = updateDeveloperPlayer(call, base, client, speed, invincible, enabled, noclip, infiniteAmmo,
                                              darklingGod, bunnyHop);
    if (speedometer && enabled) measurePlayerSpeed(base, resolveDeveloperPlayer(call, base, client).actor);
    else playerSpeed.store(-1, std::memory_order_release);
    const bool esp = espBox || espSkeleton || espTracers;
    setSkeletonCapture((enabled && esp) || overlay);
    if (enabled && (esp || aimbot))
        collectDeveloperEsp(call, base, client, resolveDeveloperPlayer(call, base, client), espBox, espSkeleton,
                            espTracers, aimbot);
    else clearDeveloperEsp();
    if (removeSpawns || savePosition || loadPosition) {
        const auto handles = resolveDeveloperPlayer(call, base, client);
        const auto done = removeSpawns ? removeDeveloperSpawns(call, base, handles) :
            savePosition ? saveDeveloperPosition(base, handles) : loadDeveloperPosition(base, handles);
        publish(request, canLoad, player.hasActivePlayer, done.status, false, true);
    } else if (probe) {
        dumpDeveloperProbe(call, base, client);
        publish(request, canLoad, player.hasActivePlayer, "Probe written to the runtime log.", false, true);
    } else if (spawn) {
        const auto spawned = spawnDeveloperObject(call, base, resolveDeveloperPlayer(call, base, client), *spawn, client);
        publish(request, canLoad, player.hasActivePlayer, spawned.status, false, true);
    } else if (weapon) {
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
