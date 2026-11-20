CXX       ?= g++
CXXFLAGS  := -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -Isrc
SANFLAGS  := -g -fsanitize=address,undefined -fno-omit-frame-pointer

WIN_CXX   := x86_64-w64-mingw32-g++
WIN_FLAGS := -std=c++17 -Os -s -Wall -Wextra -Wpedantic -Werror -Isrc -static

BUILD     := build
TEST_BIN  := $(BUILD)/test_core
APP_BIN   := $(BUILD)/privdot.exe
SRCS      := $(wildcard src/core/*.hpp src/core/*.cpp src/win32/*.hpp src/win32/*.cpp tests/*.cpp)

.PHONY: test win format format-check clean

$(BUILD):
	mkdir -p $(BUILD)

$(TEST_BIN): src/core/consent_parser.cpp src/core/consent_parser.hpp src/core/sensor_types.hpp tests/test_core.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(SANFLAGS) src/core/consent_parser.cpp tests/test_core.cpp -o $(TEST_BIN)

$(APP_BIN): src/core/consent_parser.cpp src/core/consent_parser.hpp src/core/sensor_types.hpp src/win32/consent_reader.cpp src/win32/consent_reader.hpp src/win32/consent_watcher.cpp src/win32/consent_watcher.hpp src/win32/audio_mute.cpp src/win32/audio_mute.hpp src/win32/tray_app.cpp src/win32/tray_app.hpp src/win32/main.cpp | $(BUILD)
	$(WIN_CXX) $(WIN_FLAGS) -mwindows src/core/consent_parser.cpp src/win32/consent_reader.cpp src/win32/consent_watcher.cpp src/win32/audio_mute.cpp src/win32/tray_app.cpp src/win32/main.cpp -ladvapi32 -lole32 -lshell32 -luser32 -lgdi32 -o $(APP_BIN)

test: $(TEST_BIN)
	./$(TEST_BIN)

win: $(APP_BIN)

format:
	clang-format -i $(SRCS)

format-check:
	clang-format --dry-run --Werror $(SRCS)

clean:
	rm -rf $(BUILD)
