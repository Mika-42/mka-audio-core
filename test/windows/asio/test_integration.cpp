#include <gtest/gtest.h>
#include <cstring>
#include <expected>
#include <limits>
#include <tuple>
#include "fake_driver.hpp"
import mka.audio.backend.asio;
import mka.audio.convert;
#include "conversion_helpers.hpp"

namespace {
using namespace asio_test;
using namespace mka::audio::core;
using Combination=std::tuple<Direction,std::uint32_t,Format>;
class AsioIntegration : public ::testing::TestWithParam<Combination> {};
TEST_P(AsioIntegration, PublicPipelinePreservesExactFramesChannelsFormatAndBothHalves) {
    const auto [direction,frames,format]=GetParam();
    ASIOSampleType type=ASIOSTFloat32LSB;std::uint64_t quarter=0,eighth=0;
    switch (format) {
        case Format::Int16:type=ASIOSTInt16LSB;quarter=0x2000;eighth=0x1000;break;
        case Format::Int24:type=ASIOSTInt24MSB;quarter=0x200000;eighth=0x100000;break;
        case Format::Int32:type=ASIOSTInt32MSB;quarter=0x20000000;eighth=0x10000000;break;
        case Format::Float32:type=ASIOSTFloat32LSB;quarter=0x3e800000;eighth=0x3e000000;break;
        case Format::Float64:type=ASIOSTFloat64MSB;quarter=0x3fd0000000000000ULL;eighth=0x3fc0000000000000ULL;break;
    }
    DriverFixture fixture;fixture.nativeFormat=type;
    const std::uint32_t inputs=direction==Direction::Output?0:2,outputs=direction==Direction::Input?0:2;
    struct Probe {unsigned calls=0;std::uint32_t inputs,outputs,frames;bool valid=true;} probe{0,inputs,outputs,frames};
    ASIO backend;
    ASSERT_TRUE(backend.setProcessFunction(+[](void* user,const AudioProcessContext& context) noexcept {
        auto& p=*static_cast<Probe*>(user);const float value=p.calls?0.125f:0.25f;
        p.valid &= context.input.count==p.inputs && context.output.count==p.outputs && context.frames==p.frames;
        for (unsigned channel=0;channel<context.input.count;++channel) for (unsigned frame=0;frame<context.frames;++frame)
            p.valid &= context.input.channels[channel][frame]==value;
        for (unsigned channel=0;channel<context.output.count;++channel) for (unsigned frame=0;frame<context.frames;++frame)
            context.output.channels[channel][frame]=value;
        ++p.calls;
    },&probe));
    ASSERT_TRUE(backend.open({driverId,direction,inputs,outputs,48000,format,frames}));
    const auto layout=*channelLayout(type);
    for (unsigned channel=0;channel<inputs;++channel) for (unsigned half=0;half<2;++half) {
        const auto bytes=encodedSamples(std::vector<std::uint64_t>(frames,half?eighth:quarter),layout);
        std::memcpy(fixture.createdBuffers[channel].buffers[half],bytes.data(),bytes.size());
    }
    ASSERT_TRUE(backend.start());fixture.callbacks.bufferSwitch(0,ASIOTrue);fixture.callbacks.bufferSwitch(1,ASIOFalse);
    EXPECT_EQ(probe.calls,2u);EXPECT_TRUE(probe.valid);EXPECT_EQ(activeDriver->audioBuffers().frames(),frames);
    EXPECT_EQ(activeDriver->inputLayouts().size(),inputs);EXPECT_EQ(activeDriver->outputLayouts().size(),outputs);
    for (unsigned channel=0;channel<outputs;++channel) for (unsigned half=0;half<2;++half) {
        const auto bytes=encodedSamples(std::vector<std::uint64_t>(frames,half?eighth:quarter),layout);
        EXPECT_EQ(std::memcmp(fixture.createdBuffers[inputs+channel].buffers[half],bytes.data(),bytes.size()),0);
    }
    EXPECT_FALSE(backend.status().failed);EXPECT_EQ(backend.status().xruns,0u);
    ASSERT_TRUE(backend.stop());ASSERT_TRUE(backend.close());
}
INSTANTIATE_TEST_SUITE_P(ModesBuffersFormats,AsioIntegration,
    ::testing::Combine(::testing::Values(Direction::Input,Direction::Output,Direction::Duplex),
        ::testing::Values(64u,128u,256u),
        ::testing::Values(Format::Int16,Format::Int24,Format::Int32,Format::Float32,Format::Float64)));
TEST(AsioIntegrationContract, AnEmptyDriverListNeverInventsAnEndpointOrOpenStream) {
    DriverFixture fixture;ASIO backend;fixture.registrations.clear();EXPECT_TRUE(backend.getEndPoints().empty());
    EXPECT_EQ(backend.open({driverId,Direction::Output,0,2,48000,Format::Float32,256}).error(),ErrorType::EndpointUnavailable);
    EXPECT_EQ(fixture.creates,0u);EXPECT_EQ(fixture.comAttempts,0u);
}
TEST(AsioIntegrationContract, NativeLatencyDiagnosticsValidateSignedCountsOnTheOwner) {
    DriverFixture fixture;ASIO backend;
    ASSERT_TRUE(backend.open({driverId,Direction::Output,0,2,48000,Format::Float32,256}));ASSERT_TRUE(backend.start());
    fixture.inputLatency=0;fixture.outputLatency=std::numeric_limits<long>::max();
    const auto latency=activeDriver->latencies();ASSERT_TRUE(latency);
    EXPECT_EQ(latency->input,0u);EXPECT_EQ(latency->output,static_cast<std::uint32_t>(std::numeric_limits<long>::max()));
    EXPECT_EQ(fixture.latencyThread,fixture.initThread);
    fixture.inputLatency=-1;EXPECT_FALSE(activeDriver->latencies());fixture.inputLatency=64;
    fixture.outputLatency=std::numeric_limits<long>::min();EXPECT_FALSE(activeDriver->latencies());fixture.outputLatency=64;
    fixture.latencyResult=ASE_NotPresent;EXPECT_FALSE(activeDriver->latencies());
    fixture.latencyResult=ASE_SUCCESS;EXPECT_FALSE(activeDriver->latencies());EXPECT_FALSE(backend.status().failed);
    ASSERT_TRUE(backend.stop());ASSERT_TRUE(backend.close());
}
}
