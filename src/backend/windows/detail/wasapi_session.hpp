#pragma once
#include "wasapi_devices.hpp"
#include "wasapi_event.hpp"
#include <algorithm>
#include <cstring>
namespace mka_wasapi_detail {
struct Request {
    std::string id;
    EDataFlow flow = eRender;
    std::uint32_t channels = 0, rate = 0, frames = 0;
    SampleKind kind = SampleKind::F32;
};
struct Callbacks {
    void* user = nullptr;
    void (*process)(void*,const float* const*,std::uint32_t,float* const*,std::uint32_t,std::uint32_t) noexcept = nullptr;
    void (*read)(SampleKind,const BYTE*,std::size_t,float*,std::uint32_t) noexcept = nullptr;
    void (*write)(SampleKind,BYTE*,std::size_t,const float*,std::uint32_t) noexcept = nullptr;
    void (*xrun)(void*) noexcept = nullptr;
    void (*failed)(void*) noexcept = nullptr;
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
                if (pending.kind == SampleKind::S24Packed && sampleKind(*mix) == SampleKind::S24Msb32)
                    pending.kind = SampleKind::S24Msb32;
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
            if (capacity > std::numeric_limits<std::size_t>::max()/format->Format.nBlockAlign)
                return AUDCLNT_E_BUFFER_SIZE_ERROR;
            std::vector<float> samples(static_cast<std::size_t>(capacity)*pending.channels,0.0f);
            std::vector<float*> outputs;
            std::vector<const float*> inputs;
            if (pending.flow == eRender) {
                outputs.reserve(pending.channels);
                for(std::uint32_t channel=0;channel<pending.channels;++channel)
                    outputs.push_back(samples.data()+static_cast<std::size_t>(channel)*capacity);
            } else {
                inputs.reserve(pending.channels);
                for(std::uint32_t channel=0;channel<pending.channels;++channel)
                    inputs.push_back(samples.data()+static_cast<std::size_t>(channel)*capacity);
            }
            samples_=std::move(samples);outputs_=std::move(outputs);inputs_=std::move(inputs);
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
            std::vector<float>().swap(samples_);
            std::vector<float*>().swap(outputs_);std::vector<const float*>().swap(inputs_);
            request_ = {}; format_ = {}; capacity_ = 0;
            return S_OK;
        });
    }
    HRESULT renderOnce(const Callbacks& callbacks) noexcept {
        if(!client_ || !render_) return E_UNEXPECTED;
        if(callbacks.process && !callbacks.write) return E_INVALIDARG;
        UINT32 padding=0;HRESULT result=client_->GetCurrentPadding(&padding);
        if(FAILED(result)) return result;
        if(padding>capacity_) return E_UNEXPECTED;
        const UINT32 frames=capacity_-padding;
        if(!frames) return S_FALSE;
        BYTE* bytes=nullptr;result=render_->GetBuffer(frames,&bytes);
        if(FAILED(result)) return result;
        if(!bytes) {render_->ReleaseBuffer(frames,AUDCLNT_BUFFERFLAGS_SILENT);return E_POINTER;}
        const auto stride=format_.Format.nBlockAlign;
        std::memset(bytes,0,static_cast<std::size_t>(frames)*stride);
        if(callbacks.process) {
            for(auto* channel:outputs_) std::fill_n(channel,frames,0.0f);
            callbacks.process(callbacks.user,nullptr,0,outputs_.data(),request_.channels,frames);
            const auto width=format_.Format.wBitsPerSample/8;
            for(std::uint32_t channel=0;channel<request_.channels;++channel)
                callbacks.write(request_.kind,bytes+static_cast<std::size_t>(channel)*width,stride,outputs_[channel],frames);
        }
        return render_->ReleaseBuffer(frames,callbacks.process?0:AUDCLNT_BUFFERFLAGS_SILENT);
    }
    HRESULT processCapture(const Callbacks& callbacks) noexcept {
        if(callbacks.process && !callbacks.read) return E_INVALIDARG;
        struct Context {Session* session;const Callbacks* callbacks;} context{this,&callbacks};
        return captureOnce(+[](void* user,const BYTE* bytes,std::uint32_t frames,DWORD flags) noexcept {
            const auto& context=*static_cast<Context*>(user);auto& self=*context.session;
            const auto& callbacks=*context.callbacks;
            if((flags&AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) && callbacks.xrun) callbacks.xrun(callbacks.user);
            if(!callbacks.process) return;
            const auto width=self.format_.Format.wBitsPerSample/8;
            for(std::uint32_t channel=0;channel<self.request_.channels;++channel) {
                auto* plane=self.samples_.data()+static_cast<std::size_t>(channel)*self.capacity_;
                if(flags&AUDCLNT_BUFFERFLAGS_SILENT) std::fill_n(plane,frames,0.0f);
                else callbacks.read(self.request_.kind,bytes+static_cast<std::size_t>(channel)*width,
                    self.format_.Format.nBlockAlign,plane,frames);
            }
            callbacks.process(callbacks.user,self.inputs_.data(),self.request_.channels,nullptr,0,frames);
        },&context);
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
    std::vector<float> samples_;
    std::vector<float*> outputs_;
    std::vector<const float*> inputs_;
};
}
