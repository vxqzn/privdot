CXX       ?= g++
CXXFLAGS  := -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror
SANFLAGS  := -g -fsanitize=address,undefined -fno-omit-frame-pointer

WIN_CXX   := x86_64-w64-mingw32-g++
WINDRES   := x86_64-w64-mingw32-windres
WIN_FLAGS := -std=c++17 -Os -s -Wall -Wextra -Wpedantic -Werror -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 -ffunction-sections -fdata-sections -static -Wl,--gc-sections

BUILD     := build
TEST_BIN  := $(BUILD)/test_core
APP_RES   := $(BUILD)/privdot.res
APP_BIN   := $(BUILD)/privdot.exe

.PHONY: test win clean

$(BUILD):
	mkdir -p $(BUILD)

$(TEST_BIN): src/core/consent_parser.cpp src/core/consent_parser.hpp src/core/sensor_types.hpp tests/test_core.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(SANFLAGS) src/core/consent_parser.cpp tests/test_core.cpp -o $(TEST_BIN)

$(APP_RES): src/win32/privdot.rc src/win32/privdot.ico | $(BUILD)
	$(WINDRES) src/win32/privdot.rc -O coff -o $(APP_RES)

$(APP_BIN): src/core/consent_parser.cpp src/core/consent_parser.hpp src/core/sensor_types.hpp src/win32/consent_reader.cpp src/win32/consent_reader.hpp src/win32/consent_watcher.cpp src/win32/consent_watcher.hpp src/win32/audio_mute.cpp src/win32/audio_mute.hpp src/win32/audio_watcher.cpp src/win32/audio_watcher.hpp src/win32/tray_app.cpp src/win32/tray_app.hpp src/win32/main.cpp $(APP_RES) | $(BUILD)
	$(WIN_CXX) $(WIN_FLAGS) -mwindows src/core/consent_parser.cpp src/win32/consent_reader.cpp src/win32/consent_watcher.cpp src/win32/audio_mute.cpp src/win32/audio_watcher.cpp src/win32/tray_app.cpp src/win32/main.cpp $(APP_RES) -ladvapi32 -lole32 -lshell32 -luser32 -lgdi32 -o $(APP_BIN)

test: $(TEST_BIN)
	./$(TEST_BIN)

win: $(APP_BIN)

clean:
	rm -rf $(BUILD)
