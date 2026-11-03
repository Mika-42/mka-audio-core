#include <cstdint>
#include <expected>
#include <string>
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
protected:
    std::vector<Endpoint> getEndPoints_() const override { return endpoints; }
    Result open_(EndpointConfig const& config) override { opened = config; return {}; }
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
