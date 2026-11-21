#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <shellapi.h>

#include "../core/sensor_types.hpp"
#include "consent_watcher.hpp"

namespace privdot::win32 {

class TrayApp {
  public:
    TrayApp() = default;
    ~TrayApp();

    TrayApp(const TrayApp&) = delete;
    TrayApp& operator=(const TrayApp&) = delete;

    bool init(HINSTANCE instance);
    int run_message_loop();

  private:
    static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT handle_message(UINT msg, WPARAM wp, LPARAM lp);

    void add_tray_icon();
    void remove_tray_icon();
    void refresh_state();
    void show_menu();
    HBITMAP badge_for_sensor(SensorKind s) const;

    HINSTANCE instance_{nullptr};
    HWND hwnd_{nullptr};
    NOTIFYICONDATAW nid_{};
    HICON current_icon_{nullptr};
    HBITMAP badge_mic_{nullptr};
    HBITMAP badge_cam_{nullptr};
    HBITMAP badge_loc_{nullptr};
    UINT taskbar_created_msg_{0};

    ConsentWatcher watcher_;
    IndicatorState state_;
    SensorKind recent_mask_{SensorKind::None};
    bool mic_muted_{false};
    bool show_location_{true};
    bool timer_active_{false};
};

} // namespace privdot::win32
