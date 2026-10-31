#include "cheats_overlay.h"
#include "esp_style.h"
#include "native_mouse.h"
#include "runtime/native/developer_esp.h"
#include "runtime/native/fov_settings.h"
#include <commctrl.h>
#include "runtime/native/developer_tools.h"
#include "runtime/native/input.h"
#include "runtime/native/keyboard_bindings.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <exception>
#include <map>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>

using namespace DarkRecomp::Native;

namespace {
constexpr wchar_t kWindowClass[] = L"DarkRecompCheatsOverlay";
constexpr UINT_PTR kRefreshTimer = 1, kKeyTimer = 2;
enum : int {
    kSpeed = 2001, kInvincible, kNoclip, kInfiniteAmmo, kUnlock, kMaxDarkness, kAimbot,
    kWeapons, kGive, kBox, kSkeleton, kMission, kLoad, kShortcut, kDefaults, kProbe,
    kTracers, kBoxColor, kBoneColor, kTracerColor, kOpacitySlider, kFov, kFovCircle, kCameraFov, kHealth, kSpawnName, kSpawn, kDistance, kMaxDistance, kNames, kDarklingGod, kBunnyHop, kSpeedometer, kHopMax, kAirStrafe, kSpeedGraph, kAimKey, kRemoveSpawns, kSavePosition, kLoadPosition, kAimMode, kAimSmooth,
    kCrosshair, kCrosshairColor, kCrosshairSize, kTheme, kMenuOpacity, kEnemiesOnly
};
constexpr int kWidth = 960, kHeight = 704;
constexpr COLORREF kDim = RGB(140, 140, 140);
constexpr std::array<float, 9> kSpeeds{.25f, .5f, .75f, 1.f, 1.25f, 1.5f, 2.f, 3.f, 4.f};
constexpr std::array<const wchar_t*, 9> kSpeedNames{
    L"0.25X", L"0.5X", L"0.75X", L"1X (NORMAL)", L"1.25X", L"1.5X", L"2X", L"3X", L"4X"};

const EspStyle::Theme& theme() { return EspStyle::kThemes[espStyle().menuTheme % EspStyle::kThemes.size()]; }

std::wstring upper(std::string_view text) {
    std::wstring result;
    if (!text.empty()) {
        const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
        result.resize(size_t((std::max)(size, 0)));
        if (size) MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), result.data(), size);
    }
    if (!result.empty()) CharUpperBuffW(result.data(), DWORD(result.size()));
    return result;
}

HWND control(HWND parent, const wchar_t* type, const wchar_t* text, DWORD style,
             int x, int y, int width, int height, int id = 0, HFONT font = nullptr) {
    const HWND result = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style,
        x, y, width, height, parent, reinterpret_cast<HMENU>(INT_PTR(id)), GetModuleHandleW(nullptr), nullptr);
    if (!result) throw std::runtime_error("Cannot create cheats overlay control");
    SendMessageW(result, WM_SETFONT, WPARAM(font ? font : HFONT(GetStockObject(DEFAULT_GUI_FONT))), TRUE);
    return result;
}
HWND group(HWND parent, const wchar_t* title, int x, int y, int width, int height) {
    return control(parent, L"BUTTON", title, BS_GROUPBOX, x, y, width, height);
}
HWND check(HWND parent, const wchar_t* text, int x, int y, int width, int id) {
    return control(parent, L"BUTTON", text, BS_AUTOCHECKBOX | WS_TABSTOP, x, y, width, 22, id);
}
HWND button(HWND parent, const wchar_t* text, int x, int y, int width, int id) {
    return control(parent, L"BUTTON", text, BS_PUSHBUTTON | WS_TABSTOP, x, y, width, 26, id);
}
void setCheck(HWND control, bool checked) {
    SendMessageW(control, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
}
bool checked(HWND control) { return SendMessageW(control, BM_GETCHECK, 0, 0) == BST_CHECKED; }
}

CheatsOverlay::CheatsOverlay(HWND owner, NativeMouseWindow& mouse, std::function<bool()> resolutionShortcut,
                             std::function<void(bool)> setResolutionShortcut)
    : owner_(owner), mouse_(mouse), resolutionShortcut_(std::move(resolutionShortcut)),
      setResolutionShortcut_(std::move(setResolutionShortcut)) {}

CheatsOverlay::~CheatsOverlay() {
    close();
    releaseInputGate();
    if (titleFont_) DeleteObject(titleFont_);
    if (background_) DeleteObject(background_);
    if (field_) DeleteObject(field_);
}

bool CheatsOverlay::dropDownOpen() const {
    return (speed_ && SendMessageW(speed_, CB_GETDROPPEDSTATE, 0, 0)) ||
           (mission_ && SendMessageW(mission_, CB_GETDROPPEDSTATE, 0, 0)) ||
           (spawn_ && SendMessageW(spawn_, CB_GETDROPPEDSTATE, 0, 0));
}

bool CheatsOverlay::handleMessage(MSG& message) {
    const bool inside = window_ && (message.hwnd == window_ || IsChild(window_, message.hwnd));
    if ((message.hwnd == owner_ || inside) && (message.message == WM_KEYDOWN || message.message == WM_KEYUP) &&
        message.wParam == VK_F7) {
        if (message.message == WM_KEYDOWN && !(message.lParam & (LPARAM(1) << 30))) toggle();
        return true;
    }
    if (inside && message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE) {
        // Let an open drop-down cancel its pending choice first.
        if (dropDownOpen()) return false;
        close();
        return true;
    }
    return inside && IsDialogMessageW(window_, &message);
}

void CheatsOverlay::toggle() { if (window_) close(); else open(); }

