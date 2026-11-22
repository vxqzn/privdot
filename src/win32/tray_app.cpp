#include "tray_app.hpp"

#include "../core/consent_parser.hpp"
#include "audio_mute.hpp"
#include "consent_reader.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace privdot::win32 {

namespace {

constexpr UINT kMsgTrayIcon = WM_APP + 1;
constexpr UINT kMsgConsentChanged = WM_APP + 2;
constexpr UINT kMsgAudioChanged = WM_APP + 3;
constexpr UINT_PTR kTimerActiveCheck = 1;
constexpr UINT kTrayIconId = 1;

constexpr UINT kCmdToggleMute = 1001;
constexpr UINT kCmdToggleLocation = 1002;
constexpr UINT kCmdToggleStartup = 1003;
constexpr UINT kCmdExit = 1004;

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

void enable_dpi_awareness() {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 == nullptr) {
        return;
    }
    using SetProcessDpiAwarenessContextFn = BOOL(WINAPI*)(HANDLE);
    if (auto set_dpi =
            load_proc<SetProcessDpiAwarenessContextFn>(user32, "SetProcessDpiAwarenessContext")) {
        // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 (-4) for crisp HiDPI icons & menus.
        set_dpi(reinterpret_cast<HANDLE>(static_cast<intptr_t>(-4)));
    }
}

int native_icon_size() {
    return std::clamp(GetSystemMetrics(SM_CXSMICON), 16, 64);
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

Rgb sensor_color(SensorKind s) {
    if (has_sensor(s, SensorKind::Mic)) {
        return kColorMic;
    }
    if (has_sensor(s, SensorKind::Cam)) {
        return kColorCam;
    }
    if (has_sensor(s, SensorKind::Location)) {
        return kColorLoc;
    }
    return kColorIdle;
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

HBITMAP create_argb_dib(int size, uint32_t** out_pixels) {
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = size;
    bi.bmiHeader.biHeight = -size; // top-down DIB
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC screen_dc = GetDC(nullptr);
    HBITMAP bmp = CreateDIBSection(screen_dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen_dc);
    if (bmp == nullptr || bits == nullptr) {
        if (bmp != nullptr) {
            DeleteObject(bmp);
        }
        *out_pixels = nullptr;
        return nullptr;
    }

    auto* pixels = static_cast<uint32_t*>(bits);
    std::memset(pixels, 0, static_cast<size_t>(size) * static_cast<size_t>(size) * sizeof(uint32_t));
    *out_pixels = pixels;
    return bmp;
}

uint32_t pack_premultiplied(float r_sum, float g_sum, float b_sum, float a_sum, int samples) {
    const float inv = 1.0f / static_cast<float>(samples);
    const auto pa = static_cast<uint32_t>(std::clamp(a_sum * inv * 255.0f, 0.0f, 255.0f) + 0.5f);
    const auto pr = static_cast<uint32_t>(std::clamp(r_sum * inv, 0.0f, 255.0f) + 0.5f);
    const auto pg = static_cast<uint32_t>(std::clamp(g_sum * inv, 0.0f, 255.0f) + 0.5f);
    const auto pb = static_cast<uint32_t>(std::clamp(b_sum * inv, 0.0f, 255.0f) + 0.5f);
    return (pa << 24) | (pr << 16) | (pg << 8) | pb;
}

std::vector<Rgb> collect_colors(SensorKind mask) {
    std::vector<Rgb> colors;
    if (has_sensor(mask, SensorKind::Cam)) {
        colors.push_back(kColorCam);
    }
    if (has_sensor(mask, SensorKind::Mic)) {
        colors.push_back(kColorMic);
    }
    if (has_sensor(mask, SensorKind::Location)) {
        colors.push_back(kColorLoc);
    }
    return colors;
}

// 4x4 supersampled smooth colored pill badge at native menu DPI.
HBITMAP create_badge_bitmap(SensorKind sensor, int size) {
    uint32_t* pixels = nullptr;
    HBITMAP bmp = create_argb_dib(size, &pixels);
    if (bmp == nullptr || pixels == nullptr) {
        return bmp;
    }

    static constexpr int kSub = 4;
    const Rgb bg = sensor_color(sensor);
    const float inv_size = 2.0f / static_cast<float>(size);

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float r_sum = 0.0f;
            float g_sum = 0.0f;
            float b_sum = 0.0f;
            float a_sum = 0.0f;

            for (int sy = 0; sy < kSub; ++sy) {
                const float v = (static_cast<float>(y) + (static_cast<float>(sy) + 0.5f) * 0.25f) *
                                    inv_size -
                                1.0f;
                for (int sx = 0; sx < kSub; ++sx) {
                    const float u =
                        (static_cast<float>(x) + (static_cast<float>(sx) + 0.5f) * 0.25f) *
                            inv_size -
                        1.0f;
                    const float clamped_u = std::clamp(u, -0.36f, 0.36f);
                    const float du = u - clamped_u;
                    if (du * du + v * v <= 0.34f * 0.34f) {
                        r_sum += static_cast<float>(bg.r);
                        g_sum += static_cast<float>(bg.g);
                        b_sum += static_cast<float>(bg.b);
                        a_sum += 1.0f;
                    }
                }
            }

            if (a_sum > 0.0f) {
                pixels[y * size + x] = pack_premultiplied(r_sum, g_sum, b_sum, a_sum, kSub * kSub);
            }
        }
    }
    return bmp;
}

