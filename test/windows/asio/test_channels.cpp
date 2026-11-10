#include <gtest/gtest.h>
#include <expected>
#include <limits>
#include "fake_driver.hpp"
import mka.audio.backend.asio;

namespace {
using namespace asio_test;
using namespace mka::audio::core;
EndpointConfig channelConfig(Direction direction, std::uint32_t in, std::uint32_t out) {
    return {driverId, direction, in, out, 48000, Format::Float32, 256};
}
TEST(AsioChannels, DiscoveryReportsZeroMonoStereoAndManyChannels) {
    DriverFixture fixture; ASIO backend;
    for (const long count : {0L, 1L, 2L, 32L, std::numeric_limits<long>::max()}) {
        fixture.inputs = count; fixture.outputs = count;
        const auto endpoints = backend.getEndPoints(); ASSERT_EQ(endpoints.size(), 1u);
        EXPECT_EQ(endpoints[0].input.has_value(), count != 0);
        EXPECT_EQ(endpoints[0].output.has_value(), count != 0);
        if (count) {
            EXPECT_EQ(endpoints[0].input->minChannels, 1u);
            EXPECT_EQ(endpoints[0].input->maxChannels, static_cast<std::uint32_t>(count));
            EXPECT_EQ(endpoints[0].output->maxChannels, static_cast<std::uint32_t>(count));
        }
        EXPECT_EQ(fixture.live, 0u);
    }
    EXPECT_EQ(fixture.inits, 5u); EXPECT_EQ(fixture.channelQueries, 5u);
    EXPECT_EQ(fixture.channelThread, fixture.initThread);
}
TEST(AsioChannels, CaptureOnlyAndPlaybackOnlySnapshotsDoNotInventDirections) {
    DriverFixture fixture; ASIO backend; fixture.outputs = 0;
    auto endpoints = backend.getEndPoints(); ASSERT_EQ(endpoints.size(), 1u);
    EXPECT_TRUE(endpoints[0].input); EXPECT_FALSE(endpoints[0].output);
    fixture.inputs = 0; fixture.outputs = 8;
    endpoints = backend.getEndPoints(); ASSERT_EQ(endpoints.size(), 1u);
    EXPECT_FALSE(endpoints[0].input); ASSERT_TRUE(endpoints[0].output);
    EXPECT_EQ(endpoints[0].output->maxChannels, 8u);
}
TEST(AsioChannels, OpenAcceptsExactInputOutputAndDuplexCountsOnTheOwnerThread) {
    DriverFixture fixture; ASIO backend; fixture.inputs = 3; fixture.outputs = 8;
    for (const auto& cfg : {channelConfig(Direction::Input, 3, 0), channelConfig(Direction::Output, 0, 8),
                           channelConfig(Direction::Duplex, 2, 4)}) {
        ASSERT_TRUE(backend.open(cfg)); EXPECT_EQ(fixture.channelThread, fixture.initThread);
        ASSERT_TRUE(backend.close());
    }
    EXPECT_EQ(fixture.channelQueries, 3u);
}
TEST(AsioChannels, OutOfRangeAndAbsentDirectionsRollBackAndAllowRetry) {
    DriverFixture fixture; ASIO backend; fixture.inputs = 1; fixture.outputs = 2;
    for (const auto& cfg : {channelConfig(Direction::Input, 2, 0), channelConfig(Direction::Output, 0, 3),
                           channelConfig(Direction::Duplex, 1, 3)}) {
        EXPECT_EQ(backend.open(cfg).error(), ErrorType::ChannelsNotSupported);
        EXPECT_EQ(fixture.live, 0u); EXPECT_EQ(activeDriver, nullptr);
    }
    fixture.inputs = 0;
    EXPECT_EQ(backend.open(channelConfig(Direction::Input, 1, 0)).error(), ErrorType::ChannelsNotSupported);
    ASSERT_TRUE(backend.open(channelConfig(Direction::Output, 0, 2))); ASSERT_TRUE(backend.close());
}
TEST(AsioChannels, NegativeNativeCountsCannotWrapIntoPortableCapabilities) {
    DriverFixture fixture; ASIO backend;
    for (const long count : {-1L, std::numeric_limits<long>::min()}) {
        fixture.inputs = count;
        auto endpoints = backend.getEndPoints(); ASSERT_EQ(endpoints.size(), 1u);
        EXPECT_FALSE(endpoints[0].input); EXPECT_FALSE(endpoints[0].output);
        EXPECT_EQ(backend.open(channelConfig(Direction::Output, 0, 1)).error(), ErrorType::ChannelsNotSupported);
        EXPECT_EQ(fixture.live, 0u);
        fixture.inputs = 2; fixture.outputs = count;
        EXPECT_EQ(backend.open(channelConfig(Direction::Input, 1, 0)).error(), ErrorType::ChannelsNotSupported);
        fixture.outputs = 2;
    }
}
TEST(AsioChannels, QueryErrorsAreMappedAndNeverPublishCapabilities) {
    DriverFixture fixture; ASIO backend;
    const std::pair<ASIOError, ErrorType> errors[]{{ASE_NotPresent, ErrorType::EndpointUnavailable},
        {ASE_HWMalfunction, ErrorType::EndpointUnavailable}, {ASE_InvalidParameter, ErrorType::ConfigurationFailed},
        {ASE_SUCCESS, ErrorType::ConfigurationFailed}};
    for (const auto& [error, expected] : errors) {
        fixture.channelsResult = error;
        EXPECT_EQ(backend.open(channelConfig(Direction::Output, 0, 1)).error(), expected);
        const auto endpoints = backend.getEndPoints(); ASSERT_EQ(endpoints.size(), 1u);
        EXPECT_FALSE(endpoints[0].input); EXPECT_FALSE(endpoints[0].output);
        EXPECT_EQ(fixture.live, 0u);
    }
    fixture.channelsResult = ASE_OK;
    ASSERT_TRUE(backend.open(channelConfig(Direction::Output, 0, 1))); ASSERT_TRUE(backend.close());
}
TEST(AsioChannels, DiscoveryDuringAnOpenSessionUsesCachedCountsWithoutASecondInit) {
    DriverFixture fixture; ASIO backend;
    ASSERT_TRUE(backend.open(channelConfig(Direction::Duplex, 1, 2)));
    fixture.registrations.push_back({"asio:{bbbbbbbb-1111-2222-3333-444444444444}", "Other driver"});
    const auto endpoints = backend.getEndPoints(); ASSERT_EQ(endpoints.size(), 2u);
    ASSERT_TRUE(endpoints[0].input); EXPECT_EQ(endpoints[0].input->maxChannels, 2u);
    EXPECT_FALSE(endpoints[1].input); EXPECT_FALSE(endpoints[1].output);
    EXPECT_EQ(fixture.inits, 1u); EXPECT_EQ(fixture.channelQueries, 1u);
    ASSERT_TRUE(backend.close());
    EXPECT_EQ(endpoints[0].input->maxChannels, 2u);
}
TEST(AsioChannels, ZeroAndUnsignedOverflowAreRejectedBeforeDriverCreation) {
    DriverFixture fixture; ASIO backend;
    for (const auto count : {0u, std::numeric_limits<std::uint32_t>::max()})
        EXPECT_EQ(backend.open(channelConfig(Direction::Output, 0, count)).error(), ErrorType::ChannelsNotSupported);
    EXPECT_EQ(fixture.creates, 0u);
}
}
