#include <windows.h>
#include <unknwn.h>
#include <asiosys.h>
#include <asio.h>
#include <iasiodrv.h>
#include <gtest/gtest.h>
#include <cstdint>
#include <type_traits>
import mka.audio.backend.abstract;

namespace {
static_assert(sizeof(long) == 4 && sizeof(ASIOBool) == 4 && sizeof(ASIOSampleRate) == 8);
static_assert(std::is_same_v<decltype(&ASIOInit), ASIOError (*)(ASIODriverInfo*)>);
static_assert(std::is_same_v<decltype(&ASIOExit), ASIOError (*)()>);
static_assert(std::is_same_v<decltype(ASIOCallbacks::bufferSwitch), void (*)(long, ASIOBool)>);
static_assert(std::is_same_v<decltype(ASIOCallbacks::bufferSwitchTimeInfo), ASIOTime* (*)(ASIOTime*, long, ASIOBool)>);
static_assert(std::is_same_v<decltype(&IASIO::init), ASIOBool (IASIO::*)(void*)>);

TEST(AsioSdkContract, NativeWidthsRemainSeparateFromPortableCoreTypes) {
    EXPECT_EQ(sizeof(mka::audio::core::SampleRate), 4u);
    EXPECT_EQ(sizeof(mka::audio::core::BufferSize), 4u);
    EXPECT_EQ(sizeof(float), 4u);
    EXPECT_EQ(sizeof(ASIOSamples), 8u);
    EXPECT_EQ(sizeof(ASIOTimeStamp), 8u);
}

TEST(AsioSdkContract, NativeBufferIndicesSelectTwoDistinctBorrowedBuffers) {
    float first[]{0.25f, -0.5f}, second[]{0.75f, -0.25f};
    ASIOBufferInfo info{};
    info.isInput = ASIOTrue;
    info.channelNum = 0;
    info.buffers[0] = first;
    info.buffers[1] = second;
    const float* channels[]{static_cast<const float*>(info.buffers[0])};
    mka::audio::core::AudioProcessContext context{{channels, 1}, {nullptr, 0}, 2};
    EXPECT_FLOAT_EQ(context.input.channels[0][0], 0.25f);
    channels[0] = static_cast<const float*>(info.buffers[1]);
    EXPECT_FLOAT_EQ(context.input.channels[0][0], 0.75f);
    EXPECT_EQ(context.frames, 2u);
    EXPECT_EQ(context.output.count, 0u);
}
}
