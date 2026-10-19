#pragma once
#include <windows.h>
#include <cstdint>

// Click-through, color-keyed layer over the game's client area. The display
// thread owns it and draws boxes from the value-only ESP snapshot.
class EspOverlay {
public:
    explicit EspOverlay(HWND owner) : owner_(owner) {}
    ~EspOverlay();
    EspOverlay(const EspOverlay&) = delete;
    EspOverlay& operator=(const EspOverlay&) = delete;

    // Call every display-loop iteration; repaints at most ~30 times a second.
    void update();
    void close();

private:
    static LRESULT CALLBACK windowProc(HWND, UINT, WPARAM, LPARAM);
    bool create();
    void paint(HDC dc, int width, int height);

    HWND owner_{}, window_{};
    ULONGLONG lastPaint_ = 0;
    bool shown_ = false;
};
