#include "App.h"

#include "CoreLogic.h"
#include "Localization.h"
#include "resource.h"

#include <commctrl.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <cwchar>
#include <utility>
#include <vector>

namespace audiohotkey {
namespace {

constexpr wchar_t kApplicationName[] = L"AudioHotkey";
constexpr wchar_t kRunKeyPath[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValueName[] = L"AudioHotkey";
constexpr int kLogicalClientWidth = 590;
constexpr int kLogicalClientHeight = 472;

int Scale(const int value, const UINT dpi) {
    return MulDiv(value, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
}

std::wstring ModulePath() {
    std::vector<wchar_t> buffer(512);
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return {};
        }
        if (length < buffer.size() - 1) {
            return std::wstring(buffer.data(), length);
        }
        buffer.resize(buffer.size() * 2);
    }
}

std::wstring Win32ErrorMessage(const DWORD code, const UiLanguage language) {
    wchar_t* raw = nullptr;
    const DWORD languageId = language == UiLanguage::English
                                 ? MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US)
                                 : 0;
    DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, languageId, reinterpret_cast<wchar_t*>(&raw), 0, nullptr);
    if (length == 0 && languageId != 0) {
        length = FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr, code, 0, reinterpret_cast<wchar_t*>(&raw), 0, nullptr);
    }
    std::wstring message = length != 0 && raw != nullptr
                               ? std::wstring(raw, length)
                               : (language == UiLanguage::English
                                      ? L"Unknown Windows error"
                                      : L"Unbekannter Windows-Fehler");
    if (raw != nullptr) {
        LocalFree(raw);
    }
    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' ')) {
        message.pop_back();
    }
    return message;
}

std::wstring HResultMessage(const HRESULT result, const UiLanguage language) {
    return Win32ErrorMessage(HRESULT_FACILITY(result) == FACILITY_WIN32
                                 ? HRESULT_CODE(result)
                                 : static_cast<DWORD>(result),
                             language);
}

void CopyText(wchar_t* destination, const size_t destinationCount, const std::wstring& source) {
    if (destinationCount == 0) {
        return;
    }
    wcsncpy_s(destination, destinationCount, source.c_str(), _TRUNCATE);
}

HMENU ControlId(const int id) {
    return reinterpret_cast<HMENU>(static_cast<INT_PTR>(id));
}

}  // namespace

App::App(const HINSTANCE instance)
    : instance_(instance), settingsStore_(SettingsStore::DefaultPath()) {}

App::~App() {
    Shutdown();
}

bool App::Initialize(const bool showWindow, const HANDLE readyEvent) {
    taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");
    showWindowMessage_ = RegisterWindowMessageW(kShowMessageName);
    settings_ = settingsStore_.Load();
    displayLanguage_ = settings_.language;

    audioService_ = new AudioEndpointService();
    const HRESULT audioResult = audioService_->Initialize();
    if (FAILED(audioResult)) {
        MessageBoxW(nullptr,
                    ((displayLanguage_ == UiLanguage::English
                          ? L"The Windows audio devices could not be opened.\n\n"
                          : L"Die Windows-Audiogeräte konnten nicht geöffnet werden.\n\n") +
                     HResultMessage(audioResult, displayLanguage_)).c_str(),
                    kApplicationName, MB_OK | MB_ICONERROR);
        return false;
    }

    neutralIcon_ = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(IDI_APP_ICON),
                                                 IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR));
    if (neutralIcon_ == nullptr) {
        neutralIcon_ = CopyIcon(LoadIconW(nullptr, IDI_APPLICATION));
    }
    endpointAIcon_ = neutralIcon_ != nullptr ? CopyIcon(neutralIcon_) : nullptr;
    endpointBIcon_ = neutralIcon_ != nullptr ? CopyIcon(neutralIcon_) : nullptr;

    if (!RegisterWindowClass() || !CreateMainWindow()) {
        return false;
    }
    audioService_->SetNotificationTarget(window_, kAudioChangedMessage);
    AddTrayIcon();

    std::wstring hotkeyError;
    if (!UpdateRegisteredHotkey(settings_.hotkeyModifiers, settings_.hotkeyVirtualKey,
                                displayLanguage_, hotkeyError)) {
        hotkeyRegistrationError_ = std::move(hotkeyError);
    }
    if (settings_.autoStart) {
        (void)ConfigureAutoStart(true, displayLanguage_, autoStartError_);
    }

    RefreshEndpointControls(false);
    LoadSettingsIntoControls();
    UpdateStatus();

    if (readyEvent != nullptr) {
        SetEvent(readyEvent);
    }
    if (showWindow) {
        ShowSettingsWindow();
    } else {
        // Consume a possible STARTUPINFO show-state while intentionally hidden.
        // This ensures that a later explicit user request can always show the window.
        ShowWindow(window_, SW_HIDE);
    }
    return true;
}

