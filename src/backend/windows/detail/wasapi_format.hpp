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
#include <bit>
namespace mka_wasapi_detail {
enum class SampleKind { S16, S24Packed, S24Msb32, S32, F32, F64 };
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
    if (pcm && validBits == 24 && format.wBitsPerSample == 32) return SampleKind::S24Msb32;
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
    if (f.wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        const auto mask = reinterpret_cast<const WAVEFORMATEXTENSIBLE&>(f).dwChannelMask;
        if (mask && std::popcount(mask) != f.nChannels) return false;
    }
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

namespace mka_wasapi_detail {
inline std::optional<WAVEFORMATEXTENSIBLE> makeWaveFormat(SampleKind kind, std::uint32_t channels,
    std::uint32_t rate, DWORD mask = 0) noexcept {
    WORD bits = 0, valid = 0;
    GUID subtype = KSDATAFORMAT_SUBTYPE_PCM;
    switch(kind) {
        case SampleKind::S16: bits=valid=16; break;
        case SampleKind::S24Packed: bits=valid=24; break;
        case SampleKind::S24Msb32: bits=32; valid=24; break;
        case SampleKind::S32: bits=valid=32; break;
        case SampleKind::F32: bits=valid=32; subtype=KSDATAFORMAT_SUBTYPE_IEEE_FLOAT; break;
        case SampleKind::F64: bits=valid=64; subtype=KSDATAFORMAT_SUBTYPE_IEEE_FLOAT; break;
        default: return {};
    }
    if (!channels || channels > std::numeric_limits<WORD>::max() || !rate) return {};
    const auto block=static_cast<std::uint64_t>(channels)*(bits/8);
    const auto average=block*rate;
    if (block > std::numeric_limits<WORD>::max() || average > std::numeric_limits<DWORD>::max()) return {};
    WAVEFORMATEXTENSIBLE result{};
    result.Format={WAVE_FORMAT_EXTENSIBLE,static_cast<WORD>(channels),rate,
        static_cast<DWORD>(average),static_cast<WORD>(block),bits,
        static_cast<WORD>(sizeof(WAVEFORMATEXTENSIBLE)-sizeof(WAVEFORMATEX))};
    result.Samples.wValidBitsPerSample=valid;result.SubFormat=subtype;
    result.dwChannelMask=mask ? mask : (channels==1 ? SPEAKER_FRONT_CENTER : channels==2 ? 3u : 0u);
    if (!validWaveFormat(result.Format)) return {};
    return result;
}
}

namespace mka_wasapi_detail {
inline std::optional<REFERENCE_TIME> frameDuration(std::uint32_t frames, std::uint32_t rate) noexcept {
    if (!frames || !rate) return {};
    constexpr std::uint64_t scale=10000000;
    const auto duration=(static_cast<std::uint64_t>(frames)*scale+rate-1)/rate;
    if (!duration || duration > static_cast<std::uint64_t>(std::numeric_limits<REFERENCE_TIME>::max())) return {};
    return static_cast<REFERENCE_TIME>(duration);
}
inline bool validCapacity(std::uint32_t frames,std::uint32_t channels) noexcept {
    if (!frames || !channels) return false;
    return frames <= std::numeric_limits<std::uint32_t>::max()/sizeof(float)/channels
        && frames <= std::numeric_limits<std::size_t>::max()/sizeof(float)/channels;
}
}
