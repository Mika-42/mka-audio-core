// WASAPI keeps the portable Backend contract; SDK details stay private.
module;
#include <expected>
#include <vector>
export module mka.audio.backend.wasapi;
export import mka.audio.backend.abstract;

export namespace mka::audio::core {
class Wasapi final : public Backend {
public:
    Wasapi() = default;
    ~Wasapi() override = default;
protected:
    std::vector<Endpoint> getEndPoints_() const override { return {}; }
    Result open_(const EndpointConfig&) override {
        return std::unexpected{ErrorType::ConfigurationFailed};
    }
    Result start_() override { return std::unexpected{ErrorType::ConfigurationFailed}; }
    Result stop_() override { return std::unexpected{ErrorType::ConfigurationFailed}; }
    Result close_() override { return {}; }
};
}
