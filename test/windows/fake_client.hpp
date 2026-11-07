#pragma once
#include "../../src/backend/windows/detail/wasapi_format.hpp"
#include "fake_com.hpp"
#include <atomic>
#include "fake_render.hpp"
#include "fake_capture.hpp"
#include <cstring>
namespace wasapi_test {
inline std::atomic<int> liveClients{0};
class FakeAudioClient : public FakeComObject<FakeAudioClient, IAudioClient> {
public:
    WAVEFORMATEXTENSIBLE mix{};
    HRESULT mixResult=S_OK, periodResult=S_OK, supportResult=S_OK;
    WAVEFORMATEXTENSIBLE requested{};
    AUDCLNT_SHAREMODE requestedMode=AUDCLNT_SHAREMODE_EXCLUSIVE;
    unsigned supportCalls=0, initializeCalls=0;
    HRESULT initializeResult=S_OK, bufferSizeResult=S_OK, eventResult=S_OK, renderServiceResult=S_OK;
    HRESULT captureServiceResult=S_OK;unsigned captureServiceCalls=0;
    Microsoft::WRL::ComPtr<FakeCapture> capture=makeFake<FakeCapture>();
    HANDLE audioEvent=nullptr;unsigned eventCalls=0, renderServiceCalls=0;
    Microsoft::WRL::ComPtr<FakeRender> render=makeFake<FakeRender>();
    UINT32 bufferFrames=480;
    std::atomic<UINT32> padding{0};std::atomic<HRESULT> paddingResult{S_OK};
    AUDCLNT_SHAREMODE initializedMode=AUDCLNT_SHAREMODE_EXCLUSIVE;
    DWORD initializedFlags=0;REFERENCE_TIME duration=0,periodicity=-1;
    WAVEFORMATEXTENSIBLE initializedFormat{};
    REFERENCE_TIME period=100000, minimum=30000;
    ~FakeAudioClient() { --liveClients; }
    FakeAudioClient() {
        ++liveClients;
        mix.Format={WAVE_FORMAT_EXTENSIBLE,2,48000,384000,8,32,22};
        mix.Samples.wValidBitsPerSample=32;mix.dwChannelMask=3;mix.SubFormat=KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    }
    HRESULT STDMETHODCALLTYPE Initialize(AUDCLNT_SHAREMODE mode, DWORD flags, REFERENCE_TIME requested, REFERENCE_TIME period,
        const WAVEFORMATEX* f, LPCGUID) override {
        ++initializeCalls;initializedMode=mode;initializedFlags=flags;duration=requested;periodicity=period;
        initializedFormat={};std::memcpy(&initializedFormat,f,sizeof(WAVEFORMATEX)+f->cbSize);return initializeResult;
    }
    HRESULT STDMETHODCALLTYPE GetBufferSize(UINT32* out) override { *out=bufferFrames;return bufferSizeResult; }
    HRESULT STDMETHODCALLTYPE GetStreamLatency(REFERENCE_TIME*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetCurrentPadding(UINT32* out) override { *out=padding.load();return paddingResult.load(); }
    HRESULT STDMETHODCALLTYPE IsFormatSupported(AUDCLNT_SHAREMODE mode, const WAVEFORMATEX* f, WAVEFORMATEX** closest) override {
        ++supportCalls;requestedMode=mode;requested={};
        std::memcpy(&requested,f,sizeof(WAVEFORMATEX)+f->cbSize);
        *closest=nullptr;
        if (supportResult==S_FALSE) { *closest=static_cast<WAVEFORMATEX*>(CoTaskMemAlloc(sizeof(mix)));if (!*closest) return E_OUTOFMEMORY;std::memcpy(*closest,&mix,sizeof(mix)); }
        return supportResult;
    }
    HRESULT STDMETHODCALLTYPE GetMixFormat(WAVEFORMATEX** out) override {
        *out=static_cast<WAVEFORMATEX*>(CoTaskMemAlloc(sizeof(mix)));if (!*out) return E_OUTOFMEMORY;
        std::memcpy(*out,&mix,sizeof(mix));return mixResult;
    }
    HRESULT STDMETHODCALLTYPE GetDevicePeriod(REFERENCE_TIME* normal, REFERENCE_TIME* min) override { *normal=period;*min=minimum;return periodResult; }
    HRESULT STDMETHODCALLTYPE Start() override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Stop() override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Reset() override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetEventHandle(HANDLE event) override { ++eventCalls;audioEvent=event;return eventResult; }
    HRESULT STDMETHODCALLTYPE GetService(REFIID iid, void** out) override {
        *out=nullptr;
        if (iid==__uuidof(IAudioRenderClient)) { ++renderServiceCalls;if (FAILED(renderServiceResult)) return renderServiceResult;return render->QueryInterface(iid,out); }
        if (iid==__uuidof(IAudioCaptureClient)) { ++captureServiceCalls;if(FAILED(captureServiceResult))return captureServiceResult;return capture->QueryInterface(iid,out); }
        return E_NOINTERFACE;
    }
};
}