bool CheatsOverlay::open() {
    if (window_) return true;
    if (!IsWindow(owner_)) return false;
    mouse_.release();
    nativeInput().setSettingsOpen(true);
    inputGate_ = true;
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_BAR_CLASSES};
    InitCommonControlsEx(&common);
    if (!background_) background_ = CreateSolidBrush(theme().background);
    if (!field_) field_ = CreateSolidBrush(theme().field);
    if (!titleFont_) titleFont_ = CreateFontW(-18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.lpszClassName = kWindowClass;
    if (!RegisterClassW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        releaseInputGate();
        std::fputs("[Cheats] Cannot register overlay window.\n", stderr);
        return false;
    }
    // Center on the game's client area, kept inside the monitor's work area.
    RECT client{};
    GetClientRect(owner_, &client);
    POINT origin{0, 0};
    ClientToScreen(owner_, &origin);
    int x = origin.x + (client.right - kWidth) / 2, y = origin.y + (client.bottom - kHeight) / 2;
    MONITORINFO monitor{sizeof(monitor)};
    if (GetMonitorInfoW(MonitorFromWindow(owner_, MONITOR_DEFAULTTONEAREST), &monitor)) {
        x = (std::max)(int(monitor.rcWork.left), (std::min)(x, int(monitor.rcWork.right) - kWidth));
        y = (std::max)(int(monitor.rcWork.top), (std::min)(y, int(monitor.rcWork.bottom) - kHeight));
    }
    const HWND created = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_CONTROLPARENT,
        kWindowClass, L"CHEATS", WS_POPUP | WS_BORDER, x, y, kWidth, kHeight, owner_, nullptr,
        windowClass.hInstance, this);
    if (!created) {
        releaseInputGate();
        std::fputs("[Cheats] Cannot create overlay window.\n", stderr);
        return false;
    }
    SetLayeredWindowAttributes(window_, 0, espStyle().menuOpacity, LWA_ALPHA);
    setDeveloperToolsVisible(true);
    refresh();
    SetTimer(window_, kRefreshTimer, 250, nullptr);
    ShowWindow(window_, SW_SHOW);
    SetFocus(infiniteAmmo_);
    std::puts("[Cheats] Overlay opened; game input suspended. F7 or Escape closes it.");
    return true;
}

void CheatsOverlay::close() {
    if (window_ && IsWindow(window_)) DestroyWindow(window_);
    else releaseInputGate();
}

void CheatsOverlay::releaseInputGate() {
    if (!inputGate_) return;
    inputGate_ = false;
    nativeInput().setSettingsOpen(false);
}

