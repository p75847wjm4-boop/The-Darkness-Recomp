#include "cheats_overlay.h"
#include "native_mouse.h"
#include "runtime/native/developer_tools.h"
#include "runtime/native/input.h"
#include <cstdio>
#include <exception>
#include <stdexcept>

using namespace DarkRecomp::Native;

namespace {
constexpr wchar_t kWindowClass[] = L"DarkRecompCheatsOverlay";
constexpr UINT_PTR kRefreshTimer = 1;
constexpr int kInfiniteAmmo = 2001;
constexpr int kWidth = 280, kHeight = 132, kMargin = 16;
constexpr BYTE kOpacity = 225;
constexpr COLORREF kBackground = RGB(12, 12, 14), kText = RGB(230, 230, 230), kDim = RGB(150, 150, 150);

HWND control(HWND parent, const wchar_t* type, const wchar_t* text, DWORD style,
             int x, int y, int width, int height, int id = 0, HFONT font = nullptr) {
    const HWND result = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style,
        x, y, width, height, parent, reinterpret_cast<HMENU>(INT_PTR(id)), GetModuleHandleW(nullptr), nullptr);
    if (!result) throw std::runtime_error("Cannot create cheats overlay control");
    SendMessageW(result, WM_SETFONT, WPARAM(font ? font : HFONT(GetStockObject(DEFAULT_GUI_FONT))), TRUE);
    return result;
}
}

CheatsOverlay::CheatsOverlay(HWND owner, NativeMouseWindow& mouse) : owner_(owner), mouse_(mouse) {}
CheatsOverlay::~CheatsOverlay() {
    close();
    releaseInputGate();
    if (titleFont_) DeleteObject(titleFont_);
    if (background_) DeleteObject(background_);
}

bool CheatsOverlay::handleMessage(MSG& message) {
    const bool inside = window_ && (message.hwnd == window_ || IsChild(window_, message.hwnd));
    if ((message.hwnd == owner_ || inside) && (message.message == WM_KEYDOWN || message.message == WM_KEYUP) &&
        message.wParam == VK_F7) {
        if (message.message == WM_KEYDOWN && !(message.lParam & (LPARAM(1) << 30))) toggle();
        return true;
    }
    if (inside && message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE) {
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
    if (!background_) background_ = CreateSolidBrush(kBackground);
    if (!titleFont_) titleFont_ = CreateFontW(-16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
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
    POINT origin{0, 0};
    ClientToScreen(owner_, &origin);
    const HWND created = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_CONTROLPARENT,
        kWindowClass, L"CHEATS", WS_POPUP | WS_BORDER, origin.x + kMargin, origin.y + kMargin,
        kWidth, kHeight, owner_, nullptr, windowClass.hInstance, this);
    if (!created) {
        releaseInputGate();
        std::fputs("[Cheats] Cannot create overlay window.\n", stderr);
        return false;
    }
    SetLayeredWindowAttributes(window_, 0, kOpacity, LWA_ALPHA);
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
    control(window_, L"STATIC", L"CHEATS", 0, 14, 10, 200, 22, 0, titleFont_);
    infiniteAmmo_ = control(window_, L"BUTTON", L"INFINITE AMMO", BS_AUTOCHECKBOX | WS_TABSTOP,
                            14, 40, 250, 24, kInfiniteAmmo);
    status_ = control(window_, L"STATIC", L"", SS_LEFT, 14, 70, 252, 32);
    control(window_, L"STATIC", L"F7 OR ESC CLOSES. SESSION ONLY.", 0, 14, 106, 252, 18);
}

void CheatsOverlay::refresh() {
    if (!window_ || !status_) return;
    const auto snapshot = developerSnapshot();
    EnableWindow(infiniteAmmo_, snapshot.hasActivePlayer);
    SendMessageW(infiniteAmmo_, BM_SETCHECK, snapshot.infiniteAmmo ? BST_CHECKED : BST_UNCHECKED, 0);
    std::wstring status = !snapshot.hasActivePlayer ? L"AVAILABLE WHEN A PLAYER IS ACTIVE." :
        snapshot.infiniteAmmo ? L"INFINITE AMMO ON." : L"READY.";
    if (status != displayedStatus_) {
        displayedStatus_ = std::move(status);
        SetWindowTextW(status_, displayedStatus_.c_str());
    }
}

void CheatsOverlay::selectInfiniteAmmo() {
    requestDeveloperInfiniteAmmo(SendMessageW(infiniteAmmo_, BM_GETCHECK, 0, 0) == BST_CHECKED);
    refresh();
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
    if (message == WM_ERASEBKGND) {
        RECT area{};
        GetClientRect(window_, &area);
        FillRect(HDC(key), &area, background_);
        return 1;
    }
    // Classic (unthemed) statics and check boxes take their colors from here.
    if (message == WM_CTLCOLORSTATIC || message == WM_CTLCOLORBTN) {
        const HDC dc = HDC(key);
        SetBkColor(dc, kBackground);
        SetTextColor(dc, IsWindowEnabled(HWND(detail)) ? kText : kDim);
        return LRESULT(background_);
    }
    if (message == WM_COMMAND) {
        if (LOWORD(key) == kInfiniteAmmo && HIWORD(key) == BN_CLICKED) selectInfiniteAmmo();
        else if (LOWORD(key) == IDCANCEL) close();
        return 0;
    }
    if (message == WM_CLOSE) { close(); return 0; }
    if (message == WM_NCDESTROY) {
        setDeveloperToolsVisible(false);
        const HWND closed = window_;
        const HWND foreground = GetForegroundWindow();
        const bool restoreFocus = foreground == closed || foreground == owner_;
        KillTimer(closed, kRefreshTimer);
        SetWindowLongPtrW(closed, GWLP_USERDATA, 0);
        window_ = infiniteAmmo_ = status_ = nullptr;
        displayedStatus_.clear();
        releaseInputGate();
        if (restoreFocus && IsWindow(owner_)) SetFocus(owner_);
        std::puts("[Cheats] Overlay closed; click the game to capture the mouse.");
        return DefWindowProcW(closed, message, key, detail);
    }
    return DefWindowProcW(window_, message, key, detail);
}
