# privdot

Lightweight native Win32 **C++17** privacy indicator for Windows: displays live **microphone**, **camera**, and **location** access in the system tray, tracks a **Recently used** app timeline in a native Windows 11 dark menu, and toggles hardware microphone mute in one click.

* **Zero external runtime dependencies:** pure Win32 API, COM Core Audio, and GDI/DIB icon rendering (`~220 KB` static MinGW build, `< 180 KB` MSVC build, `~2–4 MB` RAM).
* **0.00% idle CPU:** wakes on kernel registry events via `RegNotifyChangeKeyValue` and enables a 15s process-liveness timer only while a sensor is active.

---

## Layout

```text
privdot/
├── .github/workflows/ci.yml    # GitHub Actions (Linux tests + MSVC Windows build)
├── .gitignore
├── Makefile                    # Linux/WSL unit tests + MinGW cross-build
├── README.md
├── src/
│   ├── core/                   # Portable ISO C++17 parser & state machine
│   └── win32/                  # Native Windows adapter (tray, menu, audio, watcher)
└── tests/
    └── test_core.cpp           # Zero-dependency core unit tests
```

---

## Build & Run (WSL2 / Linux)

```bash
# 1. Core unit tests (GCC + ASan + UBSan)
make test

# 2. Cross-compile privdot.exe from WSL2 (Ubuntu/Debian)
sudo apt update && sudo apt install -y g++-mingw-w64-x86-64
make win
./build/privdot.exe &
```