int App::RunMessageLoop() {
    MSG message{};
    while (true) {
        const BOOL result = GetMessageW(&message, nullptr, 0, 0);
        if (result == 0) {
            return static_cast<int>(message.wParam);
        }
        if (result == -1) {
            return 1;
        }
        if (window_ == nullptr || !IsDialogMessageW(window_, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
}

void App::Shutdown() noexcept {
    if (shuttingDown_) {
        return;
    }
    shuttingDown_ = true;

    UnregisterCurrentHotkey();
    RemoveTrayIcon();
    if (audioService_ != nullptr) {
        audioService_->SetNotificationTarget(nullptr, 0);
        audioService_->Shutdown();
        audioService_->Release();
        audioService_ = nullptr;
    }
    if (window_ != nullptr && IsWindow(window_)) {
        DestroyWindow(window_);
    }
    window_ = nullptr;
    if (uiFont_ != nullptr) {
        DeleteObject(uiFont_);
        uiFont_ = nullptr;
    }
    if (neutralIcon_ != nullptr) {
        DestroyIcon(neutralIcon_);
        neutralIcon_ = nullptr;
    }
    if (endpointAIcon_ != nullptr) {
        DestroyIcon(endpointAIcon_);
        endpointAIcon_ = nullptr;
    }
    if (endpointBIcon_ != nullptr) {
        DestroyIcon(endpointBIcon_);
        endpointBIcon_ = nullptr;
    }
}

LRESULT CALLBACK App::StaticWindowProcedure(const HWND window, const UINT message,
                                            const WPARAM wParam, const LPARAM lParam) {
    App* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        app = static_cast<App*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        app->window_ = window;
    }
    return app != nullptr ? app->WindowProcedure(message, wParam, lParam)
                          : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT CALLBACK App::StaticHotkeyControlProcedure(const HWND window, const UINT message,
                                                    const WPARAM wParam, const LPARAM lParam,
                                                    const UINT_PTR subclassId,
                                                    const DWORD_PTR referenceData) {
    auto* app = reinterpret_cast<App*>(referenceData);
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(window, StaticHotkeyControlProcedure, subclassId);
    }
    return app != nullptr ? app->HotkeyControlProcedure(window, message, wParam, lParam)
                          : DefSubclassProc(window, message, wParam, lParam);
}

LRESULT App::HotkeyControlProcedure(const HWND window, const UINT message,
                                    const WPARAM wParam, const LPARAM lParam) {
    switch (message) {
        case WM_GETDLGCODE:
            if (wParam == VK_TAB) {
                return DefSubclassProc(window, message, wParam, lParam);
            }
            return DLGC_WANTALLKEYS;

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            const UINT virtualKey = static_cast<UINT>(wParam);
            if (virtualKey == VK_SHIFT || virtualKey == VK_CONTROL || virtualKey == VK_MENU ||
                virtualKey == VK_LSHIFT || virtualKey == VK_RSHIFT ||
                virtualKey == VK_LCONTROL || virtualKey == VK_RCONTROL ||
                virtualKey == VK_LMENU || virtualKey == VK_RMENU ||
                virtualKey == VK_LWIN || virtualKey == VK_RWIN) {
                return 0;
            }

            UINT modifiers = 0;
            if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) {
                modifiers |= MOD_CONTROL;
            }
            if ((GetKeyState(VK_MENU) & 0x8000) != 0) {
                modifiers |= MOD_ALT;
            }
            if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) {
                modifiers |= MOD_SHIFT;
            }
            if (modifiers == 0 &&
                (virtualKey == VK_BACK || virtualKey == VK_DELETE || virtualKey == VK_ESCAPE)) {
                pendingHotkeyModifiers_ = 0;
                pendingHotkeyVirtualKey_ = 0;
            } else {
                pendingHotkeyModifiers_ = modifiers;
                pendingHotkeyVirtualKey_ = virtualKey;
            }
            UpdateHotkeyControlText();
            return 0;
        }

        case WM_CHAR:
        case WM_SYSCHAR:
            return 0;

        default:
            return DefSubclassProc(window, message, wParam, lParam);
    }
}

LRESULT App::WindowProcedure(const UINT message, const WPARAM wParam, const LPARAM lParam) {
    if (message == showWindowMessage_ && showWindowMessage_ != 0) {
        ShowSettingsWindow();
        return 0;
    }
    if (message == taskbarCreatedMessage_ && taskbarCreatedMessage_ != 0) {
        trayIconAdded_ = false;
        AddTrayIcon();
        UpdateTrayState();
        return 0;
    }

    switch (message) {
        case WM_CREATE:
            return CreateControls() ? 0 : -1;

        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_LANGUAGE && HIWORD(wParam) == CBN_SELCHANGE) {
                ApplyLanguage(ControlLanguage());
                return 0;
            }
            switch (LOWORD(wParam)) {
                case IDC_SAVE:
                    SaveControls();
                    return 0;
                case IDC_DISCARD:
                    DiscardControls();
                    return 0;
                case IDC_REFRESH:
                    RefreshEndpointControls(true);
                    UpdateStatus();
                    return 0;
                case IDM_SETTINGS:
                    ShowSettingsWindow();
                    return 0;
                case IDM_ICON_CREDIT:
                    MessageBoxW(window_, GetText(displayLanguage_, TextId::IconCreditMessage),
                                GetText(displayLanguage_, TextId::IconCreditTitle),
                                MB_OK | MB_ICONINFORMATION);
                    return 0;
                case IDM_EXIT:
                    ExitApplication();
                    return 0;
                default:
                    break;
            }
            break;

        case WM_HOTKEY:
            if (wParam == kHotkeyId) {
                ToggleAudioEndpoint();
                return 0;
            }
            break;

        case kAudioChangedMessage:
            RefreshEndpointControls(true);
            UpdateStatus();
            return 0;

        case kTrayMessage: {
            const UINT event = LOWORD(lParam);
            if (event == WM_LBUTTONUP || event == NIN_SELECT || event == NIN_KEYSELECT) {
                ShowSettingsWindow();
            } else if (event == WM_RBUTTONUP || event == WM_CONTEXTMENU) {
                POINT point{};
                GetCursorPos(&point);
                ShowTrayMenu(point);
            }
            return 0;
        }

        case WM_DPICHANGED: {
            const auto* suggested = reinterpret_cast<RECT*>(lParam);
            SetWindowPos(window_, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left, suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            const UINT dpi = HIWORD(wParam);
            LayoutControls(dpi);
            ApplyControlFont(dpi);
            return 0;
        }

        case WM_CLOSE:
            if (settings_.closeBehavior == CloseBehavior::Exit) {
                ExitApplication();
            } else {
                HideSettingsWindow();
            }
            return 0;

        case WM_DESTROY:
            RemoveTrayIcon();
            PostQuitMessage(0);
            return 0;

        default:
            break;
    }
    return DefWindowProcW(window_, message, wParam, lParam);
}

