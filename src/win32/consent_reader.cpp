#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <tlhelp32.h>

#include "../core/consent_parser.hpp"
#include "consent_reader.hpp"

#include <cwchar>

namespace privdot::win32 {

namespace {

// RAII guards: destructor guarantees OS handles are always closed, even on early return.
struct RegKeyGuard {
    HKEY h{nullptr};
    RegKeyGuard() = default;
    explicit RegKeyGuard(HKEY key) : h(key) {}
    ~RegKeyGuard() {
        if (h != nullptr) {
            RegCloseKey(h);
        }
    }
    RegKeyGuard(const RegKeyGuard&) = delete;
    RegKeyGuard& operator=(const RegKeyGuard&) = delete;
};

struct HandleGuard {
    HANDLE h{INVALID_HANDLE_VALUE};
    explicit HandleGuard(HANDLE handle) : h(handle) {}
    ~HandleGuard() {
        if (h != nullptr && h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
        }
    }
    HandleGuard(const HandleGuard&) = delete;
    HandleGuard& operator=(const HandleGuard&) = delete;
};

std::string wide_to_utf8(const wchar_t* wstr) {
    if (!wstr || *wstr == L'\0') {
        return {};
    }
    const int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) {
        return {};
    }
    std::string out(static_cast<size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, out.data(), len, nullptr, nullptr);
    return out;
}

uint64_t read_reg_qword(HKEY key, const wchar_t* value_name) {
    uint64_t value = 0;
    DWORD type = 0;
    DWORD size = sizeof(value);
    const LSTATUS rc =
        RegQueryValueExW(key, value_name, nullptr, &type, reinterpret_cast<LPBYTE>(&value), &size);
    if (rc != ERROR_SUCCESS || type != REG_QWORD || size != sizeof(value)) {
        return 0;
    }
    return value;
}

void scan_subkeys(HKEY parent, SensorKind sensor, bool is_packaged,
                  std::vector<AccessRecord>& out) {
    for (DWORD i = 0;; ++i) {
        wchar_t name[1024];
        DWORD name_len = static_cast<DWORD>(sizeof(name) / sizeof(name[0]));
        const LSTATUS rc =
            RegEnumKeyExW(parent, i, name, &name_len, nullptr, nullptr, nullptr, nullptr);
        if (rc != ERROR_SUCCESS) {
            break;
        }

        RegKeyGuard child;
        if (RegOpenKeyExW(parent, name, 0, KEY_READ, &child.h) != ERROR_SUCCESS) {
            continue;
        }

        if (is_packaged && std::wcscmp(name, L"NonPackaged") == 0) {
            scan_subkeys(child.h, sensor, false, out);
            continue;
        }

        const uint64_t start = read_reg_qword(child.h, L"LastUsedTimeStart");
        if (start == 0) {
            continue;
        }
        const uint64_t stop = read_reg_qword(child.h, L"LastUsedTimeStop");
        out.push_back(AccessRecord{sensor, wide_to_utf8(name), {}, start, stop, is_packaged});
    }
}

} // namespace

std::vector<AccessRecord> read_all_consent_records() {
    struct Target {
        SensorKind sensor;
        const wchar_t* subkey;
    };

    const Target targets[] = {
        {SensorKind::Mic, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\"
                          L"ConsentStore\\microphone"},
        {SensorKind::Cam, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\"
                          L"ConsentStore\\webcam"},
        {SensorKind::Location,
         L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\"
         L"ConsentStore\\location"},
    };

    const HKEY roots[] = {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE};
    std::vector<AccessRecord> records;

    for (HKEY root : roots) {
        for (const auto& t : targets) {
            RegKeyGuard key;
            if (RegOpenKeyExW(root, t.subkey, 0, KEY_READ, &key.h) == ERROR_SUCCESS) {
                scan_subkeys(key.h, t.sensor, true, records);
            }
        }
    }
    return records;
}

ProcessSnapshot take_process_snapshot() {
    ProcessSnapshot result;
    HandleGuard snap(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (snap.h == INVALID_HANDLE_VALUE) {
        return result;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snap.h, &entry)) {
        return result;
    }
    do {
        std::string exe = wide_to_utf8(entry.szExeFile);
        if (!exe.empty()) {
            result.alive_lower.insert(to_lower_ascii(exe));
            result.name_by_pid.emplace(static_cast<uint32_t>(entry.th32ProcessID), std::move(exe));
        }
    } while (Process32NextW(snap.h, &entry));

    return result;
}

uint64_t current_filetime() {
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) |
           static_cast<uint64_t>(ft.dwLowDateTime);
}

} // namespace privdot::win32
