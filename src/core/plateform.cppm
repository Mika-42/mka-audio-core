module;
#include <array>
#include <cstddef>
#include <mka/audio/build_config.hpp>
export module mka.audio.backend.plateform;

export namespace mka::audio::core {
    // Known identifiers remain stable even when an implementation is unavailable.
    enum class AudioBackend {
        Alsa, PipeWire, Jack, PulseAudio, CoreAudio, Asio, Wasapi, Wmme, DirectSound
    };
    inline constexpr std::array knownBackends{
        AudioBackend::Alsa,AudioBackend::PipeWire,AudioBackend::Jack,
        AudioBackend::PulseAudio,AudioBackend::CoreAudio,AudioBackend::Asio,
        AudioBackend::Wasapi,AudioBackend::Wmme,AudioBackend::DirectSound
    };
    inline constexpr std::array<AudioBackend,
        MKA_BUILD_ALSA+MKA_BUILD_PIPEWIRE+MKA_BUILD_JACK+MKA_BUILD_PULSEAUDIO+MKA_BUILD_COREAUDIO+MKA_BUILD_WASAPI>
    supportedBackends{
#if MKA_BUILD_ALSA
        AudioBackend::Alsa,
#endif
#if MKA_BUILD_PIPEWIRE
        AudioBackend::PipeWire,
#endif
#if MKA_BUILD_JACK
        AudioBackend::Jack,
#endif
#if MKA_BUILD_PULSEAUDIO
        AudioBackend::PulseAudio,
#endif
#if MKA_BUILD_COREAUDIO
        AudioBackend::CoreAudio,
#endif
#if MKA_BUILD_WASAPI
        AudioBackend::Wasapi,
#endif
    };
    [[nodiscard]] constexpr bool isBackendCompiled(AudioBackend backend) noexcept {
        for(auto candidate:supportedBackends)if(candidate==backend)return true;
        return false;
    }
    [[nodiscard]] constexpr bool isBackendKnown(AudioBackend backend) noexcept {
        for(auto candidate:knownBackends)if(candidate==backend)return true;
        return false;
    }
}
