#include <gtest/gtest.h>
#include "utils/manual_audio.hpp"
#include <array>
#include <chrono>
#include <filesystem>
#include <iterator>
#include <string>
import mka.audio.process;

namespace {
using namespace mka::audio::core;

TEST(ManualAudioSignal, SineHasTheExpectedFrequencyAmplitudeAndSilentDrainWithVariableBlocks) {
    manual_audio::Tone tone(8000);
    std::array<float, 512> left{}, right{};
    float* channels[]{left.data(), right.data()};
    std::vector<float> rendered;
    while (tone.frames.load() < tone.total) {
        const auto block = rendered.size() % 2 ? 127u : 509u;
        tone.process(AudioProcessContext{{nullptr, 0}, {channels, 2}, block});
        EXPECT_TRUE(std::equal(left.begin(), left.begin() + block, right.begin()));
        rendered.insert(rendered.end(), left.begin(), left.begin() + block);
    }
    unsigned cycles = 0;
    float peak = 0;
    for (std::size_t i = 1; i < tone.signalFrames; ++i) {
        if (rendered[i - 1] <= 0 && rendered[i] > 0) ++cycles;
        peak = std::max(peak, std::abs(rendered[i]));
    }
    EXPECT_NEAR(cycles, 3 * 440, 1);
    EXPECT_NEAR(peak, 0.2f, 0.001f);
    EXPECT_FLOAT_EQ(rendered.front(), 0);
    EXPECT_TRUE(std::all_of(rendered.begin() + tone.signalFrames, rendered.end(), [](float x) { return x == 0; }));
    EXPECT_FALSE(tone.invalidContext.load());
}

TEST(ManualAudioSignal, CaptureDiscardsWarmupAndBoundsTheLastPartialBlockOnEveryChannel) {
    manual_audio::Clip clip(1000, 2, 5);
    clip.warmup = 2;
    const float first[]{10, 11, 12, 13, 14}, second[]{-10, -11, -12, -13, -14};
    const float* channels[]{first, second};
    clip.record(AudioProcessContext{{channels, 2}, {nullptr, 0}, 5});
    EXPECT_EQ(clip.recorded.load(), 3u);
    const float last[]{20, 21, 22, 23, 24}, other[]{-20, -21, -22, -23, -24};
    channels[0] = last; channels[1] = other;
    clip.record(AudioProcessContext{{channels, 2}, {nullptr, 0}, 5});
    clip.record(AudioProcessContext{{channels, 2}, {nullptr, 0}, 5});
    EXPECT_EQ(clip.recorded.load(), 5u);
    EXPECT_EQ(clip.samples[0], (std::vector<float>{12, 13, 14, 20, 21}));
    EXPECT_EQ(clip.samples[1], (std::vector<float>{-12, -13, -14, -20, -21}));
    EXPECT_FALSE(clip.invalidContext.load());
}

TEST(ManualAudioSignal, AConstantInputIsSilentAfterDcRemovalWithoutChangingTheRawCapture) {
    manual_audio::Clip clip(48000, 2, 100);
    std::fill(clip.samples[0].begin(), clip.samples[0].end(), 0.25f);
    std::fill(clip.samples[1].begin(), clip.samples[1].end(), -0.5f);
    clip.recorded = clip.capacity;
    const auto original = clip.samples;
    for (const auto& level : clip.levels()) {
        EXPECT_TRUE(level.finite);
        EXPECT_FLOAT_EQ(level.peak, 0);
        EXPECT_DOUBLE_EQ(level.rms, 0);
    }
    EXPECT_EQ(clip.samples, original);
}

TEST(ManualAudioSignal, NonFiniteInputIsReportedInsteadOfCountingAsCapturedAudio) {
    manual_audio::Clip clip(48000, 1, 10);
    clip.samples[0][3] = std::numeric_limits<float>::quiet_NaN();
    clip.recorded = clip.capacity;
    EXPECT_FALSE(clip.levels()[0].finite);
}

TEST(ManualAudioSignal, ReplaySelectsTheStrongestAcChannelAndPreservesDurationAtADifferentNativeRate) {
    manual_audio::Clip clip(48000, 2, 4800);
    std::fill(clip.samples[0].begin(), clip.samples[0].end(), 0.4f);
    for (std::size_t i = 0; i < clip.capacity; ++i)
        clip.samples[1][i] = static_cast<float>(0.01 * std::sin(2 * std::numbers::pi * 440 * double(i) / clip.rate));
    clip.recorded = clip.capacity;
    const auto original = clip.samples;
    manual_audio::Replay replay(clip, 44100, clip.levels());
    EXPECT_EQ(replay.sourceChannel, 1u);
    EXPECT_EQ(replay.mono.size(), 4410u);
    EXPECT_FLOAT_EQ(replay.gain, 32);
    EXPECT_FLOAT_EQ(replay.mono.front(), 0);
    EXPECT_FLOAT_EQ(replay.mono.back(), 0);
    float peak = 0;
    for (auto sample : replay.mono) peak = std::max(peak, std::abs(sample));
    EXPECT_GT(peak, 0.31f);
    EXPECT_LE(peak, 0.321f);
    EXPECT_EQ(clip.samples, original);
}

TEST(ManualAudioSignal, ReplayWritesBothSpeakersAndSilencesPaddingWithoutOverwritingTheBufferEdges) {
    manual_audio::Clip clip(100, 1, 5);
    clip.samples[0] = {-0.5f, 0.5f, -0.5f, 0.5f, 0};
    clip.recorded = clip.capacity;
    manual_audio::Replay replay(clip, 100, clip.levels());
    std::array<float, 75> left, right;
    left.fill(42); right.fill(42);
    float* channels[]{left.data() + 1, right.data() + 1};
    replay.process(AudioProcessContext{{nullptr, 0}, {channels, 2}, 73});
    EXPECT_EQ(left, right);
    EXPECT_FLOAT_EQ(left.front(), 42);
    EXPECT_FLOAT_EQ(left.back(), 42);
    for (std::size_t i = 0; i < 73; ++i) {
        const auto expected = i >= replay.lead && i - replay.lead < replay.mono.size() ? replay.mono[i - replay.lead] : 0.0f;
        EXPECT_FLOAT_EQ(left[i + 1], expected);
    }
    EXPECT_GE(replay.played.load(), replay.total());
    EXPECT_FALSE(replay.invalidContext.load());
}

TEST(ManualAudioSignal, InvalidCallbackViewsAreReportedWithoutPublishingProgress) {
    manual_audio::Tone tone(48000);
    tone.process(AudioProcessContext{{nullptr, 0}, {nullptr, 2}, 128});
    EXPECT_TRUE(tone.invalidContext.load());
    EXPECT_EQ(tone.frames.load(), 0u);
    manual_audio::Clip clip(48000, 2, 100);
    clip.record(AudioProcessContext{{nullptr, 0}, {nullptr, 0}, 128});
    EXPECT_TRUE(clip.invalidContext.load());
    EXPECT_EQ(clip.recorded.load(), 0u);
}

TEST(ManualAudioSignal, WavContainsOnlyRecordedFramesWithNativeRateAndInterleavedPcm16) {
    manual_audio::Clip clip(48000, 2, 6);
    clip.samples[0] = {1, -1, 0, 42, 42, 42};
    clip.samples[1] = {-1, 0.5f, -0.5f, 42, 42, 42};
    clip.recorded = 3;
    struct File {
        std::filesystem::path path = std::filesystem::temp_directory_path() /
            ("mka-manual-audio-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".wav");
        ~File() { std::error_code error; std::filesystem::remove(path, error); }
    } file;
    ASSERT_TRUE(clip.saveWav(file.path.string().c_str()));
    std::ifstream input(file.path, std::ios::binary);
    const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)), {});
    ASSERT_EQ(bytes.size(), 56u);
    EXPECT_EQ(std::string(bytes.begin(), bytes.begin() + 4), "RIFF");
    EXPECT_EQ(std::string(bytes.begin() + 8, bytes.begin() + 16), "WAVEfmt ");
    EXPECT_EQ(std::string(bytes.begin() + 36, bytes.begin() + 40), "data");
    EXPECT_EQ(bytes[4], 48); EXPECT_EQ(bytes[22], 2);
    EXPECT_EQ(bytes[24], 0x80); EXPECT_EQ(bytes[25], 0xBB);
    EXPECT_EQ(bytes[32], 4); EXPECT_EQ(bytes[34], 16); EXPECT_EQ(bytes[40], 12);
    EXPECT_EQ(bytes[44], 0xFF); EXPECT_EQ(bytes[45], 0x7F);
    EXPECT_EQ(bytes[46], 0x01); EXPECT_EQ(bytes[47], 0x80);
    EXPECT_EQ(bytes[50], 0xFF); EXPECT_EQ(bytes[51], 0x3F);
}
}
