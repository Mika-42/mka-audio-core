#pragma once
#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

// Private control-thread prototype. No public Core type or error path is changed.
namespace mka_study::diagnostics {
using namespace mka::audio::core;
enum class Operation {Open,Start,Stop,Close};
enum class Domain {Unknown,Alsa,Wasapi,CoreAudio,PipeWire,PulseAudio,Wmme,DirectSound,Asio};
struct NativeEvidence {Domain domain=Domain::Unknown;std::optional<std::int64_t> code;};
struct ErrorDetails {
    static constexpr std::size_t maxIdBytes=4096;
    ErrorType error=ErrorType::ConfigurationFailed;Operation operation=Operation::Open;
    std::string endpoint;std::uint64_t originalIdBytes=0;bool truncated=false;
    Direction direction{};Format format{};
    std::uint32_t inputs=0,outputs=0,rate=0,frames=0;
    Status status{};NativeEvidence native;
};
inline std::string_view operationName(Operation operation) noexcept {
    switch(operation){case Operation::Open:return "open";case Operation::Start:return "start";case Operation::Stop:return "stop";case Operation::Close:return "close";}return "Unknown";
}
inline std::string_view domainName(Domain domain) noexcept {
    switch(domain){case Domain::Alsa:return "ALSA";case Domain::Wasapi:return "WASAPI";case Domain::CoreAudio:return "CoreAudio";
        case Domain::PipeWire:return "PipeWire";case Domain::PulseAudio:return "PulseAudio";case Domain::Wmme:return "WMME";
        case Domain::DirectSound:return "DirectSound";case Domain::Asio:return "ASIO";case Domain::Unknown:return "Unknown";}return "Unknown";
}
inline Result captureError(ErrorType error,Operation operation,const EndpointConfig& request,Status status,ErrorDetails& output,NativeEvidence native={}) noexcept {
    try {
        ErrorDetails next;next.error=error;next.operation=operation;
        next.endpoint.assign(request.id.data(),std::min(request.id.size(),ErrorDetails::maxIdBytes));
        next.originalIdBytes=request.id.size();next.truncated=request.id.size()>ErrorDetails::maxIdBytes;
        next.direction=request.direction;next.format=request.format;next.inputs=request.inputChannels;next.outputs=request.outputChannels;
        next.rate=request.sampleRate;next.frames=request.bufferSize;next.status=status;next.native=native;
        output=std::move(next);return {};
    }catch(...){return std::unexpected{ErrorType::ConfigurationFailed};}
}
inline Result serialize(const ErrorDetails& details,std::string& output) noexcept {
    if(details.endpoint.size()>ErrorDetails::maxIdBytes)return std::unexpected{ErrorType::BufferSizeNotSupported};
    try {
        std::string hex;hex.reserve(details.endpoint.size()*2);
        constexpr char digits[]="0123456789abcdef";
        for(unsigned char byte:details.endpoint){hex+=digits[byte>>4];hex+=digits[byte&15];}
        std::string json;json.reserve(1024+hex.size());
        json="{\"schema_version\":1,\"error\":{\"name\":\"";json+=errorName(details.error);
        json+="\",\"value\":"+std::to_string(static_cast<int>(details.error))+"},\"operation\":\"";
        json+=operationName(details.operation);json+="\",\"request\":{\"id_encoding\":\"hex\",\"id\":\""+hex;
        json+="\",\"id_bytes\":\""+std::to_string(details.originalIdBytes)+"\",\"id_truncated\":"+(details.truncated?"true":"false");
        json+=",\"direction\":"+std::to_string(static_cast<std::uint32_t>(details.direction))+",\"format\":"+std::to_string(static_cast<std::uint32_t>(details.format));
        json+=",\"input_channels\":"+std::to_string(details.inputs)+",\"output_channels\":"+std::to_string(details.outputs);
        json+=",\"sample_rate\":"+std::to_string(details.rate)+",\"buffer_frames\":"+std::to_string(details.frames)+"},\"status\":{\"xruns\":\"";
        json+=std::to_string(details.status.xruns)+"\",\"failed\":"+(details.status.failed?"true":"false")+",\"realtime\":\"";
        json+=realtimeName(details.status.realtime);json+="\"},\"native\":{\"domain\":\"";json+=domainName(details.native.domain);
        json+="\",\"code\":";json+=details.native.code?"\""+std::to_string(*details.native.code)+"\"":"null";
        json+="}}";output=std::move(json);return {};
    }catch(...){return std::unexpected{ErrorType::ConfigurationFailed};}
}
}
