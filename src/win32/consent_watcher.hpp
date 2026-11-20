#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

namespace privdot::win32 {

// Watches HKCU ConsentStore keys on a dedicated Win32 thread (0% idle CPU)
// and posts `notify_msg` to `target_hwnd` whenever sensor state changes.
class ConsentWatcher {
  public:
    ConsentWatcher() = default;
    ~ConsentWatcher();

    ConsentWatcher(const ConsentWatcher&) = delete;
    ConsentWatcher& operator=(const ConsentWatcher&) = delete;

    bool start(HWND target_hwnd, UINT notify_msg);
    void stop();

  private:
    static DWORD WINAPI thread_proc(LPVOID param);
    void run();

    HWND target_hwnd_{nullptr};
    UINT notify_msg_{0};
    HANDLE stop_event_{nullptr};
    HANDLE thread_{nullptr};
};

} // namespace privdot::win32
