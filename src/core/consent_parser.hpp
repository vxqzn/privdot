#pragma once

#include "sensor_types.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace privdot {

// Windows FILETIME: 100-nanosecond ticks since 1601-01-01 00:00:00 UTC.
inline constexpr uint64_t kTicksPerSecond = 10'000'000ULL;
inline constexpr uint64_t kUnixEpochFileTime = 116'444'736'000'000'000ULL;
inline constexpr int64_t kRecentGraceSeconds = 300;

std::string to_lower_ascii(std::string_view input);
std::string decode_nonpackaged_path(std::string_view key_name);
std::string extract_display_name(std::string_view key_name, bool is_packaged);
std::string format_app_label(std::string_view display_name);
int64_t filetime_to_unix_seconds(uint64_t filetime);

bool is_record_active(const AccessRecord& record, bool process_alive);
std::string format_relative_time(int64_t event_unix_sec, int64_t now_unix_sec);
std::string format_duration(uint64_t start_ft, uint64_t stop_ft);
SensorKind recent_grace_mask(const IndicatorState& state, int64_t now_unix_sec,
                             int64_t window_sec = kRecentGraceSeconds);
IndicatorState
build_indicator_state(std::vector<AccessRecord> records,
                      const std::unordered_set<std::string>& alive_process_names_lower,
                      size_t max_recent = 12);

} // namespace privdot
