module;
#include <string_view>
export module mka.audio.diagnostics;
export import mka.audio.backend.abstract;

export namespace mka::audio::core {
// Static views only: no allocation, SDK dependency, logging or locale lookup.
[[nodiscard]] constexpr std::string_view errorName(ErrorType error) noexcept {
    switch(error){
        case ErrorType::InvalidState:return "InvalidState";
        case ErrorType::EndpointUnavailable:return "EndpointUnavailable";
        case ErrorType::FormatNotSupported:return "FormatNotSupported";
        case ErrorType::ChannelsNotSupported:return "ChannelsNotSupported";
        case ErrorType::SampleRateNotSupported:return "SampleRateNotSupported";
        case ErrorType::BufferSizeNotSupported:return "BufferSizeNotSupported";
        case ErrorType::ConfigurationFailed:return "ConfigurationFailed";
    }
    return "Unknown";
}
[[nodiscard]] constexpr std::string_view realtimeName(RealtimeState state) noexcept {
    switch(state){case RealtimeState::Unknown:return "Unknown";case RealtimeState::No:return "No";case RealtimeState::Yes:return "Yes";}
    return "Unknown";
}
[[nodiscard]] constexpr std::string_view eventName(EventType event) noexcept {
    switch(event){case EventType::XRun:return "XRun";case EventType::Failed:return "Failed";}
    return "Unknown";
}
}
