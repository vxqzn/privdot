#include "win32/consent_watcher.hpp"

#include <vector>

namespace privdot::win32 {

ConsentWatcher::~ConsentWatcher() {
    stop();
}

bool ConsentWatcher::start(HWND target_hwnd, UINT notify_msg) {
    if (thread_ != nullptr) {
        return false;
    }
    target_hwnd_ = target_hwnd;
    notify_msg_ = notify_msg;
    stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (stop_event_ == nullptr) {
        return false;
    }
    thread_ = CreateThread(nullptr, 0, &ConsentWatcher::thread_proc, this, 0, nullptr);
    if (thread_ == nullptr) {
        CloseHandle(stop_event_);
        stop_event_ = nullptr;
        return false;
    }
    return true;
}

void ConsentWatcher::stop() {
    if (stop_event_ != nullptr) {
        SetEvent(stop_event_);
    }
    if (thread_ != nullptr) {
        WaitForSingleObject(thread_, INFINITE);
        CloseHandle(thread_);
        thread_ = nullptr;
    }
    if (stop_event_ != nullptr) {
        CloseHandle(stop_event_);
        stop_event_ = nullptr;
    }
}

DWORD WINAPI ConsentWatcher::thread_proc(LPVOID param) {
    auto* self = static_cast<ConsentWatcher*>(param);
    self->run();
    return 0;
}

void ConsentWatcher::run() {
    const wchar_t* paths[] = {
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\"
        L"ConsentStore\\microphone",
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\"
        L"ConsentStore\\webcam",
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\"
        L"ConsentStore\\location",
    };

    std::vector<HKEY> keys;
    std::vector<HANDLE> handles;
    std::vector<bool> armed;
    handles.push_back(stop_event_);

    for (const wchar_t* path : paths) {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_NOTIFY, &key) == ERROR_SUCCESS) {
            HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (ev != nullptr) {
                keys.push_back(key);
                handles.push_back(ev);
                armed.push_back(false);
            } else {
                RegCloseKey(key);
            }
        }
    }

    while (true) {
        for (size_t i = 0; i < keys.size(); ++i) {
            if (!armed[i]) {
                const LONG rc = RegNotifyChangeKeyValue(
                    keys[i], TRUE, REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_CHANGE_LAST_SET,
                    handles[i + 1], TRUE);
                if (rc == ERROR_SUCCESS) {
                    armed[i] = true;
                }
            }
        }

        const DWORD count = static_cast<DWORD>(handles.size());
        const DWORD wait = WaitForMultipleObjects(count, handles.data(), FALSE, INFINITE);
        if (wait == WAIT_OBJECT_0 || wait == WAIT_FAILED) {
            break;
        }
        if (wait > WAIT_OBJECT_0 && wait < WAIT_OBJECT_0 + count) {
            armed[wait - WAIT_OBJECT_0 - 1] = false;
            // Coalesce rapid multi-key writes (150ms), still waking instantly on stop_event_.
            if (WaitForSingleObject(stop_event_, 150) == WAIT_OBJECT_0) {
                break;
            }
            if (target_hwnd_ != nullptr) {
                PostMessageW(target_hwnd_, notify_msg_, 0, 0);
            }
        }
    }

    for (size_t i = 0; i < keys.size(); ++i) {
        RegCloseKey(keys[i]);
        CloseHandle(handles[i + 1]);
    }
}

} // namespace privdot::win32
