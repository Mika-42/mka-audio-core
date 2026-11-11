#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef DIRECTSOUND_VERSION
#define DIRECTSOUND_VERSION 0x0800
#endif
#include <windows.h>
#include <mmreg.h>
#include <dsound.h>

namespace mka_ds_detail {
enum class Failure { none, endpoint, format, channels, rate, buffer, configuration };
inline Failure failure(HRESULT result) noexcept {
    if (SUCCEEDED(result)) return Failure::none;
    switch (result) {
        case DSERR_NODRIVER: case DSERR_ALLOCATED: case E_ACCESSDENIED: return Failure::endpoint;
        case DSERR_BADFORMAT: return Failure::format;
        default: return Failure::configuration;
    }
}
enum class Flow { input, output, duplex };
enum class Sample { i16, i24, i32, f32, f64 };
struct Request {
    std::string id;
    Flow flow = Flow::output;
    std::uint32_t inputs = 0, outputs = 0, rate = 0, frames = 0;
    Sample sample = Sample::f32;
};
struct Capabilities {
    std::uint32_t minimumChannels = 0, maximumChannels = 0;
    std::vector<std::uint32_t> rates;
    std::vector<Sample> formats;
    std::vector<std::uint32_t> buffers;
};
struct Device {
    std::string id, name;
    std::optional<Capabilities> input, output;
};
struct ProcessingCallbacks {
    void* user = nullptr;
    void (*process)(void*, const float* const*, std::uint32_t, float* const*, std::uint32_t, std::uint32_t) noexcept = nullptr;
    void (*xrun)(void*) noexcept = nullptr;
    void (*failed)(void*) noexcept = nullptr;
};
}
