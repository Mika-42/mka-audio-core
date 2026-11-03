//
// Created by mika on 9/25/26.
//
module;
#include <expected>
export module mka.audio.error;

export namespace mka::audio::core {
    enum class ErrorType {
        InvalidState,          // opération interdite dans l’état courant
        EndpointUnavailable,   // endpoint/serveur absent, inaccessible ou disparu
        FormatNotSupported,    // format demandé inconnu ou non fourni
        ChannelsNotSupported,  // comptes incohérents ou non fournis
        SampleRateNotSupported, // fréquence nulle ou non fournie exactement
        BufferSizeNotSupported, // taille nulle ou paramètre de transport non fourni
        ConfigurationFailed,   // autre échec natif, direction inconnue, exception de hook
    };
    // Succès = Result{} ; échec = std::unexpected{ErrorType::...}. Pas de valeur None.
    // Les erreurs asynchrones utilisent Status/Event, jamais une exception du callback.
    using Result = std::expected<void, ErrorType>;
}
