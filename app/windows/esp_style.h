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
    float aimFov = 25;
};

inline EspStyle& espStyle() {
    static EspStyle style;
    return style;
}