void CheatsOverlay::createControls() {
    constexpr DWORD comboStyle = CBS_DROPDOWNLIST | CBS_NOINTEGRALHEIGHT | WS_VSCROLL | WS_TABSTOP;
    control(window_, L"STATIC", L"CHEATS", 0, 16, 8, 300, 26, 0, titleFont_);

    // Left column: player, aim, weapons.
    group(window_, L"PLAYER", 12, 40, 304, 242);
    control(window_, L"STATIC", L"SPEED", 0, 26, 64, 60, 18);
    speed_ = control(window_, L"COMBOBOX", L"", comboStyle, 90, 60, 130, 240, kSpeed);
    for (const auto* name : kSpeedNames) SendMessageW(speed_, CB_ADDSTRING, 0, LPARAM(name));
    bunnyHop_ = check(window_, L"BHOP", 230, 62, 76, kBunnyHop);
    invincible_ = check(window_, L"GOD MODE", 26, 94, 130, kInvincible);
    noclip_ = check(window_, L"NOCLIP", 166, 94, 130, kNoclip);
    infiniteAmmo_ = check(window_, L"INFINITE AMMO", 26, 120, 136, kInfiniteAmmo);
    darklingGod_ = check(window_, L"GOD DARKLINGS", 166, 120, 140, kDarklingGod);
    unlock_ = button(window_, L"UNLOCK ALL DARKNESS", 26, 154, 134, kUnlock);
    maxDarkness_ = button(window_, L"MAX DARKNESS LEVEL", 168, 154, 134, kMaxDarkness);
    // Leftmost position is the original camera; 60-150 are horizontal degrees at 16:9.
    control(window_, L"STATIC", L"CAMERA FOV", 0, 26, 194, 78, 18);
    cameraFov_ = control(window_, TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, 104, 190, 150, 26, kCameraFov);
    SendMessageW(cameraFov_, TBM_SETRANGE, FALSE, MAKELPARAM(59, 150));
    cameraFovValue_ = control(window_, L"STATIC", L"", 0, 258, 194, 54, 18);
    speedometer_ = check(window_, L"SPEEDOMETER", 26, 224, 112, kSpeedometer);
    control(window_, L"STATIC", L"HOP MAX", 0, 142, 226, 56, 18);
    hopMax_ = control(window_, TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, 196, 222, 70, 26, kHopMax);
    SendMessageW(hopMax_, TBM_SETRANGE, FALSE, MAKELPARAM(15, 100)); // Tenths: 1.5x..10x.
    hopMaxValue_ = control(window_, L"STATIC", L"", 0, 268, 226, 44, 18);
    airStrafe_ = check(window_, L"AIR STRAFE", 26, 250, 130, kAirStrafe);
    speedGraph_ = check(window_, L"SPEED GRAPH", 166, 250, 130, kSpeedGraph);

    group(window_, L"AIM", 12, 290, 304, 108);
    aimbot_ = check(window_, L"AIMBOT", 26, 312, 90, kAimbot);
    aimKey_ = button(window_, L"", 122, 310, 180, kAimKey);
    control(window_, L"STATIC", L"AIM FOV", 0, 26, 342, 60, 18);
    fov_ = control(window_, TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, 84, 338, 170, 26, kFov);
    SendMessageW(fov_, TBM_SETRANGE, FALSE, MAKELPARAM(5, 90));
    fovValue_ = control(window_, L"STATIC", L"", 0, 258, 342, 50, 18);
    fovCircle_ = check(window_, L"SHOW FOV CIRCLE", 26, 368, 270, kFovCircle);

    group(window_, L"WEAPONS", 12, 406, 304, 182);
    weapons_ = control(window_, L"LISTBOX", L"", LBS_NOTIFY | WS_VSCROLL | WS_TABSTOP | WS_BORDER,
                       26, 426, 276, 120, kWeapons);
    for (const auto& weapon : developerWeapons()) {
        std::wstring title(weapon.title.begin(), weapon.title.end());
        CharUpperBuffW(title.data(), DWORD(title.size()));
        SendMessageW(weapons_, LB_ADDSTRING, 0, LPARAM(title.c_str()));
    }
    SendMessageW(weapons_, LB_SETCURSEL, 0, 0);
    give_ = button(window_, L"GIVE", 26, 552, 90, kGive);

    // Right column: visuals, mission, settings, status.
    group(window_, L"VISUALS", 328, 40, 300, 204);
    box_ = check(window_, L"BOX ESP", 342, 62, 130, kBox);
    boxColor_ = button(window_, L"", 482, 60, 132, kBoxColor);
    skeleton_ = check(window_, L"SKELETON ESP", 342, 92, 130, kSkeleton);
    boneColor_ = button(window_, L"", 482, 90, 132, kBoneColor);
    tracers_ = check(window_, L"TRACERS", 342, 122, 130, kTracers);
    tracerColor_ = button(window_, L"", 482, 120, 132, kTracerColor);
    health_ = check(window_, L"HEALTH", 342, 152, 86, kHealth);
    distance_ = check(window_, L"DISTANCE", 430, 152, 100, kDistance);
    names_ = check(window_, L"NAMES", 534, 152, 84, kNames);
    control(window_, L"STATIC", L"OPACITY", 0, 342, 190, 64, 18);
    opacity_ = control(window_, TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, 406, 186, 160, 26, kOpacitySlider);
    SendMessageW(opacity_, TBM_SETRANGE, FALSE, MAKELPARAM(20, 100));
    opacityValue_ = control(window_, L"STATIC", L"", 0, 570, 190, 50, 18);
    control(window_, L"STATIC", L"RANGE", 0, 342, 218, 64, 18);
    maxDistance_ = control(window_, TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, 406, 214, 160, 26, kMaxDistance);
    SendMessageW(maxDistance_, TBM_SETRANGE, FALSE, MAKELPARAM(10, EspStyle::kAllDistances));
    maxDistanceValue_ = control(window_, L"STATIC", L"", 0, 570, 218, 50, 18);

    group(window_, L"MISSION", 328, 252, 300, 92);
    mission_ = control(window_, L"COMBOBOX", L"", comboStyle, 342, 274, 272, 320, kMission);
    for (const auto& item : developerMissions()) {
        const auto title = upper(item.title);
        SendMessageW(mission_, CB_ADDSTRING, 0, LPARAM(title.c_str()));
    }
    SendMessageW(mission_, CB_SETCURSEL, 0, 0);
    load_ = button(window_, L"LOAD", 342, 308, 90, kLoad);

    group(window_, L"SPAWN", 328, 352, 300, 104);
    // Editable: pick a darkling or type any template name from the level.
    spawn_ = control(window_, L"COMBOBOX", L"", CBS_DROPDOWN | CBS_AUTOHSCROLL | CBS_NOINTEGRALHEIGHT |
                     WS_VSCROLL | WS_TABSTOP, 342, 374, 272, 320, kSpawnName);
    fillSpawnList({});
    spawnButton_ = button(window_, L"SPAWN", 342, 408, 90, kSpawn);
    button(window_, L"REMOVE SPAWNED", 440, 408, 174, kRemoveSpawns);

    group(window_, L"SETTINGS", 328, 464, 300, 92);
    shortcut_ = check(window_, L"ENABLE F6 RESOLUTION SHORTCUT", 342, 486, 276, kShortcut);
    defaults_ = button(window_, L"RESTORE DEFAULTS", 342, 518, 140, kDefaults);
    button(window_, L"DUMP", 492, 518, 122, kProbe);

    control(window_, L"STATIC", L"STATUS", 0, 330, 566, 296, 18);
    status_ = control(window_, L"STATIC", L"", SS_LEFT, 330, 586, 296, 64);
    control(window_, L"STATIC", L"F7 OR ESC CLOSES. MISSION LOADS, DARKNESS GRANTS AND GIVEN WEAPONS MAY AUTOSAVE.",
            0, 330, 652, 296, 44);

    // Third column: teleport, aim tuning, crosshair, menu look.
    group(window_, L"TELEPORT", 640, 40, 304, 92);
    button(window_, L"SAVE POSITION", 654, 62, 134, kSavePosition);
    button(window_, L"LOAD POSITION", 796, 62, 134, kLoadPosition);
    control(window_, L"STATIC", L"HOTKEYS: F9 SAVES, F10 LOADS.", 0, 654, 100, 276, 18);

    group(window_, L"AIMBOT TUNING", 640, 140, 304, 92);
    aimMode_ = button(window_, L"", 654, 162, 276, kAimMode);
    control(window_, L"STATIC", L"AIM SPEED", 0, 654, 200, 60, 18);
    aimSmooth_ = control(window_, TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, 714, 196, 160, 26, kAimSmooth);
    SendMessageW(aimSmooth_, TBM_SETRANGE, FALSE, MAKELPARAM(5, 100)); // Percent of the fastest stable aim correction.
    aimSmoothValue_ = control(window_, L"STATIC", L"", 0, 878, 200, 56, 18);

    group(window_, L"CROSSHAIR", 640, 240, 304, 92);
    crosshair_ = button(window_, L"", 654, 262, 134, kCrosshair);
    crosshairColor_ = button(window_, L"", 796, 262, 134, kCrosshairColor);
    control(window_, L"STATIC", L"SIZE", 0, 654, 300, 60, 18);
    crosshairSize_ = control(window_, TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, 714, 296, 160, 26, kCrosshairSize);
    SendMessageW(crosshairSize_, TBM_SETRANGE, FALSE, MAKELPARAM(2, 40));
    crosshairSizeValue_ = control(window_, L"STATIC", L"", 0, 878, 300, 56, 18);

    group(window_, L"MENU", 640, 340, 304, 92);
    theme_ = button(window_, L"", 654, 362, 276, kTheme);
    control(window_, L"STATIC", L"OPACITY", 0, 654, 400, 60, 18);
    menuOpacity_ = control(window_, TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, 714, 396, 160, 26, kMenuOpacity);
    SendMessageW(menuOpacity_, TBM_SETRANGE, FALSE, MAKELPARAM(40, 100));
    menuOpacityValue_ = control(window_, L"STATIC", L"", 0, 878, 400, 56, 18);

    group(window_, L"ESP FILTER", 640, 440, 304, 64);
    enemiesOnly_ = check(window_, L"ENEMIES ONLY (ESP AND AIMBOT)", 654, 466, 276, kEnemiesOnly);
    refreshStyle();
}

