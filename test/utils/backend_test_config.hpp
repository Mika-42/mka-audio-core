#pragma once

// Include after importing mka.audio.backend.abstract.
inline mka::audio::core::EndpointConfig testEndpointConfig() {
    using namespace mka::audio::core;
    return EndpointConfig{
        .id = "test:output", .direction = Direction::Output,
        .inputChannels = 0, .outputChannels = 2,
        .sampleRate = 48000, .format = Format::Float32, .bufferSize = 256
    };
}
