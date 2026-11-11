module;
#include <expected>
#include <optional>
#include <vector>
#include "detail/directsound_host.hpp"
export module mka.audio.backend.directsound;
export import mka.audio.backend.abstract;

export namespace mka::audio::core {
class DirectSound final : public Backend {
public:
    DirectSound() = default;
    ~DirectSound() override { stopDispatcher(); (void)stop(); (void)close(); }
    DirectSound(const DirectSound&) = delete;
    DirectSound& operator=(const DirectSound&) = delete;
    DirectSound(DirectSound&&) = delete;
    DirectSound& operator=(DirectSound&&) = delete;
protected:
    std::vector<Endpoint> getEndPoints_() const override {
        std::vector<Endpoint> result;
        for (auto& device : session_.devices())
            result.push_back({std::move(device.id), std::move(device.name), capabilities(device.input), capabilities(device.output)});
        return result;
    }
    Result open_(const EndpointConfig& config) override {
        using Flow = mka_ds_detail::Flow;
        return result(session_.open({config.id,
            config.direction == Direction::Input ? Flow::input : config.direction == Direction::Output ? Flow::output : Flow::duplex,
            config.inputChannels, config.outputChannels, config.sampleRate, config.bufferSize, sample(config.format)}));
    }
    Result start_() override {
        const auto started = result(session_.start(processingCallbacks()));
        if (started) notifyRealtime(false);
        return started;
    }
    Result stop_() override { return result(session_.stop()); }
    Result close_() override { return result(session_.close()); }
private:
    static mka_ds_detail::Sample sample(Format value) noexcept {
        using Sample = mka_ds_detail::Sample;
        switch (value) {
            case Format::Int16: return Sample::i16;
            case Format::Int24: return Sample::i24;
            case Format::Int32: return Sample::i32;
            case Format::Float32: return Sample::f32;
            case Format::Float64: return Sample::f64;
        }
        return Sample::f32; // Core validates the enum before invoking open_().
    }
    static Format format(mka_ds_detail::Sample value) noexcept {
        using Sample = mka_ds_detail::Sample;
        switch (value) {
            case Sample::i16: return Format::Int16;
            case Sample::i24: return Format::Int24;
            case Sample::i32: return Format::Int32;
            case Sample::f32: return Format::Float32;
            case Sample::f64: return Format::Float64;
        }
        return Format::Float32;
    }
    static std::optional<StreamCapabilities> capabilities(const std::optional<mka_ds_detail::Capabilities>& native) {
        if (!native) return std::nullopt;
        std::vector<Format> formats;
        for (const auto value : native->formats) formats.push_back(format(value));
        return StreamCapabilities{native->minimumChannels, native->maximumChannels, native->rates, std::move(formats), native->buffers};
    }
    mka_ds_detail::ProcessingCallbacks processingCallbacks() noexcept {
        return {.user=this,
            .process=callback ? +[](void* user, const float* const* input, std::uint32_t inputs,
                float* const* output, std::uint32_t outputs, std::uint32_t frames) noexcept {
                auto& self = *static_cast<DirectSound*>(user);
                self.callback(self.userData, AudioProcessContext{{input, inputs}, {output, outputs}, frames});
            } : nullptr,
            .xrun=+[](void* user) noexcept { static_cast<DirectSound*>(user)->notifyXRun(); },
            .failed=+[](void* user) noexcept { static_cast<DirectSound*>(user)->notifyFailed(); }};
    }
    static Result result(mka_ds_detail::Failure error) noexcept {
        using Failure = mka_ds_detail::Failure;
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
    mka_ds_detail::Session session_;
};
}
