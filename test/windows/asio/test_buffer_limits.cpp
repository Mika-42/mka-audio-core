#include <gtest/gtest.h>
#include <expected>
#include <limits>
#include "fake_driver.hpp"
import mka.audio.backend.asio;

namespace {
using namespace asio_test;
using namespace mka::audio::core;
EndpointConfig bufferConfig(std::uint32_t frames) {
    return {driverId, Direction::Output, 0, 2, 48000, Format::Float32, frames};
}
TEST(AsioBufferLimits, FixedSizeRequiresTheRequestedValueExactly) {
    DriverFixture fixture; ASIO backend;
    fixture.minimum = fixture.maximum = fixture.preferred = 256; fixture.granularity = 0;
    ASSERT_TRUE(backend.open(bufferConfig(256))); ASSERT_TRUE(backend.close());
    for (const auto frames : {128u, 255u, 257u, 512u}) {
        EXPECT_EQ(backend.open(bufferConfig(frames)).error(), ErrorType::BufferSizeNotSupported);
        EXPECT_EQ(fixture.live, 0u);
    }
    const auto endpoints = backend.getEndPoints(); ASSERT_EQ(endpoints.size(), 1u); ASSERT_TRUE(endpoints[0].output);
    EXPECT_EQ(endpoints[0].output->bufferSizes, (std::vector<std::uint32_t>{256}));
}
TEST(AsioBufferLimits, PowersOfTwoNeverSubstituteThePreferredSize) {
    DriverFixture fixture; ASIO backend;
    for (const auto frames : {64u, 128u, 256u, 512u, 1024u}) {
        ASSERT_TRUE(backend.open(bufferConfig(frames))); ASSERT_TRUE(backend.close());
        EXPECT_EQ(fixture.bufferThread, fixture.initThread);
    }
    for (const auto frames : {32u, 63u, 96u, 192u, 1025u, 2048u})
        EXPECT_EQ(backend.open(bufferConfig(frames)).error(), ErrorType::BufferSizeNotSupported);
}
TEST(AsioBufferLimits, PositiveGranularityIsRelativeToTheNativeMinimum) {
    DriverFixture fixture; ASIO backend;
    fixture.minimum = 80; fixture.maximum = 960; fixture.preferred = 256; fixture.granularity = 16;
    for (const auto frames : {80u, 96u, 256u, 960u}) {
        ASSERT_TRUE(backend.open(bufferConfig(frames))); ASSERT_TRUE(backend.close());
    }
    for (const auto frames : {64u, 81u, 255u, 961u})
        EXPECT_EQ(backend.open(bufferConfig(frames)).error(), ErrorType::BufferSizeNotSupported);
}
TEST(AsioBufferLimits, CorruptRangesAndPreferredValuesRollBackWithoutInventingSizes) {
    DriverFixture fixture; ASIO backend;
    const long invalid[][4]{{0,1024,256,-1}, {-64,1024,256,-1}, {1024,64,256,-1},
        {64,1024,32,-1}, {64,1024,2048,-1}, {64,1024,256,-2}, {64,1024,256,0},
        {96,1024,256,-1}, {64,1000,256,-1}, {64,1024,96,-1}, {80,960,255,16}, {80,959,256,16}};
    for (const auto& row : invalid) {
        fixture.minimum=row[0]; fixture.maximum=row[1]; fixture.preferred=row[2]; fixture.granularity=row[3];
        EXPECT_EQ(backend.open(bufferConfig(256)).error(), ErrorType::BufferSizeNotSupported);
        EXPECT_EQ(fixture.live, 0u);
        const auto endpoints = backend.getEndPoints(); ASSERT_EQ(endpoints.size(), 1u); ASSERT_TRUE(endpoints[0].output);
        EXPECT_TRUE(endpoints[0].output->bufferSizes.empty());
    }
    fixture.minimum=64; fixture.maximum=1024; fixture.preferred=256; fixture.granularity=-1;
    ASSERT_TRUE(backend.open(bufferConfig(256))); ASSERT_TRUE(backend.close());
}
TEST(AsioBufferLimits, DenseHugeRangesHaveBoundedSortedSnapshotsWithoutOverflow) {
    const auto limits = BufferLimits::fromNative(1, std::numeric_limits<long>::max(), 123456, 1);
    ASSERT_TRUE(limits); EXPECT_TRUE(limits->contains(std::numeric_limits<long>::max()));
    EXPECT_FALSE(limits->contains(std::numeric_limits<std::uint32_t>::max()));
    const auto sizes = limits->snapshot(); EXPECT_LE(sizes.size(), 258u);
    ASSERT_FALSE(sizes.empty()); EXPECT_EQ(sizes.front(), 1u);
    EXPECT_EQ(sizes.back(), static_cast<std::uint32_t>(std::numeric_limits<long>::max()));
    EXPECT_NE(std::find(sizes.begin(), sizes.end(), 123456u), sizes.end());
    EXPECT_TRUE(std::is_sorted(sizes.begin(), sizes.end()));
    EXPECT_EQ(std::adjacent_find(sizes.begin(), sizes.end()), sizes.end());
    for (const auto size : sizes) EXPECT_TRUE(limits->contains(size));
}
TEST(AsioBufferLimits, SignedMaximumAndPowerOfTwoBoundariesDoNotWrap) {
    const auto max = std::numeric_limits<long>::max();
    const auto fixed = BufferLimits::fromNative(max, max, max, 0); ASSERT_TRUE(fixed);
    EXPECT_EQ(fixed->snapshot(), (std::vector<std::uint32_t>{static_cast<std::uint32_t>(max)}));
    const auto powers = BufferLimits::fromNative(1, 1L << 30, 256, -1); ASSERT_TRUE(powers);
    EXPECT_EQ(powers->snapshot().size(), 31u); EXPECT_EQ(powers->snapshot().back(), 1u << 30);
    EXPECT_FALSE(BufferLimits::fromNative(1, max, 256, -1));
}
TEST(AsioBufferLimits, NativeQueryErrorsReleaseTheDriverAndAllowRetry) {
    DriverFixture fixture; ASIO backend;
    const std::pair<ASIOError,ErrorType> errors[]{{ASE_NotPresent,ErrorType::EndpointUnavailable},
        {ASE_NoMemory,ErrorType::ConfigurationFailed}, {ASE_SUCCESS,ErrorType::ConfigurationFailed}};
    for (const auto& [error, expected] : errors) {
        fixture.buffersResult = error;
        EXPECT_EQ(backend.open(bufferConfig(256)).error(), expected); EXPECT_EQ(fixture.live, 0u);
    }
    fixture.buffersResult = ASE_OK;
    ASSERT_TRUE(backend.open(bufferConfig(256)));
    const auto before = fixture.bufferQueries;
    const auto endpoints = backend.getEndPoints(); ASSERT_EQ(endpoints.size(), 1u);
    ASSERT_TRUE(endpoints[0].input); ASSERT_TRUE(endpoints[0].output);
    EXPECT_EQ(endpoints[0].input->bufferSizes, (std::vector<std::uint32_t>{64,128,256,512,1024}));
    EXPECT_EQ(endpoints[0].input->bufferSizes, endpoints[0].output->bufferSizes);
    EXPECT_EQ(fixture.bufferQueries, before); ASSERT_TRUE(backend.close());
    EXPECT_EQ(endpoints[0].output->bufferSizes.size(), 5u);
}
TEST(AsioBufferLimits, ZeroAndUnsignedFrameOverflowNeverInitializeTheDriver) {
    DriverFixture fixture; ASIO backend;
    for (const auto frames : {0u, std::numeric_limits<std::uint32_t>::max()})
        EXPECT_EQ(backend.open(bufferConfig(frames)).error(), ErrorType::BufferSizeNotSupported);
    EXPECT_EQ(fixture.creates, 0u);
}
}
