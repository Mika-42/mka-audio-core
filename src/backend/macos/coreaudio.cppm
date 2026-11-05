module;
#include <expected>
#include <vector>
#include "detail/devices.hpp"
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
    Result open_(const EndpointConfig&) override { return std::unexpected{ErrorType::ConfigurationFailed}; }
    Result start_() override { return std::unexpected{ErrorType::ConfigurationFailed}; }
    Result stop_() override { return {}; }
    Result close_() override { return {}; }
};
}
