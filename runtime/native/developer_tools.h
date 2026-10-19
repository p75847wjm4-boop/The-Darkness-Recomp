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
    bool rapidFire = false;
    bool noRecoil = false;
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
void requestDeveloperRapidFire(bool enabled);
void requestDeveloperNoRecoil(bool enabled);
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
void requestDeveloperUnlockDarkness();
void requestDeveloperMaxDarkness();
void resetDeveloperTools();

// Returns true after a mission command: the caller must not reuse its old
// client/player pointers during that update.
bool processDeveloperTools(PPCContext& ctx, uint8_t* base, uint32_t client, bool missionReady = true);
}
