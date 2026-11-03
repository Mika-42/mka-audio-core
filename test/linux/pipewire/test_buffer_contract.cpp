#include <array>
#include <cstdint>
#include <limits>
#include <gtest/gtest.h>
#include "../../../src/backend/linux/detail/pipewire_buffer.hpp"

namespace {
using mka::audio::core::detail::readableFloatFrames;
using mka::audio::core::detail::writableFloatPlane;
class NativePlaneTest : public ::testing::Test {
protected:
    std::array<float, 16> samples{};
    spa_chunk chunk{.offset = 0, .size = sizeof(samples), .stride = sizeof(float), .flags = 0};
    spa_data plane{.maxsize = sizeof(samples), .data = samples.data(), .chunk = &chunk};
};
TEST_F(NativePlaneTest, AcceptsWholePlanesAndAlignedSubranges) {
    ASSERT_EQ(readableFloatFrames(plane), 16u);
    chunk.offset = 4 * sizeof(float);
    chunk.size = 8 * sizeof(float);
    ASSERT_EQ(readableFloatFrames(plane), 8u);
}
TEST_F(NativePlaneTest, RejectsMissingOrMisalignedPointers) {
    plane.data = nullptr;
    EXPECT_FALSE(writableFloatPlane(plane));
    plane.data = reinterpret_cast<std::byte*>(samples.data()) + 1;
    EXPECT_FALSE(writableFloatPlane(plane));
    plane.data = samples.data();
    plane.chunk = nullptr;
    EXPECT_FALSE(readableFloatFrames(plane));
    EXPECT_FALSE(writableFloatPlane(plane));
}
TEST_F(NativePlaneTest, RejectsChunksOutsideTheMappedPlaneWithoutOverflow) {
    chunk.offset = std::numeric_limits<std::uint32_t>::max();
    EXPECT_FALSE(readableFloatFrames(plane));
    chunk.offset = sizeof(float);
    chunk.size = sizeof(samples);
    EXPECT_FALSE(readableFloatFrames(plane));
    chunk.offset = 0;
    chunk.size = std::numeric_limits<std::uint32_t>::max();
    EXPECT_FALSE(readableFloatFrames(plane));
}
TEST_F(NativePlaneTest, RejectsNonFloatStridesAndPartialSamples) {
    chunk.stride = 2 * sizeof(float);
    EXPECT_FALSE(readableFloatFrames(plane));
    chunk.stride = sizeof(float);
    chunk.size = 3;
    EXPECT_FALSE(readableFloatFrames(plane));
    chunk.size = sizeof(float);
    chunk.offset = 1;
    EXPECT_FALSE(readableFloatFrames(plane));
}
}
