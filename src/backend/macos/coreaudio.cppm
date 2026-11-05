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
        for(auto& device:mka_coreaudio_detail::devices(mka_coreaudio_detail::nativeApi()))
            endpoints.push_back({std::move(device.uid),std::move(device.name),{}, {}});
        return endpoints;
    }
    Result open_(const EndpointConfig&) override { return std::unexpected{ErrorType::ConfigurationFailed}; }
    Result start_() override { return std::unexpected{ErrorType::ConfigurationFailed}; }
    Result stop_() override { return {}; }
    Result close_() override { return {}; }
};
}
