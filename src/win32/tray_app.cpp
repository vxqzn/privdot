#include "win32/tray_app.hpp"

#include "core/consent_parser.hpp"
#include "win32/audio_mute.hpp"
#include "win32/consent_reader.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <string>
#include <string_view>
#include <vector>

namespace privdot::win32 {

namespace {

constexpr UINT kMsgTrayIcon = WM_APP + 1;
constexpr UINT kMsgConsentChanged = WM_APP + 2;
constexpr UINT_PTR kTimerActiveCheck = 1;
constexpr UINT kTrayIconId = 1;

constexpr UINT kCmdToggleMute = 1001;
constexpr UINT kCmdToggleLocation = 1002;
constexpr UINT kCmdRefresh = 1003;
constexpr UINT kCmdToggleStartup = 1004;
constexpr UINT kCmdExit = 1005;

constexpr const wchar_t* kStartupRunKey = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr const wchar_t* kStartupValueName = L"privdot";

struct Rgb {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

constexpr Rgb kColorMic{255, 159, 10};   // warm orange
constexpr Rgb kColorCam{48, 209, 88};    // calm green
constexpr Rgb kColorLoc{10, 132, 255};   // clean blue
constexpr Rgb kColorMuted{215, 75, 75};  // muted red
constexpr Rgb kColorIdle{165, 170, 180}; // soft neutral grey

// Safe function pointer loader (avoids -Wcast-function-type under -Werror).
template <typename Fn> Fn load_proc(HMODULE mod, LPCSTR name) {
    FARPROC raw = GetProcAddress(mod, name);
    Fn fn = nullptr;
    if (raw != nullptr) {
        std::memcpy(&fn, &raw, sizeof(fn));
    }
    return fn;
}

// Enables native Windows 10/11 dark Fluent theme on standard Win32 popup menus.
void enable_native_dark_menus() {
    HMODULE ux = LoadLibraryW(L"uxtheme.dll");
    if (ux == nullptr) {
        return;
    }
    using SetPreferredAppModeFn = int(WINAPI*)(int);
    using FlushMenuThemesFn = void(WINAPI*)();

    if (auto set_mode = load_proc<SetPreferredAppModeFn>(ux, MAKEINTRESOURCEA(135))) {
        set_mode(1); // AllowDark: follows Windows light/dark taskbar setting automatically
    }
    if (auto flush = load_proc<FlushMenuThemesFn>(ux, MAKEINTRESOURCEA(136))) {
        flush();
    }
}

std::wstring utf8_to_wide(std::string_view input) {
    if (input.empty()) {
        return {};
    }
    const int len =
        MultiByteToWideChar(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), nullptr, 0);
    if (len <= 0) {
        return {};
    }
    std::wstring out(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), out.data(), len);
    return out;
}

std::string sensor_label(SensorKind s) {
    if (has_sensor(s, SensorKind::Mic)) {
        return "mic";
    }
    if (has_sensor(s, SensorKind::Cam)) {
        return "camera";
    }
    if (has_sensor(s, SensorKind::Location)) {
        return "location";
    }
    return "sensor";
}

bool is_startup_enabled() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kStartupRunKey, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return false;
    }
    const LSTATUS rc = RegQueryValueExW(key, kStartupValueName, nullptr, nullptr, nullptr, nullptr);
    RegCloseKey(key);
    return rc == ERROR_SUCCESS;
}

void set_startup_enabled(bool enable) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kStartupRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
        return;
    }
    if (enable) {
        wchar_t exe_path[MAX_PATH];
        const DWORD len = GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
        if (len > 0 && len < MAX_PATH) {
            std::wstring quoted = L"\"";
            quoted.append(exe_path, len);
            quoted.push_back(L'"');
            const DWORD bytes = static_cast<DWORD>((quoted.size() + 1) * sizeof(wchar_t));
            RegSetValueExW(key, kStartupValueName, 0, REG_SZ,
                           reinterpret_cast<const BYTE*>(quoted.c_str()), bytes);
        }
    } else {
        RegDeleteValueW(key, kStartupValueName);
    }
    RegCloseKey(key);
}

