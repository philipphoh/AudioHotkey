#include "AudioEndpoint.h"

#include "CoreLogic.h"
#include "Localization.h"

#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propvarutil.h>

#include <algorithm>
#include <array>
#include <cwchar>

namespace audiohotkey {
namespace {

struct DeviceShareMode {
    AUDCLNT_SHAREMODE mode;
    BOOL unknown;
};

// Windows exposes no documented desktop API for changing the default endpoint.
// Keep the private interface in this translation unit so it can be replaced if
// a future Windows version provides a supported setter.
struct __declspec(novtable) IPolicyConfig : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetMixFormat(PCWSTR, WAVEFORMATEX**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(PCWSTR, INT, WAVEFORMATEX**) = 0;
    virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(PCWSTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(PCWSTR, WAVEFORMATEX*, WAVEFORMATEX*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(PCWSTR, INT, PINT64, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(PCWSTR, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetShareMode(PCWSTR, DeviceShareMode*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetShareMode(PCWSTR, DeviceShareMode*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(PCWSTR, ERole) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(PCWSTR, INT) = 0;
};

constexpr CLSID kPolicyConfigClient = {
    0x870af99c, 0x171d, 0x4f9e, {0xaf, 0x0d, 0xe6, 0x3d, 0xf4, 0x0c, 0x2b, 0xc9}};
constexpr IID kPolicyConfigInterface = {
    0xf8679f50, 0x850a, 0x41cf, {0x9c, 0x72, 0x43, 0x0f, 0x29, 0x02, 0x90, 0xc8}};
constexpr CLSID kPolicyConfigVistaClient = {
    0x294935ce, 0xf637, 0x4e7c, {0xa4, 0x1b, 0xab, 0x25, 0x54, 0x60, 0xb8, 0x62}};
constexpr IID kPolicyConfigVistaInterface = {
    0x568b9108, 0x44bf, 0x40b4, {0x90, 0x06, 0x86, 0xaf, 0xe5, 0xb5, 0xa6, 0x20}};

HRESULT CreatePolicyConfig(IPolicyConfig** policy) {
    if (policy == nullptr) {
        return E_POINTER;
    }
    *policy = nullptr;
    HRESULT result = CoCreateInstance(kPolicyConfigClient, nullptr, CLSCTX_ALL,
                                      kPolicyConfigInterface,
                                      reinterpret_cast<void**>(policy));
    if (FAILED(result)) {
        result = CoCreateInstance(kPolicyConfigVistaClient, nullptr, CLSCTX_ALL,
                                  kPolicyConfigVistaInterface,
                                  reinterpret_cast<void**>(policy));
    }
    return result;
}

std::wstring HResultText(const HRESULT result) {
    wchar_t value[32]{};
    swprintf_s(value, L"0x%08X", static_cast<unsigned int>(result));
    return value;
}

bool SameId(const std::wstring& left, const std::wstring& right) {
    return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_EQUAL;
}

}  // namespace

AudioEndpointService::AudioEndpointService() = default;

AudioEndpointService::~AudioEndpointService() {
    Shutdown();
}

HRESULT AudioEndpointService::Initialize() {
    if (enumerator_ != nullptr) {
        return S_OK;
    }
    HRESULT result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                      IID_PPV_ARGS(&enumerator_));
    if (FAILED(result)) {
        return result;
    }
    result = enumerator_->RegisterEndpointNotificationCallback(this);
    if (FAILED(result)) {
        enumerator_.Reset();
        return result;
    }
    callbackRegistered_ = true;
    return S_OK;
}

void AudioEndpointService::SetNotificationTarget(const HWND window, const UINT messageId) noexcept {
    notificationMessage_.store(messageId);
    notificationWindow_.store(window);
}

void AudioEndpointService::Shutdown() noexcept {
    notificationWindow_.store(nullptr);
    notificationMessage_.store(0);
    if (callbackRegistered_ && enumerator_ != nullptr) {
        enumerator_->UnregisterEndpointNotificationCallback(this);
    }
    callbackRegistered_ = false;
    enumerator_.Reset();
}

std::vector<AudioEndpoint> AudioEndpointService::ListActiveRenderEndpoints() const {
    std::vector<AudioEndpoint> endpoints;
    if (enumerator_ == nullptr) {
        return endpoints;
    }

    Microsoft::WRL::ComPtr<IMMDeviceCollection> collection;
    if (FAILED(enumerator_->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection))) {
        return endpoints;
    }

    UINT count = 0;
    if (FAILED(collection->GetCount(&count))) {
        return endpoints;
    }
    endpoints.reserve(count);

    for (UINT index = 0; index < count; ++index) {
        Microsoft::WRL::ComPtr<IMMDevice> device;
        if (FAILED(collection->Item(index, &device))) {
            continue;
        }

        LPWSTR rawId = nullptr;
        if (FAILED(device->GetId(&rawId)) || rawId == nullptr) {
            continue;
        }
        std::wstring id(rawId);
        CoTaskMemFree(rawId);

        std::wstring name = id;
        Microsoft::WRL::ComPtr<IPropertyStore> properties;
        if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &properties))) {
            PROPVARIANT value;
            PropVariantInit(&value);
            if (SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &value)) &&
                value.vt == VT_LPWSTR && value.pwszVal != nullptr) {
                name = value.pwszVal;
            }
            PropVariantClear(&value);
        }
        endpoints.push_back({std::move(id), std::move(name), true});
    }

