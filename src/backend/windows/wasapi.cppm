// WASAPI keeps the portable Backend contract; SDK details stay private.
module;
#include <expected>
#include <vector>
#include "detail/wasapi_session.hpp"
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
    Result open_(const EndpointConfig& config) override {
        if (config.direction == Direction::Duplex) return std::unexpected{ErrorType::ConfigurationFailed};
        using Kind = mka_wasapi_detail::SampleKind;
        const auto channels = config.direction == Direction::Input ? config.inputChannels : config.outputChannels;
        if (channels > std::numeric_limits<WORD>::max()) return std::unexpected{ErrorType::ChannelsNotSupported};
        Kind kind = Kind::F32;
        switch (config.format) {
            case Format::Int16: kind = Kind::S16; break;
            case Format::Int24: kind = Kind::S24Packed; break;
            case Format::Int32: kind = Kind::S32; break;
            case Format::Float32: kind = Kind::F32; break;
            case Format::Float64: kind = Kind::F64; break;
        }
        return nativeResult(session_.open({config.id,
            config.direction == Direction::Input ? eCapture : eRender,
            channels, config.sampleRate, config.bufferSize, kind}));
    }
    Result start_() override { return std::unexpected{ErrorType::ConfigurationFailed}; }
    Result stop_() override { return std::unexpected{ErrorType::ConfigurationFailed}; }
    Result close_() override { return nativeResult(session_.close()); }
private:
    static Result nativeResult(HRESULT result) noexcept {
        if (SUCCEEDED(result)) return {};
        if (result == HRESULT_FROM_WIN32(ERROR_NOT_FOUND) || result == E_ACCESSDENIED
            || result == AUDCLNT_E_DEVICE_INVALIDATED || result == AUDCLNT_E_SERVICE_NOT_RUNNING)
            return std::unexpected{ErrorType::EndpointUnavailable};
        if (result == AUDCLNT_E_UNSUPPORTED_FORMAT) return std::unexpected{ErrorType::FormatNotSupported};
        if (result == AUDCLNT_E_WRONG_ENDPOINT_TYPE) return std::unexpected{ErrorType::ChannelsNotSupported};
        return std::unexpected{ErrorType::ConfigurationFailed};
    }
    mutable mka_wasapi_detail::ControlThread control_;
    mka_wasapi_detail::Session session_{control_};
};
}
