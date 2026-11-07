#pragma once
#include <windows.h>
#include <mmsystem.h>
#include <mmreg.h>
#include <dsound.h>
#include <cstdint>
#include <limits>
#include <optional>

namespace mka_study::legacy {
using namespace mka::audio::core;
struct Report {
    std::optional<MMRESULT> wave;
    std::optional<HRESULT> directSound;
    DWORD flags=0;
    std::uint32_t captureChannels=0;
};
inline HRESULT renderCaps(DSCAPS* caps) noexcept {
    IDirectSound8* device=nullptr;const auto created=DirectSoundCreate8(nullptr,&device,nullptr);
    if(FAILED(created))return created;
    if(!device)return E_POINTER;
    const auto queried=device->GetCaps(caps);device->Release();return queried;
}
inline HRESULT captureCaps(DSCCAPS* caps) noexcept {
    IDirectSoundCapture8* device=nullptr;const auto created=DirectSoundCaptureCreate8(nullptr,&device,nullptr);
    if(FAILED(created))return created;
    if(!device)return E_POINTER;
    const auto queried=device->GetCaps(caps);device->Release();return queried;
}
struct Api {
    decltype(&waveOutOpen) output=&waveOutOpen;
    decltype(&waveInOpen) input=&waveInOpen;
    HRESULT(*render)(DSCAPS*) noexcept=&renderCaps;
    HRESULT(*capture)(DSCCAPS*) noexcept=&captureCaps;
};
inline Result makeFormat(const EndpointConfig& config,WAVEFORMATEX& result) noexcept {
    if(config.direction!=Direction::Input && config.direction!=Direction::Output)
        return std::unexpected{ErrorType::ConfigurationFailed};
    const bool capture=config.direction==Direction::Input;
    const auto channels=capture?config.inputChannels:config.outputChannels;
    if(channels==0 || channels>2 || (capture?config.outputChannels:config.inputChannels)!=0)
        return std::unexpected{ErrorType::ChannelsNotSupported};
    if(!config.sampleRate)return std::unexpected{ErrorType::SampleRateNotSupported};
    if(!config.bufferSize)return std::unexpected{ErrorType::BufferSizeNotSupported};
    WORD bits=0,tag=WAVE_FORMAT_PCM;
    switch(config.format){
        case Format::Int16:bits=16;break;case Format::Int24:bits=24;break;
        case Format::Int32:bits=32;break;case Format::Float32:bits=32;tag=WAVE_FORMAT_IEEE_FLOAT;break;
        case Format::Float64:bits=64;tag=WAVE_FORMAT_IEEE_FLOAT;break;
        default:return std::unexpected{ErrorType::FormatNotSupported};
    }
    const auto alignment=channels*(bits/8);
    if(static_cast<std::uint64_t>(config.sampleRate)*alignment>std::numeric_limits<DWORD>::max())
        return std::unexpected{ErrorType::SampleRateNotSupported};
    if(static_cast<std::uint64_t>(config.bufferSize)*alignment>std::numeric_limits<DWORD>::max())
        return std::unexpected{ErrorType::BufferSizeNotSupported};
    result={};result.wFormatTag=tag;result.nChannels=static_cast<WORD>(channels);
    result.nSamplesPerSec=config.sampleRate;result.wBitsPerSample=bits;
    result.nBlockAlign=static_cast<WORD>(alignment);result.nAvgBytesPerSec=config.sampleRate*alignment;
    return {};
}
inline Result queryWave(const EndpointConfig& config,Report& report,const Api& api={}) noexcept {
    report.wave.reset();WAVEFORMATEX format{};const auto valid=makeFormat(config,format);if(!valid)return valid;
    const DWORD flags=WAVE_FORMAT_QUERY|WAVE_FORMAT_DIRECT;
    const auto code=config.direction==Direction::Input
        ?api.input(nullptr,WAVE_MAPPER,&format,0,0,flags)
        :api.output(nullptr,WAVE_MAPPER,&format,0,0,flags);
    report.wave=code;
    if(code==MMSYSERR_NOERROR)return {};
    if(code==WAVERR_BADFORMAT)return std::unexpected{ErrorType::FormatNotSupported};
    if(code==MMSYSERR_NODRIVER || code==MMSYSERR_BADDEVICEID || code==MMSYSERR_ALLOCATED)
        return std::unexpected{ErrorType::EndpointUnavailable};
    return std::unexpected{ErrorType::ConfigurationFailed};
}
inline Result queryDirectSound(Direction direction,Report& report,const Api& api={}) noexcept {
    report.directSound.reset();report.flags=0;report.captureChannels=0;
    if(direction==Direction::Duplex)return std::unexpected{ErrorType::ConfigurationFailed};
    HRESULT code=E_INVALIDARG;
    if(direction==Direction::Input){DSCCAPS caps{};caps.dwSize=sizeof(caps);code=api.capture(&caps);if(SUCCEEDED(code)){report.flags=caps.dwFlags;report.captureChannels=caps.dwChannels;}}
    else if(direction==Direction::Output){DSCAPS caps{};caps.dwSize=sizeof(caps);code=api.render(&caps);if(SUCCEEDED(code))report.flags=caps.dwFlags;}
    else return std::unexpected{ErrorType::ConfigurationFailed};
    report.directSound=code;
    return SUCCEEDED(code)?Result{}:std::unexpected{ErrorType::EndpointUnavailable};
}
}