// Suggestions minus templates a spawn showed this level cannot fully load.
void CheatsOverlay::fillSpawnList(const std::vector<std::string>& unavailable) {
    shownUnavailable_ = unavailable;
    wchar_t typed[64]{};
    GetWindowTextW(spawn_, typed, int(std::size(typed)));
    SendMessageW(spawn_, CB_RESETCONTENT, 0, 0);
    for (const auto name : developerSpawnSuggestions()) {
        if (std::find(unavailable.begin(), unavailable.end(), name) != unavailable.end()) continue;
        const std::wstring text(name.begin(), name.end());
        SendMessageW(spawn_, CB_ADDSTRING, 0, LPARAM(text.c_str()));
    }
    const auto found = SendMessageW(spawn_, CB_FINDSTRINGEXACT, WPARAM(-1), LPARAM(typed));
    SendMessageW(spawn_, CB_SETCURSEL, found == CB_ERR ? 0 : found, 0);
}

void CheatsOverlay::refresh() {
    if (!window_ || !status_) return;
    const auto snapshot = developerSnapshot();
    const bool player = snapshot.hasActivePlayer;
    for (const HWND control : {speed_, invincible_, noclip_, infiniteAmmo_, darklingGod_, bunnyHop_, unlock_, maxDarkness_, aimbot_,
                               weapons_, give_, box_, skeleton_, tracers_, spawn_, spawnButton_})
        EnableWindow(control, player);
    EnableWindow(mission_, snapshot.canLoadMission);
    EnableWindow(load_, snapshot.canLoadMission);
    EnableWindow(shortcut_, bool(setResolutionShortcut_));
    // Never move a drop-down's highlighted row while the user is choosing.
    if (!dropDownOpen()) {
        const auto speed = std::find(kSpeeds.begin(), kSpeeds.end(), snapshot.playerSpeed);
        SendMessageW(speed_, CB_SETCURSEL, speed == kSpeeds.end() ? -1 : speed - kSpeeds.begin(), 0);
    }
    if (snapshot.unavailableSpawns != shownUnavailable_ && !dropDownOpen()) fillSpawnList(snapshot.unavailableSpawns);
    setCheck(invincible_, snapshot.invincible);
    setCheck(noclip_, snapshot.noclip);
    setCheck(infiniteAmmo_, snapshot.infiniteAmmo);
    setCheck(darklingGod_, snapshot.darklingGod);
    setCheck(bunnyHop_, snapshot.bunnyHop);
    setCheck(speedometer_, snapshot.speedometer);
    setCheck(aimbot_, snapshot.aimbot);
    setCheck(box_, snapshot.espBox);
    setCheck(skeleton_, snapshot.espSkeleton);
    setCheck(tracers_, snapshot.espTracers);
    setCheck(shortcut_, resolutionShortcut_ && resolutionShortcut_());
    std::wstring status = upper(snapshot.status);
    if (!snapshot.level.empty())
        status = L"LEVEL: " + upper(snapshot.level) + L"\n" + status;
    if (!player) status += L"\nPLAYER OPTIONS BECOME AVAILABLE WHEN A PLAYER IS ACTIVE.";
    if (status != displayedStatus_) {
        displayedStatus_ = std::move(status);
        SetWindowTextW(status_, displayedStatus_.c_str());
    }
}

void CheatsOverlay::selectSpeed() {
    const auto snapshot = developerSnapshot();
    const LRESULT selected = SendMessageW(speed_, CB_GETCURSEL, 0, 0);
    if (snapshot.hasActivePlayer && selected >= 0 && size_t(selected) < kSpeeds.size() &&
        snapshot.playerSpeed != kSpeeds[size_t(selected)]) requestDeveloperSpeed(kSpeeds[size_t(selected)]);
    refresh();
}

void CheatsOverlay::loadMission() {
    if (!developerSnapshot().canLoadMission) { refresh(); return; }
    const LRESULT selected = SendMessageW(mission_, CB_GETCURSEL, 0, 0);
    const auto missions = developerMissions();
    // Close before the loading movie and opening sequence, as the F5 panel does.
    if (selected >= 0 && size_t(selected) < missions.size() &&
        requestDeveloperMission(missions[size_t(selected)].id)) close();
    else refresh();
}

void CheatsOverlay::giveWeapon() {
    const auto selected = SendMessageW(weapons_, LB_GETCURSEL, 0, 0);
    const auto weapons = developerWeapons();
    if (selected >= 0 && size_t(selected) < weapons.size()) requestDeveloperWeapon(weapons[size_t(selected)].id);
    refresh();
}

