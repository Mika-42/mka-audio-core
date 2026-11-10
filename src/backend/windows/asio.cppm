module;
#include <expected>
#include <optional>
#include <vector>
#include "detail/asio_host.hpp"
export module mka.audio.backend.asio;
export import mka.audio.backend.abstract;

export namespace mka::audio::core {
class ASIO final : public Backend {
public:
    ASIO() = default;
    ~ASIO() override { stopDispatcher(); (void)stop(); (void)close(); }
    ASIO(const ASIO&) = delete;
    ASIO& operator=(const ASIO&) = delete;
    ASIO(ASIO&&) = delete;
    ASIO& operator=(ASIO&&) = delete;
protected:
    std::vector<Endpoint> getEndPoints_() const override {
        std::vector<Endpoint> result;
        for (auto& device : session_.devices()) {
            Endpoint endpoint{std::move(device.id), std::move(device.name), std::nullopt, std::nullopt};
            if (device.channels) {
                if (device.channels->inputs) endpoint.input = StreamCapabilities{1, device.channels->inputs, {}, formats(device.inputFormat), device.bufferSizes};
                if (device.channels->outputs) endpoint.output = StreamCapabilities{1, device.channels->outputs, {}, formats(device.outputFormat), device.bufferSizes};
            }
            result.push_back(std::move(endpoint));
        }
        return result;
    }
    Result open_(const EndpointConfig& config) override {
        using Sample = mka_asio_detail::Sample;
        using Flow = mka_asio_detail::Flow;
        Sample sample = Sample::f32;
        switch (config.format) {
            case Format::Int16: sample = Sample::i16; break;
            case Format::Int24: sample = Sample::i24; break;
            case Format::Int32: sample = Sample::i32; break;
            case Format::Float32: sample = Sample::f32; break;
            case Format::Float64: sample = Sample::f64; break;
        }
        return result(session_.open({config.id,
            config.direction == Direction::Input ? Flow::input : config.direction == Direction::Output ? Flow::output : Flow::duplex,
            config.inputChannels, config.outputChannels, config.sampleRate, config.bufferSize, sample}));
    }
    Result start_() override { return result(session_.start()); }
    Result stop_() override { return result(session_.stop()); }
    Result close_() override { return result(session_.close()); }
private:
    static std::vector<Format> formats(std::optional<mka_asio_detail::Sample> sample) {
        if (!sample) return {};
        switch (*sample) {
            case mka_asio_detail::Sample::i16: return {Format::Int16};
            case mka_asio_detail::Sample::i24: return {Format::Int24};
            case mka_asio_detail::Sample::i32: return {Format::Int32};
            case mka_asio_detail::Sample::f32: return {Format::Float32};
            case mka_asio_detail::Sample::f64: return {Format::Float64};
        }
        return {};
    }
    static Result result(mka_asio_detail::Failure error) noexcept {
        using Failure = mka_asio_detail::Failure;
        switch (error) {
            case Failure::none: return {};
            case Failure::endpoint: return std::unexpected(ErrorType::EndpointUnavailable);
            case Failure::format: return std::unexpected(ErrorType::FormatNotSupported);
            case Failure::channels: return std::unexpected(ErrorType::ChannelsNotSupported);
            case Failure::rate: return std::unexpected(ErrorType::SampleRateNotSupported);
            case Failure::buffer: return std::unexpected(ErrorType::BufferSizeNotSupported);
            case Failure::configuration: return std::unexpected(ErrorType::ConfigurationFailed);
        }
        return std::unexpected(ErrorType::ConfigurationFailed);
    }
    mka_asio_detail::Session session_;
};
}
