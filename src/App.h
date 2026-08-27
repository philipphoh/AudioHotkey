#pragma once

#include "AudioEndpoint.h"
#include "Settings.h"
#include "CoreLogic.h"

#include <windows.h>

#include <string>
#include <vector>

namespace audiohotkey {

class App final {
public:
    explicit App(HINSTANCE instance);
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    [[nodiscard]] bool Initialize(bool showWindow, HANDLE readyEvent);
    [[nodiscard]] int RunMessageLoop();
    void Shutdown() noexcept;

    static constexpr wchar_t kWindowClassName[] = L"AudioHotkey.SettingsWindow.v1";
    static constexpr wchar_t kShowMessageName[] = L"AudioHotkey.ShowSettings.v1";

private:
    static constexpr UINT kTrayIconId = 1;
    static constexpr UINT kHotkeyId = 1;
    static constexpr UINT kTrayMessage = WM_APP + 1;
    static constexpr UINT kAudioChangedMessage = WM_APP + 2;

    static LRESULT CALLBACK StaticWindowProcedure(HWND window, UINT message,
                                                  WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK StaticHotkeyControlProcedure(HWND window, UINT message,
                                                         WPARAM wParam, LPARAM lParam,
                                                         UINT_PTR subclassId,
                                                         DWORD_PTR referenceData);
    LRESULT WindowProcedure(UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HotkeyControlProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    [[nodiscard]] bool RegisterWindowClass();
    [[nodiscard]] bool CreateMainWindow();
    [[nodiscard]] bool CreateControls();
    void LayoutControls(UINT dpi);
    void ApplyControlFont(UINT dpi);
    void ApplyLanguage(UiLanguage language);
    void UpdateHotkeyControlText();
    void ShowSettingsWindow();
    void HideSettingsWindow();
    void ExitApplication();

    void AddTrayIcon();
    void RemoveTrayIcon() noexcept;
    void UpdateTrayState();
    void ShowTrayMessage(const std::wstring& title, const std::wstring& message, bool error);
    void ShowTrayMenu(POINT position);

    void LoadSettingsIntoControls();
    void RefreshEndpointControls(bool preserveSelections);
    void UpdateStatus();
    void SaveControls();
    void DiscardControls();
    void ToggleAudioEndpoint();

    [[nodiscard]] bool ReadControls(AppSettings& candidate, std::wstring& error) const;
    [[nodiscard]] AudioEndpoint SelectedEndpoint(HWND combo) const;
    [[nodiscard]] bool UpdateRegisteredHotkey(UINT modifiers, UINT virtualKey,
                                              UiLanguage language,
                                              std::wstring& error);
    void UnregisterCurrentHotkey() noexcept;

    [[nodiscard]] bool ConfigureAutoStart(bool enabled, UiLanguage language,
                                          std::wstring& error) const;
    [[nodiscard]] UiLanguage ControlLanguage() const;
    [[nodiscard]] const wchar_t* HotkeyValidationText(HotkeyValidationIssue issue,
                                                       UiLanguage language) const;
    [[nodiscard]] std::wstring CurrentDefaultName(std::wstring& id) const;
    [[nodiscard]] std::wstring HotkeyDescription(UINT modifiers, UINT virtualKey) const;

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    HWND deviceACombo_ = nullptr;
    HWND deviceBCombo_ = nullptr;
    HWND hotkeyControl_ = nullptr;
    HWND currentDeviceLabel_ = nullptr;
    HWND hotkeyStatusLabel_ = nullptr;
    HWND autoStartCheck_ = nullptr;
    HWND notificationsCheck_ = nullptr;
    HWND closeBehaviorCombo_ = nullptr;
    HWND languageCombo_ = nullptr;
    HWND refreshButton_ = nullptr;
    HWND discardButton_ = nullptr;
    HWND saveButton_ = nullptr;
    HWND deviceALabel_ = nullptr;
    HWND deviceBLabel_ = nullptr;
    HWND hotkeyLabel_ = nullptr;
    HWND hotkeyHintLabel_ = nullptr;
    HWND currentLabel_ = nullptr;
    HWND registrationLabel_ = nullptr;
    HWND closeBehaviorLabel_ = nullptr;
    HWND languageLabel_ = nullptr;
    HFONT uiFont_ = nullptr;

    HICON neutralIcon_ = nullptr;
    HICON endpointAIcon_ = nullptr;
    HICON endpointBIcon_ = nullptr;
    NOTIFYICONDATAW trayData_{};
    bool trayIconAdded_ = false;

    UINT taskbarCreatedMessage_ = 0;
    UINT showWindowMessage_ = 0;
    UINT registeredHotkeyModifiers_ = 0;
    UINT registeredHotkeyVirtualKey_ = 0;
    UINT pendingHotkeyModifiers_ = 0;
    UINT pendingHotkeyVirtualKey_ = 0;
    bool hotkeyRegistered_ = false;
    std::wstring hotkeyRegistrationError_;
    std::wstring autoStartError_;

    SettingsStore settingsStore_;
    AppSettings settings_;
    UiLanguage displayLanguage_ = UiLanguage::German;
    std::vector<AudioEndpoint> endpoints_;
    AudioEndpointService* audioService_ = nullptr;
    bool shuttingDown_ = false;
};

}  // namespace audiohotkey
