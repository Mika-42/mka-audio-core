// WASAPI keeps the portable Backend contract; SDK details stay private.
module;
#include <expected>
#include <vector>
#include "detail/wasapi_devices.hpp"
export module mka.audio.backend.wasapi;
export import mka.audio.backend.abstract;

export namespace mka::audio::core {
class Wasapi final : public Backend {
public:
    Wasapi() = default;
    ~Wasapi() override { stopDispatcher(); (void)stop(); (void)close(); }
protected:
    std::vector<Endpoint> getEndPoints_() const override {
        std::vector<Endpoint> endpoints;
        for (auto& device : mka_wasapi_detail::enumerateDevices(control_))
            endpoints.push_back(Endpoint{.id = std::move(device.id), .name = std::move(device.name)});
        return endpoints;
    }
    Result open_(const EndpointConfig&) override {
        if (FAILED(control_.initialization())) return std::unexpected{ErrorType::ConfigurationFailed};
        return control_.invoke([]() -> Result { return std::unexpected{ErrorType::ConfigurationFailed}; });
    }
    Result start_() override { return std::unexpected{ErrorType::ConfigurationFailed}; }
    Result stop_() override { return std::unexpected{ErrorType::ConfigurationFailed}; }
    Result close_() override { return {}; }
private:
    mutable mka_wasapi_detail::ControlThread control_;
};
}
