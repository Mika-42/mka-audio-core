//
// Created by mika on 9/25/26.
//
module;
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
export module mka.audio.endpoint;
import mka.audio.constants;

export namespace mka::audio::core {
    // Input : inputChannels > 0, outputChannels == 0 ; Output : inverse.
    // Duplex : les deux comptes sont > 0 ; le backend doit supporter les deux sens.
    enum class Direction { Input, Output, Duplex };

    struct StreamCapabilities {
        std::uint32_t minChannels;
        std::uint32_t maxChannels;

        std::vector<SampleRate> sampleRates;
        std::vector<Format> formats;
        std::vector<BufferSize> bufferSizes;
    };

    // Découverte par Backend::getEndPoints() : identité et capacités réunies.
    // input/output absents signifient que la direction n’est pas exposée.
    struct Endpoint {
        std::string id;
        std::string name;

        std::optional<StreamCapabilities> input;
        std::optional<StreamCapabilities> output;
    };

    struct EndpointConfig {
        std::string id;
        Direction direction;

        std::uint32_t inputChannels;
        std::uint32_t outputChannels;

        SampleRate sampleRate;
        Format format;
        // Paramètre de transport en frames/canal, jamais une taille de bloc DSP.
        // ALSA : période ; JACK : quantum initial ; PipeWire/PulseAudio : demande
        // de latence. AudioProcessContext::frames donne la taille de chaque appel.
        BufferSize bufferSize;
    };
}
