#pragma once
#include "wasapi_devices.hpp"
namespace mka_wasapi_detail {
struct Request {
    std::string id;
    EDataFlow flow = eRender;
    std::uint32_t channels = 0, rate = 0, frames = 0;
    SampleKind kind = SampleKind::F32;
};
class Session {
public:
    explicit Session(ControlThread& control) : control_(control) {}
    ~Session() {
        if (SUCCEEDED(control_.initialization())) {
            try { close(); } catch (...) {}
        }
    }
    HRESULT open(const Request& request) {
        if (FAILED(control_.initialization())) return control_.initialization();
        return control_.invoke([&]() -> HRESULT {
            if (client_) return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
            if ((request.flow != eRender && request.flow != eCapture)
                || !request.channels || request.channels > std::numeric_limits<WORD>::max()
                || !request.rate || !request.frames) return E_INVALIDARG;
            ComPtr<IMMDeviceEnumerator> enumerator;
            HRESULT result = createEnumerator(control_.api(), enumerator.GetAddressOf());
            if (FAILED(result)) return result;
            if (!enumerator) return E_POINTER;
            ComPtr<IMMDevice> device;
            if (request.id.empty()) result = enumerator->GetDefaultAudioEndpoint(request.flow, eConsole, device.GetAddressOf());
            else {
                const auto id = utf16(request.id);
                if (id.empty()) return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
                result = enumerator->GetDevice(id.c_str(), device.GetAddressOf());
            }
            if (FAILED(result)) return result;
            if (!device) return E_POINTER;
            DWORD state = 0;
            if (FAILED(result = device->GetState(&state))) return result;
            if (!(state & DEVICE_STATE_ACTIVE)) return AUDCLNT_E_DEVICE_INVALIDATED;
            ComPtr<IMMEndpoint> endpoint;
            if (FAILED(result = device.As(&endpoint))) return result;
            EDataFlow flow = eAll;
            if (FAILED(result = endpoint->GetDataFlow(&flow))) return result;
            if (flow != request.flow) return AUDCLNT_E_WRONG_ENDPOINT_TYPE;
            ComPtr<IAudioClient> client;
            if (FAILED(result = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                reinterpret_cast<void**>(client.GetAddressOf())))) return result;
            if (!client) return E_POINTER;
            // Commit only after the complete acquisition succeeds. Partial COM
            // resources are released here, on the owner, before returning error.
            Request pending = request;
            request_ = std::move(pending);
            device_ = std::move(device); client_ = std::move(client);
            return S_OK;
        });
    }
    HRESULT close() {
        if (FAILED(control_.initialization())) return control_.initialization();
        return control_.invoke([&]() -> HRESULT {
            client_.Reset(); device_.Reset(); request_ = {};
            return S_OK;
        });
    }
private:
    ControlThread& control_;
    ComPtr<IMMDevice> device_;
    ComPtr<IAudioClient> client_;
    Request request_;
};
}
