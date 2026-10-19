#pragma once
#include <cstdint>
#include <vector>

struct PPCContext;

namespace DarkRecomp::Native {
struct DeveloperPlayerHandles;

struct EspTarget {
    float x = 0, y = 0, z = 0; // Feet, guest world units (Z up).
    int health = 0;
};
// The player's view, approximated from the server actor matrix (rows: forward
// with pitch, left, up, position) plus the last gameplay camera FOV.
struct EspSnapshot {
    bool valid = false, esp = false;
    float eye[3]{}, forward[3]{}, left[3]{}, up[3]{};
    float tanHalfVertical = 0.525f;
    std::vector<EspTarget> targets;
};

// Engine thread, after the original update. Collects living CWObject_CharNPC
// objects from the server object table; clears the snapshot when disabled.
void collectDeveloperEsp(uint8_t* base, const DeveloperPlayerHandles& player, bool esp, bool aimbot);
EspSnapshot developerEspSnapshot();
// Guest camera FOV (degrees, 4:3 horizontal reference) after 8249A030.
void rememberGameplayFov(float degrees) noexcept;
// Mouse-look thread: while the aim key is held, the turn toward the living NPC
// nearest the crosshair, in original look units (1/65536 turn; +yaw turns
// right, +pitch looks up). False when there is nothing to apply.
bool developerAimCorrection(int64_t& yawUnits, int64_t& pitchUnits);
}
