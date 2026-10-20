#pragma once
#include <windows.h>
#include <array>

// Display-thread ESP appearance, edited by the F7 overlay and read by the ESP
// layer. Session only.
struct EspStyle {
    static constexpr std::array<COLORREF, 8> palette{
        RGB(255, 40, 40), RGB(40, 230, 70), RGB(60, 140, 255), RGB(255, 225, 40),
        RGB(40, 230, 230), RGB(235, 60, 235), RGB(255, 255, 255), RGB(255, 140, 20)};
    static constexpr std::array<const wchar_t*, 8> paletteNames{
        L"RED", L"GREEN", L"BLUE", L"YELLOW", L"CYAN", L"MAGENTA", L"WHITE", L"ORANGE"};
    unsigned box = 0, bone = 6, tracer = 3;
    BYTE opacity = 255;
    bool fovCircle = false;
    bool health = true;
    bool distance = true;
    bool names = true;
    bool speedGraph = true;
    bool enemiesOnly = false;
    // Crosshair: 0 off, 1 dot, 2 cross, 3 circle; size is the arm length in pixels.
    unsigned crosshair = 0, crosshairColor = 1;
    int crosshairSize = 8;
    // F7 menu look: theme index into kThemes and window opacity.
    unsigned menuTheme = 0;
    BYTE menuOpacity = 235;
    struct Theme { const wchar_t* name; COLORREF background, field, text; };
    static constexpr std::array<Theme, 5> kThemes{{
        {L"DARK", RGB(12, 12, 14), RGB(28, 28, 32), RGB(230, 230, 230)},
        {L"BLUE", RGB(8, 16, 34), RGB(20, 34, 64), RGB(220, 232, 255)},
        {L"RED", RGB(30, 8, 10), RGB(58, 18, 22), RGB(255, 226, 226)},
        {L"GREEN", RGB(8, 24, 12), RGB(18, 46, 26), RGB(222, 255, 228)},
        {L"PURPLE", RGB(22, 10, 34), RGB(42, 22, 62), RGB(240, 226, 255)}}};
    // Meters; kAllDistances shows every target.
    static constexpr int kAllDistances = 200;
    int maxDistance = kAllDistances;
    float aimFov = 25;
};

inline EspStyle& espStyle() {
    static EspStyle style;
    return style;
}