void CheatsOverlay::spawn() {
    wchar_t text[96]{};
    GetWindowTextW(spawn_, text, int(std::size(text)));
    std::string name;
    for (const wchar_t* c = text; *c; ++c) if (*c < 128 && *c > 32) name.push_back(char(*c));
    if (!requestDeveloperSpawn(name)) {
        SetWindowTextW(status_, L"TEMPLATE NAMES USE ONLY LETTERS, DIGITS AND UNDERSCORES.");
        displayedStatus_.clear();
        return;
    }
    refresh();
}

// Rebuild the theme brushes and repaint every control.
void CheatsOverlay::applyTheme() {
    if (background_) DeleteObject(background_);
    if (field_) DeleteObject(field_);
    background_ = CreateSolidBrush(theme().background);
    field_ = CreateSolidBrush(theme().field);
    refreshStyle();
    if (window_) RedrawWindow(window_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
}

// Aimbot key: the next key or mouse button pressed (not left click, which
// started the capture). Escape cancels; Backspace restores V / Mouse 4.
void CheatsOverlay::beginKeyCapture() {
    for (int key = 1; key < 256; ++key) keysAtCapture_[key] = (GetAsyncKeyState(key) & 0x8000) != 0;
    capturingKey_ = true;
    SetWindowTextW(aimKey_, L"PRESS A KEY...");
    SetTimer(window_, kKeyTimer, 15, nullptr);
}

void CheatsOverlay::pollKeyCapture() {
    if (!capturingKey_) { KillTimer(window_, kKeyTimer); return; }
    for (int key = 2; key < 256; ++key) {
        const bool down = (GetAsyncKeyState(key) & 0x8000) != 0;
        if (!down) { keysAtCapture_[key] = false; continue; }
        if (keysAtCapture_[key] || key == VK_SHIFT || key == VK_CONTROL || key == VK_MENU) continue;
        capturingKey_ = false;
        KillTimer(window_, kKeyTimer);
        if (key == VK_BACK) setDeveloperAimKey(0);
        else if (key != VK_ESCAPE) setDeveloperAimKey(unsigned(key));
        refreshStyle();
        return;
    }
}

// DarkRecomp.cheats.ini: one key=value per line, read back on the next start.
void CheatsOverlay::useSettingsFile(const std::filesystem::path& path) {
    settingsPath_ = path;
    std::ifstream input(path);
    if (!input) return;
    std::map<std::string, std::string> values;
    for (std::string line; std::getline(input, line);) {
        const auto equals = line.find('=');
        if (equals != std::string::npos) values[line.substr(0, equals)] = line.substr(equals + 1);
    }
    const auto number = [&](const char* key, double fallback) {
        const auto found = values.find(key);
        if (found == values.end()) return fallback;
        try { return std::stod(found->second); } catch (...) { return fallback; }
    };
    const auto flag = [&](const char* key) { return number(key, 0) != 0; };
    auto& style = espStyle();
    style.box = unsigned(number("box_color", style.box)) % 8;
    style.bone = unsigned(number("bone_color", style.bone)) % 8;
    style.tracer = unsigned(number("tracer_color", style.tracer)) % 8;
    style.opacity = BYTE(std::clamp(number("opacity", style.opacity), 51.0, 255.0));
    style.fovCircle = number("fov_circle", style.fovCircle) != 0;
    style.health = number("health", style.health) != 0;
    style.distance = number("distance", style.distance) != 0;
    style.names = number("names", style.names) != 0;
    style.speedGraph = number("speed_graph", style.speedGraph) != 0;
    style.enemiesOnly = number("enemies_only", style.enemiesOnly) != 0;
    setDeveloperEnemiesOnly(style.enemiesOnly);
    style.maxDistance = std::clamp(int(number("range", style.maxDistance)), 10, EspStyle::kAllDistances);
    style.aimFov = float(std::clamp(number("aim_fov", style.aimFov), 5.0, 90.0));
    setDeveloperAimFov(style.aimFov);
    setDeveloperBunnyHopMax(float(number("hop_max", 2.5)));
    setDeveloperAirStrafe(flag("air_strafe"));
    setDeveloperAimKey(unsigned(number("aim_key", 0)));
    setDeveloperAimMode(int(number("aim_mode", 0)));
    setDeveloperAimSmoothing(float(number("aim_speed2", 0.66)));
    style.crosshair = unsigned(number("crosshair", 0)) % 4;
    style.crosshairColor = unsigned(number("crosshair_color", style.crosshairColor)) % 8;
    style.crosshairSize = std::clamp(int(number("crosshair_size", style.crosshairSize)), 2, 40);
    style.menuTheme = unsigned(number("menu_theme", 0)) % unsigned(EspStyle::kThemes.size());
    style.menuOpacity = BYTE(std::clamp(number("menu_opacity", style.menuOpacity), 102.0, 255.0));
    if (const float speed = float(number("speed", 1)); speed != 1) requestDeveloperSpeed(speed);
    if (flag("god_mode")) requestDeveloperInvincibility(true);
    if (flag("noclip")) requestDeveloperNoclip(true);
    if (flag("infinite_ammo")) requestDeveloperInfiniteAmmo(true);
    if (flag("god_darklings")) requestDeveloperDarklingGod(true);
    if (flag("bhop")) requestDeveloperBunnyHop(true);
    if (flag("speedometer")) requestDeveloperSpeedometer(true);
    if (flag("box")) requestDeveloperEspBox(true);
    if (flag("skeleton")) requestDeveloperEspSkeleton(true);
    if (flag("tracers")) requestDeveloperEspTracers(true);
    if (flag("aimbot")) requestDeveloperAimbot(true);
    std::printf("[Cheats] Loaded settings from %ls\n", path.c_str());
}

void CheatsOverlay::saveSettings() const {
    if (settingsPath_.empty()) return;
    const auto snapshot = developerSnapshot();
    const auto& style = espStyle();
    std::ofstream output(settingsPath_, std::ios::trunc);
    if (!output) { std::fprintf(stderr, "[Cheats] Cannot save %ls\n", settingsPath_.c_str()); return; }
    output << "speed=" << snapshot.playerSpeed << "\ngod_mode=" << snapshot.invincible
           << "\nnoclip=" << snapshot.noclip << "\ninfinite_ammo=" << snapshot.infiniteAmmo
           << "\ngod_darklings=" << snapshot.darklingGod << "\nbhop=" << snapshot.bunnyHop
           << "\nspeedometer=" << snapshot.speedometer << "\nhop_max=" << developerBunnyHopMax()
           << "\nair_strafe=" << developerAirStrafe() << "\nbox=" << snapshot.espBox
           << "\nskeleton=" << snapshot.espSkeleton << "\ntracers=" << snapshot.espTracers
           << "\naimbot=" << snapshot.aimbot << "\naim_key=" << developerAimKey()
           << "\naim_fov=" << style.aimFov << "\nfov_circle=" << style.fovCircle
           << "\nbox_color=" << style.box << "\nbone_color=" << style.bone
           << "\ntracer_color=" << style.tracer << "\nopacity=" << unsigned(style.opacity)
           << "\nhealth=" << style.health << "\ndistance=" << style.distance
           << "\nnames=" << style.names << "\nspeed_graph=" << style.speedGraph
           << "\nrange=" << style.maxDistance << "\naim_mode=" << developerAimMode()
           << "\naim_speed2=" << developerAimSmoothing() << "\ncrosshair=" << style.crosshair
           << "\ncrosshair_color=" << style.crosshairColor << "\ncrosshair_size=" << style.crosshairSize
           << "\nenemies_only=" << style.enemiesOnly << "\nmenu_theme=" << style.menuTheme << "\nmenu_opacity=" << unsigned(style.menuOpacity) << "\n";
}

void CheatsOverlay::restoreDefaults() {
    requestDeveloperSpeed(1.f);
    requestDeveloperInvincibility(false);
    requestDeveloperNoclip(false);
    requestDeveloperInfiniteAmmo(false);
    requestDeveloperDarklingGod(false);
    requestDeveloperBunnyHop(false);
    requestDeveloperSpeedometer(false);
    setDeveloperBunnyHopMax(2.5f);
    setDeveloperAirStrafe(false);
    setDeveloperAimKey(0);
    setDeveloperAimMode(0);
    setDeveloperAimSmoothing(0.66f);
    setDeveloperEnemiesOnly(false);
    requestDeveloperAimbot(false);
    requestDeveloperEspBox(false);
    requestDeveloperEspSkeleton(false);
    requestDeveloperEspTracers(false);
    espStyle() = {};
    setDeveloperAimFov(espStyle().aimFov);
    SetLayeredWindowAttributes(window_, 0, espStyle().menuOpacity, LWA_ALPHA);
    applyTheme();
    if (setResolutionShortcut_) setResolutionShortcut_(false);
    refresh();
}

void CheatsOverlay::command(int id, int notification) {
    const bool clicked = notification == BN_CLICKED;
    switch (id) {
    case kSpeed:
        if (notification == CBN_CLOSEUP || (notification == CBN_SELCHANGE && !dropDownOpen())) selectSpeed();
        return;
    case kInvincible: if (clicked) requestDeveloperInvincibility(checked(invincible_)); break;
    case kNoclip: if (clicked) requestDeveloperNoclip(checked(noclip_)); break;
    case kInfiniteAmmo: if (clicked) requestDeveloperInfiniteAmmo(checked(infiniteAmmo_)); break;
    case kDarklingGod: if (clicked) requestDeveloperDarklingGod(checked(darklingGod_)); break;
    case kBunnyHop: if (clicked) requestDeveloperBunnyHop(checked(bunnyHop_)); break;
    case kSpeedometer: if (clicked) requestDeveloperSpeedometer(checked(speedometer_)); break;
    case kAirStrafe: if (clicked) setDeveloperAirStrafe(checked(airStrafe_)); return;
    case kSpeedGraph: if (clicked) espStyle().speedGraph = checked(speedGraph_); return;
    case kAimKey: if (clicked) beginKeyCapture(); return;
    case kRemoveSpawns: if (clicked) requestDeveloperRemoveSpawns(); break;
    case kSavePosition: if (clicked) requestDeveloperSavePosition(); break;
    case kLoadPosition: if (clicked) requestDeveloperLoadPosition(); break;
    case kAimMode: if (clicked) { setDeveloperAimMode(1 - developerAimMode()); refreshStyle(); } return;
    case kCrosshair: if (clicked) { espStyle().crosshair = (espStyle().crosshair + 1) % 4; refreshStyle(); } return;
    case kCrosshairColor: if (clicked) cycleColor(espStyle().crosshairColor, crosshairColor_, L"COLOR"); return;
    case kEnemiesOnly:
        if (clicked) { espStyle().enemiesOnly = checked(enemiesOnly_); setDeveloperEnemiesOnly(espStyle().enemiesOnly); }
        return;
    case kTheme: if (clicked) { espStyle().menuTheme = (espStyle().menuTheme + 1) % unsigned(EspStyle::kThemes.size()); applyTheme(); } return;
    case kUnlock: if (clicked && developerSnapshot().hasActivePlayer) requestDeveloperUnlockDarkness(); break;
    case kMaxDarkness: if (clicked && developerSnapshot().hasActivePlayer) requestDeveloperMaxDarkness(); break;
    case kAimbot: if (clicked) requestDeveloperAimbot(checked(aimbot_)); break;
    case kBox: if (clicked) requestDeveloperEspBox(checked(box_)); break;
    case kSkeleton: if (clicked) requestDeveloperEspSkeleton(checked(skeleton_)); break;
    case kTracers: if (clicked) requestDeveloperEspTracers(checked(tracers_)); break;
    case kBoxColor: if (clicked) cycleColor(espStyle().box, boxColor_, L"BOX"); return;
    case kBoneColor: if (clicked) cycleColor(espStyle().bone, boneColor_, L"BONES"); return;
    case kTracerColor: if (clicked) cycleColor(espStyle().tracer, tracerColor_, L"LINES"); return;
    case kFovCircle: if (clicked) espStyle().fovCircle = checked(fovCircle_); return;
    case kHealth: if (clicked) espStyle().health = checked(health_); return;
    case kDistance: if (clicked) espStyle().distance = checked(distance_); return;
    case kNames: if (clicked) espStyle().names = checked(names_); return;
    case kSpawn: if (clicked) spawn(); return;
    case kWeapons: if (notification == LBN_DBLCLK) giveWeapon(); return;
    case kGive: if (clicked) giveWeapon(); return;
    case kLoad: if (clicked) loadMission(); return;
    case kShortcut: if (clicked && setResolutionShortcut_) setResolutionShortcut_(checked(shortcut_)); break;
    case kDefaults: if (clicked) restoreDefaults(); return;
    case kProbe: if (clicked) requestDeveloperProbe(); break;
    case IDCANCEL: close(); return;
    default: return;
    }
    refresh();
}

static std::wstring rangeText(int meters) {
    return meters >= EspStyle::kAllDistances ? L"ALL" : std::to_wstring(meters) + L"M";
}

void CheatsOverlay::cycleColor(unsigned& index, HWND button, const wchar_t*) {
    index = (index + 1) % unsigned(EspStyle::palette.size());
    refreshStyle();
    InvalidateRect(button, nullptr, TRUE);
}

void CheatsOverlay::refreshStyle() {
    const auto& style = espStyle();
    const auto label = [](HWND button, const wchar_t* name, unsigned index) {
        const std::wstring text = std::wstring(name) + L": " + EspStyle::paletteNames[index % 8];
        SetWindowTextW(button, text.c_str());
    };
    label(boxColor_, L"BOX", style.box);
    label(boneColor_, L"BONES", style.bone);
    label(tracerColor_, L"LINES", style.tracer);
    SendMessageW(opacity_, TBM_SETPOS, TRUE, LPARAM(style.opacity * 100 / 255));
    SendMessageW(fov_, TBM_SETPOS, TRUE, LPARAM(style.aimFov));
    setCheck(fovCircle_, style.fovCircle);
    setCheck(health_, style.health);
    setCheck(distance_, style.distance);
    setCheck(airStrafe_, developerAirStrafe());
    SetWindowTextW(aimMode_, developerAimMode() == 1 ? L"TARGET: NEAREST BY DISTANCE" : L"TARGET: NEAREST TO CROSSHAIR");
    const int smooth = int(developerAimSmoothing() * 100 + 0.5f);
    SendMessageW(aimSmooth_, TBM_SETPOS, TRUE, LPARAM(smooth));
    SetWindowTextW(aimSmoothValue_, (std::to_wstring(smooth) + L"%").c_str());
    static constexpr const wchar_t* kCrosshairs[]{L"CROSSHAIR: OFF", L"CROSSHAIR: DOT", L"CROSSHAIR: CROSS", L"CROSSHAIR: CIRCLE"};
    SetWindowTextW(crosshair_, kCrosshairs[style.crosshair % 4]);
    label(crosshairColor_, L"COLOR", style.crosshairColor);
    SendMessageW(crosshairSize_, TBM_SETPOS, TRUE, LPARAM(style.crosshairSize));
    SetWindowTextW(crosshairSizeValue_, (std::to_wstring(style.crosshairSize) + L" PX").c_str());
    SetWindowTextW(theme_, (std::wstring(L"THEME: ") + EspStyle::kThemes[style.menuTheme % EspStyle::kThemes.size()].name).c_str());
    SendMessageW(menuOpacity_, TBM_SETPOS, TRUE, LPARAM(style.menuOpacity * 100 / 255));
    SetWindowTextW(menuOpacityValue_, (std::to_wstring(style.menuOpacity * 100 / 255) + L"%").c_str());
    setCheck(speedGraph_, style.speedGraph);
    setCheck(enemiesOnly_, style.enemiesOnly);
    if (!capturingKey_) {
        const auto key = developerAimKey();
        const std::wstring text = L"HOLD: " + (key ? DarkRecomp::Native::keyboardKeyName(key) : std::wstring(L"V / MOUSE 4"));
        SetWindowTextW(aimKey_, text.c_str());
    }
    SendMessageW(hopMax_, TBM_SETPOS, TRUE, LPARAM(developerBunnyHopMax() * 10 + 0.5f));
    wchar_t hop[16];
    swprintf_s(hop, L"%.1fX", developerBunnyHopMax());
    SetWindowTextW(hopMaxValue_, hop);
    setCheck(names_, style.names);
    SendMessageW(maxDistance_, TBM_SETPOS, TRUE, LPARAM(style.maxDistance));
    SetWindowTextW(maxDistanceValue_, rangeText(style.maxDistance).c_str());
    const float camera = fieldOfViewSetting();
    SendMessageW(cameraFov_, TBM_SETPOS, TRUE, LPARAM(camera == kOriginalFov ? 59 : int(camera + 0.5f)));
    SetWindowTextW(cameraFovValue_, camera == kOriginalFov ? L"ORIGINAL" :
                   (std::to_wstring(int(camera + 0.5f)) + L"\u00B0").c_str());
    SetWindowTextW(opacityValue_, (std::to_wstring(style.opacity * 100 / 255) + L"%").c_str());
    SetWindowTextW(fovValue_, (std::to_wstring(int(style.aimFov)) + L"\u00B0").c_str());
}

void CheatsOverlay::slider(HWND control) {
    auto& style = espStyle();
    const auto position = int(SendMessageW(control, TBM_GETPOS, 0, 0));
    if (control == cameraFov_) {
        // Session change; Video Settings saves the camera FOV permanently.
        setFieldOfViewSetting(position < 60 ? kOriginalFov : float((std::min)(position, 150)));
        refreshStyle();
        return;
    }
    if (control == aimSmooth_) {
        setDeveloperAimSmoothing(float(std::clamp(position, 5, 100)) / 100);
        refreshStyle();
        return;
    }
    if (control == crosshairSize_) { style.crosshairSize = std::clamp(position, 2, 40); refreshStyle(); return; }
    if (control == menuOpacity_) {
        style.menuOpacity = BYTE(std::clamp(position, 40, 100) * 255 / 100);
        SetLayeredWindowAttributes(window_, 0, style.menuOpacity, LWA_ALPHA);
        refreshStyle();
        return;
    }
    if (control == hopMax_) {
        setDeveloperBunnyHopMax(float(std::clamp(position, 15, 100)) / 10);
        refreshStyle();
        return;
    }
    if (control == maxDistance_) {
        style.maxDistance = std::clamp(position, 10, EspStyle::kAllDistances);
        SetWindowTextW(maxDistanceValue_, rangeText(style.maxDistance).c_str());
        return;
    }
    if (control == opacity_) style.opacity = BYTE(std::clamp(position, 20, 100) * 255 / 100);
    else if (control == fov_) {
        style.aimFov = float(std::clamp(position, 5, 90));
        setDeveloperAimFov(style.aimFov);
    } else return;
    SetWindowTextW(opacityValue_, (std::to_wstring(style.opacity * 100 / 255) + L"%").c_str());
    SetWindowTextW(fovValue_, (std::to_wstring(int(style.aimFov)) + L"\u00B0").c_str());
}

LRESULT CALLBACK CheatsOverlay::windowProc(HWND window, UINT message, WPARAM key, LPARAM detail) {
    auto* self = reinterpret_cast<CheatsOverlay*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<CheatsOverlay*>(reinterpret_cast<CREATESTRUCTW*>(detail)->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, LONG_PTR(self));
    }
    if (self) {
        try { return self->message(message, key, detail); }
        catch (const std::exception& error) {
            std::fprintf(stderr, "[Cheats] %s\n", error.what());
            if (message == WM_CREATE) return -1;
        }
    }
    return DefWindowProcW(window, message, key, detail);
}

