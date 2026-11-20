# privdot

Lightweight native Win32 **C++17** privacy indicator for Windows: displays live **microphone**, **camera**, and **location** access in the system tray, tracks a **Recently used** app timeline in a native Windows 11 dark menu, and toggles hardware microphone mute in one click.

* **Zero external runtime dependencies:** pure Win32 API, COM Core Audio, and GDI/DIB icon rendering (`~220 KB` static MinGW build, `< 180 KB` MSVC build, `~2–4 MB` RAM).
* **0.00% idle CPU:** wakes on kernel registry events via `RegNotifyChangeKeyValue` and enables a 15s process-liveness timer only while a sensor is active.

---

## Architecture

```text
privdot/
├── Makefile                    # Linux/WSL unit tests + MinGW cross-build
├── compile_flags.txt           # Portable clangd flags (-std=c++17, -Isrc)
├── .github/workflows/ci.yml    # GitHub Actions (Linux tests + MSVC Windows build)
├── src/
│   ├── core/                   # Portable ISO C++17 parser & state machine
│   │   ├── sensor_types.hpp
│   │   ├── consent_parser.hpp
│   │   └── consent_parser.cpp
│   └── win32/                  # Native Windows adapter (tray, menu, audio, watcher)
│       ├── audio_mute.hpp/.cpp
│       ├── consent_reader.hpp/.cpp
│       ├── consent_watcher.hpp/.cpp
│       ├── tray_app.hpp/.cpp
│       └── main.cpp
└── tests/
    └── test_core.cpp           # Zero-dependency core unit tests
```

---

## Build & Run (Fresh Clone in WSL2 / Linux)

```bash
# 1. Core unit tests (GCC + ASan + UBSan) and formatting check
make test
make format-check

# 2. Cross-compile privdot.exe from WSL2 (Ubuntu/Debian)
sudo apt update && sudo apt install -y g++-mingw-w64-x86-64
make win
./build/privdot.exe &
```

### Optional: VS Code `clangd` Win32 IntelliSense in WSL2

Add these machine-local files (gitignored) so `clangd` resolves `<windows.h>` inside `src/win32/`:

* `.clangd`:
  ```yaml
  If:
    PathMatch: src/win32/.*
  CompileFlags:
    Compiler: /usr/bin/x86_64-w64-mingw32-g++
  ```
* `.vscode/settings.json`:
  ```json
  {
      "clangd.arguments": [
          "--query-driver=/usr/bin/x86_64-w64-mingw32-g++"
      ]
  }
  ```
