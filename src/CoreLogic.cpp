#include "CoreLogic.h"

namespace audiohotkey {

bool ValidateHotkey(const UINT modifiers, const UINT virtualKey, HotkeyValidationIssue& issue) {
    issue = HotkeyValidationIssue::None;

    if (virtualKey == 0) {
        return true;
    }

    constexpr UINT allowedModifiers = MOD_ALT | MOD_CONTROL | MOD_SHIFT;
    if ((modifiers & ~allowedModifiers) != 0) {
        issue = HotkeyValidationIssue::UnsupportedModifier;
        return false;
    }
    if (virtualKey == VK_F12) {
        issue = HotkeyValidationIssue::F12Reserved;
        return false;
    }
    if (virtualKey == VK_SHIFT || virtualKey == VK_CONTROL || virtualKey == VK_MENU ||
        virtualKey == VK_LSHIFT || virtualKey == VK_RSHIFT ||
        virtualKey == VK_LCONTROL || virtualKey == VK_RCONTROL ||
        virtualKey == VK_LMENU || virtualKey == VK_RMENU ||
        virtualKey == VK_LWIN || virtualKey == VK_RWIN) {
        issue = HotkeyValidationIssue::NormalKeyRequired;
        return false;
    }
    const bool isFunctionKey = virtualKey >= VK_F1 && virtualKey <= VK_F24;
    const bool isNumpadKey =
        (virtualKey >= VK_NUMPAD0 && virtualKey <= VK_NUMPAD9) ||
        virtualKey == VK_MULTIPLY || virtualKey == VK_ADD ||
        virtualKey == VK_SEPARATOR || virtualKey == VK_SUBTRACT ||
        virtualKey == VK_DECIMAL || virtualKey == VK_DIVIDE;
    if ((modifiers & allowedModifiers) == 0 && !isFunctionKey && !isNumpadKey) {
        issue = HotkeyValidationIssue::NeedsModifier;
        return false;
    }
    return true;
}

std::wstring ChooseToggleTarget(
    const std::wstring& currentEndpointId,
    const std::wstring& endpointAId,
    const std::wstring& endpointBId) {
    if (endpointAId.empty() || endpointBId.empty() || endpointAId == endpointBId) {
        return {};
    }
    return currentEndpointId == endpointAId ? endpointBId : endpointAId;
}

}  // namespace audiohotkey
