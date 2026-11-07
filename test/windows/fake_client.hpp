#pragma once
#include "../../src/backend/windows/detail/wasapi_format.hpp"
#include "fake_com.hpp"
#include <atomic>
#include <cstring>
namespace wasapi_test {
inline std::atomic<int> liveClients{0};
class FakeAudioClient : public FakeComObject<FakeAudioClient, IAudioClient> {
public:
    WAVEFORMATEXTENSIBLE mix{};
    HRESULT mixResult=S_OK, periodResult=S_OK, supportResult=S_OK;
    WAVEFORMATEXTENSIBLE requested{};
    AUDCLNT_SHAREMODE requestedMode=AUDCLNT_SHAREMODE_EXCLUSIVE;
    unsigned supportCalls=0;
    REFERENCE_TIME period=100000, minimum=30000;
    ~FakeAudioClient() { --liveClients; }
    FakeAudioClient() {
        ++liveClients;
        mix.Format={WAVE_FORMAT_EXTENSIBLE,2,48000,384000,8,32,22};
        mix.Samples.wValidBitsPerSample=32;mix.dwChannelMask=3;mix.SubFormat=KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    }
    HRESULT STDMETHODCALLTYPE Initialize(AUDCLNT_SHAREMODE, DWORD, REFERENCE_TIME, REFERENCE_TIME, const WAVEFORMATEX*, LPCGUID) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetBufferSize(UINT32*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetStreamLatency(REFERENCE_TIME*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetCurrentPadding(UINT32*) override { return E_NOTIMPL; }
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
    HRESULT STDMETHODCALLTYPE SetEventHandle(HANDLE) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetService(REFIID, void**) override { return E_NOTIMPL; }
};
}
