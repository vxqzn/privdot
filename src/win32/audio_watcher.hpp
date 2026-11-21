#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include "../core/sensor_types.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct IMMDeviceEnumerator;
struct IMMNotificationClient;
struct IAudioSessionManager2;
struct IAudioSessionNotification;
struct IAudioSessionControl;
struct IAudioSessionEvents;

namespace privdot::win32 {

// Event-driven WASAPI capture monitor (catches live mic streams omitted by ConsentStore).
class AudioWatcher {
  public:
    AudioWatcher() = default;
    ~AudioWatcher();

    AudioWatcher(const AudioWatcher&) = delete;
    AudioWatcher& operator=(const AudioWatcher&) = delete;

    bool start(HWND hwnd, UINT msg);
    void stop();
    void rescan_endpoints();
    std::vector<AccessRecord>
    collect_records(const std::unordered_map<uint32_t, std::string>& name_by_pid, uint64_t now_ft);

  private:
    struct SessionHistory {
        std::string display_name;
        uint64_t start_ft{0};
        uint64_t stop_ft{0};
        bool active{false};
    };

    void release_sessions();

    bool com_initialized_{false};
    HWND hwnd_{nullptr};
    UINT msg_{0};
    IMMDeviceEnumerator* enumerator_{nullptr};
    IMMNotificationClient* endpoint_cb_{nullptr};
    IAudioSessionNotification* session_cb_{nullptr};
    IAudioSessionEvents* event_cb_{nullptr};
    std::vector<IAudioSessionManager2*> managers_;
    std::vector<IAudioSessionControl*> controls_;
    std::unordered_map<std::string, SessionHistory> history_;
};

} // namespace privdot::win32
