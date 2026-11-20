#pragma once

#include <optional>

namespace privdot::win32 {

// Returns current default microphone mute state, or std::nullopt if no capture device exists.
std::optional<bool> is_mic_muted();

// Toggles default microphone mute and returns the new state, or std::nullopt on failure.
std::optional<bool> toggle_mic_mute();

} // namespace privdot::win32
