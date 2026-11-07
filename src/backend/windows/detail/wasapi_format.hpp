#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <audioclient.h>
#include <mmreg.h>
#include <ks.h>
#include <ksmedia.h>
#include <cstdint>
#include <limits>
#include <optional>
namespace mka_wasapi_detail {
enum class SampleKind { S16, S24Packed, S24In32, S32, F32, F64 };
inline std::optional<SampleKind> sampleKind(const WAVEFORMATEX& format) noexcept {
    bool pcm = format.wFormatTag == WAVE_FORMAT_PCM;
    bool floating = format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
    WORD validBits = format.wBitsPerSample;
    if (format.wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        if (format.cbSize < sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) return {};
        const auto& ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE&>(format);
        pcm = ext.SubFormat == KSDATAFORMAT_SUBTYPE_PCM;
        floating = ext.SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
        validBits = ext.Samples.wValidBitsPerSample;
    }
    if (pcm && validBits == 24 && format.wBitsPerSample == 32) return SampleKind::S24In32;
    if (validBits != format.wBitsPerSample) return {};
    if (pcm) {
        switch(format.wBitsPerSample) {
            case 16:return SampleKind::S16;case 24:return SampleKind::S24Packed;case 32:return SampleKind::S32;
        }
    }
    if (floating) {
        if (format.wBitsPerSample == 32) return SampleKind::F32;
        if (format.wBitsPerSample == 64) return SampleKind::F64;
    }
    return {};
}
inline bool validWaveFormat(const WAVEFORMATEX& f) noexcept {
    if (!sampleKind(f) || !f.nChannels || !f.nSamplesPerSec || f.wBitsPerSample % 8) return false;
    const auto block = static_cast<std::uint64_t>(f.nChannels) * (f.wBitsPerSample / 8);
    const auto bytes = block * f.nSamplesPerSec;
    return block <= std::numeric_limits<WORD>::max() && f.nBlockAlign == block
        && bytes <= std::numeric_limits<DWORD>::max() && f.nAvgBytesPerSec == bytes;
}
inline std::optional<std::uint32_t> periodFrames(REFERENCE_TIME ticks, std::uint32_t rate) noexcept {
    if (ticks <= 0 || !rate) return {};
    constexpr std::uint64_t scale = 10000000;
    const auto time = static_cast<std::uint64_t>(ticks);
    if (time > (std::numeric_limits<std::uint64_t>::max() - (scale-1)) / rate) return {};
    const auto frames = (time * rate + scale-1) / scale;
    if (!frames || frames > std::numeric_limits<std::uint32_t>::max()) return {};
    return static_cast<std::uint32_t>(frames);
}
}
