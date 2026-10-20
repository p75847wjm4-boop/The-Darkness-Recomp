#pragma once
#include <windows.h>
#include <array>
#include <cstdint>

// Click-through, color-keyed layer over the game's client area. The display
// thread owns it and draws boxes from the value-only ESP snapshot.
class EspOverlay {
public:
    explicit EspOverlay(HWND owner) : owner_(owner) {}
    ~EspOverlay();
    EspOverlay(const EspOverlay&) = delete;
    EspOverlay& operator=(const EspOverlay&) = delete;

    // Call every display-loop iteration; repaints at most ~120 times a second.
    void update();
    void close();

private:
    static LRESULT CALLBACK windowProc(HWND, UINT, WPARAM, LPARAM);
    bool create();
    void paint(HDC dc, int width, int height);

    void releaseBuffer();

    HWND owner_{}, window_{};
    HDC buffer_{};
    HBITMAP bitmap_{};
    HGDIOBJ previousBitmap_{};
    HPEN pen_{}, bonePen_{}, tracerPen_{}, circlePen_{};
    HFONT speedFont_{};
    // Speedometer history: one sample per 33 ms over the last three seconds.
    std::array<float, 90> speeds_{};
    size_t speedNext_ = 0;
    unsigned long long lastSpeedSample_ = 0;
    COLORREF penColors_[4]{};
    BYTE opacity_ = 0;
    int bufferWidth_ = 0, bufferHeight_ = 0;
    ULONGLONG lastPaint_ = 0;
    bool shown_ = false;
};
