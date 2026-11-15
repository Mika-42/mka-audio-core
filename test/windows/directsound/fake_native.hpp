#pragma once
#include <atomic>
#include <cstring>
#include <vector>
#include "../../../src/backend/windows/detail/directsound_native.hpp"

namespace directsound_test {
using namespace mka_ds_detail;
struct Native;
template<class Derived, class Interface> struct Object : Interface {
    Native& owner;
    const DWORD ownerThread = GetCurrentThreadId();
    std::atomic<ULONG> references{1};
    explicit Object(Native& native);
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** result) override { *result = nullptr; return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
    ULONG STDMETHODCALLTYPE Release() override;
    void check();
};
struct OutputBuffer;
struct InputBuffer;
struct OutputDevice;
struct InputDevice;
struct Native {
    inline static Native* current = nullptr;
    NativeApi api;
    unsigned live = 0, creations = 0, releases = 0, buffers = 0, outputCreates = 0, inputCreates = 0;
    unsigned capsCalls = 0, attempts = 0, plays = 0, starts = 0, stops = 0, initialized = 0, uninitialized = 0;
    DWORD apartmentFlags = 0;
    bool wrongThread = false, validWindow = false, nullFactorySuccess = false, partialFactory = false, partialBuffer = false;
    bool allowAll = false, rejectAll = false, malformedCaps = false;
    bool substituteRate = false, substituteChannels = false, substituteBits = false, extension = false;
    bool substituteTag = false, substituteAlign = false, substituteAverage = false;
    HRESULT factoryResult = DS_OK, cooperativeResult = DS_OK, capsResult = DS_OK, formatResult = DS_OK, bufferCapsResult = DS_OK;
    DWORD captureChannels = 2, formatBytes = sizeof(WAVEFORMATEX), reportedBufferDelta = 0;
    std::optional<GUID> requestedGuid;
    HWND observedWindow = nullptr;
    std::vector<WAVEFORMATEX> requests;
    static HRESULT WINAPI createOutput(LPCGUID, LPDIRECTSOUND8*, LPUNKNOWN);
    static HRESULT WINAPI createInput(LPCGUID, LPDIRECTSOUNDCAPTURE8*, LPUNKNOWN);
    static HRESULT WINAPI initialize(LPVOID, DWORD flags) {
        auto& mock = *current; ++mock.initialized; mock.apartmentFlags = flags;
        return CoInitializeEx(nullptr, flags);
    }
    static void WINAPI uninitialize() { ++current->uninitialized; CoUninitialize(); }
    Native() {
        current = this; api.createOutput = &createOutput; api.createInput = &createInput;
        api.apartment = {&initialize, &uninitialize}; testNativeOverride = &api;
    }
    ~Native() { testNativeOverride = nullptr; current = nullptr; }
    bool accepts(const WAVEFORMATEX& wave) const {
        return !rejectAll && (allowAll || (wave.nSamplesPerSec == 48000 && wave.nChannels == 2
            && wave.wFormatTag == WAVE_FORMAT_PCM && wave.wBitsPerSample == 16));
    }
    HRESULT copyFormat(const WAVEFORMATEX& wave, WAVEFORMATEX* result, DWORD bytes, LPDWORD written) {
        if (FAILED(formatResult)) return formatResult;
        if (!result || bytes < sizeof(wave)) return DSERR_INVALIDPARAM;
        *result = wave;
        if (substituteRate) result->nSamplesPerSec = 44100;
        if (substituteChannels) result->nChannels = 1;
        if (substituteBits) result->wBitsPerSample = 32;
        if (substituteTag) result->wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
        if (substituteAlign) result->nBlockAlign = 1;
        if (substituteAverage) result->nAvgBytesPerSec = 1;
        if (extension) result->cbSize = 2;
        *written = formatBytes; return DS_OK;
    }
};
template<class D, class I> Object<D,I>::Object(Native& native) : owner(native) { ++owner.creations; ++owner.live; }
template<class D, class I> void Object<D,I>::check() { owner.wrongThread |= GetCurrentThreadId() != ownerThread; }
template<class D, class I> ULONG Object<D,I>::Release() {
    check(); const auto remaining = --references;
    if (!remaining) { ++owner.releases; --owner.live; delete static_cast<D*>(this); }
    return remaining;
}

struct OutputBuffer final : Object<OutputBuffer, IDirectSoundBuffer> {
    WAVEFORMATEX wave;
    DWORD bytes;
    OutputBuffer(Native& mock, const DSBUFFERDESC& desc) : Object(mock), wave(*desc.lpwfxFormat), bytes(desc.dwBufferBytes) { ++owner.buffers; }
    HRESULT STDMETHODCALLTYPE GetCaps(LPDSBCAPS caps) override {
        check(); if (FAILED(owner.bufferCapsResult)) return owner.bufferCapsResult;
        caps->dwBufferBytes = bytes + owner.reportedBufferDelta; return DS_OK;
    }
    HRESULT STDMETHODCALLTYPE GetFormat(LPWAVEFORMATEX result, DWORD count, LPDWORD written) override { check(); return owner.copyFormat(wave, result, count, written); }
    HRESULT STDMETHODCALLTYPE GetCurrentPosition(LPDWORD, LPDWORD) override { return DSERR_UNSUPPORTED; }
    HRESULT STDMETHODCALLTYPE GetVolume(LPLONG) override { return DSERR_UNSUPPORTED; }
    HRESULT STDMETHODCALLTYPE GetPan(LPLONG) override { return DSERR_UNSUPPORTED; }
    HRESULT STDMETHODCALLTYPE GetFrequency(LPDWORD) override { return DSERR_UNSUPPORTED; }
    HRESULT STDMETHODCALLTYPE GetStatus(LPDWORD) override { return DSERR_UNSUPPORTED; }
    HRESULT STDMETHODCALLTYPE Initialize(LPDIRECTSOUND, LPCDSBUFFERDESC) override { return DSERR_UNSUPPORTED; }
    HRESULT STDMETHODCALLTYPE Lock(DWORD, DWORD, LPVOID*, LPDWORD, LPVOID*, LPDWORD, DWORD) override { return DSERR_UNSUPPORTED; }
    HRESULT STDMETHODCALLTYPE Play(DWORD, DWORD, DWORD) override { ++owner.plays; return DS_OK; }
    HRESULT STDMETHODCALLTYPE SetCurrentPosition(DWORD) override { return DSERR_UNSUPPORTED; }
    HRESULT STDMETHODCALLTYPE SetFormat(LPCWAVEFORMATEX) override { return DSERR_UNSUPPORTED; }
    HRESULT STDMETHODCALLTYPE SetVolume(LONG) override { return DSERR_UNSUPPORTED; }
    HRESULT STDMETHODCALLTYPE SetPan(LONG) override { return DSERR_UNSUPPORTED; }
    HRESULT STDMETHODCALLTYPE SetFrequency(DWORD) override { return DSERR_UNSUPPORTED; }
    HRESULT STDMETHODCALLTYPE Stop() override { ++owner.stops; return DS_OK; }
    HRESULT STDMETHODCALLTYPE Unlock(LPVOID, DWORD, LPVOID, DWORD) override { return DSERR_UNSUPPORTED; }
    HRESULT STDMETHODCALLTYPE Restore() override { return DSERR_UNSUPPORTED; }
};
struct InputBuffer final : Object<InputBuffer, IDirectSoundCaptureBuffer> {
    WAVEFORMATEX wave;
    DWORD bytes;
    InputBuffer(Native& mock, const DSCBUFFERDESC& desc) : Object(mock), wave(*desc.lpwfxFormat), bytes(desc.dwBufferBytes) { ++owner.buffers; }
    HRESULT STDMETHODCALLTYPE GetCaps(LPDSCBCAPS caps) override {
        check(); if (FAILED(owner.bufferCapsResult)) return owner.bufferCapsResult;
        caps->dwBufferBytes = bytes + owner.reportedBufferDelta; return DS_OK;
    }
    HRESULT STDMETHODCALLTYPE GetFormat(LPWAVEFORMATEX result, DWORD count, LPDWORD written) override { check(); return owner.copyFormat(wave, result, count, written); }
    HRESULT STDMETHODCALLTYPE GetCurrentPosition(LPDWORD, LPDWORD) override { return DSERR_UNSUPPORTED; }
    HRESULT STDMETHODCALLTYPE GetStatus(LPDWORD) override { return DSERR_UNSUPPORTED; }
    HRESULT STDMETHODCALLTYPE Initialize(LPDIRECTSOUNDCAPTURE, LPCDSCBUFFERDESC) override { return DSERR_UNSUPPORTED; }
    HRESULT STDMETHODCALLTYPE Lock(DWORD, DWORD, LPVOID*, LPDWORD, LPVOID*, LPDWORD, DWORD) override { return DSERR_UNSUPPORTED; }
    HRESULT STDMETHODCALLTYPE Start(DWORD) override { ++owner.starts; return DS_OK; }
    HRESULT STDMETHODCALLTYPE Stop() override { ++owner.stops; return DS_OK; }
    HRESULT STDMETHODCALLTYPE Unlock(LPVOID, DWORD, LPVOID, DWORD) override { return DSERR_UNSUPPORTED; }
};
struct OutputDevice final : Object<OutputDevice, IDirectSound8> {
    using Object::Object;
    HRESULT STDMETHODCALLTYPE CreateSoundBuffer(LPCDSBUFFERDESC desc, LPDIRECTSOUNDBUFFER* buffer, LPUNKNOWN outer) override {
        check(); ++owner.attempts; owner.requests.push_back(*desc->lpwfxFormat); *buffer = nullptr;
        if (outer || desc->dwSize != sizeof(*desc)) return DSERR_INVALIDPARAM;
        if (!owner.accepts(*desc->lpwfxFormat)) {
            if (owner.partialBuffer) *buffer = new OutputBuffer(owner, *desc);
            return DSERR_BADFORMAT;
        }
        *buffer = new OutputBuffer(owner, *desc); return DS_OK;
    }
    HRESULT STDMETHODCALLTYPE GetCaps(LPDSCAPS caps) override {
        check(); ++owner.capsCalls; if (FAILED(owner.capsResult)) return owner.capsResult;
        caps->dwMinSecondarySampleRate = 100; caps->dwMaxSecondarySampleRate = 200000;
        caps->dwFlags = DSCAPS_SECONDARYMONO | DSCAPS_SECONDARYSTEREO | DSCAPS_SECONDARY16BIT;
        if (owner.malformedCaps) caps->dwSize = 0;
        return DS_OK;
    }
    HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND window, DWORD level) override {
        check(); owner.observedWindow = window; DWORD process = 0;
        owner.validWindow = IsWindow(window) && GetWindowThreadProcessId(window, &process) == ownerThread
            && process == GetCurrentProcessId() && level == DSSCL_NORMAL;
        return owner.cooperativeResult;
    }
    HRESULT STDMETHODCALLTYPE DuplicateSoundBuffer(LPDIRECTSOUNDBUFFER, LPDIRECTSOUNDBUFFER*) override { return DSERR_UNSUPPORTED; }
    HRESULT STDMETHODCALLTYPE Compact() override { return DSERR_UNSUPPORTED; }
    HRESULT STDMETHODCALLTYPE GetSpeakerConfig(LPDWORD) override { return DSERR_UNSUPPORTED; }
    HRESULT STDMETHODCALLTYPE SetSpeakerConfig(DWORD) override { return DSERR_UNSUPPORTED; }
    HRESULT STDMETHODCALLTYPE Initialize(LPCGUID) override { return DSERR_UNSUPPORTED; }
    HRESULT STDMETHODCALLTYPE VerifyCertification(LPDWORD) override { return DSERR_UNSUPPORTED; }
};
struct InputDevice final : Object<InputDevice, IDirectSoundCapture8> {
    using Object::Object;
    HRESULT STDMETHODCALLTYPE CreateCaptureBuffer(LPCDSCBUFFERDESC desc, LPDIRECTSOUNDCAPTUREBUFFER* buffer, LPUNKNOWN outer) override {
        check(); ++owner.attempts; owner.requests.push_back(*desc->lpwfxFormat); *buffer = nullptr;
        if (outer || desc->dwSize != sizeof(*desc)) return DSERR_INVALIDPARAM;
        if (!owner.accepts(*desc->lpwfxFormat)) {
            if (owner.partialBuffer) *buffer = new InputBuffer(owner, *desc);
            return DSERR_BADFORMAT;
        }
        *buffer = new InputBuffer(owner, *desc); return DS_OK;
    }
    HRESULT STDMETHODCALLTYPE GetCaps(LPDSCCAPS caps) override {
        check(); ++owner.capsCalls; if (FAILED(owner.capsResult)) return owner.capsResult;
        caps->dwChannels = owner.captureChannels; if (owner.malformedCaps) caps->dwSize = 0;
        return DS_OK;
    }
    HRESULT STDMETHODCALLTYPE Initialize(LPCGUID) override { return DSERR_UNSUPPORTED; }
};
inline HRESULT WINAPI Native::createOutput(LPCGUID id, LPDIRECTSOUND8* result, LPUNKNOWN outer) {
    auto& mock = *current; ++mock.outputCreates; mock.requestedGuid = id ? std::optional<GUID>(*id) : std::nullopt; *result = nullptr;
    if (outer) return DSERR_INVALIDPARAM;
    if (!mock.nullFactorySuccess && (SUCCEEDED(mock.factoryResult) || mock.partialFactory)) *result = new OutputDevice(mock);
    return mock.factoryResult;
}
inline HRESULT WINAPI Native::createInput(LPCGUID id, LPDIRECTSOUNDCAPTURE8* result, LPUNKNOWN outer) {
    auto& mock = *current; ++mock.inputCreates; mock.requestedGuid = id ? std::optional<GUID>(*id) : std::nullopt; *result = nullptr;
    if (outer) return DSERR_INVALIDPARAM;
    if (!mock.nullFactorySuccess && (SUCCEEDED(mock.factoryResult) || mock.partialFactory)) *result = new InputDevice(mock);
    return mock.factoryResult;
}
}
