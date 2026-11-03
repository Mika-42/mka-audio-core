#pragma once
#include "wasapi_control.hpp"
#include "wasapi_format.hpp"
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propsys.h>
#include <wrl/client.h>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <limits>

namespace mka_wasapi_detail {
template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;
struct TaskFree { void operator()(void* p) const noexcept { CoTaskMemFree(p); } };
template<class T> using TaskPtr = std::unique_ptr<T, TaskFree>;
struct PropertyValue {
    PROPVARIANT value{};
    ~PropertyValue() { PropVariantClear(&value); }
};
inline std::string utf8(const wchar_t* text) {
    if (!text) return {};
    const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, nullptr, 0, nullptr, nullptr);
    if (length <= 1) return {};
    std::string out(static_cast<std::size_t>(length), '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, out.data(), length, nullptr, nullptr)) return {};
    out.pop_back();
    return out;
}
inline std::wstring utf16(std::string_view text) {
    if (text.empty() || text.find('\0') != std::string_view::npos
        || text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) return {};
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0) return {};
    std::wstring out(static_cast<std::size_t>(length), L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), out.data(), length)) return {};
    return out;
}
struct DeviceInfo {
    std::string id, name;
    EDataFlow flow = eAll;
    std::optional<SampleKind> kind;
    std::uint32_t channels = 0, rate = 0;
    std::vector<std::uint32_t> bufferSizes;
};
inline HRESULT createEnumerator(const ComApi& api, IMMDeviceEnumerator** out) {
    return api.create(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                      __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(out));
}
// This entire snapshot operation executes on the COM owner, never the audio worker.
inline std::vector<DeviceInfo> enumerateDevices(ControlThread& control) {
    if (FAILED(control.initialization())) return {};
    return control.invoke([&] {
        std::vector<DeviceInfo> result;
        ComPtr<IMMDeviceEnumerator> enumerator;
        if (FAILED(createEnumerator(control.api(), enumerator.GetAddressOf())) || !enumerator) return result;
        ComPtr<IMMDeviceCollection> collection;
        if (FAILED(enumerator->EnumAudioEndpoints(eAll, DEVICE_STATE_ACTIVE, collection.GetAddressOf())) || !collection) return result;
        UINT count = 0;
        if (FAILED(collection->GetCount(&count))) return result;
        for (UINT i = 0; i < count; ++i) {
            ComPtr<IMMDevice> device;
            if (FAILED(collection->Item(i, device.GetAddressOf())) || !device) continue;
            DWORD state = 0;
            if (FAILED(device->GetState(&state)) || !(state & DEVICE_STATE_ACTIVE)) continue;
            wchar_t* rawId = nullptr;
            const auto idResult = device->GetId(&rawId);
            TaskPtr<wchar_t> id(rawId);
            if (FAILED(idResult) || !id) continue;
            DeviceInfo info; info.id = utf8(id.get());
            if (info.id.empty()) continue;
            ComPtr<IMMEndpoint> endpoint;
            if (FAILED(device.As(&endpoint)) || FAILED(endpoint->GetDataFlow(&info.flow))
                || (info.flow != eRender && info.flow != eCapture)) continue;
            ComPtr<IPropertyStore> properties;
            if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, properties.GetAddressOf())) && properties) {
                PropertyValue value;
                if (SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &value.value))
                    && value.value.vt == VT_LPWSTR) info.name = utf8(value.value.pwszVal);
            }
            if (info.name.empty()) info.name = info.id;
            ComPtr<IAudioClient> client;
            if (FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                reinterpret_cast<void**>(client.GetAddressOf()))) || !client) continue;
            WAVEFORMATEX* rawFormat = nullptr;
            const auto mixResult = client->GetMixFormat(&rawFormat);
            TaskPtr<WAVEFORMATEX> mix(rawFormat);
            if (FAILED(mixResult) || !mix || !validWaveFormat(*mix)) continue;
            info.kind = sampleKind(*mix); info.channels = mix->nChannels; info.rate = mix->nSamplesPerSec;
            REFERENCE_TIME period = 0, minimum = 0;
            if (SUCCEEDED(client->GetDevicePeriod(&period, &minimum)))
                if (auto frames = periodFrames(period, info.rate)) info.bufferSizes.push_back(*frames);
            result.push_back(std::move(info));
        }
        return result;
    });
}
}
