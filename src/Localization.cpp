#include "Localization.h"

namespace audiohotkey {

const wchar_t* GetText(const UiLanguage language, const TextId id) noexcept {
    const bool english = language == UiLanguage::English;
    switch (id) {
        case TextId::WindowTitle:
            return english ? L"AudioHotkey – Settings" : L"AudioHotkey – Einstellungen";
        case TextId::DeviceA:
            return english ? L"Playback device A:" : L"Wiedergabegerät A:";
        case TextId::DeviceB:
            return english ? L"Playback device B:" : L"Wiedergabegerät B:";
        case TextId::GlobalHotkey:
            return english ? L"Global hotkey:" : L"Globaler Hotkey:";
        case TextId::HotkeyHint:
            return english
                       ? L"Click the field and press a shortcut. F- and numpad keys work alone; Backspace clears it."
                       : L"In das Feld klicken und eine Kombination drücken. F- und Numpad-Tasten funktionieren einzeln; Rücktaste löscht.";
        case TextId::CurrentDefault:
            return english ? L"Current default device:" : L"Aktuelles Standardgerät:";
        case TextId::HotkeyStatus:
            return english ? L"Hotkey status:" : L"Hotkey-Status:";
        case TextId::AutoStart:
            return english ? L"Start in the background when I sign in" : L"Beim Anmelden im Hintergrund starten";
        case TextId::Notifications:
            return english ? L"Show a tray notification after switching" : L"Tray-Hinweis nach erfolgreichem Wechsel";
        case TextId::Language:
            return english ? L"Language:" : L"Sprache:";
        case TextId::CloseWithX:
            return english ? L"When closing with X:" : L"Beim Schließen mit X:";
        case TextId::HideToTray:
            return english ? L"Close to tray" : L"In den Tray schließen";
        case TextId::ExitProgram:
            return english ? L"Exit application" : L"Programm beenden";
        case TextId::RefreshDevices:
            return english ? L"Refresh devices" : L"Geräte aktualisieren";
        case TextId::Discard:
            return english ? L"Discard" : L"Verwerfen";
        case TextId::Save:
            return english ? L"Save" : L"Speichern";
        case TextId::Settings:
            return english ? L"Settings" : L"Einstellungen";
        case TextId::IconCredit:
            return english ? L"Icon attribution" : L"Icon-Nachweis";
        case TextId::IconCreditTitle:
            return english ? L"AudioHotkey – Icon attribution" : L"AudioHotkey – Icon-Nachweis";
        case TextId::IconCreditMessage:
            return english
                       ? L"Volume up icon created by Bharat Icons and provided by Flaticon.\n\nhttps://www.flaticon.com/free-icon/volume-up_6996058"
                       : L"Das Lautstärke-erhöhen-Icon wurde von Bharat Icons erstellt und von Flaticon bereitgestellt.\n\nhttps://www.flaticon.com/de/kostenloses-icon/lautstarke-erhohen_6996058";
        case TextId::Exit:
            return english ? L"Exit" : L"Beenden";
        case TextId::NotAvailable:
            return english ? L"Not available" : L"Nicht verfügbar";
        case TextId::NotConfigured:
            return english ? L"Not configured" : L"Nicht konfiguriert";
        case TextId::InactivePrefix:
            return english ? L"Inactive: " : L"Nicht aktiv: ";
        case TextId::ActivePrefix:
            return english ? L"Active: " : L"Aktiv: ";
        case TextId::AutoStartError:
            return english ? L" | Autostart error: " : L" | Autostart-Fehler: ";
        case TextId::MissingSuffix:
            return english ? L" (unavailable)" : L" (nicht verfügbar)";
        case TextId::SelectTwoDevices:
            return english ? L"Please select playback devices A and B." : L"Bitte Wiedergabegerät A und B auswählen.";
        case TextId::DistinctDevices:
            return english ? L"Devices A and B must be different." : L"Gerät A und B müssen unterschiedlich sein.";
        case TextId::HotkeyNeedsModifier:
            return english
                       ? L"This key requires Ctrl, Alt or Shift. F-keys and numpad keys may be used on their own."
                       : L"Diese Taste benötigt Strg, Alt oder Umschalt. F- und Numpad-Tasten dürfen einzeln verwendet werden.";
        case TextId::HotkeyUnsupportedModifier:
            return english ? L"This modifier is not supported." : L"Diese Modifikatortaste wird nicht unterstützt.";
        case TextId::HotkeyF12Reserved:
            return english ? L"F12 is reserved system-wide for debuggers." : L"F12 ist systemweit für Debugger reserviert.";
        case TextId::HotkeyNormalKeyRequired:
            return english
                       ? L"Please choose a regular key in addition to the modifier."
                       : L"Bitte zusätzlich zur Modifikatortaste eine normale Taste wählen.";
        case TextId::HotkeyDuplicate:
            return english
                       ? L"This hotkey is already registered by another application. Choose a different key to avoid a duplicate assignment."
                       : L"Dieser Hotkey ist bereits von einem anderen Programm registriert. Bitte eine andere Taste wählen, um die Doppelbelegung zu vermeiden.";
        case TextId::HotkeyRegistrationFailed:
            return english ? L"The hotkey could not be registered: " : L"Der Hotkey konnte nicht registriert werden: ";
        case TextId::AutoStartOpenFailed:
            return english ? L"Autostart could not be opened: " : L"Autostart konnte nicht geöffnet werden: ";
        case TextId::AppPathUnavailable:
            return english ? L"The application path could not be determined." : L"Der Pfad der Anwendung konnte nicht ermittelt werden.";
        case TextId::AutoStartChangeFailed:
            return english ? L"Autostart could not be changed: " : L"Autostart konnte nicht geändert werden: ";
        case TextId::TrayErrorTitle:
            return english ? L"AudioHotkey – Error" : L"AudioHotkey – Fehler";
    }
    return L"";
}

}  // namespace audiohotkey