bool hit_disc(float u, float v, float cx, float cy, float outer_r, float inner_r) {
    const float dx = u - cx;
    const float dy = v - cy;
    const float d2 = dx * dx + dy * dy;
    if (d2 > outer_r * outer_r) {
        return false;
    }
    return inner_r <= 0.0f || d2 >= inner_r * inner_r;
}

// 4x4 supersampled HiDPI tray dot (solid=active, colored ring=5m grace).
HICON create_dot_icon(SensorKind active_mask, SensorKind recent_mask, bool mic_muted, int size) {
    uint32_t* pixels = nullptr;
    HBITMAP color_bmp = create_argb_dib(size, &pixels);
    if (color_bmp == nullptr || pixels == nullptr) {
        return nullptr;
    }

    const auto active_colors = collect_colors(active_mask);
    const auto recent_colors = collect_colors(recent_mask);

    static constexpr int kSub = 4;
    const float inv_size = 2.0f / static_cast<float>(size);

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float r_sum = 0.0f;
            float g_sum = 0.0f;
            float b_sum = 0.0f;
            float a_sum = 0.0f;

            for (int sy = 0; sy < kSub; ++sy) {
                const float v = (static_cast<float>(y) + (static_cast<float>(sy) + 0.5f) * 0.25f) *
                                    inv_size -
                                1.0f;
                for (int sx = 0; sx < kSub; ++sx) {
                    const float u =
                        (static_cast<float>(x) + (static_cast<float>(sx) + 0.5f) * 0.25f) *
                            inv_size -
                        1.0f;

                    bool hit = false;
                    Rgb c{};

                    if (!active_colors.empty()) {
                        if (active_colors.size() == 1) {
                            hit = hit_disc(u, v, 0.0f, 0.0f, 0.66f, 0.0f);
                            c = active_colors[0];
                        } else if (hit_disc(u, v, -0.40f, 0.0f, 0.45f, 0.0f)) {
                            hit = true;
                            c = active_colors[0];
                        } else if (hit_disc(u, v, 0.40f, 0.0f, 0.45f, 0.0f)) {
                            hit = true;
                            c = active_colors[1];
                        }
                    } else if (!recent_colors.empty()) {
                        if (recent_colors.size() == 1) {
                            hit = hit_disc(u, v, 0.0f, 0.0f, 0.64f, 0.38f);
                            c = recent_colors[0];
                        } else if (hit_disc(u, v, -0.40f, 0.0f, 0.45f, 0.26f)) {
                            hit = true;
                            c = recent_colors[0];
                        } else if (hit_disc(u, v, 0.40f, 0.0f, 0.45f, 0.26f)) {
                            hit = true;
                            c = recent_colors[1];
                        }
                    } else if (mic_muted) {
                        hit = hit_disc(u, v, 0.0f, 0.0f, 0.60f, 0.35f);
                        c = kColorMuted;
                    } else {
                        hit = hit_disc(u, v, 0.0f, 0.0f, 0.60f, 0.39f);
                        c = kColorIdle;
                    }

                    if (hit) {
                        r_sum += static_cast<float>(c.r);
                        g_sum += static_cast<float>(c.g);
                        b_sum += static_cast<float>(c.b);
                        a_sum += 1.0f;
                    }
                }
            }

            if (a_sum > 0.0f) {
                pixels[y * size + x] = pack_premultiplied(r_sum, g_sum, b_sum, a_sum, kSub * kSub);
            }
        }
    }

    // GDI 1bpp bitmap stride must be WORD-aligned (safe for non-16-multiple sizes like 20x20).
    const size_t row_bytes = static_cast<size_t>(((size + 15) / 16) * 2);
    std::vector<uint8_t> mask_bits(row_bytes * static_cast<size_t>(size), 0);
    HBITMAP mask_bmp = CreateBitmap(size, size, 1, 1, mask_bits.data());

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

