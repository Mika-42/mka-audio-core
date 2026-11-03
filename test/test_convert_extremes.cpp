#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>
import mka.audio.convert;

namespace {
using namespace mka::audio::convert;
static_assert(noexcept(readChannel(Layout::F32, nullptr, 0, nullptr, 0)));
static_assert(noexcept(writeChannel(Layout::F32, nullptr, 0, nullptr, 0)));
constexpr auto layouts = std::array{Layout::S16, Layout::S24In32, Layout::S24Packed,
                                    Layout::S32, Layout::F32, Layout::F64};
class ConvertExtremes : public testing::TestWithParam<Layout> {};
template<class T> T load(const std::byte* p) { T v; std::memcpy(&v, p, sizeof(v)); return v; }

TEST_P(ConvertExtremes, UnalignedStridedExtremesHaveExactEncodingAndKeepCanaries) {
    const auto layout = GetParam();
    const auto width = bytesPerSample(layout), stride = width + 5;
    const auto inf = std::numeric_limits<float>::infinity();
    const auto max = std::numeric_limits<float>::max();
    const auto tiny = std::numeric_limits<float>::denorm_min();
    const std::array input{std::numeric_limits<float>::quiet_NaN(), inf, -inf,
        max, -max, 1.0f, -1.0f, std::nextafter(1.0f, 0.0f),
        std::nextafter(-1.0f, 0.0f), std::nextafter(1.0f, inf),
        std::nextafter(-1.0f, -inf), tiny, -tiny, 0.0f, -0.0f};
    std::vector<std::byte> bytes(3 + stride * input.size() + 3, std::byte{0xA5});
    writeChannel(layout, bytes.data() + 3, stride, input.data(), input.size());
    std::vector<float> output(input.size() + 2, 42.0f);
    readChannel(layout, bytes.data() + 3, stride, output.data() + 1, input.size());
    EXPECT_EQ(output.front(), 42.0f); EXPECT_EQ(output.back(), 42.0f);
    for (std::size_t byte = 0; byte < bytes.size(); ++byte) {
        const bool sample = byte >= 3 && (byte - 3) / stride < input.size()
                            && (byte - 3) % stride < width;
        if (!sample) EXPECT_EQ(bytes[byte], std::byte{0xA5}) << byte;
    }
    for (std::size_t i = 0; i < input.size(); ++i) {
        SCOPED_TRACE(i);
        const auto* p = bytes.data() + 3 + i * stride;
        const float value = std::isnan(input[i]) ? 0.0f : input[i];
        if (layout == Layout::F32 || layout == Layout::F64) {
            if (layout == Layout::F32)
                EXPECT_EQ(std::bit_cast<std::uint32_t>(load<float>(p)), std::bit_cast<std::uint32_t>(value));
            else
                EXPECT_EQ(std::bit_cast<std::uint64_t>(load<double>(p)), std::bit_cast<std::uint64_t>(static_cast<double>(value)));
            EXPECT_EQ(std::bit_cast<std::uint32_t>(output[i + 1]), std::bit_cast<std::uint32_t>(value));
        } else {
            // Endpoints and tiny values have format-independent expected codes;
            // do not rely on read(write(x)) to detect paired conversion mistakes.
            const std::int64_t peak = layout == Layout::S16 ? 32767
                : layout == Layout::S32 ? 2147483647 : 8388607;
            const bool insideFullScale = std::abs(value) == std::nextafter(1.0f, 0.0f);
            const std::int64_t insidePeak = layout == Layout::S16 ? 32767
                : layout == Layout::S32 ? 2147483519 : 8388606;
            const auto magnitude = std::abs(value) < 0.5f ? 0 : (insideFullScale ? insidePeak : peak);
            const auto expected = value < 0 ? -magnitude : magnitude;
            std::int64_t actual;
            if (layout == Layout::S16) actual = load<std::int16_t>(p);
            else if (layout == Layout::S24Packed) {
                const auto code = std::to_integer<std::uint32_t>(p[0])
                    | (std::to_integer<std::uint32_t>(p[1]) << 8)
                    | (std::to_integer<std::uint32_t>(p[2]) << 16);
                actual = (code & 0x800000u) ? static_cast<std::int64_t>(code) - 0x1000000 : code;
            } else actual = load<std::int32_t>(p);
            EXPECT_EQ(actual, expected);
            EXPECT_GE(actual, -peak); EXPECT_LE(actual, peak);
            const float expectedRead = static_cast<float>(static_cast<double>(actual) / (peak + 1.0));
            EXPECT_FLOAT_EQ(output[i + 1], expectedRead);
        }
    }
}

TEST_P(ConvertExtremes, OneFrameNeverUsesStrideToFormAnUnusedPointer) {
    // For a single frame only the initial sample address must be valid. The
    // unused next address must not overflow, even for the maximum size_t stride.
    alignas(8) std::array<std::byte, 8> bytes{};
    const float input = 0.25f;
    float output = 0;
    writeChannel(GetParam(), bytes.data(), std::numeric_limits<std::size_t>::max(), &input, 1);
    readChannel(GetParam(), bytes.data(), std::numeric_limits<std::size_t>::max(), &output, 1);
    EXPECT_NEAR(output, input, 1.0f / 32768.0f);
}

TEST_P(ConvertExtremes, ZeroFramesAllowNullPointersAndMaximumStride) {
    readChannel(GetParam(), nullptr, std::numeric_limits<std::size_t>::max(), nullptr, 0);
    writeChannel(GetParam(), nullptr, std::numeric_limits<std::size_t>::max(), nullptr, 0);
}

TEST(ConvertExtremeRead, EveryS24UpperByteIsIgnoredAtBothSignedEndpoints) {
    for (std::uint32_t high = 0; high < 256; ++high) {
        for (const auto low : {0x00800000u, 0x007FFFFFu}) {
            const auto encoded = (high << 24) | low;
            std::array<std::byte, 7> bytes{};
            std::memcpy(bytes.data() + 3, &encoded, sizeof(encoded));
            float output;
            readChannel(Layout::S24In32, bytes.data() + 3, 4, &output, 1);
            const float expected = low == 0x00800000u ? -1.0f : 8388607.0f / 8388608.0f;
            EXPECT_EQ(output, expected) << high;
        }
    }
}

TEST(ConvertExtremeRead, FloatingReadsPreserveNaNInfinityAndSignedZero) {
    for (const auto layout : {Layout::F32, Layout::F64}) {
        for (const auto input : {std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(), 0.0, -0.0}) {
            std::array<std::byte, 11> bytes{};
            if (layout == Layout::F64) std::memcpy(bytes.data() + 3, &input, sizeof(input));
            else { const auto value = static_cast<float>(input); std::memcpy(bytes.data() + 3, &value, sizeof(value)); }
            float output;
            readChannel(layout, bytes.data() + 3, bytesPerSample(layout), &output, 1);
            if (std::isnan(input)) EXPECT_TRUE(std::isnan(output));
            else { EXPECT_EQ(output, static_cast<float>(input)); EXPECT_EQ(std::signbit(output), std::signbit(input)); }
        }
    }
}

TEST(ConvertExtremeRead, F64FiniteValuesBeyondF32RangeBecomeSignedInfinity) {
    const auto limit = static_cast<double>(std::numeric_limits<float>::max());
    for (const auto input : {std::numeric_limits<double>::max(), -std::numeric_limits<double>::max(),
                            std::nextafter(limit, std::numeric_limits<double>::infinity()),
                            std::nextafter(-limit, -std::numeric_limits<double>::infinity())}) {
        float output;
        readChannel(Layout::F64, reinterpret_cast<const std::byte*>(&input), sizeof(input), &output, 1);
        EXPECT_TRUE(std::isinf(output)); EXPECT_EQ(std::signbit(output), std::signbit(input));
    }
}

TEST(ConvertExtremeRead, InvalidLayoutHasNoSizeAndTouchesNothing) {
    const auto invalid = static_cast<Layout>(255);
    EXPECT_EQ(bytesPerSample(invalid), 0u);
    readChannel(invalid, nullptr, 0, nullptr, std::numeric_limits<std::size_t>::max());
    writeChannel(invalid, nullptr, 0, nullptr, std::numeric_limits<std::size_t>::max());
}
INSTANTIATE_TEST_SUITE_P(AllLayouts, ConvertExtremes, testing::ValuesIn(layouts));
}
