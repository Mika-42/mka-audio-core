#include <gtest/gtest.h>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <expected>
#include <limits>
#include "fake_driver.hpp"
import mka.audio.backend.asio;
import mka.audio.convert;
#include "conversion_helpers.hpp"

namespace {
using namespace asio_test;
using namespace mka::audio::core;
void expectInput(ASIOSampleType type,const std::vector<std::uint64_t>& bits,const std::vector<float>& expected) {
    const auto layout=*channelLayout(type); const auto payload=encodedSamples(bits,layout);
    std::vector<std::byte> guarded(payload.size()+16,std::byte{0x5a});
    std::memcpy(guarded.data()+8,payload.data(),payload.size()); const auto before=guarded;
    std::vector<float> output(expected.size()+2,777.0f);
    ASSERT_TRUE(readSamples<Conversion>(layout,guarded.data()+8,payload.size(),output.data()+1,expected.size()));
    EXPECT_EQ(guarded,before); EXPECT_FLOAT_EQ(output.front(),777); EXPECT_FLOAT_EQ(output.back(),777);
    for (std::size_t index=0;index<expected.size();++index) {
        if (std::isnan(expected[index])) EXPECT_TRUE(std::isnan(output[index+1]));
        else EXPECT_FLOAT_EQ(output[index+1],expected[index]);
    }
}
TEST(AsioInputConvert, Int16ExtremesHaveTheCorrectScaleInBothByteOrders) {
    for (const ASIOSampleType type : {ASIOSTInt16LSB,ASIOSTInt16MSB})
        expectInput(type,{0x8000,0xc000,0,0x4000,0x7fff},{-1,-0.5f,0,0.5f,32767.0f/32768.0f});
}
TEST(AsioInputConvert, Packed24ExtremesAreNotMisreadAsFullWidth32BitSamples) {
    for (const ASIOSampleType type : {ASIOSTInt24LSB,ASIOSTInt24MSB})
        expectInput(type,{0x800000,0xc00000,0,0x400000,0x7fffff},{-1,-0.5f,0,0.5f,8388607.0f/8388608.0f});
}
TEST(AsioInputConvert, RightAligned24In32IgnoresPaddingAndRetainsFullScale) {
    for (const ASIOSampleType type : {ASIOSTInt32LSB24,ASIOSTInt32MSB24})
        expectInput(type,{0xa5800000,0xa5c00000,0xa5000000,0xa5400000,0xa57fffff},
            {-1,-0.5f,0,0.5f,8388607.0f/8388608.0f});
}
TEST(AsioInputConvert, Int32ExtremesAndChannelSamplesKeepTheirSign) {
    for (const ASIOSampleType type : {ASIOSTInt32LSB,ASIOSTInt32MSB})
        expectInput(type,{0x80000000,0xc0000000,0,0x40000000,0x7fffffff},{-1,-0.5f,0,0.5f,1});
}
TEST(AsioInputConvert, Float32ReadsPreserveNaNsAndInfinitiesAccordingToCore) {
    const std::vector<float> values{-0.75f,0,0.25f,std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()};
    std::vector<std::uint64_t> bits; for (const auto value : values) bits.push_back(std::bit_cast<std::uint32_t>(value));
    for (const ASIOSampleType type : {ASIOSTFloat32LSB,ASIOSTFloat32MSB}) expectInput(type,bits,values);
}
TEST(AsioInputConvert, Float64NarrowingIsDefinedForFiniteOverflowNaNsAndInfinities) {
    const std::vector<double> values{-1,0,0.25,std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity(),std::numeric_limits<double>::max()};
    const std::vector<float> expected{-1,0,0.25f,std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity(),std::numeric_limits<float>::infinity()};
    std::vector<std::uint64_t> bits; for (const auto value : values) bits.push_back(std::bit_cast<std::uint64_t>(value));
    for (const ASIOSampleType type : {ASIOSTFloat64LSB,ASIOSTFloat64MSB}) expectInput(type,bits,expected);
}
TEST(AsioInputConvert, InvalidRegionsAreRejectedWithoutReadingOrWriting) {
    const auto layout=*channelLayout(ASIOSTFloat32LSB); const std::array<std::byte,4> source{}; float output=777;
    EXPECT_FALSE(readSamples<Conversion>(layout,nullptr,4,&output,1));
    EXPECT_FALSE(readSamples<Conversion>(layout,source.data(),4,nullptr,1));
    EXPECT_FALSE(readSamples<Conversion>(layout,source.data(),3,&output,1));
    EXPECT_FALSE(readSamples<Conversion>(layout,source.data(),4,&output,std::numeric_limits<std::size_t>::max()));
    EXPECT_FALSE(readSamples<Conversion>(layout,reinterpret_cast<const std::byte*>(std::numeric_limits<std::uintptr_t>::max()-1),4,&output,1));
    EXPECT_FALSE(readSamples<Conversion>(layout,source.data(),4,reinterpret_cast<float*>(std::numeric_limits<std::uintptr_t>::max()-1),1));
    EXPECT_FLOAT_EQ(output,777);
    EXPECT_TRUE(readSamples<Conversion>(layout,nullptr,0,nullptr,0));
}
TEST(AsioInputConvert, UnknownOrInconsistentLayoutsNeverReachTheCoreDecoder) {
    const std::array<std::byte,8> source{}; float output=777;
    for (const auto layout : {ChannelLayout{Sample::f32,0,false},ChannelLayout{Sample::f64,4,false},
        ChannelLayout{Sample::f32,4,false,true},ChannelLayout{static_cast<Sample>(99),4,false}})
        EXPECT_FALSE(readSamples<Conversion>(layout,source.data(),source.size(),&output,1));
    EXPECT_FLOAT_EQ(output,777);
}
TEST(AsioInputConvert, TwoNativeChannelsAndHalvesReachTheDspInPlanarOrder) {
    DriverFixture fixture;
    struct Captured {unsigned calls=0;std::array<std::array<float,2>,2> values{};bool valid=true;} captured;
    ASIO backend; fixture.inputFormats={ASIOSTInt24LSB,ASIOSTInt32MSB24};
    ASSERT_TRUE(backend.open({driverId,Direction::Input,2,0,48000,Format::Int24,64}));
    const std::uint64_t native[2][2]{{0x200000,0x800000},{0xa5c00000,0x00400000}};
    for (unsigned channel=0;channel<2;++channel) for (unsigned half=0;half<2;++half) {
        const auto layout=*channelLayout(fixture.inputFormats[channel]);
        const auto payload=encodedSamples(std::vector<std::uint64_t>(64,native[channel][half]),layout);
        std::memcpy(fixture.createdBuffers[channel].buffers[half],payload.data(),payload.size());
    }
    activeDriver->configureProcessing({.user=&captured,
        .process=+[](void* user,const float* const* input,std::uint32_t inputs,float* const*,std::uint32_t outputs,std::uint32_t frames) noexcept {
            auto& c=*static_cast<Captured*>(user); c.valid &= inputs==2 && outputs==0 && frames==64;
            if (c.calls<2) c.values[c.calls]={input[0][0],input[1][frames-1]}; ++c.calls;
        },.read=&readSamples<Conversion>});
    ASSERT_TRUE(activeDriver->enableProcessing());
    fixture.callbacks.bufferSwitch(0,ASIOTrue); fixture.callbacks.bufferSwitch(1,ASIOFalse);
    EXPECT_EQ(captured.calls,2u); EXPECT_TRUE(captured.valid);
    EXPECT_FLOAT_EQ(captured.values[0][0],0.25f); EXPECT_FLOAT_EQ(captured.values[0][1],-0.5f);
    EXPECT_FLOAT_EQ(captured.values[1][0],-1); EXPECT_FLOAT_EQ(captured.values[1][1],0.5f);
    ASSERT_TRUE(backend.close());
}
}
