#pragma once

#include "../core/sensor_types.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace privdot::win32 {

struct ProcessSnapshot {
    std::unordered_set<std::string> alive_lower;
    std::unordered_map<uint32_t, std::string> name_by_pid;
};

std::vector<AccessRecord> read_all_consent_records();
ProcessSnapshot take_process_snapshot();
uint64_t current_filetime();

} // namespace privdot::win32
