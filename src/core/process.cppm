//
// Created by mika on 9/25/26.
//
module;
#include <cstdint>
export module mka.audio.process;

export namespace mka::audio::core {
    // Vues planaires Float32 empruntées : tables et échantillons appartiennent
    // au backend et ne restent valides que jusqu’au retour de ProcessFunction.
    // Ne pas les conserver, libérer, redimensionner ou utiliser depuis un autre
    // thread. count == 0 => channels == nullptr ; sinon chaque canal est non nul
    // et contient au moins AudioProcessContext::frames échantillons alignés.
    // L’entrée est en lecture seule, sortie et entrée ne se recouvrent pas.
    struct InputBuffer {
        const float* const* channels;
        std::uint32_t count;
    };

    // Échantillons modifiables, table de pointeurs non modifiable par le client.
    // La sortie est initialisée au silence avant chaque callback ; sans callback,
    // le backend produit du silence. Les écritures doivent rester dans [0, frames).
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

    // Appelé sur le thread audio natif ; ne doit pas lever, allouer, bloquer,
    // journaliser ni appeler les méthodes de contrôle du backend. user doit
    // rester valide jusqu’au retour de stop() (ou à la destruction du backend).
    using ProcessFunction = void (*)(void* user, const AudioProcessContext&) noexcept;
}