    std::sort(endpoints.begin(), endpoints.end(), [](const AudioEndpoint& left, const AudioEndpoint& right) {
        return CompareStringOrdinal(left.name.c_str(), -1, right.name.c_str(), -1, TRUE) == CSTR_LESS_THAN;
    });
    return endpoints;
}

HRESULT AudioEndpointService::GetDefaultEndpointId(const ERole role, std::wstring& id) const {
    id.clear();
    if (enumerator_ == nullptr) {
        return CO_E_NOTINITIALIZED;
    }
    Microsoft::WRL::ComPtr<IMMDevice> device;
    HRESULT result = enumerator_->GetDefaultAudioEndpoint(eRender, role, &device);
    if (FAILED(result)) {
        return result;
    }
    LPWSTR rawId = nullptr;
    result = device->GetId(&rawId);
    if (SUCCEEDED(result) && rawId != nullptr) {
        id = rawId;
    }
    CoTaskMemFree(rawId);
    return result;
}

std::wstring AudioEndpointService::GetEndpointName(const std::wstring& id) const {
    if (enumerator_ == nullptr || id.empty()) {
        return {};
    }
    Microsoft::WRL::ComPtr<IMMDevice> device;
    if (FAILED(enumerator_->GetDevice(id.c_str(), &device))) {
        return {};
    }
    Microsoft::WRL::ComPtr<IPropertyStore> properties;
    if (FAILED(device->OpenPropertyStore(STGM_READ, &properties))) {
        return {};
    }
    PROPVARIANT value;
    PropVariantInit(&value);
    std::wstring name;
    if (SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &value)) &&
        value.vt == VT_LPWSTR && value.pwszVal != nullptr) {
        name = value.pwszVal;
    }
    PropVariantClear(&value);
    return name;
}

bool AudioEndpointService::IsEndpointActive(const std::wstring& id) const {
    if (enumerator_ == nullptr || id.empty()) {
        return false;
    }
    Microsoft::WRL::ComPtr<IMMDevice> device;
    DWORD state = 0;
    return SUCCEEDED(enumerator_->GetDevice(id.c_str(), &device)) &&
           SUCCEEDED(device->GetState(&state)) && (state & DEVICE_STATE_ACTIVE) != 0;
}

