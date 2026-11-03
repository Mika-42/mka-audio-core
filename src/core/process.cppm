//
// Created by mika on 9/25/26.
//
module;
#include <cstdint>
export module mka.audio.process;

export namespace mka::audio::core {
    struct InputBuffer {
        const float* const* channels;
        std::uint32_t count;
    };

    struct OutputBuffer {
        float* const* channels;
        std::uint32_t count;
    };

    struct AudioProcessContext {
        InputBuffer input;
        OutputBuffer output;
        // Échantillons valides PAR CANAL pour cet appel, pas un nombre d’octets
        // ni frames * count. Peut varier ; ne pas déduire de EndpointConfig::bufferSize.
        // Le Core ne fait pas d’accumulation en blocs DSP fixes.
        std::uint32_t frames;
    };

    using ProcessFunction = void (*)(void* user, const AudioProcessContext&) noexcept;
}
