#include <gtest/gtest.h>
#include <expected>
#include <type_traits>
import mka.audio.backend.asio;
import asio;

TEST(AsioModule, ImportsPortableContractWithoutAnySdkIncludeDirectory) {
    using namespace mka::audio::core;
    static_assert(std::is_abstract_v<Backend>);
    static_assert(std::is_same_v<Result, std::expected<void, ErrorType>>);
    EndpointConfig config{"test", Direction::Output, 0, 2, 48000, Format::Float32, 256};
    EXPECT_EQ(config.sampleRate, 48000u);
    EXPECT_EQ(config.outputChannels, 2u);
    EXPECT_TRUE(Result{});
}

TEST(AsioModule, PublicClassConstructsWithoutSdkHeadersAndNeverInventsAnOpenStream) {
    using namespace mka::audio::core;
    ASIO backend;
    [[maybe_unused]] const auto snapshot = backend.getEndPoints();
    EXPECT_TRUE(backend.setProcessFunction(nullptr));
    const EndpointConfig config{"missing", Direction::Output, 0, 2, 48000, Format::Float32, 256};
    EXPECT_EQ(backend.open(config).error(), ErrorType::EndpointUnavailable);
    EXPECT_EQ(backend.start().error(), ErrorType::InvalidState);
}
