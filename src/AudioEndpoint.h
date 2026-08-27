#pragma once

#include "Settings.h"

#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <atomic>
#include <string>
#include <vector>

namespace audiohotkey {

struct AudioEndpoint {
    std::wstring id;
    std::wstring name;
    bool active = true;
};

struct SwitchResult {
    bool success = false;
    std::wstring endpointId;
    std::wstring endpointName;
    std::wstring message;
};

class AudioEndpointService final : public IMMNotificationClient {
public:
    AudioEndpointService();

    HRESULT Initialize();
    void SetNotificationTarget(HWND window, UINT messageId) noexcept;
    void Shutdown() noexcept;

    [[nodiscard]] std::vector<AudioEndpoint> ListActiveRenderEndpoints() const;
    [[nodiscard]] HRESULT GetDefaultEndpointId(ERole role, std::wstring& id) const;
    [[nodiscard]] std::wstring GetEndpointName(const std::wstring& id) const;
    [[nodiscard]] bool IsEndpointActive(const std::wstring& id) const;
    [[nodiscard]] SwitchResult Toggle(const std::wstring& endpointAId,
                                      const std::wstring& endpointBId,
                                      UiLanguage language) const;

    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID interfaceId, void** object) override;

    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR deviceId, DWORD newState) override;
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR deviceId) override;
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR deviceId) override;
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role,
                                                     LPCWSTR defaultDeviceId) override;
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR deviceId,
                                                     const PROPERTYKEY key) override;

private:
    ~AudioEndpointService();

    void PostChangeNotification() const noexcept;
    HRESULT SetDefaultEndpoint(const std::wstring& id, ERole role) const;
    void RollBack(const std::vector<ERole>& changedRoles,
                  const std::vector<std::wstring>& previousIds) const noexcept;

    std::atomic<ULONG> referenceCount_{1};
    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator_;
    std::atomic<HWND> notificationWindow_{nullptr};
    std::atomic<UINT> notificationMessage_{0};
    bool callbackRegistered_ = false;
};

}  // namespace audiohotkey
