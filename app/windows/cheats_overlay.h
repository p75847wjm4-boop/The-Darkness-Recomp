#pragma once
#include <windows.h>
#include <array>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

class NativeMouseWindow;

// F7 cheat overlay, centered on the game window. The display thread owns it;
// every option crosses the value-only developer-tools bridge. It includes all
// F5 developer-panel actions.
class CheatsOverlay {
public:
    CheatsOverlay(HWND owner, NativeMouseWindow& mouse,
                  std::function<bool()> resolutionShortcut = {},
                  std::function<void(bool)> setResolutionShortcut = {});
    ~CheatsOverlay();
    CheatsOverlay(const CheatsOverlay&) = delete;
    CheatsOverlay& operator=(const CheatsOverlay&) = delete;

    bool isOpen() const { return window_ != nullptr; }
    bool handleMessage(MSG& message);
    void toggle();
    void close();
    // Load cheat settings now and save them each time the overlay closes.
    void useSettingsFile(const std::filesystem::path& path);

private:
    static LRESULT CALLBACK windowProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT message(UINT, WPARAM, LPARAM);
    bool open();
    void createControls();
    void refresh();
    void fillSpawnList(const std::vector<std::string>& unavailable);
    void command(int id, int notification);
    void selectSpeed();
    void loadMission();
    void giveWeapon();
    void spawn();
    void restoreDefaults();
    void releaseInputGate();
    bool dropDownOpen() const;
    void slider(HWND control);
    void cycleColor(unsigned& index, HWND button, const wchar_t* label);
    void refreshStyle();
    void beginKeyCapture();
    void pollKeyCapture();
    void saveSettings() const;

    HWND owner_{};
    NativeMouseWindow& mouse_;
    std::function<bool()> resolutionShortcut_;
    std::function<void(bool)> setResolutionShortcut_;
    HWND window_{}, speed_{}, invincible_{}, noclip_{}, infiniteAmmo_{}, darklingGod_{}, bunnyHop_{}, speedometer_{}, hopMax_{}, hopMaxValue_{}, airStrafe_{}, speedGraph_{}, aimKey_{}, unlock_{}, maxDarkness_{},
         aimbot_{}, weapons_{}, give_{}, box_{}, skeleton_{}, mission_{}, load_{}, shortcut_{},
         defaults_{}, status_{}, tracers_{}, boxColor_{}, boneColor_{}, tracerColor_{}, opacity_{},
         opacityValue_{}, fov_{}, fovValue_{}, fovCircle_{}, cameraFov_{}, cameraFovValue_{}, health_{}, distance_{}, names_{}, maxDistance_{}, maxDistanceValue_{}, spawn_{}, spawnButton_{};
    HBRUSH background_{}, field_{};
    HFONT titleFont_{};
    bool inputGate_ = false;
    bool capturingKey_ = false;
    std::array<bool, 256> keysAtCapture_{};
    std::filesystem::path settingsPath_;
    std::wstring displayedStatus_;
    std::vector<std::string> shownUnavailable_;
};
