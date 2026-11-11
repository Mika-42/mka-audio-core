#include <gtest/gtest.h>
#include <array>
#include <cstdint>
#include <expected>
#include <new>
#include <stdexcept>
#include <type_traits>
#include "../../../src/backend/windows/detail/directsound_host.hpp"
import mka.audio.backend.directsound;

namespace {
using namespace mka::audio::core;
using namespace mka_ds_detail;
EndpointConfig config() { return {"directsound:output:test", Direction::Output, 0, 2, 48000, Format::Float32, 256}; }
struct Mock {
    struct Owned { Mock& owner; };
    inline static Mock* current = nullptr;
    Api api;
    Failure opened = Failure::none, started = Failure::none, stopped = Failure::none, closed = Failure::none;
    bool nullSuccess = false;
    unsigned opens = 0, starts = 0, stops = 0, closes = 0, live = 0;
    Request requested;
    ProcessingCallbacks processing;
    std::vector<Device> endpoints;
    Mock() {
        current = this;
        api.devices = +[] { return current->endpoints; };
        api.open = +[](const Request& request, void** out) noexcept {
            auto& m = *current; ++m.opens; m.requested = request;
            if (m.nullSuccess) { *out = nullptr; return m.opened; }
            auto* owned = new(std::nothrow) Owned{m};
            if (!owned) return Failure::configuration;
            *out = owned; ++m.live; return m.opened;
        };
        api.start = +[](void* state, const ProcessingCallbacks& callbacks) noexcept {
            auto& m = static_cast<Owned*>(state)->owner; ++m.starts; m.processing = callbacks; return m.started;
        };
        api.stop = +[](void* state) noexcept {
            auto& m = static_cast<Owned*>(state)->owner; ++m.stops; return m.stopped;
        };
        api.close = +[](void** state) noexcept {
            auto* owned = static_cast<Owned*>(std::exchange(*state, nullptr));
            auto& m = owned->owner; ++m.closes; --m.live; const auto result = m.closed;
            delete owned; return result;
        };
        testApiOverride = &api;
    }
    ~Mock() { testApiOverride = nullptr; current = nullptr; EXPECT_EQ(live, 0u); }
};

TEST(DirectSoundHooks, InheritsTheLifecycleAndPreventsIllegalNativeCalls) {
    Mock mock; DirectSound backend;
    EXPECT_EQ(mock.opens, 0u);
    EXPECT_EQ(backend.start().error(), ErrorType::InvalidState);
    EXPECT_EQ(backend.stop().error(), ErrorType::InvalidState);
    EXPECT_EQ(backend.close().error(), ErrorType::InvalidState);
    ASSERT_TRUE(backend.open(config()));
    EXPECT_EQ(backend.open(config()).error(), ErrorType::InvalidState);
    ASSERT_TRUE(backend.start());
    EXPECT_EQ(backend.start().error(), ErrorType::InvalidState);
    EXPECT_EQ(backend.close().error(), ErrorType::InvalidState);
    EXPECT_EQ(backend.setProcessFunction(nullptr).error(), ErrorType::InvalidState);
    EXPECT_EQ(backend.status().realtime, RealtimeState::No);
    ASSERT_TRUE(backend.stop()); ASSERT_TRUE(backend.close());
    EXPECT_EQ(mock.opens, 1u); EXPECT_EQ(mock.starts, 1u); EXPECT_EQ(mock.stops, 1u); EXPECT_EQ(mock.closes, 1u);
}

TEST(DirectSoundHooks, PreservesAllFormatDirectionAndNumericRequestFields) {
    Mock mock; DirectSound backend;
    const std::pair<Format, Sample> samples[]{{Format::Int16, Sample::i16}, {Format::Int24, Sample::i24},
        {Format::Int32, Sample::i32}, {Format::Float32, Sample::f32}, {Format::Float64, Sample::f64}};
    for (const auto [format, native] : samples) {
        auto cfg = config(); cfg.format = format;
        ASSERT_TRUE(backend.open(cfg));
        EXPECT_EQ(mock.requested.sample, native); EXPECT_EQ(mock.requested.flow, Flow::output);
        EXPECT_EQ(mock.requested.id, cfg.id); EXPECT_EQ(mock.requested.rate, cfg.sampleRate);
        EXPECT_EQ(mock.requested.frames, cfg.bufferSize); EXPECT_EQ(mock.requested.outputs, 2u);
        EXPECT_EQ(mock.requested.inputs, 0u); ASSERT_TRUE(backend.close());
    }
    auto cfg = config(); cfg.direction = Direction::Input; cfg.inputChannels = 1; cfg.outputChannels = 0;
    ASSERT_TRUE(backend.open(cfg)); EXPECT_EQ(mock.requested.flow, Flow::input);
    EXPECT_EQ(mock.requested.inputs, 1u); EXPECT_EQ(mock.requested.outputs, 0u); ASSERT_TRUE(backend.close());
}

TEST(DirectSoundHooks, MapsEveryPortableFailureAndRollsBackPartialOwnership) {
    Mock mock; DirectSound backend;
    const std::pair<Failure, ErrorType> errors[]{
        {Failure::endpoint, ErrorType::EndpointUnavailable}, {Failure::format, ErrorType::FormatNotSupported},
        {Failure::channels, ErrorType::ChannelsNotSupported}, {Failure::rate, ErrorType::SampleRateNotSupported},
        {Failure::buffer, ErrorType::BufferSizeNotSupported}, {Failure::configuration, ErrorType::ConfigurationFailed}};
    for (const auto [native, expected] : errors) {
        mock.opened = native; EXPECT_EQ(backend.open(config()).error(), expected);
        EXPECT_EQ(mock.live, 0u); EXPECT_EQ(backend.start().error(), ErrorType::InvalidState);
    }
    EXPECT_EQ(mock.closes, 6u); mock.opened = Failure::none;
    ASSERT_TRUE(backend.open(config())); ASSERT_TRUE(backend.close());
}

TEST(DirectSoundHooks, RejectsSuccessfulOpenWithoutAnOwnedNativeSession) {
    Mock mock; DirectSound backend; mock.nullSuccess = true;
    EXPECT_EQ(backend.open(config()).error(), ErrorType::ConfigurationFailed);
    EXPECT_EQ(mock.closes, 0u); mock.nullSuccess = false;
    ASSERT_TRUE(backend.open(config())); ASSERT_TRUE(backend.close());
}

TEST(DirectSoundHooks, MissingOpenOrCloseAdapterCannotAcquireUnreleasableOwnership) {
    Mock mock;
    mock.api.open = nullptr;
    { DirectSound backend; EXPECT_EQ(backend.open(config()).error(), ErrorType::ConfigurationFailed); }
    mock.api = Api{}; mock.api.close = nullptr;
    { DirectSound backend; EXPECT_EQ(backend.open(config()).error(), ErrorType::ConfigurationFailed); }
    EXPECT_EQ(mock.opens, 0u); EXPECT_EQ(mock.live, 0u);
}

TEST(DirectSoundHooks, StartAndStopErrorsKeepThePublicStateRetryable) {
    Mock mock; DirectSound backend; ASSERT_TRUE(backend.open(config()));
    mock.started = Failure::configuration;
    EXPECT_EQ(backend.start().error(), ErrorType::ConfigurationFailed);
    mock.started = Failure::none; ASSERT_TRUE(backend.start());
    mock.stopped = Failure::endpoint;
    EXPECT_EQ(backend.stop().error(), ErrorType::EndpointUnavailable);
    EXPECT_EQ(backend.start().error(), ErrorType::InvalidState);
    EXPECT_EQ(backend.close().error(), ErrorType::InvalidState);
    mock.stopped = Failure::none; ASSERT_TRUE(backend.stop()); ASSERT_TRUE(backend.close());
    EXPECT_EQ(mock.starts, 2u); EXPECT_EQ(mock.stops, 2u);
}

TEST(DirectSoundHooks, CleanupErrorConsumesOwnershipOnlyOnceAndCloseCanRecover) {
    Mock mock; DirectSound backend; ASSERT_TRUE(backend.open(config()));
    mock.closed = Failure::configuration;
    EXPECT_EQ(backend.close().error(), ErrorType::ConfigurationFailed);
    EXPECT_EQ(mock.live, 0u); ASSERT_TRUE(backend.close()); EXPECT_EQ(mock.closes, 1u);
    mock.closed = Failure::none; ASSERT_TRUE(backend.open(config())); ASSERT_TRUE(backend.close());
}

TEST(DirectSoundHooks, DestructionReleasesOpenRunningAndFailedStopSessions) {
    Mock mock;
    { DirectSound backend; ASSERT_TRUE(backend.open(config())); }
    { DirectSound backend; ASSERT_TRUE(backend.open(config())); ASSERT_TRUE(backend.start()); }
    mock.stopped = Failure::configuration;
    { DirectSound backend; ASSERT_TRUE(backend.open(config())); ASSERT_TRUE(backend.start()); }
    EXPECT_EQ(mock.live, 0u); EXPECT_EQ(mock.closes, 3u);
}

TEST(DirectSoundHooks, InvalidPortableConfigurationNeverEntersTheAdapter) {
    Mock mock; DirectSound backend; auto cfg = config(); cfg.outputChannels = 0;
    EXPECT_EQ(backend.open(cfg).error(), ErrorType::ChannelsNotSupported);
    cfg = config(); cfg.sampleRate = 0; EXPECT_EQ(backend.open(cfg).error(), ErrorType::SampleRateNotSupported);
    cfg = config(); cfg.bufferSize = 0; EXPECT_EQ(backend.open(cfg).error(), ErrorType::BufferSizeNotSupported);
    cfg = config(); cfg.format = static_cast<Format>(123); EXPECT_EQ(backend.open(cfg).error(), ErrorType::FormatNotSupported);
    EXPECT_EQ(mock.opens, 0u);
}

TEST(DirectSoundHooks, SdkHresultsMapToPortableFailuresIncludingUnknownErrors) {
    EXPECT_EQ(failure(DS_OK), Failure::none); EXPECT_EQ(failure(S_FALSE), Failure::none);
    EXPECT_EQ(failure(DSERR_NODRIVER), Failure::endpoint); EXPECT_EQ(failure(DSERR_ALLOCATED), Failure::endpoint);
    EXPECT_EQ(failure(E_ACCESSDENIED), Failure::endpoint); EXPECT_EQ(failure(DSERR_BADFORMAT), Failure::format);
    for (const auto error : {DSERR_INVALIDPARAM, DSERR_INVALIDCALL, DSERR_OUTOFMEMORY, DSERR_BUFFERLOST, E_FAIL, HRESULT(-42)})
        EXPECT_EQ(failure(error), Failure::configuration);
}

TEST(DirectSoundHooks, CallbackAndNotificationsStayBoundToTheirOwnInstance) {
    Mock mock; DirectSound first, second;
    struct User { float value; unsigned calls = 0; bool valid = true; } a{0.25f}, b{-0.5f};
    const auto process = +[](void* user, const AudioProcessContext& context) noexcept {
        auto& data = *static_cast<User*>(user); ++data.calls;
        data.valid &= context.input.count == 0 && context.output.count == 2 && context.frames == 3;
        context.output.channels[0][0] = data.value;
    };
    ASSERT_TRUE(first.setProcessFunction(process, &a)); ASSERT_TRUE(first.open(config())); ASSERT_TRUE(first.start());
    const auto firstCallbacks = mock.processing;
    ASSERT_TRUE(second.setProcessFunction(process, &b)); ASSERT_TRUE(second.open(config())); ASSERT_TRUE(second.start());
    const auto secondCallbacks = mock.processing;
    float left[3]{}, right[3]{}; float* outputs[]{left, right};
    firstCallbacks.process(firstCallbacks.user, nullptr, 0, outputs, 2, 3); EXPECT_FLOAT_EQ(left[0], 0.25f);
    secondCallbacks.process(secondCallbacks.user, nullptr, 0, outputs, 2, 3); EXPECT_FLOAT_EQ(left[0], -0.5f);
    EXPECT_EQ(a.calls, 1u); EXPECT_EQ(b.calls, 1u); EXPECT_TRUE(a.valid); EXPECT_TRUE(b.valid);
    firstCallbacks.xrun(firstCallbacks.user); firstCallbacks.failed(firstCallbacks.user); firstCallbacks.failed(firstCallbacks.user);
    EXPECT_EQ(first.status().xruns, 1u); EXPECT_TRUE(first.status().failed);
    EXPECT_EQ(second.status().xruns, 0u); EXPECT_FALSE(second.status().failed);
    ASSERT_TRUE(first.stop()); ASSERT_TRUE(first.close()); ASSERT_TRUE(second.stop()); ASSERT_TRUE(second.close());
}

TEST(DirectSoundHooks, NullProcessBindingAndPerInstanceAdapterSnapshotArePreserved) {
    Mock mock; DirectSound first;
    mock.api.start = nullptr; DirectSound missingStart;
    ASSERT_TRUE(first.open(config())); ASSERT_TRUE(first.start()); EXPECT_EQ(mock.processing.process, nullptr);
    ASSERT_TRUE(missingStart.open(config())); EXPECT_EQ(missingStart.start().error(), ErrorType::ConfigurationFailed);
    ASSERT_TRUE(missingStart.close()); ASSERT_TRUE(first.stop()); ASSERT_TRUE(first.close());
}

TEST(DirectSoundHooks, MissingStopAdapterCannotReportAQuiescentRunningStream) {
    Mock mock; mock.api.stop = nullptr;
    { DirectSound backend; ASSERT_TRUE(backend.open(config())); ASSERT_TRUE(backend.start());
      EXPECT_EQ(backend.stop().error(), ErrorType::ConfigurationFailed);
      EXPECT_EQ(backend.close().error(), ErrorType::InvalidState); }
    EXPECT_EQ(mock.closes, 1u); EXPECT_EQ(mock.live, 0u);
}

TEST(DirectSoundHooks, DiscoveryMapsIndependentDirectionsAndAllFormatsWithoutNativeHeaderExposure) {
    Mock mock;
    const Capabilities caps{1, 2, {44100, 48000}, {Sample::i16, Sample::i24, Sample::i32, Sample::f32, Sample::f64}, {128, 256}};
    mock.endpoints = {{"in", "Input", caps, std::nullopt}, {"out", "Output", std::nullopt, caps}};
    DirectSound backend; const auto endpoints = backend.getEndPoints(); ASSERT_EQ(endpoints.size(), 2u);
    EXPECT_EQ(endpoints[0].id, "in"); EXPECT_EQ(endpoints[0].name, "Input");
    ASSERT_TRUE(endpoints[0].input); EXPECT_FALSE(endpoints[0].output); EXPECT_FALSE(endpoints[1].input); ASSERT_TRUE(endpoints[1].output);
    EXPECT_EQ(endpoints[0].input->minChannels, 1u); EXPECT_EQ(endpoints[1].output->maxChannels, 2u);
    EXPECT_EQ(endpoints[1].output->sampleRates, (std::vector<SampleRate>{44100, 48000}));
    EXPECT_EQ(endpoints[1].output->bufferSizes, (std::vector<BufferSize>{128, 256}));
    EXPECT_EQ(endpoints[1].output->formats, (std::vector<Format>{Format::Int16, Format::Int24, Format::Int32, Format::Float32, Format::Float64}));
    mock.api.devices = +[]() -> std::vector<Device> { throw std::runtime_error("native discovery failure"); };
    DirectSound failed; EXPECT_TRUE(failed.getEndPoints().empty());
    mock.api.devices = nullptr; DirectSound absent; EXPECT_TRUE(absent.getEndPoints().empty());
}
}
