# privdot

Native Win32 **C++17** system-tray sensor monitor for Windows 10/11 covering live **microphone**, **camera**, and **location** access, a 5-minute post-use grace ring, recent-activity timeline, and 1-click hardware microphone mute.

* **Footprint:** `238 KB` static MinGW binary (`-Os -s -Wl,--gc-sections`), zero external runtime DLLs (Win32, COM Core Audio, and GDI DIB rendering only).
* **Idle overhead:** `0.00%` idle CPU — wakes strictly on kernel registry notifications (`RegNotifyChangeKeyValue`) and WASAPI COM session/endpoint callbacks (`IAudioSessionManager2`, `IMMNotificationClient`). An adaptive 15 s liveness timer runs only while a sensor is active or within the 5-minute grace window.
* **Dual mic telemetry:** Merges Windows `CapabilityAccessManager\ConsentStore` (mic, camera, location, history) with live WASAPI `eCapture` sessions across all active capture endpoints to catch Win32/Electron apps (e.g., Discord) omitted by `ConsentStore`.

## Layout

```text
privdot/
├── .github/workflows/ci.yml    # GitHub Actions (Linux tests + MSVC Windows build)
├── .gitignore
├── Makefile                    # Core tests + static MinGW cross-build
├── README.md
├── src/
│   ├── core/                   # Portable ISO C++17 parser & state machine
│   └── win32/                  # Native Windows adapter (tray, audio_watcher.*, mute, consent)
└── tests/
    └── test_core.cpp           # Zero-dependency core unit tests
```

## Build & Install

```bash
make test                       # Core unit tests (GCC + ASan + UBSan)
make win                        # Static x64 binary -> build/privdot.exe
```

Install `build/privdot.exe` to `%LOCALAPPDATA%\privdot\privdot.exe` and launch directly from the Windows desktop session.
