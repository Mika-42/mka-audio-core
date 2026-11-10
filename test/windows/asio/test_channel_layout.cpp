#include <gtest/gtest.h>
#include <expected>
#include "fake_driver.hpp"
import mka.audio.backend.asio;

namespace {
using namespace asio_test;
using namespace mka::audio::core;
EndpointConfig formatConfig(Format format, Direction direction = Direction::Output, std::uint32_t in = 0, std::uint32_t out = 2) {
    return {driverId, direction, in, out, 48000, format, 256};
}
TEST(AsioChannelLayout, NativeTypesRetainWidthEndiannessAndRightAligned24BitPacking) {
    struct Case { ASIOSampleType type; Sample sample; std::uint32_t bytes; bool big, in32; };
    const Case cases[]{{ASIOSTInt16LSB,Sample::i16,2,false,false},{ASIOSTInt16MSB,Sample::i16,2,true,false},
        {ASIOSTInt24LSB,Sample::i24,3,false,false},{ASIOSTInt24MSB,Sample::i24,3,true,false},
        {ASIOSTInt32LSB24,Sample::i24,4,false,true},{ASIOSTInt32MSB24,Sample::i24,4,true,true},
        {ASIOSTInt32LSB,Sample::i32,4,false,false},{ASIOSTInt32MSB,Sample::i32,4,true,false},
        {ASIOSTFloat32LSB,Sample::f32,4,false,false},{ASIOSTFloat32MSB,Sample::f32,4,true,false},
        {ASIOSTFloat64LSB,Sample::f64,8,false,false},{ASIOSTFloat64MSB,Sample::f64,8,true,false}};
    for (const auto& c : cases) {
        const auto layout = channelLayout(c.type); ASSERT_TRUE(layout);
        EXPECT_EQ(layout->sample,c.sample); EXPECT_EQ(layout->bytes,c.bytes);
        EXPECT_EQ(layout->bigEndian,c.big); EXPECT_EQ(layout->low24In32,c.in32);
    }
}
TEST(AsioChannelLayout, AllFivePortableFormatsRequireAnExactNativeMatch) {
    DriverFixture fixture; ASIO backend;
    const std::pair<ASIOSampleType,Format> cases[]{{ASIOSTInt16LSB,Format::Int16},
        {ASIOSTInt24LSB,Format::Int24},{ASIOSTInt32LSB,Format::Int32},
        {ASIOSTFloat32LSB,Format::Float32},{ASIOSTFloat64LSB,Format::Float64}};
    for (const auto& [native,format] : cases) {
        fixture.nativeFormat=native;
        ASSERT_TRUE(backend.open(formatConfig(format)));
        ASSERT_EQ(activeDriver->outputLayouts().size(),2u);
        EXPECT_EQ(activeDriver->outputLayouts()[0],*channelLayout(native));
        EXPECT_EQ(fixture.infoThread,fixture.initThread); ASSERT_TRUE(backend.close());
    }
}
TEST(AsioChannelLayout, ChannelsMayDifferInEndiannessAndContainerWithoutLosingTheirLayouts) {
    DriverFixture fixture; ASIO backend;
    fixture.outputFormats={ASIOSTInt24LSB,ASIOSTInt32MSB24};
    ASSERT_TRUE(backend.open(formatConfig(Format::Int24)));
    const auto& layouts=activeDriver->outputLayouts(); ASSERT_EQ(layouts.size(),2u);
    EXPECT_EQ(layouts[0].bytes,3u); EXPECT_FALSE(layouts[0].bigEndian); EXPECT_FALSE(layouts[0].low24In32);
    EXPECT_EQ(layouts[1].bytes,4u); EXPECT_TRUE(layouts[1].bigEndian); EXPECT_TRUE(layouts[1].low24In32);
    ASSERT_TRUE(backend.close());
}
TEST(AsioChannelLayout, UnknownDsdAndUnimplementedIntegerPackingsAreNeverAdvertised) {
    DriverFixture fixture; ASIO backend;
    const ASIOSampleType unsupported[]{-1,ASIOSTLastEntry,ASIOSTDSDInt8LSB1,ASIOSTDSDInt8MSB1,ASIOSTDSDInt8NER8,
        ASIOSTInt32LSB16,ASIOSTInt32MSB16,ASIOSTInt32LSB18,ASIOSTInt32MSB18,ASIOSTInt32LSB20,ASIOSTInt32MSB20};
    for (const auto type : unsupported) {
        fixture.nativeFormat=type; EXPECT_FALSE(channelLayout(type));
        EXPECT_EQ(backend.open(formatConfig(Format::Float32)).error(),ErrorType::FormatNotSupported);
        EXPECT_EQ(fixture.live,0u);
        const auto endpoints=backend.getEndPoints(); ASSERT_EQ(endpoints.size(),1u);
        ASSERT_TRUE(endpoints[0].input); ASSERT_TRUE(endpoints[0].output);
        EXPECT_TRUE(endpoints[0].input->formats.empty()); EXPECT_TRUE(endpoints[0].output->formats.empty());
    }
}
TEST(AsioChannelLayout, AFormatMismatchRollsBackInsteadOfCoercingTheRequest) {
    DriverFixture fixture; ASIO backend; fixture.nativeFormat=ASIOSTInt32LSB;
    EXPECT_EQ(backend.open(formatConfig(Format::Float32)).error(),ErrorType::FormatNotSupported);
    EXPECT_EQ(fixture.live,0u);
    ASSERT_TRUE(backend.open(formatConfig(Format::Int32))); ASSERT_TRUE(backend.close());
}
TEST(AsioChannelLayout, ReturnedChannelIndexAndDirectionMustMatchTheQuery) {
    DriverFixture fixture; ASIO backend;
    fixture.wrongChannel=true;
    EXPECT_EQ(backend.open(formatConfig(Format::Float32)).error(),ErrorType::ChannelsNotSupported);
    fixture.wrongChannel=false; fixture.wrongDirection=true;
    EXPECT_EQ(backend.open(formatConfig(Format::Float32)).error(),ErrorType::ChannelsNotSupported);
    fixture.wrongDirection=false;
    ASSERT_TRUE(backend.open(formatConfig(Format::Float32))); ASSERT_TRUE(backend.close());
}
TEST(AsioChannelLayout, InfoErrorsReleasePartialLayoutsAndPermitReopening) {
    DriverFixture fixture; ASIO backend;
    const std::pair<ASIOError,ErrorType> errors[]{{ASE_NotPresent,ErrorType::EndpointUnavailable},
        {ASE_NoMemory,ErrorType::ConfigurationFailed},{ASE_InvalidParameter,ErrorType::ConfigurationFailed},
        {ASE_SUCCESS,ErrorType::ConfigurationFailed}};
    for (const auto& [error,expected] : errors) {
        fixture.infoResult=error;
        EXPECT_EQ(backend.open(formatConfig(Format::Float32)).error(),expected); EXPECT_EQ(fixture.live,0u);
    }
    fixture.infoResult=ASE_OK;
    ASSERT_TRUE(backend.open(formatConfig(Format::Float32))); ASSERT_TRUE(backend.close());
}
TEST(AsioChannelLayout, InputAndOutputDiscoveryKeepTheirSeparateNativeFormats) {
    DriverFixture fixture; ASIO backend;
    fixture.inputFormats={ASIOSTInt16LSB,ASIOSTInt16LSB};
    fixture.outputFormats={ASIOSTFloat64MSB,ASIOSTFloat64MSB};
    const auto endpoints=backend.getEndPoints(); ASSERT_EQ(endpoints.size(),1u);
    ASSERT_TRUE(endpoints[0].input); ASSERT_TRUE(endpoints[0].output);
    EXPECT_EQ(endpoints[0].input->formats,(std::vector<Format>{Format::Int16}));
    EXPECT_EQ(endpoints[0].output->formats,(std::vector<Format>{Format::Float64}));
    ASSERT_TRUE(backend.open(formatConfig(Format::Int16,Direction::Input,2,0))); ASSERT_TRUE(backend.close());
    ASSERT_TRUE(backend.open(formatConfig(Format::Float64))); ASSERT_TRUE(backend.close());
    EXPECT_EQ(backend.open(formatConfig(Format::Int16,Direction::Duplex,2,2)).error(),ErrorType::FormatNotSupported);
}
TEST(AsioChannelLayout, QueriesFollowInputThenOutputOrderAndLeaveConfigurationUntouched) {
    DriverFixture fixture; ASIO backend; const auto cfg=formatConfig(Format::Float32,Direction::Duplex,2,2);
    ASSERT_TRUE(backend.open(cfg));
    EXPECT_EQ(fixture.infoRequests,(std::vector<std::pair<bool,long>>{{true,0},{true,1},{false,0},{false,1}}));
    EXPECT_EQ(cfg.id,driverId); EXPECT_EQ(cfg.inputChannels,2u); EXPECT_EQ(cfg.outputChannels,2u);
    EXPECT_EQ(cfg.format,Format::Float32); EXPECT_EQ(cfg.bufferSize,256u);
    const auto layouts=activeDriver->outputLayouts(); const auto queries=fixture.infoRequests.size();
    const auto endpoints=backend.getEndPoints(); ASSERT_EQ(endpoints.size(),1u);
    EXPECT_EQ(fixture.infoRequests.size(),queries); EXPECT_EQ(activeDriver->outputLayouts(),layouts);
    ASSERT_TRUE(backend.close());
}
TEST(AsioChannelLayout, OnlyRequestedChannelsAreValidatedButEveryRequestedChannelMustMatch) {
    DriverFixture fixture; ASIO backend; fixture.outputs=3;
    fixture.outputFormats={ASIOSTFloat32LSB,ASIOSTFloat32MSB,ASIOSTInt32LSB};
    ASSERT_TRUE(backend.open(formatConfig(Format::Float32))); ASSERT_TRUE(backend.close());
    EXPECT_EQ(backend.open(formatConfig(Format::Float32,Direction::Output,0,3)).error(),ErrorType::FormatNotSupported);
    EXPECT_EQ(fixture.live,0u);
}
}
