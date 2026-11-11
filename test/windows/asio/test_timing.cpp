#include <gtest/gtest.h>
#include <array>
#include <atomic>
#include <cstring>
#include <expected>
#include <limits>
#include <thread>
#include "fake_driver.hpp"
import mka.audio.backend.asio;

namespace {
using namespace asio_test;
using namespace mka::audio::core;
template<class Words> Words words(std::uint64_t value) {
    return {static_cast<decltype(Words::hi)>(value>>32),static_cast<decltype(Words::lo)>(value)};
}
EndpointConfig timingConfig() {return {driverId,Direction::Output,0,2,48000,Format::Float32,256};}
struct TimingCalls {std::atomic<unsigned> calls{0},failed{0};};
ProcessingCallbacks countTiming(TimingCalls& calls) {
    return {.user=&calls,
        .process=+[](void* user,const float* const*,std::uint32_t,float* const*,std::uint32_t,std::uint32_t) noexcept {
            ++static_cast<TimingCalls*>(user)->calls;
        },.failed=+[](void* user) noexcept {++static_cast<TimingCalls*>(user)->failed;}};
}
TEST(AsioTiming, AdvertisesTimeInfoWithoutClaimingTimecodeOrUnknownSelectors) {
    DriverFixture fixture; ASIO backend; ASSERT_TRUE(backend.open(timingConfig()));
    const auto callbacks=fixture.callbacks;
    EXPECT_EQ(callbacks.asioMessage(kAsioEngineVersion,0,nullptr,nullptr),2);
    EXPECT_EQ(callbacks.asioMessage(kAsioSupportsTimeInfo,0,nullptr,nullptr),1);
    EXPECT_EQ(callbacks.asioMessage(kAsioSelectorSupported,kAsioSupportsTimeInfo,nullptr,nullptr),1);
    EXPECT_EQ(callbacks.asioMessage(kAsioSupportsTimeCode,0,nullptr,nullptr),0);
    EXPECT_EQ(callbacks.asioMessage(kAsioSelectorSupported,kAsioSupportsTimeCode,nullptr,nullptr),0);
    EXPECT_EQ(callbacks.asioMessage(-123,0,nullptr,nullptr),0);
    ASSERT_TRUE(backend.close());
}
TEST(AsioTiming, CopiesWidePositionsAndReturnsTheUnmodifiedBorrowedSdkObject) {
    DriverFixture fixture; TimingCalls calls; ASIO backend; ASSERT_TRUE(backend.open(timingConfig()));
    activeDriver->configureProcessing(countTiming(calls)); ASSERT_TRUE(activeDriver->enableProcessing());
    ASIOTime time{}; time.timeInfo.flags=kSamplePositionValid|kSystemTimeValid;
    time.timeInfo.samplePosition=words<ASIOSamples>(0x123456789abcdef0ULL);
    time.timeInfo.systemTime=words<ASIOTimeStamp>(0xfedcba9876543210ULL);
    std::array<std::byte,sizeof(time)> before{}; std::memcpy(before.data(),&time,sizeof(time));
    EXPECT_EQ(fixture.callbacks.bufferSwitchTimeInfo(&time,1,ASIOFalse),&time);
    const auto snapshot=activeDriver->timing();
    EXPECT_EQ(snapshot.samplePosition,0x123456789abcdef0ULL); EXPECT_EQ(snapshot.systemNanoseconds,0xfedcba9876543210ULL);
    EXPECT_EQ(snapshot.valid,kSamplePositionValid|kSystemTimeValid); EXPECT_EQ(calls.calls,1u);
    EXPECT_EQ(std::memcmp(before.data(),&time,sizeof(time)),0); EXPECT_EQ(fixture.positionCalls,0u);
    time={}; EXPECT_EQ(activeDriver->timing().samplePosition,snapshot.samplePosition);
    ASSERT_TRUE(backend.close());
}
TEST(AsioTiming, MissingAndPartialValidityFlagsDoNotTurnInvalidFieldsIntoMeasurements) {
    DriverFixture fixture; TimingCalls calls; ASIO backend; ASSERT_TRUE(backend.open(timingConfig()));
    activeDriver->configureProcessing(countTiming(calls)); ASSERT_TRUE(activeDriver->enableProcessing());
    ASIOTime time{}; time.timeInfo.samplePosition=words<ASIOSamples>(123);
    time.timeInfo.systemTime=words<ASIOTimeStamp>(456);
    time.timeInfo.sampleRate=std::numeric_limits<double>::quiet_NaN(); time.timeInfo.speed=std::numeric_limits<double>::infinity();
    for (const unsigned long flags : {0UL,static_cast<unsigned long>(kSystemTimeValid),static_cast<unsigned long>(kSamplePositionValid)}) {
        time.timeInfo.flags=flags; fixture.callbacks.bufferSwitchTimeInfo(&time,0,ASIOTrue);
        const auto snapshot=activeDriver->timing(); EXPECT_EQ(snapshot.valid,flags);
        EXPECT_EQ(snapshot.samplePosition,(flags&kSamplePositionValid)?123u:0u);
        EXPECT_EQ(snapshot.systemNanoseconds,(flags&kSystemTimeValid)?456u:0u);
    }
    EXPECT_EQ(calls.calls,3u); EXPECT_EQ(calls.failed,0u); EXPECT_EQ(fixture.positionCalls,0u);
    ASSERT_TRUE(backend.close());
}
TEST(AsioTiming, LegacyAndNullTimeInfoCallbacksUseTheSdkPositionGetterOnTheAudioCaller) {
    DriverFixture fixture; TimingCalls calls; ASIO backend; fixture.positionResult=ASE_OK;
    fixture.reportedPosition=words<ASIOSamples>(0x100000002ULL); fixture.reportedTimestamp=words<ASIOTimeStamp>(0x300000004ULL);
    ASSERT_TRUE(backend.open(timingConfig())); activeDriver->configureProcessing(countTiming(calls));
    ASSERT_TRUE(activeDriver->enableProcessing());
    fixture.callbacks.bufferSwitch(0,ASIOTrue);
    EXPECT_EQ(fixture.callbacks.bufferSwitchTimeInfo(nullptr,1,ASIOFalse),nullptr);
    const auto snapshot=activeDriver->timing(); EXPECT_EQ(snapshot.samplePosition,0x100000002ULL);
    EXPECT_EQ(snapshot.systemNanoseconds,0x300000004ULL); EXPECT_EQ(snapshot.valid,kSamplePositionValid|kSystemTimeValid);
    EXPECT_EQ(fixture.positionCalls,2u); EXPECT_EQ(fixture.positionThread,GetCurrentThreadId());
    EXPECT_NE(fixture.positionThread,fixture.initThread); EXPECT_EQ(calls.calls,2u);
    ASSERT_TRUE(backend.close());
}
TEST(AsioTiming, GetterFailuresClearDiagnosticValidityAndKeepAudioProcessingIndependent) {
    DriverFixture fixture; TimingCalls calls; ASIO backend; ASSERT_TRUE(backend.open(timingConfig()));
    activeDriver->configureProcessing(countTiming(calls)); ASSERT_TRUE(activeDriver->enableProcessing());
    const ASIOError errors[]{ASE_SPNotAdvancing,ASE_NotPresent,ASE_NoClock,ASE_SUCCESS};
    for (const auto error : errors) {
        fixture.positionResult=error; fixture.callbacks.bufferSwitch(0,ASIOTrue);
        const auto snapshot=activeDriver->timing(); EXPECT_EQ(snapshot.valid,0u);
        EXPECT_EQ(snapshot.samplePosition,0u); EXPECT_EQ(snapshot.systemNanoseconds,0u);
    }
    EXPECT_EQ(calls.calls,4u); EXPECT_EQ(calls.failed,0u); ASSERT_TRUE(backend.close());
}
TEST(AsioTiming, ZeroAndMaximumUnsignedWordsRemainExactWithoutOverflow) {
    DriverFixture fixture; TimingCalls calls; ASIO backend; ASSERT_TRUE(backend.open(timingConfig()));
    activeDriver->configureProcessing(countTiming(calls)); ASSERT_TRUE(activeDriver->enableProcessing());
    for (const auto value : {std::uint64_t{0},std::numeric_limits<std::uint64_t>::max()}) {
        ASIOTime time{}; time.timeInfo.flags=kSamplePositionValid|kSystemTimeValid;
        time.timeInfo.samplePosition=words<ASIOSamples>(value); time.timeInfo.systemTime=words<ASIOTimeStamp>(value);
        fixture.callbacks.bufferSwitchTimeInfo(&time,0,ASIOTrue);
        const auto snapshot=activeDriver->timing(); EXPECT_EQ(snapshot.samplePosition,value); EXPECT_EQ(snapshot.systemNanoseconds,value);
    }
    ASSERT_TRUE(backend.close());
}
TEST(AsioTiming, DiagnosticsSurviveTheSdkObjectScopeAndCallbacksAfterCloseIgnoreIt) {
    DriverFixture fixture; TimingCalls calls; ASIO backend; ASSERT_TRUE(backend.open(timingConfig()));
    activeDriver->configureProcessing(countTiming(calls)); ASSERT_TRUE(activeDriver->enableProcessing());
    {
        ASIOTime time{}; time.timeInfo.flags=kSamplePositionValid|kSystemTimeValid;
        time.timeInfo.samplePosition=words<ASIOSamples>(777); time.timeInfo.systemTime=words<ASIOTimeStamp>(888);
        fixture.callbacks.bufferSwitchTimeInfo(&time,0,ASIOTrue);
    }
    const auto snapshot=activeDriver->timing(); EXPECT_EQ(snapshot.samplePosition,777u); EXPECT_EQ(snapshot.systemNanoseconds,888u);
    const auto saved=fixture.callbacks; ASSERT_TRUE(backend.close()); ASIOTime after{};
    EXPECT_EQ(saved.bufferSwitchTimeInfo(&after,0,ASIOTrue),&after); EXPECT_EQ(calls.calls,1u);
}
TEST(AsioTiming, InvalidBufferIndicesAreRejectedBeforeInspectingTimingData) {
    DriverFixture fixture; TimingCalls calls; ASIO backend; ASSERT_TRUE(backend.open(timingConfig()));
    activeDriver->configureProcessing(countTiming(calls)); ASSERT_TRUE(activeDriver->enableProcessing());
    EXPECT_EQ(fixture.callbacks.bufferSwitchTimeInfo(nullptr,-1,ASIOTrue),nullptr);
    EXPECT_EQ(calls.calls,0u); EXPECT_EQ(calls.failed,1u); EXPECT_EQ(fixture.positionCalls,0u);
    EXPECT_EQ(activeDriver->timing().valid,0u); ASSERT_TRUE(backend.close());
}
TEST(AsioTiming, ConcurrentDiagnosticReadsObserveCoherentPositionTimestampPairs) {
    DriverFixture fixture; TimingCalls calls; ASIO backend; ASSERT_TRUE(backend.open(timingConfig()));
    activeDriver->configureProcessing(countTiming(calls)); ASSERT_TRUE(activeDriver->enableProcessing());
    std::atomic<bool> done{false};
    std::thread audio([&] {
        ASIOTime time{}; time.timeInfo.flags=kSamplePositionValid|kSystemTimeValid;
        for (std::uint64_t block=1;block<=10000;++block) {
            time.timeInfo.samplePosition=words<ASIOSamples>(block); time.timeInfo.systemTime=words<ASIOTimeStamp>(block*10);
            fixture.callbacks.bufferSwitchTimeInfo(&time,block%2,ASIOTrue);
        }
        done=true;
    });
    bool coherent=true;
    do {
        const auto snapshot=activeDriver->timing();
        if (snapshot.valid) coherent &= snapshot.systemNanoseconds==snapshot.samplePosition*10;
    } while (!done.load());
    audio.join(); EXPECT_TRUE(coherent); EXPECT_EQ(calls.calls,10000u); ASSERT_TRUE(backend.close());
}
}
