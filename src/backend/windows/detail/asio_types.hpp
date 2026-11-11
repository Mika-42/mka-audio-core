#pragma once
#include <cstdint>
#include <string>
#include <asiosys.h>
#include <asio.h>

namespace mka_asio_detail {
enum class Failure { none, endpoint, format, channels, rate, buffer, configuration };
enum class Flow { input, output, duplex };
enum class Sample { i16, i24, i32, f32, f64 };
struct Request {
    std::string id;
    Flow flow;
    std::uint32_t inputs, outputs, rate, frames;
    Sample sample;
};
struct LatencySnapshot {std::uint32_t input, output;};
inline Failure failure(ASIOError error) noexcept {
    switch (error) {
        case ASE_OK: return Failure::none;
        case ASE_NotPresent: case ASE_HWMalfunction: return Failure::endpoint;
        case ASE_NoClock: return Failure::rate;
        default: return Failure::configuration;
    }
}
}
