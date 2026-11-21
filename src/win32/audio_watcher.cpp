#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <mmdeviceapi.h>
#include <audiopolicy.h>

#include "../core/consent_parser.hpp"
#include "audio_watcher.hpp"

#include <atomic>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace privdot::win32 {

namespace {

// Official Windows Core Audio COM GUIDs (avoids MSVC-only __uuidof under -Wpedantic).
constexpr GUID kIidIUnknown = {
    0x00000000, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
constexpr GUID kClsidMMDeviceEnumerator = {
    0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
constexpr GUID kIidIMMDeviceEnumerator = {
    0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
constexpr GUID kIidIMMNotificationClient = {
    0x7991EEC9, 0x7E89, 0x4D85, {0x83, 0x90, 0x6C, 0x70, 0x3C, 0xEC, 0x60, 0xC0}};
constexpr GUID kIidIAudioSessionManager2 = {
    0x77AA99A0, 0x1BD6, 0x484F, {0x8B, 0xC7, 0x2C, 0x65, 0x4C, 0x9A, 0x9B, 0x6F}};
constexpr GUID kIidIAudioSessionControl2 = {
    0xBFB7FF88, 0x7239, 0x4FC9, {0x8F, 0xA2, 0x07, 0xC9, 0x50, 0xBE, 0x9C, 0x6D}};
constexpr GUID kIidIAudioSessionNotification = {
    0x641DD20B, 0x4D41, 0x49CC, {0xAB, 0xA3, 0x17, 0x4B, 0x94, 0x77, 0xBB, 0x08}};
constexpr GUID kIidIAudioSessionEvents = {
    0x24918ACC, 0x64B3, 0x37C1, {0x8C, 0xA9, 0x74, 0xA6, 0x6E, 0x99, 0x57, 0xA8}};

bool same_guid(REFIID a, const GUID& b) {
    return std::memcmp(&a, &b, sizeof(GUID)) == 0;
}

template <typename T> struct ComPtr {
    T* ptr{nullptr};
    ~ComPtr() {
        if (ptr != nullptr) {
            ptr->Release();
        }
    }
    ComPtr() = default;
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
};

class EndpointNotification final : public IMMNotificationClient {
  public:
    EndpointNotification(HWND hwnd, UINT msg) : hwnd_(hwnd), msg_(msg) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (ppv == nullptr) {
            return E_POINTER;
        }
        if (same_guid(riid, kIidIUnknown) || same_guid(riid, kIidIMMNotificationClient)) {
            *ppv = static_cast<IMMNotificationClient*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(ref_.fetch_add(1, std::memory_order_relaxed) + 1);
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const LONG next = ref_.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (next == 0) {
            delete this;
        }
        return static_cast<ULONG>(next);
    }

    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override {
        PostMessageW(hwnd_, msg_, 1, 0);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override {
        PostMessageW(hwnd_, msg_, 1, 0);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override {
        PostMessageW(hwnd_, msg_, 1, 0);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole, LPCWSTR) override {
        if (flow == eCapture) {
            PostMessageW(hwnd_, msg_, 1, 0);
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override {
        return S_OK;
    }

  private:
    ~EndpointNotification() = default;
    std::atomic<LONG> ref_{1};
    HWND hwnd_;
    UINT msg_;
};

class SessionNotification final : public IAudioSessionNotification {
  public:
    SessionNotification(HWND hwnd, UINT msg) : hwnd_(hwnd), msg_(msg) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (ppv == nullptr) {
            return E_POINTER;
        }
        if (same_guid(riid, kIidIUnknown) || same_guid(riid, kIidIAudioSessionNotification)) {
            *ppv = static_cast<IAudioSessionNotification*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(ref_.fetch_add(1, std::memory_order_relaxed) + 1);
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const LONG next = ref_.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (next == 0) {
            delete this;
        }
        return static_cast<ULONG>(next);
    }

    HRESULT STDMETHODCALLTYPE OnSessionCreated(IAudioSessionControl*) override {
        PostMessageW(hwnd_, msg_, 1, 0);
        return S_OK;
    }

  private:
    ~SessionNotification() = default;
    std::atomic<LONG> ref_{1};
    HWND hwnd_;
    UINT msg_;
};

class SessionEvents final : public IAudioSessionEvents {
  public:
    SessionEvents(HWND hwnd, UINT msg) : hwnd_(hwnd), msg_(msg) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (ppv == nullptr) {
            return E_POINTER;
        }
        if (same_guid(riid, kIidIUnknown) || same_guid(riid, kIidIAudioSessionEvents)) {
            *ppv = static_cast<IAudioSessionEvents*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(ref_.fetch_add(1, std::memory_order_relaxed) + 1);
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const LONG next = ref_.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (next == 0) {
            delete this;
        }
        return static_cast<ULONG>(next);
    }

    HRESULT STDMETHODCALLTYPE OnDisplayNameChanged(LPCWSTR, LPCGUID) override {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnIconPathChanged(LPCWSTR, LPCGUID) override {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnSimpleVolumeChanged(float, BOOL, LPCGUID) override {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnChannelVolumeChanged(DWORD, float[], DWORD, LPCGUID) override {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnGroupingParamChanged(LPCGUID, LPCGUID) override {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnStateChanged(AudioSessionState) override {
        PostMessageW(hwnd_, msg_, 0, 0);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnSessionDisconnected(AudioSessionDisconnectReason) override {
        PostMessageW(hwnd_, msg_, 1, 0);
        return S_OK;
    }

  private:
    ~SessionEvents() = default;
    std::atomic<LONG> ref_{1};
    HWND hwnd_;
    UINT msg_;
};

} // namespace

AudioWatcher::~AudioWatcher() {
    stop();
}

bool AudioWatcher::start(HWND hwnd, UINT msg) {
    if (enumerator_ != nullptr) {
        return true;
    }

    hwnd_ = hwnd;
    msg_ = msg;

    const HRESULT co_hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    com_initialized_ = SUCCEEDED(co_hr);
    if (FAILED(co_hr) && co_hr != RPC_E_CHANGED_MODE) {
        return false;
    }

    const HRESULT hr =
        CoCreateInstance(kClsidMMDeviceEnumerator, nullptr, CLSCTX_ALL, kIidIMMDeviceEnumerator,
                         reinterpret_cast<void**>(&enumerator_));
    if (FAILED(hr) || enumerator_ == nullptr) {
        stop();
        return false;
    }

    endpoint_cb_ = new EndpointNotification(hwnd_, msg_);
    enumerator_->RegisterEndpointNotificationCallback(endpoint_cb_);
    session_cb_ = new SessionNotification(hwnd_, msg_);
    event_cb_ = new SessionEvents(hwnd_, msg_);

    rescan_endpoints();
    return true;
}

void AudioWatcher::release_sessions() {
    for (IAudioSessionControl* ctrl : controls_) {
        if (ctrl != nullptr) {
            if (event_cb_ != nullptr) {
                ctrl->UnregisterAudioSessionNotification(event_cb_);
            }
            ctrl->Release();
        }
    }
    controls_.clear();

    for (IAudioSessionManager2* mgr : managers_) {
        if (mgr != nullptr) {
            if (session_cb_ != nullptr) {
                mgr->UnregisterSessionNotification(session_cb_);
            }
            mgr->Release();
        }
    }
    managers_.clear();
}

void AudioWatcher::stop() {
    release_sessions();

    if (enumerator_ != nullptr) {
        if (endpoint_cb_ != nullptr) {
            enumerator_->UnregisterEndpointNotificationCallback(endpoint_cb_);
        }
        enumerator_->Release();
        enumerator_ = nullptr;
    }

    if (endpoint_cb_ != nullptr) {
        endpoint_cb_->Release();
        endpoint_cb_ = nullptr;
    }
    if (session_cb_ != nullptr) {
        session_cb_->Release();
        session_cb_ = nullptr;
    }
    if (event_cb_ != nullptr) {
        event_cb_->Release();
        event_cb_ = nullptr;
    }

    if (com_initialized_) {
        CoUninitialize();
        com_initialized_ = false;
    }
    hwnd_ = nullptr;
    msg_ = 0;
}

void AudioWatcher::rescan_endpoints() {
    release_sessions();
    if (enumerator_ == nullptr) {
        return;
    }

    ComPtr<IMMDeviceCollection> devices;
    if (FAILED(enumerator_->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &devices.ptr)) ||
        devices.ptr == nullptr) {
        return;
    }

    UINT dev_count = 0;
    if (FAILED(devices.ptr->GetCount(&dev_count))) {
        return;
    }

    for (UINT d = 0; d < dev_count; ++d) {
        ComPtr<IMMDevice> dev;
        if (FAILED(devices.ptr->Item(d, &dev.ptr)) || dev.ptr == nullptr) {
            continue;
        }

        IAudioSessionManager2* mgr = nullptr;
        if (FAILED(dev.ptr->Activate(kIidIAudioSessionManager2, CLSCTX_ALL, nullptr,
                                     reinterpret_cast<void**>(&mgr))) ||
            mgr == nullptr) {
            continue;
        }
        if (session_cb_ != nullptr) {
            mgr->RegisterSessionNotification(session_cb_);
        }
        managers_.push_back(mgr);

        // WASAPI requires enumerating existing sessions before OnSessionCreated fires.
        ComPtr<IAudioSessionEnumerator> session_enum;
        if (FAILED(mgr->GetSessionEnumerator(&session_enum.ptr)) || session_enum.ptr == nullptr) {
            continue;
        }

        int session_count = 0;
        if (FAILED(session_enum.ptr->GetCount(&session_count))) {
            continue;
        }
        for (int s = 0; s < session_count; ++s) {
            IAudioSessionControl* ctrl = nullptr;
            if (FAILED(session_enum.ptr->GetSession(s, &ctrl)) || ctrl == nullptr) {
                continue;
            }
            if (event_cb_ != nullptr) {
                ctrl->RegisterAudioSessionNotification(event_cb_);
            }
            controls_.push_back(ctrl);
        }
    }
}

std::vector<AccessRecord>
AudioWatcher::collect_records(const std::unordered_map<uint32_t, std::string>& name_by_pid,
                              uint64_t now_ft) {
    std::unordered_map<std::string, std::string> active_now;

    for (IAudioSessionManager2* mgr : managers_) {
        if (mgr == nullptr) {
            continue;
        }
        ComPtr<IAudioSessionEnumerator> session_enum;
        if (FAILED(mgr->GetSessionEnumerator(&session_enum.ptr)) || session_enum.ptr == nullptr) {
            continue;
        }
        int session_count = 0;
        if (FAILED(session_enum.ptr->GetCount(&session_count))) {
            continue;
        }
        for (int s = 0; s < session_count; ++s) {
            ComPtr<IAudioSessionControl> ctrl;
            if (FAILED(session_enum.ptr->GetSession(s, &ctrl.ptr)) || ctrl.ptr == nullptr) {
                continue;
            }
            AudioSessionState state = AudioSessionStateInactive;
            if (FAILED(ctrl.ptr->GetState(&state)) || state != AudioSessionStateActive) {
                continue;
            }
            ComPtr<IAudioSessionControl2> ctrl2;
            if (FAILED(ctrl.ptr->QueryInterface(kIidIAudioSessionControl2,
                                                reinterpret_cast<void**>(&ctrl2.ptr))) ||
                ctrl2.ptr == nullptr) {
                continue;
            }
            if (ctrl2.ptr->IsSystemSoundsSession() == S_OK) {
                continue;
            }
            DWORD pid = 0;
            if (FAILED(ctrl2.ptr->GetProcessId(&pid)) || pid == 0) {
                continue;
            }
            const auto it = name_by_pid.find(static_cast<uint32_t>(pid));
            if (it == name_by_pid.end() || it->second.empty()) {
                continue;
            }
            active_now.emplace(to_lower_ascii(it->second), it->second);
        }
    }

    for (const auto& kv : active_now) {
        auto& entry = history_[kv.first];
        if (!entry.active) {
            entry.display_name = kv.second;
            entry.start_ft = now_ft;
            entry.stop_ft = 0;
            entry.active = true;
        } else if (entry.display_name.empty()) {
            entry.display_name = kv.second;
        }
    }

    for (auto& kv : history_) {
        SessionHistory& entry = kv.second;
        if (entry.active && active_now.find(kv.first) == active_now.end()) {
            entry.active = false;
            entry.stop_ft = now_ft;
        }
    }

    std::vector<AccessRecord> records;
    records.reserve(history_.size());
    for (const auto& kv : history_) {
        const SessionHistory& e = kv.second;
        records.push_back(AccessRecord{SensorKind::Mic, e.display_name, e.display_name, e.start_ft,
                                       e.stop_ft, false});
    }
    return records;
}

} // namespace privdot::win32
