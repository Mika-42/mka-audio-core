#include <gtest/gtest.h>
#include <atomic>
#include <expected>
#include <limits>
#include <thread>
#include "fake_driver.hpp"
import mka.audio.backend.asio;

namespace {
using namespace asio_test;
using namespace mka::audio::core;
EndpointConfig invalidationConfig() {return {driverId,Direction::Output,0,2,48000,Format::Float32,256};}
void countBlock(void* user,const AudioProcessContext&) noexcept {++*static_cast<std::atomic<unsigned>*>(user);}
template<class Predicate> bool awaitInvalidation(Predicate predicate) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while (!predicate()) {if (std::chrono::steady_clock::now()>=deadline)return false;std::this_thread::yield();}
    return true;
}
TEST(AsioInvalidation, SelectorQueriesDoNotClaimLiveRenegotiationOrUnknownFeatures) {
    DriverFixture fixture;ASIO backend;ASSERT_TRUE(backend.open(invalidationConfig()));ASSERT_TRUE(backend.start());
    const long supported[]{kAsioEngineVersion,kAsioSupportsTimeInfo,kAsioResetRequest,kAsioResyncRequest,kAsioOverload};
    for (const auto selector : supported) EXPECT_EQ(fixture.callbacks.asioMessage(kAsioSelectorSupported,selector,nullptr,nullptr),1);
    const long unsupported[]{kAsioBufferSizeChange,kAsioLatenciesChanged,kAsioSupportsTimeCode,kAsioSupportsInputMonitor,-1};
    for (const auto selector : unsupported) EXPECT_EQ(fixture.callbacks.asioMessage(kAsioSelectorSupported,selector,nullptr,nullptr),0);
    EXPECT_EQ(fixture.callbacks.asioMessage(-1,0,reinterpret_cast<void*>(1),reinterpret_cast<double*>(1)),0);
    EXPECT_FALSE(backend.status().failed);ASSERT_TRUE(backend.stop());ASSERT_TRUE(backend.close());
}
TEST(AsioInvalidation, ResetDefersControlAndPublishesOneFailedEventOutsideTheAudioCaller) {
    DriverFixture fixture;std::atomic<unsigned> calls{0};
    struct Events {std::atomic<unsigned> failures{0};std::atomic<DWORD> thread{0};} events;
    ASIO backend;ASSERT_TRUE(backend.setProcessFunction(countBlock,&calls));
    ASSERT_TRUE(backend.setEventHandler(+[](void* user,const Event& event) noexcept {
        auto& e=*static_cast<Events*>(user);if (event.type==EventType::Failed) {e.thread=GetCurrentThreadId();++e.failures;}
    },&events));
    ASSERT_TRUE(backend.open(invalidationConfig()));ASSERT_TRUE(backend.start());fixture.callbacks.bufferSwitch(0,ASIOTrue);
    const auto rates=fixture.getCalls,channels=fixture.channelQueries,buffers=fixture.bufferQueries;
    const auto formats=fixture.infoRequests.size();
    for (unsigned count=0;count<32;++count) EXPECT_EQ(fixture.callbacks.asioMessage(kAsioResetRequest,0,nullptr,nullptr),1);
    fixture.callbacks.bufferSwitch(1,ASIOFalse);EXPECT_EQ(calls,1u);EXPECT_TRUE(backend.status().failed);
    EXPECT_EQ(fixture.stops,0u);EXPECT_EQ(fixture.bufferDisposals,0u);EXPECT_EQ(fixture.getCalls,rates);
    EXPECT_EQ(fixture.channelQueries,channels);EXPECT_EQ(fixture.bufferQueries,buffers);EXPECT_EQ(fixture.infoRequests.size(),formats);
    ASSERT_TRUE(awaitInvalidation([&]{return events.failures.load()==1;}));EXPECT_NE(events.thread,GetCurrentThreadId());
    ASSERT_TRUE(backend.stop());EXPECT_EQ(events.failures,1u);ASSERT_TRUE(backend.close());
}
TEST(AsioInvalidation, UnsolicitedBufferAndLatencyChangesInvalidateWithoutClaimingAcceptance) {
    for (const long selector : {kAsioBufferSizeChange,kAsioLatenciesChanged}) {
        DriverFixture fixture;std::atomic<unsigned> calls{0};ASIO backend;
        ASSERT_TRUE(backend.setProcessFunction(countBlock,&calls));ASSERT_TRUE(backend.open(invalidationConfig()));ASSERT_TRUE(backend.start());
        EXPECT_EQ(fixture.callbacks.asioMessage(selector,512,reinterpret_cast<void*>(1),reinterpret_cast<double*>(1)),0);
        EXPECT_TRUE(backend.status().failed);fixture.callbacks.bufferSwitch(0,ASIOTrue);EXPECT_EQ(calls,0u);
        EXPECT_EQ(fixture.bufferCreations,1u);EXPECT_EQ(fixture.stops,0u);
        ASSERT_TRUE(backend.stop());ASSERT_TRUE(backend.close());
    }
}
TEST(AsioInvalidation, OverloadsCountXRunsAndResyncAlsoRequiresAControlThreadRestart) {
    DriverFixture fixture;std::atomic<unsigned> calls{0};ASIO backend;
    ASSERT_TRUE(backend.setProcessFunction(countBlock,&calls));ASSERT_TRUE(backend.open(invalidationConfig()));ASSERT_TRUE(backend.start());
    for (unsigned count=0;count<2;++count) EXPECT_EQ(fixture.callbacks.asioMessage(kAsioOverload,0,nullptr,nullptr),1);
    EXPECT_EQ(backend.status().xruns,2u);EXPECT_FALSE(backend.status().failed);
    fixture.callbacks.bufferSwitch(0,ASIOTrue);EXPECT_EQ(calls,1u);
    EXPECT_EQ(fixture.callbacks.asioMessage(kAsioResyncRequest,0,nullptr,nullptr),1);
    EXPECT_EQ(backend.status().xruns,3u);EXPECT_TRUE(backend.status().failed);
    fixture.callbacks.bufferSwitch(1,ASIOFalse);EXPECT_EQ(calls,1u);EXPECT_EQ(fixture.stops,0u);
    ASSERT_TRUE(backend.stop());ASSERT_TRUE(backend.close());
}
TEST(AsioInvalidation, ChangedUnknownFractionalAndNonFiniteRatesFailWithoutSdkControlCalls) {
    for (const double rate : {44100.0,0.0,48000.5,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()}) {
        DriverFixture fixture;std::atomic<unsigned> calls{0};ASIO backend;
        ASSERT_TRUE(backend.setProcessFunction(countBlock,&calls));ASSERT_TRUE(backend.open(invalidationConfig()));ASSERT_TRUE(backend.start());
        const auto reads=fixture.getCalls;fixture.callbacks.sampleRateDidChange(rate);
        EXPECT_TRUE(backend.status().failed);fixture.callbacks.bufferSwitch(0,ASIOTrue);EXPECT_EQ(calls,0u);
        EXPECT_EQ(fixture.getCalls,reads);EXPECT_TRUE(fixture.setRequests.empty());EXPECT_EQ(fixture.stops,0u);
        ASSERT_TRUE(backend.stop());ASSERT_TRUE(backend.close());
    }
}
TEST(AsioInvalidation, UnchangedRateNotificationsLeaveTheFrozenStreamRunning) {
    DriverFixture fixture;std::atomic<unsigned> calls{0};ASIO backend;
    ASSERT_TRUE(backend.setProcessFunction(countBlock,&calls));ASSERT_TRUE(backend.open(invalidationConfig()));ASSERT_TRUE(backend.start());
    fixture.callbacks.sampleRateDidChange(48000);fixture.callbacks.bufferSwitch(0,ASIOTrue);
    EXPECT_EQ(calls,1u);EXPECT_FALSE(backend.status().failed);ASSERT_TRUE(backend.stop());ASSERT_TRUE(backend.close());
}
TEST(AsioInvalidation, TimeInfoRateAndClockChangeFlagsCannotSilentlyAlterTheTransport) {
    struct Change {unsigned long flags;double rate;};
    const Change changes[]{{kSampleRateValid,44100},{kSampleRateChanged,48000},{kClockSourceChanged|kSampleRateValid,48000}};
    for (const auto& change : changes) {
        DriverFixture fixture;std::atomic<unsigned> calls{0};ASIO backend;
        ASSERT_TRUE(backend.setProcessFunction(countBlock,&calls));ASSERT_TRUE(backend.open(invalidationConfig()));ASSERT_TRUE(backend.start());
        ASIOTime time{};time.timeInfo.flags=change.flags;time.timeInfo.sampleRate=change.rate;
        EXPECT_EQ(fixture.callbacks.bufferSwitchTimeInfo(&time,0,ASIOTrue),&time);
        EXPECT_TRUE(backend.status().failed);EXPECT_EQ(calls,0u);ASSERT_TRUE(backend.stop());ASSERT_TRUE(backend.close());
    }
}
TEST(AsioInvalidation, MatchingTimeInfoRateRemainsCompatibleWithPositionDiagnostics) {
    DriverFixture fixture;std::atomic<unsigned> calls{0};ASIO backend;
    ASSERT_TRUE(backend.setProcessFunction(countBlock,&calls));ASSERT_TRUE(backend.open(invalidationConfig()));ASSERT_TRUE(backend.start());
    ASIOTime time{};time.timeInfo.flags=kSampleRateValid|kSamplePositionValid|kSystemTimeValid;
    time.timeInfo.sampleRate=48000;time.timeInfo.samplePosition={0,123};time.timeInfo.systemTime={0,456};
    fixture.callbacks.bufferSwitchTimeInfo(&time,0,ASIOTrue);
    EXPECT_EQ(calls,1u);EXPECT_FALSE(backend.status().failed);EXPECT_EQ(activeDriver->timing().samplePosition,123u);
    ASSERT_TRUE(backend.stop());ASSERT_TRUE(backend.close());
}
TEST(AsioInvalidation, AResetBeforeStartCannotActivateAnInvalidConfiguration) {
    DriverFixture fixture;std::atomic<unsigned> calls{0};ASIO backend;
    ASSERT_TRUE(backend.setProcessFunction(countBlock,&calls));ASSERT_TRUE(backend.open(invalidationConfig()));
    EXPECT_EQ(fixture.callbacks.asioMessage(kAsioResetRequest,0,nullptr,nullptr),1);
    EXPECT_EQ(backend.start().error(),ErrorType::ConfigurationFailed);EXPECT_EQ(fixture.starts,0u);EXPECT_TRUE(backend.status().failed);
    ASSERT_TRUE(backend.close());ASSERT_TRUE(backend.open(invalidationConfig()));ASSERT_TRUE(backend.start());
    fixture.callbacks.bufferSwitch(0,ASIOTrue);EXPECT_EQ(calls,1u);ASSERT_TRUE(backend.stop());ASSERT_TRUE(backend.close());
}
TEST(AsioInvalidation, DeviceLossIsReportedAndReopenDoesNotInventAnAvailableDriver) {
    DriverFixture fixture;ASIO backend;ASSERT_TRUE(backend.open(invalidationConfig()));ASSERT_TRUE(backend.start());
    fixture.callbacks.asioMessage(kAsioResetRequest,0,nullptr,nullptr);EXPECT_TRUE(backend.status().failed);
    ASSERT_TRUE(backend.stop());ASSERT_TRUE(backend.close());fixture.acceptInit=false;
    EXPECT_EQ(backend.open(invalidationConfig()).error(),ErrorType::EndpointUnavailable);EXPECT_EQ(fixture.live,0u);
}
TEST(AsioInvalidation, ConcurrentDriverMessagesLeaveReconnectionToTheApplication) {
    DriverFixture fixture;std::atomic<unsigned> calls{0};ASIO backend;fixture.runAudioThread=true;
    ASSERT_TRUE(backend.setProcessFunction(countBlock,&calls));ASSERT_TRUE(backend.open(invalidationConfig()));ASSERT_TRUE(backend.start());
    ASSERT_TRUE(awaitInvalidation([&]{return calls.load()>=2;}));
    std::thread messages([&] {
        for (unsigned count=0;count<1000;++count) fixture.callbacks.asioMessage(kAsioOverload,0,nullptr,nullptr);
        for (unsigned count=0;count<32;++count) fixture.callbacks.asioMessage(kAsioResetRequest,0,nullptr,nullptr);
    });messages.join();
    EXPECT_TRUE(backend.status().failed);EXPECT_EQ(backend.status().xruns,1000u);EXPECT_EQ(fixture.starts,1u);EXPECT_EQ(fixture.stops,0u);
    ASSERT_TRUE(backend.stop());ASSERT_TRUE(backend.close());ASSERT_TRUE(backend.open(invalidationConfig()));ASSERT_TRUE(backend.start());
    EXPECT_FALSE(backend.status().failed);EXPECT_EQ(backend.status().xruns,0u);ASSERT_TRUE(backend.stop());ASSERT_TRUE(backend.close());
}
}
