#include <gtest/gtest.h>
#include <expected>
#include <limits>
#include "fake_driver.hpp"
import mka.audio.backend.asio;

namespace {
using namespace asio_test;
using namespace mka::audio::core;
EndpointConfig rateConfig(std::uint32_t rate) { return {driverId,Direction::Output,0,2,rate,Format::Float32,256}; }
TEST(AsioRates, PortableConversionRejectsNonFiniteFractionalAndOutOfRangeValues) {
    const auto infinity=std::numeric_limits<double>::infinity();
    const auto max=std::numeric_limits<std::uint32_t>::max();
    for (const double value : {0.0,-1.0,44100.5,infinity,-infinity,std::numeric_limits<double>::quiet_NaN(),double(max)+1})
        EXPECT_FALSE(portableRate(value));
    for (const auto value : {1u,44100u,max}) { ASSERT_TRUE(portableRate(value)); EXPECT_EQ(*portableRate(value),value); }
}
TEST(AsioRates, AnExactCurrentClockIsRecheckedWithoutCallingTheSetter) {
    DriverFixture fixture; ASIO backend;
    ASSERT_TRUE(backend.open(rateConfig(48000)));
    EXPECT_EQ(fixture.getCalls,2u); EXPECT_TRUE(fixture.setRequests.empty());
    EXPECT_EQ(fixture.rateThread,fixture.initThread);
    const auto endpoints=backend.getEndPoints(); ASSERT_EQ(endpoints.size(),1u); ASSERT_TRUE(endpoints[0].output);
    EXPECT_EQ(endpoints[0].output->sampleRates,(std::vector<std::uint32_t>{48000}));
    EXPECT_EQ(fixture.getCalls,2u); ASSERT_TRUE(backend.close()); EXPECT_TRUE(fixture.setRequests.empty());
}
TEST(AsioRates, ExplicitClockSelectionPrecedesAllOtherCapabilitiesAndRestoresOnClose) {
    DriverFixture fixture; ASIO backend; fixture.currentRate=44100;
    ASSERT_TRUE(backend.open(rateConfig(48000)));
    EXPECT_DOUBLE_EQ(fixture.currentRate,48000); EXPECT_EQ(fixture.setRequests,(std::vector<double>{48000}));
    EXPECT_DOUBLE_EQ(fixture.channelsAtRate,48000); EXPECT_DOUBLE_EQ(fixture.buffersAtRate,48000);
    EXPECT_DOUBLE_EQ(fixture.infoAtRate,48000);
    ASSERT_TRUE(backend.close()); EXPECT_DOUBLE_EQ(fixture.currentRate,44100);
    EXPECT_EQ(fixture.setRequests,(std::vector<double>{48000,44100}));
}
TEST(AsioRates, UnsupportedRatesNeverSelectAnApproximation) {
    DriverFixture fixture; ASIO backend; fixture.supportedRates={48000};
    EXPECT_EQ(backend.open(rateConfig(44100)).error(),ErrorType::SampleRateNotSupported);
    EXPECT_TRUE(fixture.setRequests.empty()); EXPECT_EQ(fixture.getCalls,0u); EXPECT_EQ(fixture.live,0u);
    ASSERT_TRUE(backend.open(rateConfig(48000))); ASSERT_TRUE(backend.close());
}
TEST(AsioRates, AnExternalClockRefusalRollsBackAndAllowsASecondAttempt) {
    DriverFixture fixture; ASIO backend; fixture.currentRate=44100; fixture.setResult=ASE_InvalidMode;
    EXPECT_EQ(backend.open(rateConfig(48000)).error(),ErrorType::SampleRateNotSupported);
    EXPECT_EQ(fixture.live,0u); EXPECT_DOUBLE_EQ(fixture.currentRate,44100);
    fixture.setResult=ASE_OK;
    ASSERT_TRUE(backend.open(rateConfig(48000))); ASSERT_TRUE(backend.close());
}
TEST(AsioRates, ASetterThatIgnoresTheRequestFailsVerificationAndRestoresTheInitialClock) {
    DriverFixture fixture; ASIO backend; fixture.currentRate=44100; fixture.ignoreRateSet=true;
    EXPECT_EQ(backend.open(rateConfig(48000)).error(),ErrorType::SampleRateNotSupported);
    EXPECT_EQ(fixture.setRequests,(std::vector<double>{48000,44100})); EXPECT_EQ(fixture.live,0u);
    fixture.ignoreRateSet=false;
    ASSERT_TRUE(backend.open(rateConfig(48000))); ASSERT_TRUE(backend.close());
}
TEST(AsioRates, InvalidInitialMeasurementsAreNotNarrowedOrUsedAsARestoreTarget) {
    DriverFixture fixture; ASIO backend;
    for (const double value : {0.0,-1.0,44100.5,std::numeric_limits<double>::infinity(),
         std::numeric_limits<double>::quiet_NaN(),double(std::numeric_limits<std::uint32_t>::max())+1}) {
        fixture.currentRate=value;
        EXPECT_EQ(backend.open(rateConfig(48000)).error(),ErrorType::SampleRateNotSupported);
        EXPECT_EQ(fixture.live,0u); EXPECT_TRUE(fixture.setRequests.empty());
    }
}
TEST(AsioRates, AFailedVerificationAndAClockDriftAreBothRejected) {
    DriverFixture fixture; ASIO backend; fixture.currentRate=44100;
    fixture.getResult=ASE_NoClock; fixture.failGetNumber=2;
    EXPECT_EQ(backend.open(rateConfig(48000)).error(),ErrorType::SampleRateNotSupported);
    EXPECT_DOUBLE_EQ(fixture.currentRate,44100); EXPECT_EQ(fixture.live,0u);
    fixture.getResult=ASE_OK; fixture.failGetNumber=0; fixture.getCalls=0;
    fixture.currentRate=48000; fixture.setRequests.clear(); fixture.reportedRates={48000,44100};
    EXPECT_EQ(backend.open(rateConfig(48000)).error(),ErrorType::SampleRateNotSupported);
    EXPECT_TRUE(fixture.setRequests.empty()); EXPECT_EQ(fixture.getCalls,2u);
}
TEST(AsioRates, NativeErrorsAreMappedWithoutConfusingFutureSuccessWithTransportSuccess) {
    DriverFixture fixture; ASIO backend;
    const std::pair<ASIOError,ErrorType> errors[]{{ASE_NotPresent,ErrorType::EndpointUnavailable},
        {ASE_NoMemory,ErrorType::ConfigurationFailed},{ASE_NoClock,ErrorType::SampleRateNotSupported},
        {ASE_SUCCESS,ErrorType::ConfigurationFailed}};
    for (const auto& [error,expected] : errors) {
        fixture.canResult=error;
        EXPECT_EQ(backend.open(rateConfig(48000)).error(),expected); EXPECT_EQ(fixture.live,0u);
    }
    fixture.canResult=ASE_OK; fixture.getResult=ASE_NotPresent;
    EXPECT_EQ(backend.open(rateConfig(48000)).error(),ErrorType::EndpointUnavailable);
    fixture.getResult=ASE_OK; fixture.currentRate=44100; fixture.setResult=ASE_NoClock; fixture.mutateOnRateError=true;
    EXPECT_EQ(backend.open(rateConfig(48000)).error(),ErrorType::SampleRateNotSupported);
    EXPECT_DOUBLE_EQ(fixture.currentRate,44100); EXPECT_EQ(fixture.live,0u);
}
TEST(AsioRates, DiscoveryProbesSupportedAndCurrentRatesWithoutChangingTheClock) {
    DriverFixture fixture; ASIO backend; fixture.currentRate=32000; fixture.supportedRates={32000,48000};
    const auto endpoints=backend.getEndPoints(); ASSERT_EQ(endpoints.size(),1u);
    ASSERT_TRUE(endpoints[0].input); ASSERT_TRUE(endpoints[0].output);
    EXPECT_EQ(endpoints[0].output->sampleRates,(std::vector<std::uint32_t>{32000,48000}));
    EXPECT_EQ(endpoints[0].input->sampleRates,endpoints[0].output->sampleRates);
    EXPECT_TRUE(fixture.setRequests.empty()); EXPECT_DOUBLE_EQ(fixture.currentRate,32000); EXPECT_EQ(fixture.live,0u);
}
TEST(AsioRates, ChannelBufferAndFormatFailuresAfterClockSelectionRestoreItBeforeRelease) {
    DriverFixture fixture; ASIO backend; fixture.currentRate=44100;
    fixture.outputs=0;
    EXPECT_EQ(backend.open(rateConfig(48000)).error(),ErrorType::ChannelsNotSupported);
    EXPECT_DOUBLE_EQ(fixture.currentRate,44100); EXPECT_EQ(fixture.live,0u);
    fixture.outputs=2; fixture.maximum=128; fixture.preferred=128;
    EXPECT_EQ(backend.open(rateConfig(48000)).error(),ErrorType::BufferSizeNotSupported);
    EXPECT_DOUBLE_EQ(fixture.currentRate,44100); EXPECT_EQ(fixture.live,0u);
    fixture.maximum=1024; fixture.preferred=256; fixture.nativeFormat=ASIOSTInt32LSB;
    EXPECT_EQ(backend.open(rateConfig(48000)).error(),ErrorType::FormatNotSupported);
    EXPECT_DOUBLE_EQ(fixture.currentRate,44100); EXPECT_EQ(fixture.live,0u);
}
TEST(AsioRates, RestoreFailureStillReleasesOwnershipAndTheNextSessionRefreshesItsClock) {
    DriverFixture fixture; ASIO backend; fixture.currentRate=44100;
    ASSERT_TRUE(backend.open(rateConfig(48000))); fixture.setResult=ASE_InvalidMode;
    EXPECT_EQ(backend.close().error(),ErrorType::ConfigurationFailed); EXPECT_EQ(fixture.live,0u);
    ASSERT_TRUE(backend.close()); fixture.setResult=ASE_OK; fixture.currentRate=96000;
    ASSERT_TRUE(backend.open(rateConfig(48000))); ASSERT_TRUE(backend.close());
    EXPECT_DOUBLE_EQ(fixture.currentRate,96000); EXPECT_EQ(fixture.setRequests.back(),96000);
}
}
