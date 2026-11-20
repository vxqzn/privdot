#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace privdot {

enum class SensorKind : uint8_t {
    None = 0,
    Mic = 1 << 0,     // 0b001 (1)
    Cam = 1 << 1,     // 0b010 (2)
    Location = 1 << 2 // 0b100 (4)
};

constexpr SensorKind operator|(SensorKind a, SensorKind b) noexcept {
    return static_cast<SensorKind>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

constexpr SensorKind operator&(SensorKind a, SensorKind b) noexcept {
    return static_cast<SensorKind>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}

constexpr SensorKind& operator|=(SensorKind& a, SensorKind b) noexcept {
    a = a | b;
    return a;
}

constexpr bool has_sensor(SensorKind mask, SensorKind flag) noexcept {
    return static_cast<uint8_t>(mask & flag) != 0;
}

struct AccessRecord {
    SensorKind sensor{SensorKind::None};
    std::string raw_key;
    std::string display_name;
    uint64_t last_used_start_ft{0};
    uint64_t last_used_stop_ft{0};
    bool is_packaged{false};
};

struct IndicatorState {
    SensorKind active_mask{SensorKind::None};
    std::vector<AccessRecord> active;
    std::vector<AccessRecord> recent;
};

} // namespace privdot
