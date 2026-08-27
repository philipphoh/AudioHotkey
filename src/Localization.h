#pragma once

#include "Settings.h"

namespace audiohotkey {

enum class TextId {
    WindowTitle,
    DeviceA,
    DeviceB,
    GlobalHotkey,
    HotkeyHint,
    CurrentDefault,
    HotkeyStatus,
    AutoStart,
    Notifications,
    Language,
    CloseWithX,
    HideToTray,
    ExitProgram,
    RefreshDevices,
    Discard,
    Save,
    Settings,
    IconCredit,
    IconCreditTitle,
    IconCreditMessage,
    Exit,
    NotAvailable,
    NotConfigured,
    InactivePrefix,
    ActivePrefix,
    AutoStartError,
    MissingSuffix,
    SelectTwoDevices,
    DistinctDevices,
    HotkeyNeedsModifier,
    HotkeyUnsupportedModifier,
    HotkeyF12Reserved,
    HotkeyNormalKeyRequired,
    HotkeyDuplicate,
    HotkeyRegistrationFailed,
    AutoStartOpenFailed,
    AppPathUnavailable,
    AutoStartChangeFailed,
    TrayErrorTitle,
};

const wchar_t* GetText(UiLanguage language, TextId id) noexcept;

}  // namespace audiohotkey
