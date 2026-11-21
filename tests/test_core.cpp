#include "../src/core/consent_parser.hpp"

#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

bool check_impl(bool cond, int line, const char* expr) {
    if (!cond) {
        std::cerr << "fail: line " << line << ": " << expr << '\n';
        return false;
    }
    return true;
}

} // namespace

#define CHECK(expr)                                                                                \
    if (!check_impl((expr), __LINE__, #expr))                                                      \
    return 1

int main() {
    CHECK(privdot::to_lower_ascii("Discord.EXE") == "discord.exe");

    CHECK(privdot::decode_nonpackaged_path("C:#Program Files#Discord#Discord.exe") ==
          "C:\\Program Files\\Discord\\Discord.exe");
    CHECK(privdot::decode_nonpackaged_path("plain.exe") == "plain.exe");

    CHECK(privdot::extract_display_name("C:#Program Files#Google#Chrome#chrome.exe", false) ==
          "chrome.exe");
    CHECK(privdot::extract_display_name("Microsoft.WindowsCamera_8wekyb3d8bbwe", true) ==
          "WindowsCamera");
    CHECK(privdot::extract_display_name("SoundRecorder_123abc", true) == "SoundRecorder");

    CHECK(privdot::format_app_label("Discord.exe") == "Discord");
    CHECK(privdot::format_app_label("OBS64.EXE") == "OBS64");
    CHECK(privdot::format_app_label("WindowsCamera") == "WindowsCamera");
    CHECK(privdot::format_app_label(".exe") == ".exe");

    CHECK(privdot::filetime_to_unix_seconds(0) == 0);
    CHECK(privdot::filetime_to_unix_seconds(privdot::kUnixEpochFileTime) == 0);
    const int64_t jan_2026 = 1'767'225'600LL; // 2026-01-01 00:00:00 UTC
    const uint64_t jan_2026_ft =
        privdot::kUnixEpochFileTime + static_cast<uint64_t>(jan_2026) * privdot::kTicksPerSecond;
    CHECK(privdot::filetime_to_unix_seconds(jan_2026_ft) == jan_2026);
    // 4. Active-session detection (including crashed ghost guard)
    const privdot::AccessRecord open_rec{
        privdot::SensorKind::Mic, "Discord.exe", "Discord.exe", 500, 0, false};
    CHECK(privdot::is_record_active(open_rec, true));
    CHECK(!privdot::is_record_active(open_rec, false)); // crashed ghost

    const privdot::AccessRecord restarted{privdot::SensorKind::Cam, "Cam", "Cam", 900, 400, true};
    CHECK(privdot::is_record_active(restarted, true));

    const privdot::AccessRecord stopped{
        privdot::SensorKind::Mic, "Teams.exe", "Teams.exe", 500, 800, false};
    CHECK(!privdot::is_record_active(stopped, true));

    // 5. Relative time & duration formatting
    const int64_t now = 1'700'000'000LL;
    CHECK(privdot::format_relative_time(now + 20, now) == "just now");
    CHECK(privdot::format_relative_time(now - 45, now) == "just now");
    CHECK(privdot::format_relative_time(now - 360, now) == "6m ago");
    CHECK(privdot::format_relative_time(now - 7'200, now) == "2h ago");
    CHECK(privdot::format_relative_time(now - 172'800, now) == "2d ago");

    static constexpr uint64_t base = privdot::kUnixEpochFileTime;
    CHECK(privdot::format_duration(base + 10 * privdot::kTicksPerSecond, base) == "0s");
    CHECK(privdot::format_duration(base, base + 12 * privdot::kTicksPerSecond) == "12s");
    CHECK(privdot::format_duration(base, base + 125 * privdot::kTicksPerSecond) == "2m 05s");
    CHECK(privdot::format_duration(base, base + 3'840 * privdot::kTicksPerSecond) == "1h 04m");

    // 6. Full state aggregation (active bitmask + ghost rejection + deduplication + sorting + cap)
    auto ft = [](uint64_t sec) { return base + sec * privdot::kTicksPerSecond; };
    std::vector<privdot::AccessRecord> records = {
        {privdot::SensorKind::Mic, "C:#Program Files#Discord#Discord.exe", "", ft(300), 0, false},
        {privdot::SensorKind::Mic, "C:#Users#Dennis#AppData#Local#Discord#app-1.0#Discord.exe", "",
         ft(50), ft(90), false},
        {privdot::SensorKind::Cam, "Microsoft.WindowsCamera_8wekyb3d8bbwe", "", ft(500), ft(100),
         true},
        {privdot::SensorKind::Mic, "C:#Program Files#OBS#obs64.exe", "", ft(400), 0, false},
        {privdot::SensorKind::Mic, "D:#Apps#OBS#obs64.exe", "", ft(150), ft(200), false},
        {privdot::SensorKind::Location, "C:#Windows#SearchHost.exe", "", ft(100), ft(250), false},
        {privdot::SensorKind::Location, "C:#Zoom#Zoom.exe", "", 0, 0, false},
    };
    const std::unordered_set<std::string> alive = {"discord.exe", "searchhost.exe"};

    const auto full = privdot::build_indicator_state(records, alive, 12);
    CHECK(privdot::has_sensor(full.active_mask, privdot::SensorKind::Mic));
    CHECK(privdot::has_sensor(full.active_mask, privdot::SensorKind::Cam));
    CHECK(!privdot::has_sensor(full.active_mask, privdot::SensorKind::Location));
    CHECK(full.active.size() == 2);
    CHECK(full.active[0].display_name == "WindowsCamera");
    CHECK(full.active[1].display_name == "Discord.exe");
    CHECK(full.recent.size() == 2);
    CHECK(full.recent[0].display_name == "obs64.exe");
    CHECK(full.recent[0].last_used_start_ft == ft(400));
    CHECK(full.recent[1].display_name == "SearchHost.exe");

    const auto capped = privdot::build_indicator_state(records, alive, 1);
    CHECK(capped.recent.size() == 1);
    CHECK(capped.recent[0].display_name == "obs64.exe");

    // 7. Recent grace window (5m post-use ring)
    const int64_t grace_now = privdot::filetime_to_unix_seconds(base) + 520; // obs64 stopped at 400 (120s ago), SearchHost at 250 (270s ago)
    const privdot::SensorKind grace_all = privdot::recent_grace_mask(full, grace_now, 300);
    CHECK(privdot::has_sensor(grace_all, privdot::SensorKind::Mic));
    CHECK(privdot::has_sensor(grace_all, privdot::SensorKind::Location));

    const int64_t grace_later = privdot::filetime_to_unix_seconds(base) + 600; // obs64 200s ago (in), SearchHost 350s ago (out)
    const privdot::SensorKind grace_partial = privdot::recent_grace_mask(full, grace_later, 300);
    CHECK(privdot::has_sensor(grace_partial, privdot::SensorKind::Mic));
    CHECK(!privdot::has_sensor(grace_partial, privdot::SensorKind::Location));

    const int64_t grace_expired = privdot::filetime_to_unix_seconds(base) + 801; // obs64 401s ago (out)
    CHECK(privdot::recent_grace_mask(full, grace_expired, 300) == privdot::SensorKind::None);

    std::cout << "ok: all core parser tests passed.\n";
    return 0;
}