bool App::RegisterWindowClass() {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = StaticWindowProcedure;
    windowClass.hInstance = instance_;
    windowClass.hIcon = neutralIcon_ != nullptr ? neutralIcon_ : LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hIconSm = windowClass.hIcon;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = kWindowClassName;
    return RegisterClassExW(&windowClass) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

bool App::CreateMainWindow() {
    const UINT dpi = GetDpiForSystem();
    RECT rectangle{0, 0, Scale(kLogicalClientWidth, dpi), Scale(kLogicalClientHeight, dpi)};
    constexpr DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRectExForDpi(&rectangle, style, FALSE, 0, dpi);

    window_ = CreateWindowExW(
        0, kWindowClassName, GetText(displayLanguage_, TextId::WindowTitle), style,
        CW_USEDEFAULT, CW_USEDEFAULT, rectangle.right - rectangle.left,
        rectangle.bottom - rectangle.top, nullptr, nullptr, instance_, this);
    return window_ != nullptr;
}

bool App::CreateControls() {
    deviceALabel_ = CreateWindowExW(0, WC_STATICW, GetText(displayLanguage_, TextId::DeviceA), WS_CHILD | WS_VISIBLE,
                                    0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    deviceBLabel_ = CreateWindowExW(0, WC_STATICW, GetText(displayLanguage_, TextId::DeviceB), WS_CHILD | WS_VISIBLE,
                                    0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    hotkeyLabel_ = CreateWindowExW(0, WC_STATICW, GetText(displayLanguage_, TextId::GlobalHotkey), WS_CHILD | WS_VISIBLE,
                                   0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    deviceACombo_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_COMBOBOXW, L"",
                                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                                    0, 0, 0, 0, window_, ControlId(IDC_DEVICE_A), instance_, nullptr);
    deviceBCombo_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_COMBOBOXW, L"",
                                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                                    0, 0, 0, 0, window_, ControlId(IDC_DEVICE_B), instance_, nullptr);
    hotkeyControl_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, L"",
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_READONLY | ES_AUTOHSCROLL,
                                     0, 0, 0, 0, window_, ControlId(IDC_HOTKEY), instance_, nullptr);
    hotkeyHintLabel_ = CreateWindowExW(
        0, WC_STATICW,
        GetText(displayLanguage_, TextId::HotkeyHint),
        WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    currentLabel_ = CreateWindowExW(0, WC_STATICW, GetText(displayLanguage_, TextId::CurrentDefault), WS_CHILD | WS_VISIBLE,
                                    0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    currentDeviceLabel_ = CreateWindowExW(0, WC_STATICW, L"–", WS_CHILD | WS_VISIBLE | SS_PATHELLIPSIS,
                                          0, 0, 0, 0, window_, ControlId(IDC_CURRENT_DEVICE), instance_, nullptr);
    registrationLabel_ = CreateWindowExW(0, WC_STATICW, GetText(displayLanguage_, TextId::HotkeyStatus), WS_CHILD | WS_VISIBLE,
                                         0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    hotkeyStatusLabel_ = CreateWindowExW(0, WC_STATICW, L"–", WS_CHILD | WS_VISIBLE | SS_PATHELLIPSIS,
                                         0, 0, 0, 0, window_, ControlId(IDC_HOTKEY_STATUS), instance_, nullptr);
    autoStartCheck_ = CreateWindowExW(0, WC_BUTTONW, GetText(displayLanguage_, TextId::AutoStart),
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                      0, 0, 0, 0, window_, ControlId(IDC_AUTOSTART), instance_, nullptr);
    notificationsCheck_ = CreateWindowExW(0, WC_BUTTONW, GetText(displayLanguage_, TextId::Notifications),
                                          WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                          0, 0, 0, 0, window_, ControlId(IDC_NOTIFICATIONS), instance_, nullptr);
    languageLabel_ = CreateWindowExW(0, WC_STATICW, GetText(displayLanguage_, TextId::Language), WS_CHILD | WS_VISIBLE,
                                     0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    languageCombo_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_COMBOBOXW, L"",
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                                     0, 0, 0, 0, window_, ControlId(IDC_LANGUAGE), instance_, nullptr);
    closeBehaviorLabel_ = CreateWindowExW(0, WC_STATICW, GetText(displayLanguage_, TextId::CloseWithX), WS_CHILD | WS_VISIBLE,
                                          0, 0, 0, 0, window_, nullptr, instance_, nullptr);
    closeBehaviorCombo_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_COMBOBOXW, L"",
                                          WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                                          0, 0, 0, 0, window_, ControlId(IDC_CLOSE_BEHAVIOR), instance_, nullptr);
    refreshButton_ = CreateWindowExW(0, WC_BUTTONW, GetText(displayLanguage_, TextId::RefreshDevices),
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                     0, 0, 0, 0, window_, ControlId(IDC_REFRESH), instance_, nullptr);
    discardButton_ = CreateWindowExW(0, WC_BUTTONW, GetText(displayLanguage_, TextId::Discard),
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                     0, 0, 0, 0, window_, ControlId(IDC_DISCARD), instance_, nullptr);
    saveButton_ = CreateWindowExW(0, WC_BUTTONW, GetText(displayLanguage_, TextId::Save),
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                                  0, 0, 0, 0, window_, ControlId(IDC_SAVE), instance_, nullptr);

    const std::array<HWND, 20> controls = {
        deviceALabel_, deviceBLabel_, hotkeyLabel_, deviceACombo_, deviceBCombo_, hotkeyControl_,
        hotkeyHintLabel_, currentLabel_, currentDeviceLabel_, registrationLabel_, hotkeyStatusLabel_,
        autoStartCheck_, notificationsCheck_, languageLabel_, languageCombo_,
        closeBehaviorLabel_, closeBehaviorCombo_,
        refreshButton_, discardButton_, saveButton_};
    if (std::any_of(controls.begin(), controls.end(), [](const HWND control) { return control == nullptr; })) {
        return false;
    }
    if (SetWindowSubclass(hotkeyControl_, StaticHotkeyControlProcedure, 1,
                          reinterpret_cast<DWORD_PTR>(this)) == FALSE) {
        return false;
    }

    SendMessageW(languageCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Deutsch"));
    SendMessageW(languageCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"English"));
    SendMessageW(closeBehaviorCombo_, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(GetText(displayLanguage_, TextId::HideToTray)));
    SendMessageW(closeBehaviorCombo_, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(GetText(displayLanguage_, TextId::ExitProgram)));

    const UINT dpi = GetDpiForWindow(window_);
    LayoutControls(dpi);
    ApplyControlFont(dpi);
    return true;
}

void App::LayoutControls(const UINT dpi) {
    auto move = [dpi](const HWND control, const int x, const int y, const int width, const int height) {
        MoveWindow(control, Scale(x, dpi), Scale(y, dpi), Scale(width, dpi), Scale(height, dpi), TRUE);
    };

    move(deviceALabel_, 20, 23, 145, 22);
    move(deviceACombo_, 175, 18, 395, 220);
    move(deviceBLabel_, 20, 65, 145, 22);
    move(deviceBCombo_, 175, 60, 395, 220);
    move(hotkeyLabel_, 20, 107, 145, 22);
    move(hotkeyControl_, 175, 102, 220, 27);
    move(hotkeyHintLabel_, 175, 135, 395, 36);

    move(currentLabel_, 20, 187, 145, 22);
    move(currentDeviceLabel_, 175, 187, 395, 22);
    move(registrationLabel_, 20, 217, 145, 22);
    move(hotkeyStatusLabel_, 175, 217, 395, 22);

    move(autoStartCheck_, 20, 260, 400, 24);
    move(notificationsCheck_, 20, 294, 400, 24);
    move(languageLabel_, 20, 337, 145, 22);
    move(languageCombo_, 175, 332, 250, 180);
    move(closeBehaviorLabel_, 20, 379, 145, 22);
    move(closeBehaviorCombo_, 175, 374, 250, 180);

    move(refreshButton_, 20, 424, 160, 30);
    move(discardButton_, 366, 424, 95, 30);
    move(saveButton_, 475, 424, 95, 30);
}

void App::ApplyControlFont(const UINT dpi) {
    HFONT newFont = CreateFontW(-MulDiv(10, static_cast<int>(dpi), 72), 0, 0, 0, FW_NORMAL,
                                FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
                                L"Segoe UI");
    if (newFont == nullptr) {
        return;
    }
    EnumChildWindows(window_, [](const HWND child, const LPARAM font) -> BOOL {
        SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(font), TRUE);
        return TRUE;
    }, reinterpret_cast<LPARAM>(newFont));
    if (uiFont_ != nullptr) {
        DeleteObject(uiFont_);
    }
    uiFont_ = newFont;
}

void App::ApplyLanguage(const UiLanguage language) {
    displayLanguage_ = language;
    if (window_ == nullptr) {
        return;
    }

    SetWindowTextW(window_, GetText(language, TextId::WindowTitle));
    SetWindowTextW(deviceALabel_, GetText(language, TextId::DeviceA));
    SetWindowTextW(deviceBLabel_, GetText(language, TextId::DeviceB));
    SetWindowTextW(hotkeyLabel_, GetText(language, TextId::GlobalHotkey));
    SetWindowTextW(hotkeyHintLabel_, GetText(language, TextId::HotkeyHint));
    SetWindowTextW(currentLabel_, GetText(language, TextId::CurrentDefault));
    SetWindowTextW(registrationLabel_, GetText(language, TextId::HotkeyStatus));
    SetWindowTextW(autoStartCheck_, GetText(language, TextId::AutoStart));
    SetWindowTextW(notificationsCheck_, GetText(language, TextId::Notifications));
    SetWindowTextW(languageLabel_, GetText(language, TextId::Language));
    SetWindowTextW(closeBehaviorLabel_, GetText(language, TextId::CloseWithX));
    SetWindowTextW(refreshButton_, GetText(language, TextId::RefreshDevices));
    SetWindowTextW(discardButton_, GetText(language, TextId::Discard));
    SetWindowTextW(saveButton_, GetText(language, TextId::Save));
    UpdateHotkeyControlText();

    LRESULT closeSelection = SendMessageW(closeBehaviorCombo_, CB_GETCURSEL, 0, 0);
    if (closeSelection == CB_ERR) {
        closeSelection = 0;
    }
    SendMessageW(closeBehaviorCombo_, CB_RESETCONTENT, 0, 0);
    SendMessageW(closeBehaviorCombo_, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(GetText(language, TextId::HideToTray)));
    SendMessageW(closeBehaviorCombo_, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(GetText(language, TextId::ExitProgram)));
    SendMessageW(closeBehaviorCombo_, CB_SETCURSEL, closeSelection, 0);

    RefreshEndpointControls(true);
    UpdateStatus();
}

void App::UpdateHotkeyControlText() {
    if (hotkeyControl_ != nullptr) {
        const std::wstring description =
            HotkeyDescription(pendingHotkeyModifiers_, pendingHotkeyVirtualKey_);
        SetWindowTextW(hotkeyControl_, description.c_str());
    }
}

void App::ShowSettingsWindow() {
    if (window_ == nullptr) {
        return;
    }
    if (IsIconic(window_)) {
        ShowWindow(window_, SW_RESTORE);
    } else {
        ShowWindow(window_, SW_SHOW);
    }
    SetForegroundWindow(window_);
}

void App::HideSettingsWindow() {
    if (window_ != nullptr) {
        ShowWindow(window_, SW_HIDE);
    }
}

void App::ExitApplication() {
    if (window_ != nullptr) {
        DestroyWindow(window_);
    } else {
        PostQuitMessage(0);
    }
}

void App::AddTrayIcon() {
    if (window_ == nullptr || trayIconAdded_) {
        return;
    }
    trayData_ = {};
    trayData_.cbSize = sizeof(trayData_);
    trayData_.hWnd = window_;
    trayData_.uID = kTrayIconId;
    trayData_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    trayData_.uCallbackMessage = kTrayMessage;
    trayData_.hIcon = neutralIcon_ != nullptr ? neutralIcon_ : LoadIconW(nullptr, IDI_APPLICATION);
    CopyText(trayData_.szTip, _countof(trayData_.szTip), L"AudioHotkey");
    if (Shell_NotifyIconW(NIM_ADD, &trayData_) != FALSE) {
        trayIconAdded_ = true;
        trayData_.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &trayData_);
    }
}

void App::RemoveTrayIcon() noexcept {
    if (trayIconAdded_) {
        Shell_NotifyIconW(NIM_DELETE, &trayData_);
        trayIconAdded_ = false;
    }
}

void App::UpdateTrayState() {
    if (!trayIconAdded_ || audioService_ == nullptr) {
        return;
    }
    std::wstring currentId;
    std::wstring currentName = CurrentDefaultName(currentId);
    HICON icon = neutralIcon_;
    if (!currentId.empty() && currentId == settings_.endpointAId) {
        icon = endpointAIcon_;
    } else if (!currentId.empty() && currentId == settings_.endpointBId) {
        icon = endpointBIcon_;
    }
    if (icon == nullptr) {
        icon = LoadIconW(nullptr, IDI_APPLICATION);
    }
    trayData_.uFlags = NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    trayData_.hIcon = icon;
    const std::wstring tooltip = currentName.empty() ? L"AudioHotkey" : L"AudioHotkey – " + currentName;
    CopyText(trayData_.szTip, _countof(trayData_.szTip), tooltip);
    Shell_NotifyIconW(NIM_MODIFY, &trayData_);
}

void App::ShowTrayMessage(const std::wstring& title, const std::wstring& message, const bool error) {
    if (!trayIconAdded_) {
        return;
    }
    trayData_.uFlags = NIF_INFO;
    CopyText(trayData_.szInfoTitle, _countof(trayData_.szInfoTitle), title);
    CopyText(trayData_.szInfo, _countof(trayData_.szInfo), message);
    trayData_.dwInfoFlags = error ? NIIF_ERROR : NIIF_INFO;
    trayData_.uTimeout = 3000;
    Shell_NotifyIconW(NIM_MODIFY, &trayData_);
}

void App::ShowTrayMenu(const POINT position) {
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return;
    }
    AppendMenuW(menu, MF_STRING | MF_DEFAULT, IDM_SETTINGS,
                GetText(displayLanguage_, TextId::Settings));
    AppendMenuW(menu, MF_STRING, IDM_ICON_CREDIT,
                GetText(displayLanguage_, TextId::IconCredit));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_EXIT, GetText(displayLanguage_, TextId::Exit));
    SetForegroundWindow(window_);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN,
                   position.x, position.y, 0, window_, nullptr);
    DestroyMenu(menu);
    PostMessageW(window_, WM_NULL, 0, 0);
}

