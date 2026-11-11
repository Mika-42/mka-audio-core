#pragma once
#include <atomic>
#include <memory>
#include "win32_control.hpp"
#include "directsound_ids.hpp"
#include "directsound_format.hpp"

namespace mka_ds_detail {
template<class Interface> struct ReleaseInterface {
    void operator()(Interface* value) const noexcept { if (value) value->Release(); }
};
template<class Interface> using ComRef = std::unique_ptr<Interface, ReleaseInterface<Interface>>;
struct NativeApi {
    decltype(&DirectSoundCreate8) createOutput = &DirectSoundCreate8;
    decltype(&DirectSoundCaptureCreate8) createInput = &DirectSoundCaptureCreate8;
    mka_windows_detail::ApartmentApi apartment;
    mka_windows_detail::WindowApi windows;
};
#if defined(MKA_DIRECTSOUND_TESTING)
inline std::atomic<const NativeApi*> testNativeOverride{nullptr};
#endif
inline NativeApi defaultNativeApi() noexcept {
#if defined(MKA_DIRECTSOUND_TESTING)
    if (const auto* api = testNativeOverride.load(std::memory_order_acquire)) return *api;
#endif
    return {};
}

// All setup, probes and interface releases run in this private MTA. The hidden
// HWND is valid for the entire cooperative-level context and is never exported.
class NativeDevice {
public:
    explicit NativeDevice(NativeApi api = defaultNativeApi())
        : api_(api), control_(api.apartment, COINIT_MULTITHREADED, L"MKA DirectSound control", api.windows) {}
    ~NativeDevice() {
        if (SUCCEEDED(control_.initialization())) control_.invoke([&] { output_.reset(); input_.reset(); });
    }
    NativeDevice(const NativeDevice&) = delete;
    NativeDevice& operator=(const NativeDevice&) = delete;
    Failure initialize(const DeviceId& id) {
        if (FAILED(control_.initialization())) return Failure::configuration;
        return control_.invoke([&] {
            if (output_ || input_ || (id.flow != Flow::input && id.flow != Flow::output)) return Failure::configuration;
            const GUID* guid = id.guid ? &*id.guid : nullptr;
            if (id.flow == Flow::output) {
                if (!api_.createOutput) return Failure::configuration;
                IDirectSound8* raw = nullptr;
                const auto created = api_.createOutput(guid, &raw, nullptr);
                ComRef<IDirectSound8> pending(raw);
                if (FAILED(created)) return failure(created);
                if (!pending) return Failure::configuration;
                const auto cooperative = pending->SetCooperativeLevel(control_.window(), DSSCL_NORMAL);
                if (FAILED(cooperative)) return failure(cooperative);
                output_ = std::move(pending);
            } else {
                if (!api_.createInput) return Failure::configuration;
                IDirectSoundCapture8* raw = nullptr;
                const auto created = api_.createInput(guid, &raw, nullptr);
                ComRef<IDirectSoundCapture8> pending(raw);
                if (FAILED(created)) return failure(created);
                if (!pending) return Failure::configuration;
                input_ = std::move(pending);
            }
            return Failure::none;
        });
    }
    bool capabilitiesValid() {
        return control_.invoke([&] {
            if (output_) {
                DSCAPS caps{}; caps.dwSize = sizeof(caps);
                return SUCCEEDED(output_->GetCaps(&caps)) && caps.dwSize == sizeof(caps)
                    && caps.dwMinSecondarySampleRate && caps.dwMinSecondarySampleRate <= caps.dwMaxSecondarySampleRate;
            }
            if (input_) {
                DSCCAPS caps{}; caps.dwSize = sizeof(caps);
                return SUCCEEDED(input_->GetCaps(&caps)) && caps.dwSize == sizeof(caps) && caps.dwChannels;
            }
            return false;
        });
    }
    bool supports(const Request& request) {
        const auto spec = waveSpec(request);
        if (!spec) return false;
        return control_.invoke([&] {
            auto wave = spec->wave;
            if (output_ && request.flow == Flow::output) {
                DSBUFFERDESC desc{}; desc.dwSize = sizeof(desc); desc.dwFlags = DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_CTRLPOSITIONNOTIFY;
                desc.dwBufferBytes = spec->totalBytes; desc.lpwfxFormat = &wave;
                IDirectSoundBuffer* raw = nullptr;
                const auto created = output_->CreateSoundBuffer(&desc, &raw, nullptr);
                ComRef<IDirectSoundBuffer> pending(raw);
                if (FAILED(created) || !pending) return false;
                WAVEFORMATEX actual{}; DWORD written = 0; DSBCAPS caps{}; caps.dwSize = sizeof(caps);
                return SUCCEEDED(pending->GetFormat(&actual, sizeof(actual), &written)) && written >= sizeof(PCMWAVEFORMAT) && written <= sizeof(actual)
                    && exactWaveFormat(actual, spec->wave) && SUCCEEDED(pending->GetCaps(&caps))
                    && caps.dwSize == sizeof(caps) && caps.dwBufferBytes == spec->totalBytes;
            }
            if (input_ && request.flow == Flow::input) {
                DSCBUFFERDESC desc{}; desc.dwSize = sizeof(desc); desc.dwBufferBytes = spec->totalBytes; desc.lpwfxFormat = &wave;
                IDirectSoundCaptureBuffer* raw = nullptr;
                const auto created = input_->CreateCaptureBuffer(&desc, &raw, nullptr);
                ComRef<IDirectSoundCaptureBuffer> pending(raw);
                if (FAILED(created) || !pending) return false;
                WAVEFORMATEX actual{}; DWORD written = 0; DSCBCAPS caps{}; caps.dwSize = sizeof(caps);
                return SUCCEEDED(pending->GetFormat(&actual, sizeof(actual), &written)) && written >= sizeof(PCMWAVEFORMAT) && written <= sizeof(actual)
                    && exactWaveFormat(actual, spec->wave) && SUCCEEDED(pending->GetCaps(&caps))
                    && caps.dwSize == sizeof(caps) && caps.dwBufferBytes == spec->totalBytes;
            }
            return false;
        });
    }
    DWORD thread() const noexcept { return control_.thread(); }
    HWND window() const noexcept { return control_.window(); }
private:
    const NativeApi api_;
    mka_windows_detail::ControlThread control_;
    ComRef<IDirectSound8> output_;
    ComRef<IDirectSoundCapture8> input_;
};
}
