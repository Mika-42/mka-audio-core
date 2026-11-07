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

    // Instantané indicatif : les listes ne garantissent ni toutes les valeurs
    // possibles, ni le produit cartésien de leurs combinaisons. Elles peuvent
    // provenir de sondes natives (ALSA/JACK) ou de candidats (PipeWire/PulseAudio).
    // L’état du serveur/périphérique peut changer ; open() revalide le flux réel.
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

    // Demande immuable pendant l’ouverture : ni approximation de fréquence,
    // ni substitution de format/canaux. open() vérifie le flux natif avant succès.
    // Les capacités de découverte ne remplacent jamais cette validation.
    struct EndpointConfig {
        std::string id;
        // Duplex désigne UN endpoint et UN callback avec les deux sens.
        // ALSA : PCM liés ; JACK : ports du même client. PipeWire/PulseAudio
        // rejettent actuellement Duplex avec ConfigurationFailed.
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

#if defined(__MINGW32__) && defined(__GNUC__) && !defined(__clang__) && __GNUC__ == 15
namespace {
// GCC 15/COFF can omit implicit aggregate destructors needed by module consumers.
// Emit them in their owning module without declaring special members, which
// would suppress implicit moves. Keep this unused anchor uninlined in Release.
[[gnu::used, gnu::optimize("O0")]]
void emitEndpointDestructors(mka::audio::core::Endpoint* endpoint,
                            mka::audio::core::StreamCapabilities* capabilities) noexcept {
    endpoint->~Endpoint();
    capabilities->~StreamCapabilities();
}
}
#endif
