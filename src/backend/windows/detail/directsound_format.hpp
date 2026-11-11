#pragma once
#include <expected>
#include <limits>
#include "directsound_types.hpp"

namespace mka_ds_detail {
// Processing frames are exact; the native circular buffer holds eight periods.
inline constexpr DWORD ringPeriods = 8;
struct WaveSpec {
    WAVEFORMATEX wave{};
    DWORD periodBytes = 0, totalBytes = 0;
    std::uint32_t frames = 0;
    Sample sample = Sample::f32;
};
inline std::expected<WaveSpec, Failure> waveSpec(const Request& request) noexcept {
    if (request.flow != Flow::input && request.flow != Flow::output) return std::unexpected(Failure::configuration);
    const auto channels = request.flow == Flow::input ? request.inputs : request.outputs;
    if (!channels || channels > 2 || (request.flow == Flow::input ? request.outputs != 0 : request.inputs != 0))
        return std::unexpected(Failure::channels);
    WORD bits = 0, tag = WAVE_FORMAT_PCM;
    switch (request.sample) {
        case Sample::i16: bits = 16; break;
        case Sample::i24: bits = 24; break;
        case Sample::i32: bits = 32; break;
        case Sample::f32: bits = 32; tag = WAVE_FORMAT_IEEE_FLOAT; break;
        case Sample::f64: bits = 64; tag = WAVE_FORMAT_IEEE_FLOAT; break;
        default: return std::unexpected(Failure::format);
    }
    if (!request.rate) return std::unexpected(Failure::rate);
    if (!request.frames) return std::unexpected(Failure::buffer);
    const auto align = std::uint64_t(channels) * (bits / 8);
    const auto average = std::uint64_t(request.rate) * align;
    const auto period = std::uint64_t(request.frames) * align;
    const auto total = period * ringPeriods;
    if (align > std::numeric_limits<WORD>::max()) return std::unexpected(Failure::channels);
    if (average > std::numeric_limits<DWORD>::max()) return std::unexpected(Failure::rate);
    if (period > std::numeric_limits<DWORD>::max() || total < DSBSIZE_MIN || total > DSBSIZE_MAX)
        return std::unexpected(Failure::buffer);
    return WaveSpec{{tag, static_cast<WORD>(channels), request.rate, static_cast<DWORD>(average),
                     static_cast<WORD>(align), bits, 0}, static_cast<DWORD>(period), static_cast<DWORD>(total), request.frames, request.sample};
}
inline bool exactWaveFormat(const WAVEFORMATEX& actual, const WAVEFORMATEX& requested) noexcept {
    return actual.wFormatTag == requested.wFormatTag && actual.nChannels == requested.nChannels
        && actual.nSamplesPerSec == requested.nSamplesPerSec && actual.nAvgBytesPerSec == requested.nAvgBytesPerSec
        && actual.nBlockAlign == requested.nBlockAlign && actual.wBitsPerSample == requested.wBitsPerSample
        && actual.cbSize == 0;
}
}
