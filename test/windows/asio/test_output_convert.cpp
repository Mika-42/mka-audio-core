#include <gtest/gtest.h>
#include <array>
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
void expectOutput(ASIOSampleType type,const std::vector<float>& input,const std::vector<std::uint64_t>& bits) {
    const auto layout=*channelLayout(type); const auto expected=encodedSamples(bits,layout); const auto before=input;
    std::vector<std::byte> output(expected.size()+16,std::byte{0x5a});
    ASSERT_TRUE(writeSamples<Conversion>(layout,output.data()+8,expected.size(),input.data(),input.size()));
    EXPECT_EQ(std::memcmp(output.data()+8,expected.data(),expected.size()),0);
    EXPECT_EQ(std::memcmp(input.data(),before.data(),input.size()*sizeof(float)),0);
    for (unsigned i=0;i<8;++i) {EXPECT_EQ(output[i],std::byte{0x5a});EXPECT_EQ(output[output.size()-1-i],std::byte{0x5a});}
}
std::vector<float> integerValues() {return {-2,-1,-0.5f,0,0.5f,1,2,std::numeric_limits<float>::quiet_NaN(),
    std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()};}
TEST(AsioOutputConvert, Int16SaturatesSymmetricallyAndSanitizesNaNsInBothByteOrders) {
    for (const ASIOSampleType type : {ASIOSTInt16LSB,ASIOSTInt16MSB})
        expectOutput(type,integerValues(),{0x8001,0x8001,0xc000,0,0x4000,0x7fff,0x7fff,0,0x7fff,0x8001});
}
TEST(AsioOutputConvert, Packed24WritesExactlyThreeBytesAtFullScale) {
    for (const ASIOSampleType type : {ASIOSTInt24LSB,ASIOSTInt24MSB})
        expectOutput(type,integerValues(),{0x800001,0x800001,0xc00000,0,0x400000,0x7fffff,0x7fffff,0,0x7fffff,0x800001});
}
TEST(AsioOutputConvert, RightAligned24In32UsesThe24BitScaleAndSignExtendsPadding) {
    for (const ASIOSampleType type : {ASIOSTInt32LSB24,ASIOSTInt32MSB24})
        expectOutput(type,integerValues(),{0xff800001,0xff800001,0xffc00000,0,0x00400000,0x007fffff,0x007fffff,0,0x007fffff,0xff800001});
}
TEST(AsioOutputConvert, Int32PositiveFullScaleDoesNotOverflowTheSignedContainer) {
    for (const ASIOSampleType type : {ASIOSTInt32LSB,ASIOSTInt32MSB})
        expectOutput(type,integerValues(),{0x80000001,0x80000001,0xc0000000,0,0x40000000,0x7fffffff,0x7fffffff,0,0x7fffffff,0x80000001});
}
TEST(AsioOutputConvert, Float32PreservesRangeAndInfinitiesWhileWritingNaNAsSilence) {
    const std::vector<float> values{-2,-0.5f,-0.0f,0.25f,2,std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()};
    for (const ASIOSampleType type : {ASIOSTFloat32LSB,ASIOSTFloat32MSB})
        expectOutput(type,values,{0xc0000000,0xbf000000,0x80000000,0x3e800000,0x40000000,0,0x7f800000,0xff800000});
}
TEST(AsioOutputConvert, Float64WritesExactWidenedValuesInBothByteOrders) {
    const std::vector<float> values{-2,-0.5f,-0.0f,0.25f,2,std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()};
    for (const ASIOSampleType type : {ASIOSTFloat64LSB,ASIOSTFloat64MSB})
        expectOutput(type,values,{0xc000000000000000ULL,0xbfe0000000000000ULL,0x8000000000000000ULL,
            0x3fd0000000000000ULL,0x4000000000000000ULL,0,0x7ff0000000000000ULL,0xfff0000000000000ULL});
}
TEST(AsioOutputConvert, InvalidRegionsAreRejectedBeforeAnyNativeWrite) {
    const auto layout=*channelLayout(ASIOSTFloat32LSB); const float input=0.5f;
    std::array<std::byte,4> output{std::byte{0x5a},std::byte{0x5a},std::byte{0x5a},std::byte{0x5a}}; const auto before=output;
    EXPECT_FALSE(writeSamples<Conversion>(layout,nullptr,4,&input,1));
    EXPECT_FALSE(writeSamples<Conversion>(layout,output.data(),4,nullptr,1));
    EXPECT_FALSE(writeSamples<Conversion>(layout,output.data(),3,&input,1));
    EXPECT_FALSE(writeSamples<Conversion>(layout,output.data(),4,&input,std::numeric_limits<std::size_t>::max()));
    EXPECT_FALSE(writeSamples<Conversion>(layout,reinterpret_cast<std::byte*>(std::numeric_limits<std::uintptr_t>::max()-1),4,&input,1));
    EXPECT_FALSE(writeSamples<Conversion>(layout,output.data(),4,reinterpret_cast<const float*>(std::numeric_limits<std::uintptr_t>::max()-1),1));
    EXPECT_EQ(output,before); EXPECT_TRUE(writeSamples<Conversion>(layout,nullptr,0,nullptr,0));
}
TEST(AsioOutputConvert, UnknownOrInconsistentLayoutsLeaveTheDestinationUntouched) {
    std::array<std::byte,8> output{}; const float input=0.5f; const auto before=output;
    for (const auto layout : {ChannelLayout{Sample::i16,4,false},ChannelLayout{Sample::i24,3,false,true},
        ChannelLayout{Sample::f64,0,false},ChannelLayout{static_cast<Sample>(99),4,false}})
        EXPECT_FALSE(writeSamples<Conversion>(layout,output.data(),output.size(),&input,1));
    EXPECT_EQ(output,before);
}
TEST(AsioOutputConvert, NullAndPartialUserCallbacksSilenceEveryUnwrittenSampleAndChannel) {
    DriverFixture fixture; ASIO backend;
    ASSERT_TRUE(backend.open({driverId,Direction::Output,0,2,48000,Format::Float32,64}));
    const auto dirty=[&] {for (const auto& info : fixture.createdBuffers) for (auto half : info.buffers) std::memset(half,0x5a,64*4);};
    dirty(); activeDriver->configureProcessing({.write=&writeSamples<Conversion>}); ASSERT_TRUE(activeDriver->enableProcessing());
    fixture.callbacks.bufferSwitch(0,ASIOTrue);
    for (const auto& info : fixture.createdBuffers) {
        const auto* first=static_cast<const std::byte*>(info.buffers[0]); const auto* second=static_cast<const std::byte*>(info.buffers[1]);
        for (unsigned i=0;i<64*4;++i) {EXPECT_EQ(first[i],std::byte{0});EXPECT_EQ(second[i],std::byte{0x5a});}
    }
    fixture.callbacks.bufferSwitch(1,ASIOFalse); activeDriver->retireProcessing(); dirty();
    activeDriver->configureProcessing({.process=+[](void*,const float* const*,std::uint32_t,float* const* output,std::uint32_t,std::uint32_t) noexcept {
        output[0][0]=0.5f;
    },.write=&writeSamples<Conversion>}); ASSERT_TRUE(activeDriver->enableProcessing());
    fixture.callbacks.bufferSwitch(0,ASIOTrue);
    for (unsigned channel=0;channel<2;++channel) for (unsigned frame=0;frame<64;++frame) {
        float value=0; std::memcpy(&value,static_cast<const std::byte*>(fixture.createdBuffers[channel].buffers[0])+frame*4,4);
        EXPECT_FLOAT_EQ(value,channel==0&&frame==0?0.5f:0.0f);
    }
    ASSERT_TRUE(backend.close());
}
TEST(AsioOutputConvert, MixedNativeContainersWriteOnlyTheSelectedHalfAndKeepTheirGuards) {
    DriverFixture fixture; ASIO backend; fixture.outputFormats={ASIOSTInt24LSB,ASIOSTInt32MSB24};
    ASSERT_TRUE(backend.open({driverId,Direction::Output,0,2,48000,Format::Int24,64}));
    for (unsigned channel=0;channel<2;++channel) for (auto half : fixture.createdBuffers[channel].buffers)
        std::memset(half,0x5a,64*channelLayout(fixture.outputFormats[channel])->bytes);
    activeDriver->configureProcessing({.process=+[](void*,const float* const*,std::uint32_t,float* const* output,std::uint32_t,std::uint32_t frames) noexcept {
        for (unsigned frame=0;frame<frames;++frame) {output[0][frame]=0.25f;output[1][frame]=-0.5f;}
    },.write=&writeSamples<Conversion>}); ASSERT_TRUE(activeDriver->enableProcessing());
    fixture.callbacks.bufferSwitch(0,ASIOTrue);
    for (unsigned channel=0;channel<2;++channel) {
        const auto layout=*channelLayout(fixture.outputFormats[channel]);
        const auto expected=encodedSamples(std::vector<std::uint64_t>(64,channel?0xffc00000:0x200000),layout);
        EXPECT_EQ(std::memcmp(fixture.createdBuffers[channel].buffers[0],expected.data(),expected.size()),0);
        const auto* untouched=static_cast<const std::byte*>(fixture.createdBuffers[channel].buffers[1]);
        for (std::size_t i=0;i<expected.size();++i) EXPECT_EQ(untouched[i],std::byte{0x5a});
    }
    for (const auto& storage : fixture.storage) for (unsigned i=0;i<8;++i) {
        EXPECT_EQ(storage[i],std::byte{0x5a});EXPECT_EQ(storage[storage.size()-1-i],std::byte{0x5a});
    }
    ASSERT_TRUE(backend.close());
}
}
