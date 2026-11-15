#include <algorithm>
#include <array>
#include <cstdint>
#include <expected>
#include <string_view>
#include <vector>
#include <gtest/gtest.h>
import mka.audio.diagnostics;
#include "callback_transport.hpp"
using namespace mka::audio::core;
TEST(ReleaseDiagnostics, NamesAreStableNonAllocatingAndHandleUnknownEnumValues) {
    static_assert(noexcept(errorName(ErrorType::InvalidState)) && noexcept(realtimeName(RealtimeState::Unknown)));
    const std::array errors{ErrorType::InvalidState,ErrorType::EndpointUnavailable,ErrorType::FormatNotSupported,
        ErrorType::ChannelsNotSupported,ErrorType::SampleRateNotSupported,ErrorType::BufferSizeNotSupported,ErrorType::ConfigurationFailed};
    const std::array<std::string_view,7> names{"InvalidState","EndpointUnavailable","FormatNotSupported","ChannelsNotSupported","SampleRateNotSupported","BufferSizeNotSupported","ConfigurationFailed"};
    for(std::size_t i=0;i<errors.size();++i)EXPECT_EQ(errorName(errors[i]),names[i]);
    EXPECT_EQ(errorName(static_cast<ErrorType>(99)),"Unknown");
    EXPECT_EQ(realtimeName(RealtimeState::Unknown),"Unknown");EXPECT_EQ(realtimeName(RealtimeState::No),"No");EXPECT_EQ(realtimeName(RealtimeState::Yes),"Yes");
    EXPECT_EQ(realtimeName(static_cast<RealtimeState>(255)),"Unknown");
    EXPECT_EQ(eventName(EventType::XRun),"XRun");EXPECT_EQ(eventName(EventType::Failed),"Failed");EXPECT_EQ(eventName(static_cast<EventType>(255)),"Unknown");
}
TEST(ReleaseDiagnostics, ExactConfigurationErrorsKeepTheClosedStateReusable) {
    dsp_test::Transport backend;
    const EndpointConfig valid{"A",Direction::Duplex,1,2,48000,Format::Float32,64};
    auto check=[&](EndpointConfig config,ErrorType expected){
        const auto result=backend.open(config);ASSERT_FALSE(result);EXPECT_EQ(result.error(),expected);EXPECT_NE(errorName(result.error()),"Unknown");
        EXPECT_EQ(backend.start().error(),ErrorType::InvalidState);
    };
    auto config=valid;config.inputChannels=0;check(config,ErrorType::ChannelsNotSupported);
    config=valid;config.sampleRate=0;check(config,ErrorType::SampleRateNotSupported);
    config=valid;config.bufferSize=0;check(config,ErrorType::BufferSizeNotSupported);
    config=valid;config.format=static_cast<Format>(99);check(config,ErrorType::FormatNotSupported);
    config=valid;config.direction=static_cast<Direction>(99);check(config,ErrorType::ConfigurationFailed);
    config=valid;config.id="absent";check(config,ErrorType::EndpointUnavailable);
    ASSERT_TRUE(backend.open(valid));ASSERT_TRUE(backend.start());
    EXPECT_EQ(backend.open(valid).error(),ErrorType::InvalidState);
    EXPECT_EQ(realtimeName(backend.status().realtime),"Unknown");EXPECT_EQ(backend.status().xruns,0u);EXPECT_FALSE(backend.status().failed);
    ASSERT_TRUE(backend.stop());ASSERT_TRUE(backend.close());
}
TEST(ReleaseDiagnostics, FailureSnapshotsAndXrunsResetOnlyForTheNextSuccessfulRun) {
    dsp_test::Transport backend;
    const EndpointConfig config{"A",Direction::Output,0,1,48000,Format::Float32,64};
    ASSERT_TRUE(backend.open(config));ASSERT_TRUE(backend.start());backend.reportFault();
    EXPECT_EQ(backend.status().xruns,1u);EXPECT_TRUE(backend.status().failed);
    ASSERT_TRUE(backend.stop());ASSERT_TRUE(backend.close());EXPECT_TRUE(backend.status().failed);
    ASSERT_TRUE(backend.open(config));ASSERT_TRUE(backend.start());
    EXPECT_EQ(backend.status().xruns,0u);EXPECT_FALSE(backend.status().failed);
    ASSERT_TRUE(backend.stop());ASSERT_TRUE(backend.close());
}