SwitchResult AudioEndpointService::Toggle(const std::wstring& endpointAId,
                                          const std::wstring& endpointBId,
                                          const UiLanguage language) const {
    const bool english = language == UiLanguage::English;
    SwitchResult result;
    std::wstring currentId;
    const HRESULT currentResult = GetDefaultEndpointId(eConsole, currentId);
    if (FAILED(currentResult)) {
        result.message = (english
                              ? L"The current default device could not be determined ("
                              : L"Das aktuelle Standardgerät konnte nicht ermittelt werden (") +
                         HResultText(currentResult) + L").";
        return result;
    }

    result.endpointId = ChooseToggleTarget(currentId, endpointAId, endpointBId);
    if (result.endpointId.empty()) {
        result.message = english
                             ? L"Please configure two different playback devices."
                             : L"Bitte zwei unterschiedliche Wiedergabegeräte konfigurieren.";
        return result;
    }
    if (!IsEndpointActive(result.endpointId)) {
        result.message = english
                             ? L"The target device is currently disconnected or disabled."
                             : L"Das Zielgerät ist derzeit getrennt oder deaktiviert.";
        return result;
    }
    result.endpointName = GetEndpointName(result.endpointId);
    if (result.endpointName.empty()) {
        result.endpointName = result.endpointId;
    }

    const std::array<ERole, 3> roles = {eConsole, eMultimedia, eCommunications};
    std::vector<std::wstring> previousIds;
    previousIds.reserve(roles.size());
    for (const ERole role : roles) {
        std::wstring previous;
        (void)GetDefaultEndpointId(role, previous);
        previousIds.push_back(std::move(previous));
    }

    std::vector<ERole> changedRoles;
    changedRoles.reserve(roles.size());
    for (const ERole role : roles) {
        const HRESULT setResult = SetDefaultEndpoint(result.endpointId, role);
        if (FAILED(setResult)) {
            RollBack(changedRoles, previousIds);
            result.message = (english
                                  ? L"Windows rejected the device switch ("
                                  : L"Windows hat den Gerätewechsel abgelehnt (") +
                             HResultText(setResult) + L").";
            return result;
        }
        changedRoles.push_back(role);
    }

    for (const ERole role : roles) {
        std::wstring verifiedId;
        const HRESULT verifyResult = GetDefaultEndpointId(role, verifiedId);
        if (FAILED(verifyResult) || !SameId(verifiedId, result.endpointId)) {
            RollBack(changedRoles, previousIds);
            result.message = english
                                 ? L"The device switch could not be fully verified."
                                 : L"Der Gerätewechsel konnte nicht vollständig bestätigt werden.";
            return result;
        }
    }

    result.success = true;
    result.message = std::wstring(GetText(language, TextId::ActivePrefix)) + result.endpointName;
    return result;
}

ULONG AudioEndpointService::AddRef() {
    return ++referenceCount_;
}

ULONG AudioEndpointService::Release() {
    const ULONG remaining = --referenceCount_;
    if (remaining == 0) {
        delete this;
    }
    return remaining;
}

HRESULT AudioEndpointService::QueryInterface(REFIID interfaceId, void** object) {
    if (object == nullptr) {
        return E_POINTER;
    }
    *object = nullptr;
    if (interfaceId == __uuidof(IUnknown) || interfaceId == __uuidof(IMMNotificationClient)) {
        *object = static_cast<IMMNotificationClient*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

HRESULT AudioEndpointService::OnDeviceStateChanged(LPCWSTR, DWORD) {
    PostChangeNotification();
    return S_OK;
}

HRESULT AudioEndpointService::OnDeviceAdded(LPCWSTR) {
    PostChangeNotification();
    return S_OK;
}

HRESULT AudioEndpointService::OnDeviceRemoved(LPCWSTR) {
    PostChangeNotification();
    return S_OK;
}

HRESULT AudioEndpointService::OnDefaultDeviceChanged(const EDataFlow flow, ERole, LPCWSTR) {
    if (flow == eRender || flow == eAll) {
        PostChangeNotification();
    }
    return S_OK;
}

HRESULT AudioEndpointService::OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) {
    PostChangeNotification();
    return S_OK;
}

void AudioEndpointService::PostChangeNotification() const noexcept {
    const HWND window = notificationWindow_.load();
    const UINT message = notificationMessage_.load();
    if (window != nullptr && message != 0) {
        PostMessageW(window, message, 0, 0);
    }
}

HRESULT AudioEndpointService::SetDefaultEndpoint(const std::wstring& id, const ERole role) const {
    Microsoft::WRL::ComPtr<IPolicyConfig> policy;
    IPolicyConfig* rawPolicy = nullptr;
    const HRESULT createResult = CreatePolicyConfig(&rawPolicy);
    if (FAILED(createResult)) {
        return createResult;
    }
    policy.Attach(rawPolicy);
    return policy->SetDefaultEndpoint(id.c_str(), role);
}

void AudioEndpointService::RollBack(const std::vector<ERole>& changedRoles,
                                    const std::vector<std::wstring>& previousIds) const noexcept {
    for (size_t index = 0; index < changedRoles.size() && index < previousIds.size(); ++index) {
        if (!previousIds[index].empty()) {
            SetDefaultEndpoint(previousIds[index], changedRoles[index]);
        }
    }
}

}  // namespace audiohotkey
