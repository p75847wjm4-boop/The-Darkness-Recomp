#pragma once
#include <windows.h>
#include <string>

class NativeMouseWindow;

// F7 cheat overlay drawn over the top-left of the game window. The display
// thread owns it; options cross the value-only developer-tools bridge.
class CheatsOverlay {
public:
    CheatsOverlay(HWND owner, NativeMouseWindow& mouse);
    ~CheatsOverlay();
    CheatsOverlay(const CheatsOverlay&) = delete;
    CheatsOverlay& operator=(const CheatsOverlay&) = delete;

    bool isOpen() const { return window_ != nullptr; }
    bool handleMessage(MSG& message);
    void toggle();
    void close();

private:
    static LRESULT CALLBACK windowProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT message(UINT, WPARAM, LPARAM);
    bool open();
    void createControls();
    void refresh();
    void selectInfiniteAmmo();
    void giveWeapon();
    void releaseInputGate();

    HWND owner_{};
    NativeMouseWindow& mouse_;
    HWND window_{}, infiniteAmmo_{}, weapons_{}, give_{}, status_{};
    HBRUSH background_{};
    HFONT titleFont_{};
    bool inputGate_ = false;
    std::wstring displayedStatus_;
};
