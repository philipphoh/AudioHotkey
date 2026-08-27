#include "App.h"

#include <commctrl.h>
#include <shellapi.h>
#include <windows.h>

#include <string_view>

namespace {

constexpr wchar_t kMutexName[] = L"Local\\AudioHotkey.Singleton.v1";
constexpr wchar_t kReadyEventName[] = L"Local\\AudioHotkey.Ready.v1";

bool HasBackgroundArgument() {
    int argumentCount = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (arguments == nullptr) {
        return false;
    }
    bool background = false;
    for (int index = 1; index < argumentCount; ++index) {
        if (_wcsicmp(arguments[index], L"--background") == 0) {
            background = true;
            break;
        }
    }
    LocalFree(arguments);
    return background;
}
void ActivateExistingInstance(const bool background, const HANDLE readyEvent) {
    if (background) {
        return;
    }
    if (readyEvent != nullptr) {
        WaitForSingleObject(readyEvent, 4000);
    }
    const HWND window = FindWindowW(audiohotkey::App::kWindowClassName, nullptr);
    if (window == nullptr) {
        return;
    }
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (processId != 0) {
        AllowSetForegroundWindow(processId);
    }
    const UINT message = RegisterWindowMessageW(audiohotkey::App::kShowMessageName);
    if (message != 0) {
        PostMessageW(window, message, 0, 0);
    }
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    const bool background = HasBackgroundArgument();

    HANDLE singletonMutex = CreateMutexW(nullptr, FALSE, kMutexName);
    if (singletonMutex == nullptr) {
        return 1;
    }
    const bool alreadyRunning = GetLastError() == ERROR_ALREADY_EXISTS;
    HANDLE readyEvent = CreateEventW(nullptr, TRUE, FALSE, kReadyEventName);
    if (alreadyRunning) {
        ActivateExistingInstance(background, readyEvent);
        if (readyEvent != nullptr) {
            CloseHandle(readyEvent);
        }
        CloseHandle(singletonMutex);
        return 0;
    }

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES};
    InitCommonControlsEx(&controls);

    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(comResult)) {
        if (readyEvent != nullptr) {
            SetEvent(readyEvent);
            CloseHandle(readyEvent);
        }
        CloseHandle(singletonMutex);
        return 1;
    }

    int exitCode = 1;
    {
        audiohotkey::App app(instance);
        if (app.Initialize(!background, readyEvent)) {
            exitCode = app.RunMessageLoop();
        } else if (readyEvent != nullptr) {
            SetEvent(readyEvent);
        }
        app.Shutdown();
    }

    CoUninitialize();
    if (readyEvent != nullptr) {
        CloseHandle(readyEvent);
    }
    CloseHandle(singletonMutex);
    return exitCode;
}
