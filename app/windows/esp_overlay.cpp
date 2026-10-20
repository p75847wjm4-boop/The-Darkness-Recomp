#include "esp_overlay.h"
#include "esp_style.h"
#include "runtime/native/developer_esp.h"
#include "runtime/native/developer_tools.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

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
    if (speedFont_) { DeleteObject(speedFont_); speedFont_ = nullptr; }
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
    const bool speed = DarkRecomp::Native::developerPlayerSpeed() >= 0;
    const bool visible = ((view.valid && (view.box || view.skeleton || view.tracers || circle)) || speed ||
                          style.crosshair) &&
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
    if (const float speed = DarkRecomp::Native::developerPlayerSpeed(); speed >= 0) {
        if (!speedFont_) speedFont_ = CreateFontW(-28, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        SelectObject(dc, speedFont_);
        wchar_t text[48];
        const int length = swprintf_s(text, L"%.1f m/s  (%.0f km/h)", speed, speed * 3.6f);
        if (const auto now = GetTickCount64(); now - lastSpeedSample_ >= 33) {
            lastSpeedSample_ = now;
            speeds_[speedNext_] = speed;
            speedNext_ = (speedNext_ + 1) % speeds_.size();
        }
        if (style.speedGraph) {
            float top = 5;
            for (const float sample : speeds_) top = (std::max)(top, sample);
            const int graphWidth = 270, graphHeight = 56;
            const int left = (width - graphWidth) / 2, bottom = height - 116;
            const HPEN outline = CreatePen(PS_SOLID, 1, RGB(110, 110, 110));
            const HPEN line = CreatePen(PS_SOLID, 2, RGB(80, 230, 120));
            const auto previous = SelectObject(dc, outline);
            Rectangle(dc, left - 2, bottom - graphHeight - 2, left + graphWidth + 2, bottom + 2);
            SelectObject(dc, line);
            for (size_t i = 0; i < speeds_.size(); ++i) {
                const float sample = speeds_[(speedNext_ + i) % speeds_.size()];
                const int x = left + int(i * (graphWidth - 1) / (speeds_.size() - 1));
                const int y = bottom - int(sample / top * graphHeight);
                if (i) LineTo(dc, x, y); else MoveToEx(dc, x, y, nullptr);
            }
            SelectObject(dc, previous);
            DeleteObject(outline);
            DeleteObject(line);
        }
        RECT line{0, height - 110, width, height - 76};
        SetTextColor(dc, RGB(255, 255, 255));
        if (length > 0) DrawTextW(dc, text, length, &line, DT_CENTER | DT_SINGLELINE | DT_NOCLIP);
        // Bunny hop run: time since the hop started and its best speed so far.
        const float run = DarkRecomp::Native::developerBunnyHopSeconds();
        if (run > 0) {
            if (run < lastRun_) bestSpeed_ = 0;
            bestSpeed_ = (std::max)(bestSpeed_, speed);
        }
        if (run > 0 || lastRun_ > 0) {
            if (run > 0) shownRun_ = run;
            wchar_t stats[64];
            const int statsLength = swprintf_s(stats, L"RUN %.1f s   BEST %.1f m/s", shownRun_, bestSpeed_);
            RECT statsLine{0, height - 76, width, height - 48};
            SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
            SetTextColor(dc, RGB(200, 200, 200));
            if (statsLength > 0) DrawTextW(dc, stats, statsLength, &statsLine, DT_CENTER | DT_SINGLELINE | DT_NOCLIP);
        }
        if (run > 0) lastRun_ = run;
        SetTextColor(dc, colors[0]);
    }
    SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
    for (const auto& target : view.targets) {
        // Starbreeze world units: 32 per meter (the 62-unit body is ~1.9 m).
        const float dx = target.x - view.eye[0], dy = target.y - view.eye[1], dz = target.z - view.eye[2];
        const float meters = std::sqrt(dx * dx + dy * dy + dz * dz) / 32;
        if (style.maxDistance < EspStyle::kAllDistances && meters > style.maxDistance) continue;
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
        int length = 0;
        if (style.health) length = swprintf_s(label, L"%d  ", target.health);
        if (style.distance && length >= 0)
            length += swprintf_s(label + length, std::size(label) - length, L"%.0fm", meters);
        if (length > 0 && (view.box || view.skeleton))
            TextOutW(dc, left, int(head.y) - 14, label, length);
        if (style.names && !target.name.empty() && (view.box || view.skeleton)) {
            const std::wstring name(target.name.begin(), target.name.end());
            TextOutW(dc, left, int(head.y) - (length > 0 ? 28 : 14), name.c_str(), int(name.size()));
        }
    }
    if (style.crosshair) {
        const COLORREF color = EspStyle::palette[style.crosshairColor % 8];
        const HPEN pen = CreatePen(PS_SOLID, 2, color);
        const auto previous = SelectObject(dc, pen);
        const int cx = width / 2, cy = height / 2, size = std::clamp(style.crosshairSize, 2, 40);
        if (style.crosshair == 1) {
            const HBRUSH dot = CreateSolidBrush(color);
            const RECT center{cx - 2, cy - 2, cx + 3, cy + 3};
            FillRect(dc, &center, dot);
            DeleteObject(dot);
        } else if (style.crosshair == 2) {
            MoveToEx(dc, cx - size, cy, nullptr); LineTo(dc, cx - 3, cy);
            MoveToEx(dc, cx + 3, cy, nullptr); LineTo(dc, cx + size + 1, cy);
            MoveToEx(dc, cx, cy - size, nullptr); LineTo(dc, cx, cy - 3);
            MoveToEx(dc, cx, cy + 3, nullptr); LineTo(dc, cx, cy + size + 1);
        } else {
            Ellipse(dc, cx - size, cy - size, cx + size + 1, cy + size + 1);
        }
        SelectObject(dc, previous);
        DeleteObject(pen);
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
