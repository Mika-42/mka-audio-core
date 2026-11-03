#include <cstdint>
#include <expected>
#include <string>
#include <limits>
#include <type_traits>
#include <vector>
#include <gtest/gtest.h>

import mka.audio.backend.abstract;

namespace {
using namespace mka::audio::core;

// A client importing only the documented module can discover and configure a stream.
class EndpointBackend : public Backend {
public:
    std::vector<Endpoint> endpoints;
    EndpointConfig opened{};
    unsigned openCalls = 0;
protected:
    std::vector<Endpoint> getEndPoints_() const override { return endpoints; }
    Result open_(EndpointConfig const& config) override { ++openCalls; opened = config; return {}; }
    Result start_() override { return {}; }
    Result stop_() override { return {}; }
    Result close_() override { return {}; }
};

TEST(EndpointApiTest, DiscoveryProvidesIdentityAndCapabilitiesTogether) {
    EndpointBackend backend;
    backend.endpoints = {{
        .id = "test:output", .name = "Test output", .input = std::nullopt,
        .output = StreamCapabilities{1, 2, {48000}, {Format::Float32}, {256}}
    }};
    const Backend& discovery = backend;
    const auto endpoints = discovery.getEndPoints();
    ASSERT_EQ(endpoints.size(), 1u);
    EXPECT_FALSE(endpoints[0].input.has_value());
    ASSERT_TRUE(endpoints[0].output.has_value());
    const auto& caps = *endpoints[0].output;
    EndpointConfig config{
        .id = endpoints[0].id, .direction = Direction::Output,
        .inputChannels = 0, .outputChannels = caps.maxChannels,
        .sampleRate = caps.sampleRates.front(), .format = caps.formats.front(),
        .bufferSize = caps.bufferSizes.front()
    };
    ASSERT_TRUE(backend.open(config));
    EXPECT_EQ(backend.opened.id, "test:output");
    EXPECT_EQ(backend.opened.outputChannels, 2u);
    EXPECT_EQ(backend.opened.bufferSize, 256u);
    EXPECT_TRUE(backend.close());
}

TEST(EndpointApiTest, EmptyDiscoveryIsAValidResult) {
    EndpointBackend backend;
    EXPECT_TRUE(backend.getEndPoints().empty());
}

static_assert(noexcept(std::declval<const Backend&>().getEndPoints()));
static_assert(std::is_same_v<SampleRate, std::uint32_t>);
static_assert(std::is_same_v<BufferSize, std::uint32_t>);
static_assert(std::is_same_v<decltype(EndpointConfig::inputChannels), std::uint32_t>);
static_assert(std::is_same_v<decltype(EndpointConfig::outputChannels), std::uint32_t>);
}

namespace {
struct DirectionCase { Direction direction; std::uint32_t input; std::uint32_t output; };
class DirectionContractTest : public ::testing::TestWithParam<DirectionCase> {};

TEST_P(DirectionContractTest, RejectsInconsistentCountsBeforeTheNativeHook) {
    EndpointBackend backend;
    const auto [direction, input, output] = GetParam();
    EndpointConfig config{"test", direction, input, output, 48000, Format::Float32, 256};
    const auto result = backend.open(config);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), ErrorType::ChannelsNotSupported);
    EXPECT_EQ(backend.openCalls, 0u);
    config = {"test", Direction::Output, 0, 2, 48000, Format::Float32, 256};
    ASSERT_TRUE(backend.open(config)); // failed validation leaves the backend Closed
    EXPECT_TRUE(backend.close());
}
INSTANTIATE_TEST_SUITE_P(InvalidCounts, DirectionContractTest, ::testing::Values(
    DirectionCase{Direction::Input, 0, 0}, DirectionCase{Direction::Input, 2, 1},
    DirectionCase{Direction::Output, 0, 0}, DirectionCase{Direction::Output, 1, 2},
    DirectionCase{Direction::Duplex, 0, 2}, DirectionCase{Direction::Duplex, 2, 0},
    DirectionCase{Direction::Duplex, 0, 0},
    DirectionCase{Direction::Input, 2, std::numeric_limits<std::uint32_t>::max()},
    DirectionCase{Direction::Output, std::numeric_limits<std::uint32_t>::max(), 2}
));

TEST(DirectionContractTest, ForwardsAllValidDirectionsAndCountsUnchanged) {
    EndpointBackend backend;
    for (const auto& c : {DirectionCase{Direction::Input, 3, 0},
                         DirectionCase{Direction::Output, 0, 4},
                         DirectionCase{Direction::Duplex, 1, 2}}) {
        EndpointConfig config{"test", c.direction, c.input, c.output, 48000, Format::Float32, 256};
        ASSERT_TRUE(backend.open(config));
        EXPECT_EQ(backend.opened.direction, config.direction);
        EXPECT_EQ(backend.opened.inputChannels, config.inputChannels);
        EXPECT_EQ(backend.opened.outputChannels, config.outputChannels);
        ASSERT_TRUE(backend.close());
    }
}

TEST(DirectionContractTest, RejectsAnUnknownDirectionBeforeTheNativeHook) {
    EndpointBackend backend;
    EndpointConfig config{"test", static_cast<Direction>(-1), 1, 1, 48000, Format::Float32, 256};
    const auto result = backend.open(config);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), ErrorType::ConfigurationFailed);
    EXPECT_EQ(backend.openCalls, 0u);
}
}

static_assert(std::is_same_v<decltype(mka::audio::core::InputBuffer::channels), const float* const*>);
static_assert(std::is_same_v<decltype(mka::audio::core::OutputBuffer::channels), float* const*>);
static_assert(std::is_nothrow_invocable_v<mka::audio::core::ProcessFunction, void*, const mka::audio::core::AudioProcessContext&>);
static_assert(!std::is_assignable_v<decltype(std::declval<mka::audio::core::InputBuffer>().channels[0][0]), float>);
static_assert(!std::is_assignable_v<decltype(std::declval<mka::audio::core::OutputBuffer>().channels[0]), float*>);

namespace {
TEST(EndpointConfigurationTest, RejectsInvalidScalarsBeforeCallingTheNativeHook) {
    EndpointBackend backend;
    const EndpointConfig valid{"test", Direction::Output, 0, 2, 48000, Format::Float32, 256};
    for (const auto error : {ErrorType::FormatNotSupported, ErrorType::SampleRateNotSupported,
                             ErrorType::BufferSizeNotSupported}) {
        auto config = valid;
        if (error == ErrorType::FormatNotSupported) config.format = static_cast<Format>(UINT32_MAX);
        if (error == ErrorType::SampleRateNotSupported) config.sampleRate = 0;
        if (error == ErrorType::BufferSizeNotSupported) config.bufferSize = 0;
        const auto result = backend.open(config);
        ASSERT_FALSE(result);
        EXPECT_EQ(result.error(), error);
        EXPECT_EQ(backend.openCalls, 0u);
    }
    ASSERT_TRUE(backend.open(valid));
    EXPECT_EQ(backend.opened.sampleRate, valid.sampleRate);
    EXPECT_EQ(backend.opened.format, valid.format);
    EXPECT_TRUE(backend.close());
}
}