void append_badge_item(HMENU menu, const std::wstring& text, HBITMAP badge) {
    MENUITEMINFOW mii{};
    mii.cbSize = sizeof(mii);
    mii.fMask = MIIM_STRING | MIIM_ID | MIIM_STATE | (badge != nullptr ? MIIM_BITMAP : 0U);
    mii.fState = MFS_ENABLED;
    mii.wID = 0;
    mii.hbmpItem = badge;
    mii.dwTypeData = const_cast<LPWSTR>(text.c_str());
    InsertMenuItemW(menu, static_cast<UINT>(GetMenuItemCount(menu)), TRUE, &mii);
}

} // namespace

TrayApp::~TrayApp() {
    watcher_.stop();
    audio_watcher_.stop();
    remove_tray_icon();
    if (current_icon_ != nullptr) {
        DestroyIcon(current_icon_);
        current_icon_ = nullptr;
    }
    for (HBITMAP* bmp : {&badge_mic_, &badge_cam_, &badge_loc_}) {
        if (*bmp != nullptr) {
            DeleteObject(*bmp);
            *bmp = nullptr;
        }
    }
}

bool TrayApp::init(HINSTANCE instance) {
    instance_ = instance;
    enable_dpi_awareness();
    enable_native_dark_menus();

    const int badge_px = native_icon_size();
    badge_mic_ = create_badge_bitmap(SensorKind::Mic, badge_px);
    badge_cam_ = create_badge_bitmap(SensorKind::Cam, badge_px);
    badge_loc_ = create_badge_bitmap(SensorKind::Location, badge_px);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &TrayApp::wnd_proc;
    wc.hInstance = instance_;
    wc.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(1));
    wc.hIconSm = wc.hIcon;
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
    audio_watcher_.start(hwnd_, kMsgAudioChanged);
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

HBITMAP TrayApp::badge_for_sensor(SensorKind s) const {
    if (has_sensor(s, SensorKind::Mic)) {
        return badge_mic_;
    }
    if (has_sensor(s, SensorKind::Cam)) {
        return badge_cam_;
    }
    if (has_sensor(s, SensorKind::Location)) {
        return badge_loc_;
    }
    return nullptr;
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

    case kMsgAudioChanged:
        if (wp != 0) {
            audio_watcher_.rescan_endpoints();
        }
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
        audio_watcher_.stop();
        remove_tray_icon();
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcW(hwnd_, msg, wp, lp);
    }
}

