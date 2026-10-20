#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <string_view>

struct PPCContext;

namespace DarkRecomp::Native {
struct DeveloperMission {
    std::string_view id;
    std::string_view title;
};
std::span<const DeveloperMission> developerMissions() noexcept;
// Player campaign guns, by original RPG template name.
struct DeveloperWeapon {
    std::string_view id;
    std::string_view title;
};
std::span<const DeveloperWeapon> developerWeapons() noexcept;
void initializeDeveloperTools() noexcept;
// The update boundary may avoid guest inspection while the panel is closed
// and no requests or player overrides need work. This reads one atomic flag.
bool developerToolsNeedsUpdate() noexcept;
void setDeveloperToolsVisible(bool visible);

// Host UI exchanges values only. Guest objects are accessed on the engine
// thread after the original application update has completed.
struct DeveloperSnapshot {
    bool canLoadMission = false;
    bool hasActivePlayer = false;
    float playerSpeed = 1;
    bool invincible = false;
    bool noclip = false;
    bool infiniteAmmo = false;
    bool bunnyHop = false;
    bool speedometer = false;
    bool darklingGod = false;
    bool espBox = false;
    bool espSkeleton = false;
    bool espTracers = false;
    bool aimbot = false;
    std::string level;  // Last level archive opened, e.g. NY1_Chinatown.
    // Spawn templates found to need models the current level lacks.
    std::vector<std::string> unavailableSpawns;
    std::string status = "Waiting for the game.";
    uint64_t revision = 0;
};
DeveloperSnapshot developerSnapshot();
// Called for each opened file; records <level>_Common.XDF as the current level.
void noteDeveloperArchive(std::string_view fileName);
// While a spawn is being created, the file layer reports model files the
// level cannot supply (game-relative, lowercase). The spawn then marks its
// template unavailable for this level.
void noteDeveloperMissingFile(std::string_view relativePath);
void beginDeveloperSpawnWatch();
std::vector<std::string> endDeveloperSpawnWatch();
void markDeveloperSpawnUnavailable(std::string_view templateName);
bool requestDeveloperMission(std::string_view id);
bool requestDeveloperSpeed(float multiplier);
void requestDeveloperInvincibility(bool enabled);
void requestDeveloperNoclip(bool enabled);
void requestDeveloperInfiniteAmmo(bool enabled);
void requestDeveloperBunnyHop(bool enabled);
// Input thread: whether held jump should re-press. Each pad poll reports the
// held hop and strafe direction (-1 left, 0, +1 right); mouse motion reports
// horizontal turns. Holding jump builds speed (+30%/s); with air strafing it
// builds only while strafing toward the mouse turn (+80%/s) and decays otherwise.
bool developerBunnyHopEnabled() noexcept;
void updateDeveloperBunnyHop(bool hopping, int strafe) noexcept;
void noteDeveloperMouseTurn(long long dx) noexcept;
float developerBunnyHopBoost() noexcept;
void setDeveloperAirStrafe(bool enabled) noexcept;
bool developerAirStrafe() noexcept;
// Aimbot hold key (Win32 virtual key); 0 keeps the default V or Mouse 4.
void setDeveloperAimKey(unsigned virtualKey) noexcept;
unsigned developerAimKey() noexcept;
void setDeveloperBunnyHopMax(float multiplier) noexcept; // 1.5..10
float developerBunnyHopMax() noexcept;
void requestDeveloperSpeedometer(bool enabled);
// Player's horizontal speed in meters per second; negative while unavailable.
float developerPlayerSpeed() noexcept;
void requestDeveloperDarklingGod(bool enabled);
void requestDeveloperEspBox(bool enabled);
void requestDeveloperEspSkeleton(bool enabled);
void requestDeveloperEspTracers(bool enabled);
void requestDeveloperAimbot(bool enabled);
bool requestDeveloperWeapon(std::string_view id);
// Template names: letters, digits and underscores, up to 63 characters.
bool requestDeveloperSpawn(std::string_view templateName);
// Darkling templates every campaign level loads for the summon power.
std::span<const std::string_view> developerSpawnSuggestions() noexcept;
void requestDeveloperProbe();
void requestDeveloperRemoveSpawns();
void requestDeveloperSavePosition();
void requestDeveloperLoadPosition();
// Seconds since the current bunny hop run started; 0 while not hopping.
float developerBunnyHopSeconds() noexcept;
// Aimbot target choice (0 = nearest the crosshair, 1 = nearest by distance)
// and the aim speed as a fraction (0.05..1) of the fastest stable correction.
void setDeveloperAimMode(int mode) noexcept;
int developerAimMode() noexcept;
void setDeveloperAimSmoothing(float gain) noexcept;
float developerAimSmoothing() noexcept;
void requestDeveloperUnlockDarkness();
void requestDeveloperMaxDarkness();
void resetDeveloperTools();

// Returns true after a mission command: the caller must not reuse its old
// client/player pointers during that update.
bool processDeveloperTools(PPCContext& ctx, uint8_t* base, uint32_t client, bool missionReady = true);
}
