#include "esp_overlay.h"
#include "runtime/native/developer_esp.h"
#include <cmath>
#include <cstdio>

using namespace DarkRecomp::Native;

namespace {
constexpr wchar_t kWindowClass[] = L"DarkRecompEspOverlay";
constexpr COLORREF kKey = RGB(0, 0, 0), kBox = RGB(255, 40, 40), kText = RGB(255, 230, 230),
                   kBone = RGB(255, 255, 255);
constexpr float kBodyHeight = 62;

struct Point { float x, y; bool ok; };
Point project(const EspSnapshot& view, float x, float y, float z, int width, int height) {
    const float dx = x - view.eye[0], dy = y - view.eye[1], dz = z - view.eye[2];
    const float depth = dx * view.forward[0] + dy * view.forward[1] + dz * view.forward[2];
    if (depth < 8) return {0, 0, false};
    const float right = -(dx * view.left[0] + dy * view.left[1] + dz * view.left[2]);
    const float up = dx * view.up[0] + dy * view.up[1] + dz * view.up[2];
    const float aspect = float(width) / float(height);
    const float t = view.tanHalfVertical;
    return {width * 0.5f * (1 + right / (depth * t * aspect)), height * 0.5f * (1 - up / (depth * t)), true};
}
}

EspOverlay::~EspOverlay() { close(); }

void EspOverlay::releaseBuffer() {
    if (buffer_) {
        SelectObject(buffer_, previousBitmap_);
        DeleteDC(buffer_);
    }
    if (bitmap_) DeleteObject(bitmap_);
    buffer_ = nullptr; bitmap_ = nullptr; previousBitmap_ = nullptr;
    bufferWidth_ = bufferHeight_ = 0;
}

void EspOverlay::close() {
    releaseBuffer();
    if (pen_) DeleteObject(pen_);
    if (bonePen_) DeleteObject(bonePen_);
    pen_ = bonePen_ = nullptr;
    if (window_ && IsWindow(window_)) DestroyWindow(window_);
    window_ = nullptr;
    shown_ = false;
}

bool EspOverlay::create() {
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = kWindowClass;
    if (!RegisterClassW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    window_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kWindowClass, L"ESP", WS_POPUP, 0, 0, 1, 1, owner_, nullptr, windowClass.hInstance, this);
    if (!window_) { std::fputs("[Cheats] Cannot create ESP layer.\n", stderr); return false; }
    SetLayeredWindowAttributes(window_, kKey, 0, LWA_COLORKEY);
    return true;
}

void EspOverlay::update() {
    const auto now = GetTickCount64();
    if (now - lastPaint_ < 8) return;
    lastPaint_ = now;
    const auto view = developerEspSnapshot();
    const bool visible = view.valid && (view.box || view.skeleton) && IsWindow(owner_) && !IsIconic(owner_);
    if (!visible) {
        if (shown_) { ShowWindow(window_, SW_HIDE); shown_ = false; }
        return;
    }
    if (!window_ && !create()) return;
    RECT client{};
    GetClientRect(owner_, &client);
    POINT origin{0, 0};
    ClientToScreen(owner_, &origin);
    const int width = client.right, height = client.bottom;
    if (width <= 0 || height <= 0) return;
    SetWindowPos(window_, nullptr, origin.x, origin.y, width, height,
                 SWP_NOACTIVATE | SWP_NOZORDER | (shown_ ? 0 : SWP_SHOWWINDOW));
    shown_ = true;
    const HDC screen = GetDC(window_);
    if (!buffer_ || bufferWidth_ != width || bufferHeight_ != height) {
        releaseBuffer();
        buffer_ = CreateCompatibleDC(screen);
        bitmap_ = CreateCompatibleBitmap(screen, width, height);
        previousBitmap_ = SelectObject(buffer_, bitmap_);
        bufferWidth_ = width; bufferHeight_ = height;
    }
    paint(buffer_, width, height);
    BitBlt(screen, 0, 0, width, height, buffer_, 0, 0, SRCCOPY);
    ReleaseDC(window_, screen);
}

void EspOverlay::paint(HDC dc, int width, int height) {
    RECT all{0, 0, width, height};
    FillRect(dc, &all, HBRUSH(GetStockObject(BLACK_BRUSH)));
    const auto view = developerEspSnapshot();
    if (!pen_) pen_ = CreatePen(PS_SOLID, 2, kBox);
    if (!bonePen_) bonePen_ = CreatePen(PS_SOLID, 2, kBone);
    const auto oldPen = SelectObject(dc, pen_);
    const auto oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, kText);
    SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
    for (const auto& target : view.targets) {
        const auto feet = project(view, target.x, target.y, target.z, width, height);
        const auto head = project(view, target.x, target.y, target.z + kBodyHeight, width, height);
        if (!feet.ok || !head.ok) continue;
        const float tall = feet.y - head.y;
        if (!(tall > 2) || tall > height * 2) continue;
        const float half = tall * 0.22f;
        const int left = int(feet.x - half), right = int(feet.x + half);
        if (view.box) Rectangle(dc, left, int(head.y), right, int(feet.y));
        SelectObject(dc, bonePen_);
        if (view.skeleton) for (const auto& bone : target.bones) {
            const auto from = project(view, bone[0], bone[1], bone[2], width, height);
            const auto to = project(view, bone[3], bone[4], bone[5], width, height);
            if (!from.ok || !to.ok) continue;
            MoveToEx(dc, int(from.x), int(from.y), nullptr);
            LineTo(dc, int(to.x), int(to.y));
        }
        SelectObject(dc, pen_);
        wchar_t label[32];
        const int length = swprintf_s(label, L"%d", target.health);
        if (length > 0) TextOutW(dc, left, int(head.y) - 14, label, length);
    }
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
}

LRESULT CALLBACK EspOverlay::windowProc(HWND window, UINT message, WPARAM key, LPARAM detail) {
    if (message == WM_NCHITTEST) return HTTRANSPARENT;
    if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (message == WM_ERASEBKGND) return 1;
    return DefWindowProcW(window, message, key, detail);
}
