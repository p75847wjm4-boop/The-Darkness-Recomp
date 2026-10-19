#pragma once
#include <cstdint>
#include <vector>

struct PPCContext;

namespace DarkRecomp::Native {
struct DeveloperPlayerHandles;

struct EspTarget {
    float x = 0, y = 0, z = 0; // Feet, guest world units (Z up), as rendered.
    int health = 0;
    uint32_t id = 0;
};
// The player's view, approximated from the client (rendered) player matrix
// (rows: forward with pitch, left, up, position) plus the last camera FOV.
struct EspSnapshot {
    bool valid = false, esp = false;
    float eye[3]{}, forward[3]{}, left[3]{}, up[3]{};
    float tanHalfVertical = 0.525f;
    std::vector<EspTarget> targets;
};

// Engine thread, after the original update. Collects living CWObject_CharNPC
// objects that also exist on the client (spawned and replicated), with their
// client positions; clears the snapshot when disabled.
void collectDeveloperEsp(PPCContext& ctx, uint8_t* base, uint32_t client,
                         const DeveloperPlayerHandles& player, bool esp, bool aimbot);
EspSnapshot developerEspSnapshot();
void clearDeveloperEsp() noexcept;
// Guest camera FOV (degrees, 4:3 horizontal reference) after 8249A030.
void rememberGameplayFov(float degrees) noexcept;
// Mouse-look hook: while the aim key is held, the turn toward the locked (or
// nearest to the crosshair) living NPC, from the client camera at this call,
// in original look units (1/65536 turn; +yaw turns right, +pitch looks up).
bool developerAimCorrection(PPCContext& ctx, uint8_t* base, uint32_t client,
                            int64_t& yawUnits, int64_t& pitchUnits);
}
