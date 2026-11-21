#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include "tray_app.hpp"

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int) {
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\privdot_single_instance");
    if (mutex != nullptr && GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex);
        return 0;
    }

    privdot::win32::TrayApp app;
    int exit_code = 0;
    if (app.init(instance)) {
        exit_code = app.run_message_loop();
    }

    if (mutex != nullptr) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    return exit_code;
}
