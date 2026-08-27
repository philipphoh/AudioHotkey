#pragma once

#include <windows.h>

#include <string>

namespace audiohotkey {

enum class CloseBehavior : UINT {
    HideToTray = 0,
    Exit = 1,
};

enum class UiLanguage : UINT {
    German = 0,
    English = 1,
};

struct AppSettings {
    std::wstring endpointAId;
    std::wstring endpointAName;
    std::wstring endpointBId;
    std::wstring endpointBName;
    UINT hotkeyModifiers = 0;
    UINT hotkeyVirtualKey = 0;
    bool autoStart = false;
    bool notifications = true;
    CloseBehavior closeBehavior = CloseBehavior::HideToTray;
    UiLanguage language = UiLanguage::German;
};

class SettingsStore final {
public:
    explicit SettingsStore(std::wstring path);

    static std::wstring DefaultPath();

    [[nodiscard]] const std::wstring& path() const noexcept { return path_; }
    [[nodiscard]] AppSettings Load() const;
    [[nodiscard]] bool Save(const AppSettings& settings, std::wstring& error) const;

private:
    std::wstring path_;
};

}  // namespace audiohotkey