// Renders a crisp, anti-aliased 32x32 ARGB privacy dot (or split pair) in memory.
HICON create_dot_icon(SensorKind mask, bool mic_muted) {
    constexpr int kSize = 32;
    BITMAPV5HEADER bi{};
    bi.bV5Size = sizeof(bi);
    bi.bV5Width = kSize;
    bi.bV5Height = -kSize; // top-down DIB
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;

    void* bits = nullptr;
    HDC screen_dc = GetDC(nullptr);
    HBITMAP color_bmp = CreateDIBSection(screen_dc, reinterpret_cast<BITMAPINFO*>(&bi),
                                         DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen_dc);
    if (color_bmp == nullptr || bits == nullptr) {
        return nullptr;
    }

    auto* pixels = static_cast<uint32_t*>(bits);
    std::memset(pixels, 0, kSize * kSize * sizeof(uint32_t));

    auto blend_pixel = [pixels](int x, int y, Rgb c, float alpha) {
        if (x < 0 || x >= kSize || y < 0 || y >= kSize || alpha <= 0.0f) {
            return;
        }
        const float a = std::clamp(alpha, 0.0f, 1.0f);
        const auto pa = static_cast<uint32_t>(a * 255.0f + 0.5f);
        const auto pr = static_cast<uint32_t>(c.r * a + 0.5f);
        const auto pg = static_cast<uint32_t>(c.g * a + 0.5f);
        const auto pb = static_cast<uint32_t>(c.b * a + 0.5f);
        pixels[y * kSize + x] = (pa << 24) | (pr << 16) | (pg << 8) | pb;
    };

    auto draw_disc = [&](float cx, float cy, float r, Rgb c, float inner_r = 0.0f) {
        for (int y = 0; y < kSize; ++y) {
            for (int x = 0; x < kSize; ++x) {
                const float dx = (static_cast<float>(x) + 0.5f) - cx;
                const float dy = (static_cast<float>(y) + 0.5f) - cy;
                const float dist = std::sqrt(dx * dx + dy * dy);
                float alpha = std::clamp(0.5f - (dist - r) / 1.25f, 0.0f, 1.0f);
                if (inner_r > 0.0f) {
                    const float inner = std::clamp(0.5f + (dist - inner_r) / 1.25f, 0.0f, 1.0f);
                    alpha = std::min(alpha, inner);
                }
                if (alpha > 0.01f) {
                    blend_pixel(x, y, c, alpha);
                }
            }
        }
    };

    std::vector<Rgb> active_colors;
    if (has_sensor(mask, SensorKind::Cam)) {
        active_colors.push_back(kColorCam);
    }
    if (has_sensor(mask, SensorKind::Mic)) {
        active_colors.push_back(kColorMic);
    }
    if (has_sensor(mask, SensorKind::Location)) {
        active_colors.push_back(kColorLoc);
    }

    if (active_colors.empty()) {
        if (mic_muted) {
            draw_disc(16.0f, 16.0f, 9.5f, kColorMuted, 5.5f);
        } else {
            draw_disc(16.0f, 16.0f, 9.5f, kColorIdle, 6.2f);
        }
    } else if (active_colors.size() == 1) {
        draw_disc(16.0f, 16.0f, 10.5f, active_colors[0]);
    } else {
        draw_disc(9.5f, 16.0f, 7.2f, active_colors[0]);
        draw_disc(22.5f, 16.0f, 7.2f, active_colors[1]);
    }

    std::vector<uint8_t> mask_bits(kSize * kSize / 8, 0);
    HBITMAP mask_bmp = CreateBitmap(kSize, kSize, 1, 1, mask_bits.data());
    ICONINFO ii{};
    ii.fIcon = TRUE;
    ii.hbmMask = mask_bmp;
    ii.hbmColor = color_bmp;
    HICON icon = CreateIconIndirect(&ii);

    DeleteObject(color_bmp);
    DeleteObject(mask_bmp);
    return icon;
}

void copy_tooltip(wchar_t (&dst)[128], const std::wstring& text) {
    const size_t n = std::min<size_t>(text.size(), 127);
    std::wmemcpy(dst, text.data(), n);
    dst[n] = L'\0';
}

} // namespace

