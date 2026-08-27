#pragma once

#include <windows.h>

#include <string>

namespace audiohotkey {

enum class HotkeyValidationIssue {
    None,
    NeedsModifier,
    UnsupportedModifier,
    F12Reserved,
    NormalKeyRequired,
};

bool ValidateHotkey(UINT modifiers, UINT virtualKey, HotkeyValidationIssue& issue);

std::wstring ChooseToggleTarget(
    const std::wstring& currentEndpointId,
    const std::wstring& endpointAId,
    const std::wstring& endpointBId);

}  // namespace audiohotkey
