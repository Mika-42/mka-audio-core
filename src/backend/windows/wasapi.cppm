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
        for (auto& device : mka_wasapi_detail::enumerateDevices(control_)) {
            if (!device.kind) continue;
            Format format = Format::Float32;
            using Kind = mka_wasapi_detail::SampleKind;
            switch (*device.kind) {
                case Kind::S16: format = Format::Int16; break;
                case Kind::S24Packed: case Kind::S24In32: format = Format::Int24; break;
                case Kind::S32: format = Format::Int32; break;
                case Kind::F32: format = Format::Float32; break;
                case Kind::F64: format = Format::Float64; break;
            }
            StreamCapabilities caps{device.channels, device.channels, {device.rate}, {format}, std::move(device.bufferSizes)};
            Endpoint endpoint{.id = std::move(device.id), .name = std::move(device.name)};
            if (device.flow == eCapture) endpoint.input = std::move(caps);
            else endpoint.output = std::move(caps);
            endpoints.push_back(std::move(endpoint));
        }
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