TrayApp::~TrayApp() {
    watcher_.stop();
    remove_tray_icon();
    if (current_icon_ != nullptr) {
        DestroyIcon(current_icon_);
        current_icon_ = nullptr;
    }
}

bool TrayApp::init(HINSTANCE instance) {
    instance_ = instance;
    enable_native_dark_menus();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &TrayApp::wnd_proc;
    wc.hInstance = instance_;
    wc.lpszClassName = L"privdot_tray_window";
    if (!RegisterClassExW(&wc)) {
        return false;
    }

    taskbar_created_msg_ = RegisterWindowMessageW(L"TaskbarCreated");
    hwnd_ = CreateWindowExW(0, wc.lpszClassName, L"privdot", 0, 0, 0, 0, 0, nullptr, nullptr,
                            instance_, this);
    if (hwnd_ == nullptr) {
        return false;
    }

    add_tray_icon();
    refresh_state();
    watcher_.start(hwnd_, kMsgConsentChanged);
    return true;
}

int TrayApp::run_message_loop() {
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK TrayApp::wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    TrayApp* self = nullptr;
    if (msg == WM_NCCREATE) {
        const auto* cs = reinterpret_cast<const CREATESTRUCTW*>(lp);
        self = static_cast<TrayApp*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<TrayApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self != nullptr) {
        return self->handle_message(msg, wp, lp);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT TrayApp::handle_message(UINT msg, WPARAM wp, LPARAM lp) {
    if (taskbar_created_msg_ != 0 && msg == taskbar_created_msg_) {
        add_tray_icon();
        refresh_state();
        return 0;
    }

    switch (msg) {
    case kMsgTrayIcon:
        if (lp == WM_LBUTTONUP || lp == WM_RBUTTONUP || lp == WM_CONTEXTMENU) {
            refresh_state();
            show_menu();
        }
        return 0;

    case kMsgConsentChanged:
    case WM_TIMER:
        refresh_state();
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case kCmdToggleMute:
            toggle_mic_mute();
            refresh_state();
            break;
        case kCmdToggleLocation:
            show_location_ = !show_location_;
            refresh_state();
            break;
        case kCmdRefresh:
            refresh_state();
            break;
        case kCmdToggleStartup:
            set_startup_enabled(!is_startup_enabled());
            break;
        case kCmdExit:
            DestroyWindow(hwnd_);
            break;
        default:
            break;
        }
        return 0;

    case WM_DESTROY:
        if (timer_active_) {
            KillTimer(hwnd_, kTimerActiveCheck);
            timer_active_ = false;
        }
        watcher_.stop();
        remove_tray_icon();
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcW(hwnd_, msg, wp, lp);
    }
}

void TrayApp::add_tray_icon() {
    if (current_icon_ == nullptr) {
        current_icon_ = create_dot_icon(state_.active_mask, mic_muted_);
    }
    nid_ = {};
    nid_.cbSize = sizeof(nid_);
    nid_.hWnd = hwnd_;
    nid_.uID = kTrayIconId;
    nid_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid_.uCallbackMessage = kMsgTrayIcon;
    nid_.hIcon = current_icon_;
    copy_tooltip(nid_.szTip, L"privdot");
    Shell_NotifyIconW(NIM_ADD, &nid_);
}

void TrayApp::remove_tray_icon() {
    if (nid_.hWnd != nullptr) {
        Shell_NotifyIconW(NIM_DELETE, &nid_);
        nid_.hWnd = nullptr;
    }
}

void TrayApp::refresh_state() {
    auto records = read_all_consent_records();
    if (!show_location_) {
        records.erase(
            std::remove_if(records.begin(), records.end(),
                           [](const AccessRecord& r) { return r.sensor == SensorKind::Location; }),
            records.end());
    }

    const auto alive = get_alive_process_names_lower();
    state_ = build_indicator_state(std::move(records), alive, 8);
    mic_muted_ = is_mic_muted().value_or(false);

    // Run 15s fallback timer only while a sensor is active (0.00% CPU when idle).
    if (!state_.active.empty() && !timer_active_) {
        SetTimer(hwnd_, kTimerActiveCheck, 15000, nullptr);
        timer_active_ = true;
    } else if (state_.active.empty() && timer_active_) {
        KillTimer(hwnd_, kTimerActiveCheck);
        timer_active_ = false;
    }

    HICON next_icon = create_dot_icon(state_.active_mask, mic_muted_);
    if (next_icon != nullptr) {
        if (current_icon_ != nullptr) {
            DestroyIcon(current_icon_);
        }
        current_icon_ = next_icon;
        nid_.hIcon = current_icon_;
    }

    std::string tip = "privdot";
    if (!state_.active.empty()) {
        tip += " - " + state_.active[0].display_name + " (" +
               sensor_label(state_.active[0].sensor) + ")";
        if (state_.active.size() > 1) {
            tip += " +" + std::to_string(state_.active.size() - 1) + " more";
        }
    } else if (mic_muted_) {
        tip += " - mic muted";
    } else {
        tip += " - no active sensors";
    }

    copy_tooltip(nid_.szTip, utf8_to_wide(tip));
    if (!Shell_NotifyIconW(NIM_MODIFY, &nid_)) {
        Shell_NotifyIconW(NIM_ADD, &nid_);
    }
}

void TrayApp::show_menu() {
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return;
    }

    const int64_t now_unix = filetime_to_unix_seconds(current_filetime());

    if (state_.active.empty()) {
        AppendMenuW(menu, MF_STRING | MF_DISABLED | MF_GRAYED, 0, L"No active sensors");
    } else {
        AppendMenuW(menu, MF_STRING | MF_DISABLED | MF_GRAYED, 0, L"Active right now");
        for (const auto& r : state_.active) {
            const int64_t start_unix = filetime_to_unix_seconds(r.last_used_start_ft);
            const std::string line = r.display_name + "  [" + sensor_label(r.sensor) + ", " +
                                     format_relative_time(start_unix, now_unix) + "]";
            AppendMenuW(menu, MF_STRING | MF_CHECKED, 0, utf8_to_wide(line).c_str());
        }
    }

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | MF_DISABLED | MF_GRAYED, 0, L"Recently used");
    if (state_.recent.empty()) {
        AppendMenuW(menu, MF_STRING | MF_DISABLED | MF_GRAYED, 0, L"No recent activity");
    } else {
        for (const auto& r : state_.recent) {
            const uint64_t latest = std::max(r.last_used_start_ft, r.last_used_stop_ft);
            const int64_t latest_unix = filetime_to_unix_seconds(latest);
            std::string line = r.display_name + "  [" + sensor_label(r.sensor) + ", " +
                               format_relative_time(latest_unix, now_unix);
            if (r.last_used_stop_ft > r.last_used_start_ft) {
                line += ", " + format_duration(r.last_used_start_ft, r.last_used_stop_ft);
            }
            line += "]";
            AppendMenuW(menu, MF_STRING, 0, utf8_to_wide(line).c_str());
        }
    }

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (mic_muted_ ? MF_CHECKED : MF_UNCHECKED), kCmdToggleMute,
                L"Mute microphone");
    AppendMenuW(menu, MF_STRING | (show_location_ ? MF_CHECKED : MF_UNCHECKED), kCmdToggleLocation,
                L"Show location activity");
    AppendMenuW(menu, MF_STRING, kCmdRefresh, L"Refresh now");
    AppendMenuW(menu, MF_STRING | (is_startup_enabled() ? MF_CHECKED : MF_UNCHECKED),
                kCmdToggleStartup, L"Start with Windows");

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCmdExit, L"Exit");

    POINT pt{};
    GetCursorPos(&pt);
    // Win32 requirement: foreground window + trailing WM_NULL so clicking outside dismisses menu.
    SetForegroundWindow(hwnd_);
    TrackPopupMenuEx(menu, TPM_RIGHTALIGN | TPM_BOTTOMALIGN | TPM_LEFTBUTTON, pt.x, pt.y, hwnd_,
                     nullptr);
    PostMessageW(hwnd_, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

} // namespace privdot::win32