LRESULT CheatsOverlay::message(UINT message, WPARAM key, LPARAM detail) {
    if (message == WM_CREATE) { createControls(); return 0; }
    if (message == WM_TIMER && key == kRefreshTimer) { refresh(); return 0; }
    if (message == WM_TIMER && key == kKeyTimer) { pollKeyCapture(); return 0; }
    if (message == WM_ERASEBKGND) {
        RECT area{};
        GetClientRect(window_, &area);
        FillRect(HDC(key), &area, background_);
        return 1;
    }
    // Classic (unthemed) controls take their colors from here.
    if (message == WM_CTLCOLORSTATIC || message == WM_CTLCOLORBTN) {
        const HDC dc = HDC(key);
        SetBkColor(dc, theme().background);
        SetTextColor(dc, IsWindowEnabled(HWND(detail)) ? theme().text : kDim);
        return LRESULT(background_);
    }
    if (message == WM_CTLCOLORLISTBOX || message == WM_CTLCOLOREDIT) {
        const HDC dc = HDC(key);
        SetBkColor(dc, theme().field);
        SetTextColor(dc, theme().text);
        return LRESULT(field_);
    }
    if (message == WM_COMMAND) { command(LOWORD(key), HIWORD(key)); return 0; }
    if (message == WM_HSCROLL && detail) { slider(HWND(detail)); return 0; }
    if (message == WM_CLOSE) { close(); return 0; }
    if (message == WM_NCDESTROY) {
        setDeveloperToolsVisible(false);
        const HWND closed = window_;
        const HWND foreground = GetForegroundWindow();
        const bool restoreFocus = foreground == closed || foreground == owner_;
        KillTimer(closed, kRefreshTimer);
        KillTimer(closed, kKeyTimer);
        capturingKey_ = false;
        saveSettings();
        SetWindowLongPtrW(closed, GWLP_USERDATA, 0);
        window_ = speed_ = invincible_ = noclip_ = infiniteAmmo_ = darklingGod_ = bunnyHop_ = speedometer_ = hopMax_ = hopMaxValue_ = airStrafe_ = speedGraph_ = aimKey_ = aimMode_ = aimSmooth_ = aimSmoothValue_ = crosshair_ =
                crosshairColor_ = crosshairSize_ = crosshairSizeValue_ = theme_ = menuOpacity_ = menuOpacityValue_ = enemiesOnly_ = unlock_ = maxDarkness_ = aimbot_ =
            weapons_ = give_ = box_ = skeleton_ = mission_ = load_ = shortcut_ = defaults_ = status_ =
            tracers_ = boxColor_ = boneColor_ = tracerColor_ = opacity_ = opacityValue_ = fov_ = fovValue_ =
            fovCircle_ = cameraFov_ = cameraFovValue_ = health_ = distance_ = names_ = maxDistance_ = maxDistanceValue_ = spawn_ = spawnButton_ = nullptr;
        displayedStatus_.clear();
        releaseInputGate();
        if (restoreFocus && IsWindow(owner_)) SetFocus(owner_);
        std::puts("[Cheats] Overlay closed; click the game to capture the mouse.");
        return DefWindowProcW(closed, message, key, detail);
    }
    return DefWindowProcW(window_, message, key, detail);
}