void App::LoadSettingsIntoControls() {
    if (window_ == nullptr) {
        return;
    }
    RefreshEndpointControls(false);
    pendingHotkeyModifiers_ = settings_.hotkeyModifiers;
    pendingHotkeyVirtualKey_ = settings_.hotkeyVirtualKey;
    UpdateHotkeyControlText();
    SendMessageW(autoStartCheck_, BM_SETCHECK, settings_.autoStart ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(notificationsCheck_, BM_SETCHECK,
                 settings_.notifications ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(languageCombo_, CB_SETCURSEL,
                 settings_.language == UiLanguage::English ? 1 : 0, 0);
    SendMessageW(closeBehaviorCombo_, CB_SETCURSEL,
                 settings_.closeBehavior == CloseBehavior::Exit ? 1 : 0, 0);
    ApplyLanguage(settings_.language);
}

void App::RefreshEndpointControls(const bool preserveSelections) {
    if (audioService_ == nullptr || deviceACombo_ == nullptr || deviceBCombo_ == nullptr) {
        return;
    }

    std::wstring endpointAId = settings_.endpointAId;
    std::wstring endpointAName = settings_.endpointAName;
    std::wstring endpointBId = settings_.endpointBId;
    std::wstring endpointBName = settings_.endpointBName;
    if (preserveSelections) {
        const AudioEndpoint selectedA = SelectedEndpoint(deviceACombo_);
        const AudioEndpoint selectedB = SelectedEndpoint(deviceBCombo_);
        if (!selectedA.id.empty()) {
            endpointAId = selectedA.id;
            endpointAName = selectedA.name;
        }
        if (!selectedB.id.empty()) {
            endpointBId = selectedB.id;
            endpointBName = selectedB.name;
        }
    }

    endpoints_ = audioService_->ListActiveRenderEndpoints();
    auto addMissing = [this](const std::wstring& id, const std::wstring& configuredName) {
        if (id.empty()) {
            return;
        }
        const bool found = std::any_of(endpoints_.begin(), endpoints_.end(),
                                       [&id](const AudioEndpoint& endpoint) { return endpoint.id == id; });
        if (!found) {
            endpoints_.push_back({id, configuredName.empty() ? id : configuredName, false});
        }
    };
    addMissing(endpointAId, endpointAName);
    addMissing(endpointBId, endpointBName);

    SendMessageW(deviceACombo_, CB_RESETCONTENT, 0, 0);
    SendMessageW(deviceBCombo_, CB_RESETCONTENT, 0, 0);
    int selectedAIndex = -1;
    int selectedBIndex = -1;
    for (size_t index = 0; index < endpoints_.size(); ++index) {
        std::wstring label = endpoints_[index].name;
        if (!endpoints_[index].active) {
            label += GetText(displayLanguage_, TextId::MissingSuffix);
        }
        const LRESULT itemA = SendMessageW(deviceACombo_, CB_ADDSTRING, 0,
                                           reinterpret_cast<LPARAM>(label.c_str()));
        const LRESULT itemB = SendMessageW(deviceBCombo_, CB_ADDSTRING, 0,
                                           reinterpret_cast<LPARAM>(label.c_str()));
        if (itemA >= 0) {
            SendMessageW(deviceACombo_, CB_SETITEMDATA, itemA, static_cast<LPARAM>(index));
        }
        if (itemB >= 0) {
            SendMessageW(deviceBCombo_, CB_SETITEMDATA, itemB, static_cast<LPARAM>(index));
        }
        if (endpoints_[index].id == endpointAId) {
            selectedAIndex = static_cast<int>(itemA);
        }
        if (endpoints_[index].id == endpointBId) {
            selectedBIndex = static_cast<int>(itemB);
        }
    }
    SendMessageW(deviceACombo_, CB_SETCURSEL, selectedAIndex, 0);
    SendMessageW(deviceBCombo_, CB_SETCURSEL, selectedBIndex, 0);
}

void App::UpdateStatus() {
    if (window_ == nullptr || audioService_ == nullptr) {
        return;
    }
    std::wstring currentId;
    std::wstring currentName = CurrentDefaultName(currentId);
    SetWindowTextW(currentDeviceLabel_, currentName.empty()
                                              ? GetText(displayLanguage_, TextId::NotAvailable)
                                              : currentName.c_str());

    std::wstring hotkeyStatus;
    if (!hotkeyRegistrationError_.empty()) {
        hotkeyStatus = std::wstring(GetText(displayLanguage_, TextId::InactivePrefix)) +
                       hotkeyRegistrationError_;
    } else if (!hotkeyRegistered_) {
        hotkeyStatus = GetText(displayLanguage_, TextId::NotConfigured);
    } else {
        hotkeyStatus = std::wstring(GetText(displayLanguage_, TextId::ActivePrefix)) +
                       HotkeyDescription(registeredHotkeyModifiers_, registeredHotkeyVirtualKey_);
    }
    if (!autoStartError_.empty()) {
        hotkeyStatus += std::wstring(GetText(displayLanguage_, TextId::AutoStartError)) +
                        autoStartError_;
    }
    SetWindowTextW(hotkeyStatusLabel_, hotkeyStatus.c_str());
    UpdateTrayState();
}

void App::SaveControls() {
    AppSettings candidate;
    std::wstring error;
    if (!ReadControls(candidate, error)) {
        MessageBoxW(window_, error.c_str(), kApplicationName, MB_OK | MB_ICONWARNING);
        return;
    }

    const AppSettings oldSettings = settings_;
    const UINT oldModifiers = registeredHotkeyModifiers_;
    const UINT oldVirtualKey = registeredHotkeyVirtualKey_;
    const bool oldHotkeyRegistered = hotkeyRegistered_;

    if (!UpdateRegisteredHotkey(candidate.hotkeyModifiers, candidate.hotkeyVirtualKey,
                                candidate.language, error)) {
        MessageBoxW(window_, error.c_str(), kApplicationName, MB_OK | MB_ICONWARNING);
        return;
    }
    if (!ConfigureAutoStart(candidate.autoStart, candidate.language, error)) {
        std::wstring ignored;
        if (oldHotkeyRegistered) {
            (void)UpdateRegisteredHotkey(oldModifiers, oldVirtualKey,
                                         oldSettings.language, ignored);
        } else {
            (void)UpdateRegisteredHotkey(0, 0, oldSettings.language, ignored);
        }
        MessageBoxW(window_, error.c_str(), kApplicationName, MB_OK | MB_ICONERROR);
        return;
    }
    if (!settingsStore_.Save(candidate, error)) {
        std::wstring ignored;
        (void)ConfigureAutoStart(oldSettings.autoStart, oldSettings.language, ignored);
        if (oldHotkeyRegistered) {
            (void)UpdateRegisteredHotkey(oldModifiers, oldVirtualKey,
                                         oldSettings.language, ignored);
        } else {
            (void)UpdateRegisteredHotkey(0, 0, oldSettings.language, ignored);
        }
        MessageBoxW(window_, error.c_str(), kApplicationName, MB_OK | MB_ICONERROR);
        return;
    }

    settings_ = std::move(candidate);
    hotkeyRegistrationError_.clear();
    autoStartError_.clear();
    ApplyLanguage(settings_.language);
}

void App::DiscardControls() {
    LoadSettingsIntoControls();
    UpdateStatus();
}

void App::ToggleAudioEndpoint() {
    if (audioService_ == nullptr) {
        return;
    }
    const SwitchResult result = audioService_->Toggle(settings_.endpointAId, settings_.endpointBId,
                                                      displayLanguage_);
    if (!result.success) {
        ShowTrayMessage(GetText(displayLanguage_, TextId::TrayErrorTitle), result.message, true);
        return;
    }
    UpdateStatus();
    if (settings_.notifications) {
        ShowTrayMessage(L"AudioHotkey", result.message, false);
    }
}

bool App::ReadControls(AppSettings& candidate, std::wstring& error) const {
    error.clear();
    candidate.language = ControlLanguage();
    const AudioEndpoint endpointA = SelectedEndpoint(deviceACombo_);
    const AudioEndpoint endpointB = SelectedEndpoint(deviceBCombo_);
    if (endpointA.id.empty() || endpointB.id.empty()) {
        error = GetText(candidate.language, TextId::SelectTwoDevices);
        return false;
    }
    if (endpointA.id == endpointB.id) {
        error = GetText(candidate.language, TextId::DistinctDevices);
        return false;
    }

    candidate.hotkeyVirtualKey = pendingHotkeyVirtualKey_;
    candidate.hotkeyModifiers = pendingHotkeyModifiers_;
    HotkeyValidationIssue issue = HotkeyValidationIssue::None;
    if (!ValidateHotkey(candidate.hotkeyModifiers, candidate.hotkeyVirtualKey, issue)) {
        error = HotkeyValidationText(issue, candidate.language);
        return false;
    }

    candidate.endpointAId = endpointA.id;
    candidate.endpointAName = endpointA.name;
    candidate.endpointBId = endpointB.id;
    candidate.endpointBName = endpointB.name;
    candidate.autoStart = SendMessageW(autoStartCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    candidate.notifications = SendMessageW(notificationsCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    candidate.closeBehavior = SendMessageW(closeBehaviorCombo_, CB_GETCURSEL, 0, 0) == 1
                                  ? CloseBehavior::Exit
                                  : CloseBehavior::HideToTray;
    return true;
}

AudioEndpoint App::SelectedEndpoint(const HWND combo) const {
    if (combo == nullptr) {
        return {};
    }
    const LRESULT selection = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (selection == CB_ERR) {
        return {};
    }
    const LRESULT data = SendMessageW(combo, CB_GETITEMDATA, selection, 0);
    if (data == CB_ERR || data < 0 || static_cast<size_t>(data) >= endpoints_.size()) {
        return {};
    }
    return endpoints_[static_cast<size_t>(data)];
}

bool App::UpdateRegisteredHotkey(const UINT modifiers, const UINT virtualKey,
                                 const UiLanguage language, std::wstring& error) {
    error.clear();
    HotkeyValidationIssue issue = HotkeyValidationIssue::None;
    if (!ValidateHotkey(modifiers, virtualKey, issue)) {
        error = HotkeyValidationText(issue, language);
        return false;
    }
    if (hotkeyRegistered_ && registeredHotkeyModifiers_ == modifiers &&
        registeredHotkeyVirtualKey_ == virtualKey) {
        hotkeyRegistrationError_.clear();
        return true;
    }

    const bool hadOldHotkey = hotkeyRegistered_;
    const UINT oldModifiers = registeredHotkeyModifiers_;
    const UINT oldVirtualKey = registeredHotkeyVirtualKey_;
    UnregisterCurrentHotkey();

    if (virtualKey == 0) {
        hotkeyRegistrationError_.clear();
        return true;
    }
    if (RegisterHotKey(window_, kHotkeyId, modifiers | MOD_NOREPEAT, virtualKey) == FALSE) {
        const DWORD code = GetLastError();
        if (hadOldHotkey && RegisterHotKey(window_, kHotkeyId,
                                           oldModifiers | MOD_NOREPEAT, oldVirtualKey) != FALSE) {
            hotkeyRegistered_ = true;
            registeredHotkeyModifiers_ = oldModifiers;
            registeredHotkeyVirtualKey_ = oldVirtualKey;
        }
        error = code == ERROR_HOTKEY_ALREADY_REGISTERED
                    ? GetText(language, TextId::HotkeyDuplicate)
                    : std::wstring(GetText(language, TextId::HotkeyRegistrationFailed)) +
                          Win32ErrorMessage(code, language);
        hotkeyRegistrationError_ = error;
        return false;
    }

    hotkeyRegistered_ = true;
    registeredHotkeyModifiers_ = modifiers;
    registeredHotkeyVirtualKey_ = virtualKey;
    hotkeyRegistrationError_.clear();
    return true;
}

void App::UnregisterCurrentHotkey() noexcept {
    if (hotkeyRegistered_ && window_ != nullptr) {
        UnregisterHotKey(window_, kHotkeyId);
    }
    hotkeyRegistered_ = false;
    registeredHotkeyModifiers_ = 0;
    registeredHotkeyVirtualKey_ = 0;
}

bool App::ConfigureAutoStart(const bool enabled, const UiLanguage language,
                             std::wstring& error) const {
    error.clear();
    HKEY rawKey = nullptr;
    const LSTATUS openResult = RegCreateKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, nullptr,
                                               REG_OPTION_NON_VOLATILE, KEY_SET_VALUE,
                                               nullptr, &rawKey, nullptr);
    if (openResult != ERROR_SUCCESS) {
        error = std::wstring(GetText(language, TextId::AutoStartOpenFailed)) +
                Win32ErrorMessage(openResult, language);
        return false;
    }

    LSTATUS result = ERROR_SUCCESS;
    if (enabled) {
        const std::wstring executable = ModulePath();
        if (executable.empty()) {
            RegCloseKey(rawKey);
            error = GetText(language, TextId::AppPathUnavailable);
            return false;
        }
        const std::wstring command = L"\"" + executable + L"\" --background";
        result = RegSetValueExW(rawKey, kRunValueName, 0, REG_SZ,
                                reinterpret_cast<const BYTE*>(command.c_str()),
                                static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    } else {
        result = RegDeleteValueW(rawKey, kRunValueName);
        if (result == ERROR_FILE_NOT_FOUND) {
            result = ERROR_SUCCESS;
        }
    }
    RegCloseKey(rawKey);

    if (result != ERROR_SUCCESS) {
        error = std::wstring(GetText(language, TextId::AutoStartChangeFailed)) +
                Win32ErrorMessage(result, language);
        return false;
    }
    return true;
}

UiLanguage App::ControlLanguage() const {
    return languageCombo_ != nullptr && SendMessageW(languageCombo_, CB_GETCURSEL, 0, 0) == 1
               ? UiLanguage::English
               : UiLanguage::German;
}

const wchar_t* App::HotkeyValidationText(const HotkeyValidationIssue issue,
                                         const UiLanguage language) const {
    switch (issue) {
        case HotkeyValidationIssue::NeedsModifier:
            return GetText(language, TextId::HotkeyNeedsModifier);
        case HotkeyValidationIssue::UnsupportedModifier:
            return GetText(language, TextId::HotkeyUnsupportedModifier);
        case HotkeyValidationIssue::F12Reserved:
            return GetText(language, TextId::HotkeyF12Reserved);
        case HotkeyValidationIssue::NormalKeyRequired:
            return GetText(language, TextId::HotkeyNormalKeyRequired);
        case HotkeyValidationIssue::None:
            break;
    }
    return L"";
}

std::wstring App::CurrentDefaultName(std::wstring& id) const {
    id.clear();
    if (audioService_ == nullptr || FAILED(audioService_->GetDefaultEndpointId(eConsole, id))) {
        return {};
    }
    std::wstring name = audioService_->GetEndpointName(id);
    return name.empty() ? id : name;
}

std::wstring App::HotkeyDescription(const UINT modifiers, const UINT virtualKey) const {
    if (virtualKey == 0) {
        return GetText(displayLanguage_, TextId::NotConfigured);
    }
    std::wstring value;
    if ((modifiers & MOD_CONTROL) != 0) {
        value += displayLanguage_ == UiLanguage::English ? L"Ctrl + " : L"Strg + ";
    }
    if ((modifiers & MOD_ALT) != 0) {
        value += L"Alt + ";
    }
    if ((modifiers & MOD_SHIFT) != 0) {
        value += displayLanguage_ == UiLanguage::English ? L"Shift + " : L"Umschalt + ";
    }

    std::wstring keyName;
    if (virtualKey >= VK_F1 && virtualKey <= VK_F24) {
        keyName = L"F" + std::to_wstring(virtualKey - VK_F1 + 1);
    } else if (virtualKey >= VK_NUMPAD0 && virtualKey <= VK_NUMPAD9) {
        keyName = L"Numpad " + std::to_wstring(virtualKey - VK_NUMPAD0);
    } else if ((virtualKey >= L'A' && virtualKey <= L'Z') ||
               (virtualKey >= L'0' && virtualKey <= L'9')) {
        keyName.assign(1, static_cast<wchar_t>(virtualKey));
    } else {
        switch (virtualKey) {
            case VK_MULTIPLY: keyName = L"Numpad *"; break;
            case VK_ADD: keyName = L"Numpad +"; break;
            case VK_SEPARATOR: keyName = L"Numpad Separator"; break;
            case VK_SUBTRACT: keyName = L"Numpad -"; break;
            case VK_DECIMAL:
                keyName = displayLanguage_ == UiLanguage::English ? L"Numpad Decimal" : L"Numpad Dezimal";
                break;
            case VK_DIVIDE: keyName = L"Numpad /"; break;
            case VK_SPACE:
                keyName = displayLanguage_ == UiLanguage::English ? L"Space" : L"Leertaste";
                break;
            case VK_BACK:
                keyName = displayLanguage_ == UiLanguage::English ? L"Backspace" : L"Rücktaste";
                break;
            case VK_DELETE:
                keyName = displayLanguage_ == UiLanguage::English ? L"Delete" : L"Entfernen";
                break;
            case VK_INSERT:
                keyName = displayLanguage_ == UiLanguage::English ? L"Insert" : L"Einfügen";
                break;
            case VK_PRIOR:
                keyName = displayLanguage_ == UiLanguage::English ? L"Page Up" : L"Bild auf";
                break;
            case VK_NEXT:
                keyName = displayLanguage_ == UiLanguage::English ? L"Page Down" : L"Bild ab";
                break;
            case VK_HOME:
                keyName = displayLanguage_ == UiLanguage::English ? L"Home" : L"Pos1";
                break;
            case VK_END: keyName = L"End"; break;
            case VK_LEFT:
                keyName = displayLanguage_ == UiLanguage::English ? L"Left" : L"Links";
                break;
            case VK_RIGHT:
                keyName = displayLanguage_ == UiLanguage::English ? L"Right" : L"Rechts";
                break;
            case VK_UP:
                keyName = displayLanguage_ == UiLanguage::English ? L"Up" : L"Hoch";
                break;
            case VK_DOWN:
                keyName = displayLanguage_ == UiLanguage::English ? L"Down" : L"Runter";
                break;
            case VK_RETURN: keyName = L"Enter"; break;
            case VK_TAB: keyName = L"Tab"; break;
            case VK_ESCAPE: keyName = L"Esc"; break;
            default: break;
        }
    }

    if (keyName.empty()) {
        const UINT scanCode = MapVirtualKeyW(virtualKey, MAPVK_VK_TO_VSC);
        wchar_t systemKeyName[64]{};
        if (scanCode != 0 && GetKeyNameTextW(static_cast<LONG>(scanCode << 16), systemKeyName,
                                             static_cast<int>(_countof(systemKeyName))) > 0) {
            keyName = systemKeyName;
        }
    }
    if (!keyName.empty()) {
        value += keyName;
    } else {
        wchar_t fallback[16]{};
        swprintf_s(fallback, L"VK %u", virtualKey);
        value += fallback;
    }
    return value;
}


}  // namespace audiohotkey
