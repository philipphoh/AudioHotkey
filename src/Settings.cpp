#include "Settings.h"

#include <shlobj.h>

#include <array>
#include <utility>

namespace audiohotkey {
namespace {

constexpr wchar_t kSection[] = L"AudioHotkey";

std::wstring ReadString(const std::wstring& path, const wchar_t* key) {
    std::array<wchar_t, 4096> buffer{};
    GetPrivateProfileStringW(kSection, key, L"", buffer.data(),
                             static_cast<DWORD>(buffer.size()), path.c_str());
    return buffer.data();
}

UINT ReadUInt(const std::wstring& path, const wchar_t* key, const UINT fallback) {
    return static_cast<UINT>(GetPrivateProfileIntW(kSection, key, static_cast<INT>(fallback), path.c_str()));
}

bool WriteString(const std::wstring& path, const wchar_t* key, const std::wstring& value) {
    return WritePrivateProfileStringW(kSection, key, value.c_str(), path.c_str()) != FALSE;
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

bool EnsureParentDirectory(const std::wstring& path, const UiLanguage language,
                           std::wstring& error) {
    const size_t separator = path.find_last_of(L"\\/");
    if (separator == std::wstring::npos) {
        return true;
    }
    const std::wstring directory = path.substr(0, separator);
    const int result = SHCreateDirectoryExW(nullptr, directory.c_str(), nullptr);
    if (result == ERROR_SUCCESS || result == ERROR_FILE_EXISTS || result == ERROR_ALREADY_EXISTS) {
        return true;
    }
    error = (language == UiLanguage::English
                 ? L"The settings directory could not be created: "
                 : L"Konfigurationsordner konnte nicht erstellt werden: ") +
            Win32ErrorMessage(result, language);
    return false;
}

}  // namespace

SettingsStore::SettingsStore(std::wstring path) : path_(std::move(path)) {}

std::wstring SettingsStore::DefaultPath() {
    PWSTR localAppData = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &localAppData)) ||
        localAppData == nullptr) {
        return {};
    }
    std::wstring path(localAppData);
    CoTaskMemFree(localAppData);
    path += L"\\AudioHotkey\\settings.ini";
    return path;
}

AppSettings SettingsStore::Load() const {
    AppSettings settings;
    if (path_.empty()) {
        return settings;
    }

    settings.endpointAId = ReadString(path_, L"DeviceAId");
    settings.endpointAName = ReadString(path_, L"DeviceAName");
    settings.endpointBId = ReadString(path_, L"DeviceBId");
    settings.endpointBName = ReadString(path_, L"DeviceBName");
    settings.hotkeyModifiers = ReadUInt(path_, L"HotkeyModifiers", 0);
    settings.hotkeyVirtualKey = ReadUInt(path_, L"HotkeyVirtualKey", 0);
    settings.autoStart = ReadUInt(path_, L"AutoStart", 0) != 0;
    settings.notifications = ReadUInt(path_, L"Notifications", 1) != 0;
    settings.closeBehavior = ReadUInt(path_, L"CloseBehavior", 0) == 1
                                 ? CloseBehavior::Exit
                                 : CloseBehavior::HideToTray;
    settings.language = ReadUInt(path_, L"Language", 0) == 1
                            ? UiLanguage::English
                            : UiLanguage::German;
    return settings;
}

bool SettingsStore::Save(const AppSettings& settings, std::wstring& error) const {
    error.clear();
    if (path_.empty()) {
        error = settings.language == UiLanguage::English
                    ? L"The settings file path is not available."
                    : L"Der Pfad für die Konfigurationsdatei ist nicht verfügbar.";
        return false;
    }
    if (!EnsureParentDirectory(path_, settings.language, error)) {
        return false;
    }

    const std::wstring temporaryPath = path_ + L".tmp";
    DeleteFileW(temporaryPath.c_str());

    const bool written =
        WriteString(temporaryPath, L"DeviceAId", settings.endpointAId) &&
        WriteString(temporaryPath, L"DeviceAName", settings.endpointAName) &&
        WriteString(temporaryPath, L"DeviceBId", settings.endpointBId) &&
        WriteString(temporaryPath, L"DeviceBName", settings.endpointBName) &&
        WriteString(temporaryPath, L"HotkeyModifiers", std::to_wstring(settings.hotkeyModifiers)) &&
        WriteString(temporaryPath, L"HotkeyVirtualKey", std::to_wstring(settings.hotkeyVirtualKey)) &&
        WriteString(temporaryPath, L"AutoStart", settings.autoStart ? L"1" : L"0") &&
        WriteString(temporaryPath, L"Notifications", settings.notifications ? L"1" : L"0") &&
        WriteString(temporaryPath, L"CloseBehavior",
                    settings.closeBehavior == CloseBehavior::Exit ? L"1" : L"0") &&
        WriteString(temporaryPath, L"Language",
                    settings.language == UiLanguage::English ? L"1" : L"0");

    WritePrivateProfileStringW(nullptr, nullptr, nullptr, temporaryPath.c_str());

    if (!written) {
        const DWORD code = GetLastError() == ERROR_SUCCESS ? ERROR_WRITE_FAULT : GetLastError();
        DeleteFileW(temporaryPath.c_str());
        error = (settings.language == UiLanguage::English
                     ? L"Settings could not be written: "
                     : L"Einstellungen konnten nicht geschrieben werden: ") +
                Win32ErrorMessage(code, settings.language);
        return false;
    }
    if (!MoveFileExW(temporaryPath.c_str(), path_.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD code = GetLastError();
        DeleteFileW(temporaryPath.c_str());
        error = (settings.language == UiLanguage::English
                     ? L"Settings could not be applied: "
                     : L"Einstellungen konnten nicht übernommen werden: ") +
                Win32ErrorMessage(code, settings.language);
        return false;
    }
    return true;
}

}  // namespace audiohotkey
