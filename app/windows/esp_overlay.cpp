#include "esp_overlay.h"
#include "esp_style.h"
#include "runtime/native/developer_esp.h"
#include <cmath>
#include <cstdio>

using namespace DarkRecomp::Native;

namespace {
constexpr wchar_t kWindowClass[] = L"DarkRecompEspOverlay";
constexpr COLORREF kKey = RGB(0, 0, 0);
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
    for (HPEN* pen : {&pen_, &bonePen_, &tracerPen_, &circlePen_}) {
        if (*pen) DeleteObject(*pen);
        *pen = nullptr;
    }
    opacity_ = 0;
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
    return true;
}

void EspOverlay::update() {
    const auto now = GetTickCount64();
    if (now - lastPaint_ < 8) return;
    lastPaint_ = now;
    const auto view = developerEspSnapshot();
    const auto& style = espStyle();
    const bool circle = style.fovCircle && view.aimbot;
    const bool visible = view.valid && (view.box || view.skeleton || view.tracers || circle) &&
                         IsWindow(owner_) && !IsIconic(owner_);
    if (!visible) {
        if (shown_) { ShowWindow(window_, SW_HIDE); shown_ = false; }
        return;
    }
    if (!window_ && !create()) return;
    if (opacity_ != style.opacity) {
        // Black stays fully transparent; everything drawn uses the chosen opacity.
        SetLayeredWindowAttributes(window_, kKey, style.opacity, LWA_COLORKEY | LWA_ALPHA);
        opacity_ = style.opacity;
    }
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
    const auto& style = espStyle();
    const COLORREF colors[4]{EspStyle::palette[style.box % 8], EspStyle::palette[style.bone % 8],
                             EspStyle::palette[style.tracer % 8], RGB(200, 200, 200)};
    HPEN* pens[4]{&pen_, &bonePen_, &tracerPen_, &circlePen_};
    for (int i = 0; i < 4; ++i)
        if (!*pens[i] || penColors_[i] != colors[i]) {
            if (*pens[i]) DeleteObject(*pens[i]);
            *pens[i] = CreatePen(i == 3 ? PS_DOT : PS_SOLID, i == 3 ? 1 : 2, colors[i]);
            penColors_[i] = colors[i];
        }
    const auto oldPen = SelectObject(dc, pen_);
    // Outlines only: rectangles and the FOV circle must never be filled.
    const auto oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    SetTextColor(dc, colors[0]);
    if (style.fovCircle && view.aimbot) {
        // The aim cone's edge at the screen center, in the vertical FOV's scale.
        const float radius = height * 0.5f * std::tan(view.aimFovDegrees * 3.14159265f / 360) / view.tanHalfVertical;
        SelectObject(dc, circlePen_);
        Ellipse(dc, int(width / 2 - radius), int(height / 2 - radius), int(width / 2 + radius), int(height / 2 + radius));
        SelectObject(dc, pen_);
    }
    SetBkMode(dc, TRANSPARENT);
    SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
    for (const auto& target : view.targets) {
        const auto feet = project(view, target.x, target.y, target.z, width, height);
        const auto head = project(view, target.x, target.y, target.z + kBodyHeight, width, height);
        if (!feet.ok || !head.ok) continue;
        const float tall = feet.y - head.y;
        if (!(tall > 2) || tall > height * 2) continue;
        const float half = tall * 0.22f;
        const int left = int(feet.x - half), right = int(feet.x + half);
        if (view.tracers) {
            SelectObject(dc, tracerPen_);
            MoveToEx(dc, width / 2, height - 1, nullptr);
            LineTo(dc, int(feet.x), int(feet.y));
            SelectObject(dc, pen_);
        }
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
