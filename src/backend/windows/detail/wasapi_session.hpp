#pragma once
#include "wasapi_devices.hpp"
#include "wasapi_event.hpp"
namespace mka_wasapi_detail {
struct Request {
    std::string id;
    EDataFlow flow = eRender;
    std::uint32_t channels = 0, rate = 0, frames = 0;
    SampleKind kind = SampleKind::F32;
};
class Session {
public:
    explicit Session(ControlThread& control, EventApi events = {}) : control_(control), events_(events) {}
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
            UniqueEvent pendingEvent; // declared before COM clients: released after them on rollback
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
            WAVEFORMATEX* rawMix = nullptr;
            const auto mixResult = client->GetMixFormat(&rawMix);
            TaskPtr<WAVEFORMATEX> mix(rawMix);
            if (FAILED(mixResult)) return mixResult;
            if (!mix || !validWaveFormat(*mix)) return AUDCLNT_E_UNSUPPORTED_FORMAT;
            DWORD mask = 0;
            if (mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
                if (mix->nChannels == request.channels)
                    mask = reinterpret_cast<const WAVEFORMATEXTENSIBLE&>(*mix).dwChannelMask;
                if (pending.kind == SampleKind::S24Packed && sampleKind(*mix) == SampleKind::S24In32)
                    pending.kind = SampleKind::S24In32;
            }
            const auto format = makeWaveFormat(pending.kind, pending.channels, pending.rate, mask);
            if (!format) return AUDCLNT_E_UNSUPPORTED_FORMAT;
            WAVEFORMATEX* rawClosest = nullptr;
            const auto support = client->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED, &format->Format, &rawClosest);
            TaskPtr<WAVEFORMATEX> closest(rawClosest);
            // S_FALSE offers a replacement format, not the exact requested one.
            if (support != S_OK) return FAILED(support) ? support : AUDCLNT_E_UNSUPPORTED_FORMAT;
            const auto duration = frameDuration(pending.frames,pending.rate);
            if (!duration) return E_INVALIDARG;
            result = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                *duration, 0, &format->Format, nullptr);
            if (FAILED(result)) return result;
            UINT32 capacity = 0;
            if (FAILED(result = client->GetBufferSize(&capacity))) return result;
            if (!validCapacity(capacity,pending.channels)) return AUDCLNT_E_BUFFER_SIZE_ERROR;
            pendingEvent = UniqueEvent(events_.create(nullptr,FALSE,FALSE,nullptr),events_.close);
            if (!pendingEvent.get()) return lastEventError();
            if (FAILED(result = client->SetEventHandle(pendingEvent.get()))) return result;
            ComPtr<IAudioCaptureClient> capture;
            if (pending.flow == eCapture) {
                if (FAILED(result = client->GetService(__uuidof(IAudioCaptureClient),
                    reinterpret_cast<void**>(capture.GetAddressOf())))) return result;
                if (!capture) return E_POINTER;
            }
            ComPtr<IAudioRenderClient> render;
            if (pending.flow == eRender) {
                if (FAILED(result = client->GetService(__uuidof(IAudioRenderClient),
                    reinterpret_cast<void**>(render.GetAddressOf())))) return result;
                if (!render) return E_POINTER;
            }
            capture_ = std::move(capture);
            render_ = std::move(render);
            audioEvent_ = std::move(pendingEvent);
            capacity_ = capacity;
            format_ = *format;
            request_ = std::move(pending);
            device_ = std::move(device); client_ = std::move(client);
            return S_OK;
        });
    }
    HRESULT close() {
        if (FAILED(control_.initialization())) return control_.initialization();
        return control_.invoke([&]() -> HRESULT {
            capture_.Reset(); render_.Reset(); client_.Reset(); device_.Reset(); audioEvent_.reset();
            request_ = {}; format_ = {}; capacity_ = 0;
            return S_OK;
        });
    }
    using CapturePacket = void (*)(void*, const BYTE*, std::uint32_t, DWORD) noexcept;
    // Called only by the processing owner. No queue, allocation, wait or lock.
    HRESULT captureOnce(CapturePacket process, void* user) noexcept {
        if (!capture_) return E_UNEXPECTED;
        UINT32 pending = 0;
        HRESULT result = capture_->GetNextPacketSize(&pending);
        if (FAILED(result)) return result;
        if (!pending) return S_FALSE;
        if (pending > capacity_) return E_UNEXPECTED;
        BYTE* bytes = nullptr; UINT32 frames = 0; DWORD flags = 0;
        result = capture_->GetBuffer(&bytes,&frames,&flags,nullptr,nullptr);
        if (result == AUDCLNT_S_BUFFER_EMPTY) return S_FALSE;
        if (FAILED(result)) return result;
        constexpr DWORD known = AUDCLNT_BUFFERFLAGS_SILENT | AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY
            | AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR;
        HRESULT status = S_OK;
        if (frames > capacity_ || (flags & ~known)) status = E_UNEXPECTED;
        else if (!frames) status = S_FALSE;
        else if (!bytes && !(flags & AUDCLNT_BUFFERFLAGS_SILENT)) status = E_POINTER;
        else if (process) process(user,bytes,frames,flags);
        const auto released = capture_->ReleaseBuffer(frames);
        return FAILED(released) ? released : status;
    }
    HANDLE audioEvent() const noexcept { return audioEvent_.get(); }
    std::uint32_t capacity() const noexcept { return capacity_; }
    const WAVEFORMATEXTENSIBLE& nativeFormat() const noexcept { return format_; }
private:
    ControlThread& control_;
    EventApi events_;
    UniqueEvent audioEvent_;
    ComPtr<IMMDevice> device_;
    ComPtr<IAudioClient> client_;
    ComPtr<IAudioRenderClient> render_;
    ComPtr<IAudioCaptureClient> capture_;
    Request request_;
    WAVEFORMATEXTENSIBLE format_{};
    std::uint32_t capacity_ = 0;
};
}
