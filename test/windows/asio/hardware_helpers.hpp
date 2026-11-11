#pragma once
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace asio_hardware {
using namespace mka::audio::core;
inline bool nativeWindows() noexcept {
    return !GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"wine_get_version");
}
inline const char* errorName(ErrorType error) noexcept {
    switch (error) {
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
inline const char* formatName(Format format) noexcept {
    switch (format) {
        case Format::Int16:return "S16";case Format::Int24:return "S24";case Format::Int32:return "S32";
        case Format::Float32:return "Float32";case Format::Float64:return "Float64";
    }
    return "unknown";
}
inline std::expected<std::uint32_t,std::string> numericEnv(const char* name,std::uint32_t fallback,
                                                        std::uint32_t min,std::uint32_t max) {
    const char* raw=std::getenv(name);
    if (!raw || !*raw) {
        if (fallback<min || fallback>max) return std::unexpected(std::string("Capacite native hors limites du test pour ")+name);
        return fallback;
    }
    std::string_view text(raw);std::uint32_t value=0;
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
    if (parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size() || value<min || value>max)
        return std::unexpected(std::string(name)+" invalide ("+std::to_string(min)+".."+std::to_string(max)+")");
    return value;
}
struct Selection {EndpointConfig config;std::string name;};
inline std::expected<Selection,std::string> configuration(Direction direction,std::uint32_t forcedFrames=0) {
    const bool input=direction!=Direction::Output,output=direction!=Direction::Input;
    const char* id=std::getenv(direction==Direction::Input?"MKA_TEST_ASIO_INPUT_DEVICE":"MKA_TEST_ASIO_OUTPUT_DEVICE");
    if (!id || !*id) id=std::getenv("MKA_TEST_ASIO_DEVICE");
    ASIO discovery;
    for (const auto& endpoint : discovery.getEndPoints()) {
        if (id && *id && endpoint.id!=id) continue;
        const auto usable=[](const auto& caps) {return caps && caps->maxChannels && !caps->formats.empty()
            && !caps->sampleRates.empty() && !caps->bufferSizes.empty();};
        if ((input&&!usable(endpoint.input)) || (output&&!usable(endpoint.output))) continue;
        const auto& base=input?*endpoint.input:*endpoint.output;
        auto format=base.formats.front();
        if (input&&output) {
            const auto match=std::find_first_of(base.formats.begin(),base.formats.end(),endpoint.output->formats.begin(),endpoint.output->formats.end());
            if (match==base.formats.end()) continue;
            format=*match;
        }
        const auto preferredRate=std::find(base.sampleRates.begin(),base.sampleRates.end(),48000u)!=base.sampleRates.end()?48000u:base.sampleRates.front();
        const auto rate=numericEnv("MKA_TEST_ASIO_RATE",preferredRate,8000,384000);
        if (!rate) return std::unexpected(rate.error());
        const auto preferredBuffer=std::find(base.bufferSizes.begin(),base.bufferSizes.end(),256u)!=base.bufferSizes.end()?256u:base.bufferSizes.front();
        const auto frames=numericEnv("MKA_TEST_ASIO_BUFFER",preferredBuffer,1,65536);
        if (!frames) return std::unexpected(frames.error());
        if (forcedFrames && std::find(base.bufferSizes.begin(),base.bufferSizes.end(),forcedFrames)==base.bufferSizes.end()) continue;
        const auto inputs=input?numericEnv("MKA_TEST_ASIO_INPUT_CHANNELS",std::min(2u,endpoint.input->maxChannels),1,std::min(32u,endpoint.input->maxChannels))
            :std::expected<std::uint32_t,std::string>(0);
        const auto outputs=output?numericEnv("MKA_TEST_ASIO_OUTPUT_CHANNELS",std::min(2u,endpoint.output->maxChannels),1,std::min(32u,endpoint.output->maxChannels))
            :std::expected<std::uint32_t,std::string>(0);
        if (!inputs) return std::unexpected(inputs.error());if (!outputs) return std::unexpected(outputs.error());
        return Selection{{endpoint.id,direction,*inputs,*outputs,*rate,format,forcedFrames?forcedFrames:*frames},endpoint.name};
    }
    return std::unexpected(id&&*id?std::string("Pilote ASIO demande absent ou capacites incompatibles : ")+id
        :"Aucun pilote ASIO utilisable pour cette direction/configuration. Verifier le SDK, le pilote et les autres applications audio.");
}
inline void describe(const Selection& selected) {
    const auto& c=selected.config;
    std::printf("ASIO : %s\n  ID=%s | %u Hz | entrees=%u sorties=%u | %s | periode %u frames\n",
        selected.name.c_str(),c.id.c_str(),c.sampleRate,c.inputChannels,c.outputChannels,formatName(c.format),c.bufferSize);
}
inline bool open(ASIO& backend,const EndpointConfig& config) {
    const auto result=backend.open(config);
    if (!result) std::fprintf(stderr,"ASIO open : %s. Verifier le pilote selectionne, son panneau de controle et la configuration exacte.\n",errorName(result.error()));
    return bool(result);
}
template<class Predicate> bool wait(ASIO& backend,Predicate done,std::chrono::seconds timeout) {
    const auto deadline=std::chrono::steady_clock::now()+timeout;
    while (!done()) {
        if (backend.status().failed || std::chrono::steady_clock::now()>=deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return true;
}
}
