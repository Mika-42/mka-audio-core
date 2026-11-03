#include <expected>
#include <type_traits>
#include <gtest/gtest.h>
import mka.audio.backend.wasapi;

namespace {
using namespace mka::audio::core;
static_assert(std::is_base_of_v<Backend, Wasapi>);
static_assert(std::is_same_v<decltype(std::declval<Wasapi&>().start()), Result>);
TEST(WasapiModule, ConstructsAndPreservesClosedStateGuards) {
    Wasapi backend;
    EXPECT_EQ(backend.start().error(), ErrorType::InvalidState);
    EXPECT_EQ(backend.stop().error(), ErrorType::InvalidState);
    EXPECT_EQ(backend.close().error(), ErrorType::InvalidState);
    EXPECT_TRUE(backend.setProcessFunction(nullptr));
    EXPECT_TRUE(backend.setEventHandler(nullptr));
}
}