void TrayApp::add_tray_icon() {
    if (current_icon_ == nullptr) {
        current_icon_ = create_dot_icon(state_.active_mask, recent_mask_, mic_muted_,
                                        native_icon_size());
    }
    nid_ = {};
    nid_.cbSize = sizeof(nid_);
    nid_.hWnd = hwnd_;
    nid_.uID = kTrayIconId;
    nid_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid_.uCallbackMessage = kMsgTrayIcon;
    nid_.hIcon =
        (current_icon_ != nullptr) ? current_icon_ : LoadIconW(nullptr, IDI_APPLICATION);
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

    const auto snap = take_process_snapshot();
    const uint64_t now_ft = current_filetime();
    auto wasapi_records = audio_watcher_.collect_records(snap.name_by_pid, now_ft);
    records.insert(records.end(), std::make_move_iterator(wasapi_records.begin()),
                   std::make_move_iterator(wasapi_records.end()));

    state_ = build_indicator_state(std::move(records), snap.alive_lower, 8);
    mic_muted_ = is_mic_muted().value_or(false);

    const int64_t now_unix = filetime_to_unix_seconds(now_ft);
    recent_mask_ = recent_grace_mask(state_, now_unix);

    // Run 15s fallback timer only while active or in 5m post-use grace window (0.00% idle CPU).
    const bool need_timer = !state_.active.empty() || recent_mask_ != SensorKind::None;
    if (need_timer && !timer_active_) {
        SetTimer(hwnd_, kTimerActiveCheck, 15000, nullptr);
        timer_active_ = true;
    } else if (!need_timer && timer_active_) {
        KillTimer(hwnd_, kTimerActiveCheck);
        timer_active_ = false;
    }

    HICON next_icon =
        create_dot_icon(state_.active_mask, recent_mask_, mic_muted_, native_icon_size());
    if (next_icon != nullptr) {
        if (current_icon_ != nullptr) {
            DestroyIcon(current_icon_);
        }
        current_icon_ = next_icon;
        nid_.hIcon = current_icon_;
    }

    std::string tip = "privdot";
    if (!state_.active.empty()) {
        tip += " - " + format_app_label(state_.active[0].display_name) + " (" +
               sensor_label(state_.active[0].sensor) + ")";
        if (state_.active.size() > 1) {
            tip += " +" + std::to_string(state_.active.size() - 1) + " more";
        }
    } else if (recent_mask_ != SensorKind::None && !state_.recent.empty()) {
        const uint64_t latest =
            std::max(state_.recent[0].last_used_start_ft, state_.recent[0].last_used_stop_ft);
        tip += " - " + format_app_label(state_.recent[0].display_name) + " (" +
               sensor_label(state_.recent[0].sensor) + ", " +
               format_relative_time(filetime_to_unix_seconds(latest), now_unix) + ")";
    } else if (mic_muted_) {
        tip += " - mic silenced";
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

    MENUINFO mi{};
    mi.cbSize = sizeof(mi);
    mi.fMask = MIM_STYLE;
    mi.dwStyle = MNS_CHECKORBMP;
    SetMenuInfo(menu, &mi);

    const int64_t now_unix = filetime_to_unix_seconds(current_filetime());

    if (state_.active.empty()) {
        AppendMenuW(menu, MF_STRING | MF_DISABLED | MF_GRAYED, 0, L"No active sensors");
    } else {
        AppendMenuW(menu, MF_STRING | MF_DISABLED | MF_GRAYED, 0, L"Active right now");
        for (const auto& r : state_.active) {
            const int64_t start_unix = filetime_to_unix_seconds(r.last_used_start_ft);
            const std::string line = format_app_label(r.display_name) + "\tactive \xc2\xb7 " +
                                     format_relative_time(start_unix, now_unix);
            append_badge_item(menu, utf8_to_wide(line), badge_for_sensor(r.sensor));
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
            std::string line = format_app_label(r.display_name) + "\t" +
                               format_relative_time(latest_unix, now_unix);
            if (r.last_used_stop_ft > r.last_used_start_ft &&
                (r.last_used_stop_ft - r.last_used_start_ft) >= kTicksPerSecond) {
                line += " \xc2\xb7 " + format_duration(r.last_used_start_ft, r.last_used_stop_ft);
            }
            append_badge_item(menu, utf8_to_wide(line), badge_for_sensor(r.sensor));
        }
    }

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | MF_DISABLED | MF_GRAYED, 0,
                L"Orange: mic \u00b7 Green: camera \u00b7 Blue: location");
    AppendMenuW(menu, MF_STRING | (mic_muted_ ? MF_CHECKED : MF_UNCHECKED), kCmdToggleMute,
                L"Silence microphone");
    AppendMenuW(menu, MF_STRING | (show_location_ ? MF_CHECKED : MF_UNCHECKED), kCmdToggleLocation,
                L"Show location activity");
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
