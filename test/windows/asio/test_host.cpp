#include <gtest/gtest.h>
#include <expected>
#include <type_traits>
#include "../../../src/backend/windows/detail/asio_host.hpp"
import mka.audio.backend.asio;

namespace {
using namespace mka::audio::core;
using namespace mka_asio_detail;
EndpointConfig config() { return {"asio:test", Direction::Output, 0, 2, 48000, Format::Float32, 256}; }
struct Mock {
    inline static Mock* current = nullptr;
    Api api;
    ASIOError opened = ASE_OK, started = ASE_OK, stopped = ASE_OK, closed = ASE_OK;
    bool nullSuccess = false;
    unsigned opens = 0, starts = 0, stops = 0, closes = 0, live = 0;
    Request requested{};
    Mock() {
        current = this;
        api.open = +[](const Request& request, void** out) noexcept {
            auto& m = *current; ++m.opens; m.requested = request;
            if (m.nullSuccess) *out = nullptr;
            else { *out = &m; ++m.live; }
            return m.opened;
        };
        api.start = +[](void* value) noexcept { auto& m = *static_cast<Mock*>(value); ++m.starts; return m.started; };
        api.stop = +[](void* value) noexcept { auto& m = *static_cast<Mock*>(value); ++m.stops; return m.stopped; };
        api.close = +[](void** value) noexcept {
            auto& m = *static_cast<Mock*>(*value); ++m.closes; --m.live; *value = nullptr; return m.closed;
        };
        testApiOverride = &api;
    }
    ~Mock() { testApiOverride = nullptr; current = nullptr; EXPECT_EQ(live, 0u); }
};

TEST(AsioHooks, OwnsThePortableLifecycleAndRejectsCopyOrMove) {
    static_assert(std::is_base_of_v<Backend, ASIO> && std::is_final_v<ASIO>);
    static_assert(!std::is_copy_constructible_v<ASIO> && !std::is_move_constructible_v<ASIO>);
    Mock mock;
    ASIO backend;
    EXPECT_EQ(backend.start().error(), ErrorType::InvalidState);
    EXPECT_EQ(backend.close().error(), ErrorType::InvalidState);
    ASSERT_TRUE(backend.open(config()));
    EXPECT_EQ(mock.requested.id, "asio:test");
    EXPECT_EQ(mock.requested.outputs, 2u);
    EXPECT_EQ(mock.requested.rate, 48000u);
    EXPECT_EQ(mock.requested.frames, 256u);
    ASSERT_TRUE(backend.start());
    EXPECT_EQ(backend.open(config()).error(), ErrorType::InvalidState);
    ASSERT_TRUE(backend.stop());
    ASSERT_TRUE(backend.close());
    EXPECT_EQ(mock.starts, 1u); EXPECT_EQ(mock.stops, 1u); EXPECT_EQ(mock.closes, 1u);
}

TEST(AsioHooks, PartialOpenFailureCleansUpAndAllowsRetry) {
    Mock mock; ASIO backend;
    mock.opened = ASE_HWMalfunction;
    EXPECT_EQ(backend.open(config()).error(), ErrorType::EndpointUnavailable);
    EXPECT_EQ(mock.live, 0u);
    EXPECT_EQ(mock.closes, 1u);
    mock.opened = ASE_OK;
    ASSERT_TRUE(backend.open(config()));
    ASSERT_TRUE(backend.close());
    EXPECT_EQ(mock.closes, 2u);
}

TEST(AsioHooks, EveryPortableFormatAndDirectionReachesTheNativeAdapterExactly) {
    Mock mock; ASIO backend;
    const std::pair<Format, Sample> formats[]{{Format::Int16, Sample::i16}, {Format::Int24, Sample::i24},
        {Format::Int32, Sample::i32}, {Format::Float32, Sample::f32}, {Format::Float64, Sample::f64}};
    for (const auto& [format, native] : formats) {
        auto cfg = config(); cfg.format = format;
        ASSERT_TRUE(backend.open(cfg)); EXPECT_EQ(mock.requested.sample, native);
        ASSERT_TRUE(backend.close());
    }
    auto cfg = config(); cfg.direction = Direction::Input; cfg.inputChannels = 1; cfg.outputChannels = 0;
    ASSERT_TRUE(backend.open(cfg)); EXPECT_EQ(mock.requested.flow, Flow::input);
    EXPECT_EQ(mock.requested.inputs, 1u); EXPECT_EQ(mock.requested.outputs, 0u); ASSERT_TRUE(backend.close());
    cfg.direction = Direction::Duplex; cfg.outputChannels = 2;
    ASSERT_TRUE(backend.open(cfg)); EXPECT_EQ(mock.requested.flow, Flow::duplex);
    EXPECT_EQ(mock.requested.inputs, 1u); EXPECT_EQ(mock.requested.outputs, 2u); ASSERT_TRUE(backend.close());
}

TEST(AsioHooks, SuccessWithoutAnOwnedHandleIsRejected) {
    Mock mock; ASIO backend; mock.nullSuccess = true;
    EXPECT_EQ(backend.open(config()).error(), ErrorType::ConfigurationFailed);
    EXPECT_EQ(mock.live, 0u);
    mock.nullSuccess = false;
    ASSERT_TRUE(backend.open(config()));
    ASSERT_TRUE(backend.close());
}

TEST(AsioHooks, StartAndStopErrorsPreserveTheStateForRetry) {
    Mock mock; ASIO backend;
    ASSERT_TRUE(backend.open(config()));
    mock.started = ASE_NoClock;
    EXPECT_EQ(backend.start().error(), ErrorType::SampleRateNotSupported);
    mock.started = ASE_OK;
    ASSERT_TRUE(backend.start());
    mock.stopped = ASE_InvalidMode;
    EXPECT_EQ(backend.stop().error(), ErrorType::ConfigurationFailed);
    EXPECT_EQ(backend.start().error(), ErrorType::InvalidState);
    mock.stopped = ASE_OK;
    ASSERT_TRUE(backend.stop()); ASSERT_TRUE(backend.close());
}

TEST(AsioHooks, CloseFailureReleasesOwnershipAndTheNextCloseRecovers) {
    Mock mock; ASIO backend;
    ASSERT_TRUE(backend.open(config()));
    mock.closed = ASE_InvalidMode;
    EXPECT_EQ(backend.close().error(), ErrorType::ConfigurationFailed);
    EXPECT_EQ(mock.live, 0u);
    ASSERT_TRUE(backend.close());
    EXPECT_EQ(mock.closes, 1u);
}

TEST(AsioHooks, DestructionClosesAnOpenAndARunningSession) {
    Mock mock;
    { ASIO backend; ASSERT_TRUE(backend.open(config())); }
    EXPECT_EQ(mock.closes, 1u);
    { ASIO backend; ASSERT_TRUE(backend.open(config())); ASSERT_TRUE(backend.start()); }
    EXPECT_EQ(mock.closes, 2u); EXPECT_EQ(mock.stops, 1u);
}

TEST(AsioHooks, InvalidPortableConfigurationNeverEntersTheNativeAdapter) {
    Mock mock; ASIO backend; auto cfg = config(); cfg.outputChannels = 0;
    EXPECT_EQ(backend.open(cfg).error(), ErrorType::ChannelsNotSupported);
    cfg = config(); cfg.sampleRate = 0;
    EXPECT_EQ(backend.open(cfg).error(), ErrorType::SampleRateNotSupported);
    cfg = config(); cfg.bufferSize = 0;
    EXPECT_EQ(backend.open(cfg).error(), ErrorType::BufferSizeNotSupported);
    EXPECT_EQ(mock.opens, 0u);
}

TEST(AsioHooks, MapsNativeErrorsIncludingUnknownCodesWithoutExposingSdkTypes) {
    for (auto native : {ASE_InvalidParameter, ASE_InvalidMode, ASE_NoMemory, ASE_NoTimeInfo, ASIOError(-42), ASIOError(42)})
        EXPECT_EQ(failure(native), Failure::configuration);
    EXPECT_EQ(failure(ASE_NotPresent), Failure::endpoint);
    EXPECT_EQ(failure(ASE_HWMalfunction), Failure::endpoint);
    EXPECT_EQ(failure(ASE_NoClock), Failure::rate);
    EXPECT_EQ(failure(ASE_OK), Failure::none);
    EXPECT_EQ(failure(ASE_SUCCESS), Failure::none);
}
}
