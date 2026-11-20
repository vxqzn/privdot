#pragma once

#include "core/sensor_types.hpp"

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace privdot::win32 {

std::vector<AccessRecord> read_all_consent_records();
std::unordered_set<std::string> get_alive_process_names_lower();
uint64_t current_filetime();

} // namespace privdot::win32
