#include "consent_parser.hpp"
#include <algorithm>
#include <utility>

namespace privdot {

std::string to_lower_ascii(std::string_view input) {
    std::string out(input);
    for (char& ch : out) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return out;
}

std::string decode_nonpackaged_path(std::string_view key_name) {
    std::string out(key_name);
    for (char& ch : out) {
        if (ch == '#') {
            ch = '\\';
        }
    }
    return out;
}

std::string extract_display_name(std::string_view key_name, bool is_packaged) {
    if (!is_packaged) {
        std::string decoded = decode_nonpackaged_path(key_name);
        const size_t sep = decoded.find_last_of("\\/");
        return (sep == std::string::npos) ? decoded : decoded.substr(sep + 1);
    }

    // UWP package key format: "Microsoft.WindowsCamera_8wekyb3d8bbwe"
    std::string_view name = key_name;
    if (const size_t under = name.rfind('_'); under != std::string_view::npos) {
        name = name.substr(0, under); // -> "Microsoft.WindowsCamera"
    }
    if (const size_t dot = name.rfind('.');
        dot != std::string_view::npos && dot + 1 < name.size()) {
        name = name.substr(dot + 1); // -> "WindowsCamera"
    }
    return std::string(name);
}

int64_t filetime_to_unix_seconds(uint64_t filetime) {
    if (filetime < kUnixEpochFileTime) {
        return 0;
    }
    return static_cast<int64_t>((filetime - kUnixEpochFileTime) / kTicksPerSecond);
}

bool is_record_active(const AccessRecord& record, bool process_alive) {
    if (!process_alive || record.last_used_start_ft == 0) {
        return false;
    }
    return record.last_used_stop_ft == 0 || record.last_used_stop_ft < record.last_used_start_ft;
}

std::string format_relative_time(int64_t event_unix_sec, int64_t now_unix_sec) {
    const int64_t diff = (now_unix_sec > event_unix_sec) ? (now_unix_sec - event_unix_sec) : 0;
    if (diff < 60) {
        return "just now";
    }
    if (diff < 3600) {
        return std::to_string(diff / 60) + "m ago";
    }
    if (diff < 86400) {
        return std::to_string(diff / 3600) + "h ago";
    }
    return std::to_string(diff / 86400) + "d ago";
}

std::string format_duration(uint64_t start_ft, uint64_t stop_ft) {
    if (stop_ft <= start_ft) {
        return "0s";
    }
    const uint64_t total_sec = (stop_ft - start_ft) / kTicksPerSecond;
    if (total_sec < 60) {
        return std::to_string(total_sec) + "s";
    }

    auto two_digits = [](uint64_t val) { return (val < 10 ? "0" : "") + std::to_string(val); };

    if (total_sec < 3600) {
        const uint64_t mins = total_sec / 60;
        const uint64_t secs = total_sec % 60;
        return std::to_string(mins) + "m " + two_digits(secs) + "s";
    }
    const uint64_t hrs = total_sec / 3600;
    const uint64_t mins = (total_sec % 3600) / 60;
    return std::to_string(hrs) + "h " + two_digits(mins) + "m";
}

IndicatorState
build_indicator_state(std::vector<AccessRecord> records,
                      const std::unordered_set<std::string>& alive_process_names_lower,
                      size_t max_recent) {
    IndicatorState state;

    for (auto& rec : records) {
        if (rec.last_used_start_ft == 0) {
            continue;
        }
        if (rec.display_name.empty()) {
            rec.display_name = extract_display_name(rec.raw_key, rec.is_packaged);
        }

        // UWP package names don't map 1:1 to .exe names, so trust ConsentStore for packaged apps.
        const bool alive =
            rec.is_packaged
                ? true
                : (alive_process_names_lower.count(to_lower_ascii(rec.display_name)) > 0);

        if (is_record_active(rec, alive)) {
            state.active_mask |= rec.sensor;
            state.active.push_back(std::move(rec));
        } else {
            state.recent.push_back(std::move(rec));
        }
    }

    std::sort(state.active.begin(), state.active.end(),
              [](const AccessRecord& a, const AccessRecord& b) {
                  return a.last_used_start_ft > b.last_used_start_ft;
              });

    auto last_activity = [](const AccessRecord& r) {
        return std::max(r.last_used_start_ft, r.last_used_stop_ft);
    };
    std::sort(state.recent.begin(), state.recent.end(),
              [&](const AccessRecord& a, const AccessRecord& b) {
                  return last_activity(a) > last_activity(b);
              });

    if (state.recent.size() > max_recent) {
        state.recent.resize(max_recent);
    }

    return state;
}

} // namespace privdot
