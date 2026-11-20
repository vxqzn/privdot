#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <endpointvolume.h>
#include <mmdeviceapi.h>

#include "win32/audio_mute.hpp"

namespace privdot::win32 {

namespace {

// Official Windows Core Audio COM GUIDs (avoids MSVC-only __uuidof under -Wpedantic).
constexpr GUID kClsidMMDeviceEnumerator = {
    0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
constexpr GUID kIidIMMDeviceEnumerator = {
    0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
constexpr GUID kIidIAudioEndpointVolume = {
    0x5CDF2C82, 0x841E, 0x4546, {0x97, 0x22, 0x0C, 0xF7, 0x40, 0x78, 0x22, 0x9A}};

// Minimal RAII smart pointer for COM interfaces (calls Release() automatically).
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

struct ComScope {
    bool initialized{false};
    ComScope() {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        initialized = SUCCEEDED(hr);
    }
    ~ComScope() {
        if (initialized) {
            CoUninitialize();
        }
    }
};

bool open_default_mic(ComPtr<IAudioEndpointVolume>& out) {
    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr =
        CoCreateInstance(kClsidMMDeviceEnumerator, nullptr, CLSCTX_ALL, kIidIMMDeviceEnumerator,
                         reinterpret_cast<void**>(&enumerator.ptr));
    if (FAILED(hr) || enumerator.ptr == nullptr) {
        return false;
    }

    ComPtr<IMMDevice> device;
    hr = enumerator.ptr->GetDefaultAudioEndpoint(eCapture, eConsole, &device.ptr);
    if (FAILED(hr) || device.ptr == nullptr) {
        return false;
    }

    hr = device.ptr->Activate(kIidIAudioEndpointVolume, CLSCTX_ALL, nullptr,
                              reinterpret_cast<void**>(&out.ptr));
    return SUCCEEDED(hr) && out.ptr != nullptr;
}

} // namespace

std::optional<bool> is_mic_muted() {
    ComScope com;
    ComPtr<IAudioEndpointVolume> vol;
    if (!open_default_mic(vol)) {
        return std::nullopt;
    }
    BOOL muted = FALSE;
    if (FAILED(vol.ptr->GetMute(&muted))) {
        return std::nullopt;
    }
    return muted != FALSE;
}

std::optional<bool> toggle_mic_mute() {
    ComScope com;
    ComPtr<IAudioEndpointVolume> vol;
    if (!open_default_mic(vol)) {
        return std::nullopt;
    }
    BOOL muted = FALSE;
    if (FAILED(vol.ptr->GetMute(&muted))) {
        return std::nullopt;
    }
    const BOOL next = muted ? FALSE : TRUE;
    if (FAILED(vol.ptr->SetMute(next, nullptr))) {
        return std::nullopt;
    }
    return next != FALSE;
}

} // namespace privdot::win32
