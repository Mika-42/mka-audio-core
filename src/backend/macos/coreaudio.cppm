module;
#include <expected>
#include <vector>
#include "detail/session.hpp"
export module mka.audio.backend.coreaudio;
export import mka.audio.backend.abstract;

export namespace mka::audio::core {
class CoreAudio final : public Backend {
public:
    ~CoreAudio() override { stopDispatcher(); (void)stop(); (void)close(); }
protected:
    std::vector<Endpoint> getEndPoints_() const override {
        std::vector<Endpoint> endpoints;
        const auto& api=mka_coreaudio_detail::nativeApi();
        for(auto& device:mka_coreaudio_detail::devices(api)){
            Endpoint endpoint{std::move(device.uid),std::move(device.name),{}, {}};
            auto convert=[](mka_coreaudio_detail::Capabilities caps){
                return StreamCapabilities{caps.channels,caps.channels,std::move(caps.rates),{Format::Float32},std::move(caps.buffers)};
            };
            if(auto caps=mka_coreaudio_detail::capabilities(api,device.object,kAudioObjectPropertyScopeInput))endpoint.input=convert(std::move(*caps));
            if(auto caps=mka_coreaudio_detail::capabilities(api,device.object,kAudioObjectPropertyScopeOutput))endpoint.output=convert(std::move(*caps));
            if(endpoint.input || endpoint.output)endpoints.push_back(std::move(endpoint));
        }
        return endpoints;
    }
    Result open_(const EndpointConfig& config) override {
        if(config.format!=Format::Float32)return std::unexpected{ErrorType::FormatNotSupported};
        return translate(session_.open({config.id,config.direction!=Direction::Output,config.direction!=Direction::Input,
            config.inputChannels,config.outputChannels,config.sampleRate,config.bufferSize}));
    }
    Result start_() override { return std::unexpected{ErrorType::ConfigurationFailed}; }
    Result stop_() override { return {}; }
    Result close_() override { return translate(session_.close()); }
private:
    static Result translate(mka_coreaudio_detail::Result result) noexcept {
        if(result)return {};
        using Failure=mka_coreaudio_detail::Failure;
        switch(result.error()){
            case Failure::Unavailable:return std::unexpected{ErrorType::EndpointUnavailable};
            case Failure::Format:return std::unexpected{ErrorType::FormatNotSupported};
            case Failure::Channels:return std::unexpected{ErrorType::ChannelsNotSupported};
            case Failure::Rate:return std::unexpected{ErrorType::SampleRateNotSupported};
            case Failure::Buffer:return std::unexpected{ErrorType::BufferSizeNotSupported};
            default:return std::unexpected{ErrorType::ConfigurationFailed};
        }
    }
    mka_coreaudio_detail::Session session_;
};
}
