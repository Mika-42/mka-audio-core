// WASAPI keeps the portable Backend contract; SDK details stay private.
module;
#include <expected>
#include <vector>
#include "detail/wasapi_control.hpp"
export module mka.audio.backend.wasapi;
export import mka.audio.backend.abstract;

export namespace mka::audio::core {
class Wasapi final : public Backend {
public:
    Wasapi() = default;
    ~Wasapi() override { stopDispatcher(); (void)stop(); (void)close(); }
protected:
    std::vector<Endpoint> getEndPoints_() const override {
        if (FAILED(control_.initialization())) return {};
        return control_.invoke([] { return std::vector<Endpoint>{}; });
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
