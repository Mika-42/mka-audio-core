#include <gtest/gtest.h>
#include <atomic>
#include <cstring>
#include <expected>
#include "fake_driver.hpp"
import mka.audio.backend.asio;

namespace {
using namespace asio_test;
using namespace mka::audio::core;
struct TransportCalls {std::atomic<unsigned> calls{0}; std::atomic<bool> valid{true};};
void produce(void* user,const AudioProcessContext& context) noexcept {
    auto& data=*static_cast<TransportCalls*>(user);
    if (context.frames!=256 || context.output.count!=2) data.valid=false;
    for (unsigned channel=0;channel<context.output.count;++channel)
        for (unsigned frame=0;frame<context.frames;++frame) context.output.channels[channel][frame]=0.25f;
    ++data.calls;
}
EndpointConfig transportConfig() {return {driverId,Direction::Output,0,2,48000,Format::Float32,256};}
template<class Predicate> bool awaitTransport(Predicate predicate) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while (!predicate()) {if (std::chrono::steady_clock::now()>=deadline) return false;std::this_thread::yield();}
    return true;
}
TEST(AsioTransport, PublicLifecycleEnablesDspOnlyAfterNativeStartAndStopsQuiescently) {
    DriverFixture fixture; TransportCalls calls; ASIO backend; fixture.fireStartCallback=true;
    ASSERT_TRUE(backend.setProcessFunction(produce,&calls));
    EXPECT_EQ(backend.start().error(),ErrorType::InvalidState);
    ASSERT_TRUE(backend.open(transportConfig())); ASSERT_TRUE(backend.start());
    EXPECT_EQ(calls.calls,0u); EXPECT_EQ(fixture.starts,1u); EXPECT_TRUE(fixture.nativeRunning);
    EXPECT_EQ(fixture.startThread,fixture.initThread); EXPECT_EQ(backend.start().error(),ErrorType::InvalidState);
    fixture.callbacks.bufferSwitch(0,ASIOTrue); fixture.callbacks.bufferSwitch(1,ASIOFalse);
    EXPECT_EQ(calls.calls,2u); EXPECT_TRUE(calls.valid);
    for (const auto& descriptor : fixture.createdBuffers) for (auto buffer : descriptor.buffers) {
        float sample=0;std::memcpy(&sample,buffer,4);EXPECT_FLOAT_EQ(sample,0.25f);
    }
    const auto saved=fixture.callbacks; ASSERT_TRUE(backend.stop());
    EXPECT_FALSE(fixture.nativeRunning); EXPECT_EQ(fixture.stopThread,fixture.initThread);
    saved.bufferSwitch(0,ASIOTrue);EXPECT_EQ(calls.calls,2u);
    EXPECT_EQ(backend.stop().error(),ErrorType::InvalidState);
    ASSERT_TRUE(backend.start()); EXPECT_EQ(calls.calls,2u); saved.bufferSwitch(0,ASIOTrue);EXPECT_EQ(calls.calls,3u);
    EXPECT_EQ(backend.close().error(),ErrorType::InvalidState);
    ASSERT_TRUE(backend.stop()); ASSERT_TRUE(backend.close()); EXPECT_EQ(fixture.bufferDisposals,1u);
}
TEST(AsioTransport, PublicDuplexUsesTheActualInputAndOutputConverters) {
    DriverFixture fixture; float captured=0; unsigned calls=0;
    struct User {float* input;unsigned* calls;} user{&captured,&calls};
    ASIO backend;
    fixture.inputFormats={ASIOSTInt32MSB24}; fixture.outputFormats={ASIOSTInt24LSB,ASIOSTInt32LSB24};
    ASSERT_TRUE(backend.setProcessFunction(+[](void* pointer,const AudioProcessContext& context) noexcept {
        auto& u=*static_cast<User*>(pointer);*u.input=context.input.channels[0][0];++*u.calls;
        for (unsigned frame=0;frame<context.frames;++frame) {
            context.output.channels[0][frame]=context.input.channels[0][frame];
            context.output.channels[1][frame]=2*context.input.channels[0][frame];
        }
    },&user));
    ASSERT_TRUE(backend.open({driverId,Direction::Duplex,1,2,48000,Format::Int24,256}));
    const std::byte quarter[]{std::byte{0},std::byte{0x20},std::byte{0},std::byte{0}};
    for (unsigned frame=0;frame<256;++frame) std::memcpy(static_cast<std::byte*>(fixture.createdBuffers[0].buffers[0])+frame*4,quarter,4);
    ASSERT_TRUE(backend.start());fixture.callbacks.bufferSwitch(0,ASIOTrue);
    EXPECT_EQ(calls,1u);EXPECT_FLOAT_EQ(captured,0.25f);
    const std::byte packed[]{std::byte{0},std::byte{0},std::byte{0x20}};
    const std::byte in32[]{std::byte{0},std::byte{0},std::byte{0x40},std::byte{0}};
    EXPECT_EQ(std::memcmp(fixture.createdBuffers[1].buffers[0],packed,3),0);
    EXPECT_EQ(std::memcmp(fixture.createdBuffers[2].buffers[0],in32,4),0);
    ASSERT_TRUE(backend.stop());ASSERT_TRUE(backend.close());
}
TEST(AsioTransport, PublicNullCallbackWritesNativeSilence) {
    DriverFixture fixture;ASIO backend;ASSERT_TRUE(backend.open(transportConfig()));ASSERT_TRUE(backend.start());
    for (const auto& info : fixture.createdBuffers) std::memset(info.buffers[0],0x5a,256*4);
    fixture.callbacks.bufferSwitch(0,ASIOTrue);
    for (const auto& info : fixture.createdBuffers) {
        const auto* bytes=static_cast<const std::byte*>(info.buffers[0]);
        for (unsigned i=0;i<256*4;++i) EXPECT_EQ(bytes[i],std::byte{0});
    }
    EXPECT_FALSE(backend.status().failed);ASSERT_TRUE(backend.stop());ASSERT_TRUE(backend.close());
}
TEST(AsioTransport, PartialStartFailuresNeverRunDspAndAreStoppedBeforeRetry) {
    DriverFixture fixture;TransportCalls calls;ASIO backend;
    fixture.partialStart=true;fixture.runAudioThread=true;fixture.fireStartCallback=true;
    ASSERT_TRUE(backend.setProcessFunction(produce,&calls));ASSERT_TRUE(backend.open(transportConfig()));
    const std::pair<ASIOError,ErrorType> errors[]{{ASE_NotPresent,ErrorType::EndpointUnavailable},
        {ASE_NoMemory,ErrorType::ConfigurationFailed},{ASE_NoClock,ErrorType::SampleRateNotSupported},
        {ASE_SUCCESS,ErrorType::ConfigurationFailed}};
    unsigned attempts=0;
    for (const auto& [error,expected] : errors) {
        fixture.startResult=error;EXPECT_EQ(backend.start().error(),expected);++attempts;
        EXPECT_EQ(calls.calls,0u);EXPECT_FALSE(fixture.nativeRunning);EXPECT_EQ(fixture.stops,attempts);
    }
    fixture.startResult=ASE_OK;fixture.partialStart=false;fixture.runAudioThread=false;
    ASSERT_TRUE(backend.start());fixture.callbacks.bufferSwitch(0,ASIOTrue);EXPECT_EQ(calls.calls,1u);
    ASSERT_TRUE(backend.stop());ASSERT_TRUE(backend.close());
}
TEST(AsioTransport, ClockDriftBeforeOrDuringStartFailsWithoutSilentRenegotiation) {
    DriverFixture fixture;TransportCalls calls;ASIO backend;
    ASSERT_TRUE(backend.setProcessFunction(produce,&calls));ASSERT_TRUE(backend.open(transportConfig()));
    fixture.currentRate=44100;EXPECT_EQ(backend.start().error(),ErrorType::SampleRateNotSupported);
    EXPECT_EQ(fixture.starts,0u);fixture.currentRate=48000;fixture.changeRateOnStart=44100;fixture.fireStartCallback=true;
    EXPECT_EQ(backend.start().error(),ErrorType::SampleRateNotSupported);
    EXPECT_EQ(fixture.starts,1u);EXPECT_EQ(fixture.stops,1u);EXPECT_EQ(calls.calls,0u);
    EXPECT_TRUE(fixture.setRequests.empty());EXPECT_FALSE(fixture.nativeRunning);ASSERT_TRUE(backend.close());
}
TEST(AsioTransport, FailedStartRollbackRetainsNativeStateUntilStopCanSucceed) {
    DriverFixture fixture;TransportCalls calls;ASIO backend;
    fixture.partialStart=true;fixture.runAudioThread=true;fixture.startResult=ASE_NoMemory;fixture.stopResult=ASE_HWMalfunction;
    ASSERT_TRUE(backend.setProcessFunction(produce,&calls));ASSERT_TRUE(backend.open(transportConfig()));
    EXPECT_EQ(backend.start().error(),ErrorType::ConfigurationFailed);EXPECT_TRUE(fixture.nativeRunning);
    EXPECT_EQ(backend.start().error(),ErrorType::EndpointUnavailable);EXPECT_EQ(fixture.starts,1u);EXPECT_EQ(calls.calls,0u);
    fixture.stopResult=ASE_OK;fixture.startResult=ASE_OK;fixture.partialStart=false;fixture.runAudioThread=false;
    ASSERT_TRUE(backend.start());EXPECT_EQ(fixture.starts,2u);fixture.callbacks.bufferSwitch(0,ASIOTrue);
    EXPECT_EQ(calls.calls,1u);ASSERT_TRUE(backend.stop());ASSERT_TRUE(backend.close());
}
TEST(AsioTransport, ClockQueryErrorsBeforeAndAfterNativeStartDoNotActivateDsp) {
    for (const unsigned failingRead : {3u,4u}) {
        DriverFixture fixture;TransportCalls calls;ASIO backend;
        fixture.getResult=ASE_NoClock;fixture.failGetNumber=failingRead;fixture.fireStartCallback=true;
        ASSERT_TRUE(backend.setProcessFunction(produce,&calls));ASSERT_TRUE(backend.open(transportConfig()));
        EXPECT_EQ(backend.start().error(),ErrorType::SampleRateNotSupported);EXPECT_EQ(calls.calls,0u);
        EXPECT_EQ(fixture.starts,failingRead==3?0u:1u);EXPECT_EQ(fixture.stops,failingRead==3?0u:1u);
        EXPECT_FALSE(fixture.nativeRunning);ASSERT_TRUE(backend.close());
    }
}
TEST(AsioTransport, NativeStopErrorsPreserveStateForRetryWithDspDisabled) {
    DriverFixture fixture;TransportCalls calls;ASIO backend;
    ASSERT_TRUE(backend.setProcessFunction(produce,&calls));ASSERT_TRUE(backend.open(transportConfig()));ASSERT_TRUE(backend.start());
    fixture.callbacks.bufferSwitch(0,ASIOTrue);fixture.stopResult=ASE_HWMalfunction;
    EXPECT_EQ(backend.stop().error(),ErrorType::EndpointUnavailable);EXPECT_TRUE(fixture.nativeRunning);
    fixture.callbacks.bufferSwitch(1,ASIOFalse);EXPECT_EQ(calls.calls,1u);
    EXPECT_EQ(backend.start().error(),ErrorType::InvalidState);EXPECT_EQ(backend.close().error(),ErrorType::InvalidState);
    fixture.stopResult=ASE_OK;ASSERT_TRUE(backend.stop());ASSERT_TRUE(backend.start());
    fixture.callbacks.bufferSwitch(0,ASIOTrue);EXPECT_EQ(calls.calls,2u);
    ASSERT_TRUE(backend.stop());ASSERT_TRUE(backend.close());
}
TEST(AsioTransport, SdkWorkerStopsAndNoUserCallbackSurvivesSuccessfulStop) {
    DriverFixture fixture;TransportCalls calls;ASIO backend;fixture.runAudioThread=true;
    ASSERT_TRUE(backend.setProcessFunction(produce,&calls));ASSERT_TRUE(backend.open(transportConfig()));ASSERT_TRUE(backend.start());
    ASSERT_TRUE(awaitTransport([&]{return calls.calls.load()>=4;}));ASSERT_TRUE(backend.stop());
    const auto count=calls.calls.load();const auto saved=fixture.callbacks;
    saved.bufferSwitch(0,ASIOTrue);std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_EQ(calls.calls,count);EXPECT_FALSE(fixture.nativeRunning);EXPECT_TRUE(calls.valid);ASSERT_TRUE(backend.close());
}
TEST(AsioTransport, OptionalOutputReadyRunsAfterConversionOnTheAudioCaller) {
    DriverFixture fixture;TransportCalls calls;ASIO backend;fixture.readyResult=ASE_OK;
    ASSERT_TRUE(backend.setProcessFunction(produce,&calls));ASSERT_TRUE(backend.open(transportConfig()));ASSERT_TRUE(backend.start());
    EXPECT_EQ(fixture.readyCalls,1u);EXPECT_EQ(fixture.readyThread,fixture.initThread);
    fixture.callbacks.bufferSwitch(0,ASIOTrue);EXPECT_EQ(fixture.readyCalls,2u);
    EXPECT_FLOAT_EQ(fixture.readyObservedSample.load(),0.25f);EXPECT_EQ(fixture.readyThread,GetCurrentThreadId());
    EXPECT_FALSE(backend.status().failed);ASSERT_TRUE(backend.stop());ASSERT_TRUE(backend.close());
}
TEST(AsioTransport, UnsupportedOutputReadyIsProbedOnceWithoutFailingPlayback) {
    for (const ASIOError result : {static_cast<ASIOError>(ASE_NotPresent),static_cast<ASIOError>(ASE_SUCCESS)}) {
        DriverFixture fixture;TransportCalls calls;ASIO backend;fixture.readyResult=result;
        ASSERT_TRUE(backend.setProcessFunction(produce,&calls));ASSERT_TRUE(backend.open(transportConfig()));ASSERT_TRUE(backend.start());
        fixture.callbacks.bufferSwitch(0,ASIOTrue);fixture.callbacks.bufferSwitch(1,ASIOFalse);
        EXPECT_EQ(calls.calls,2u);EXPECT_EQ(fixture.readyCalls,1u);EXPECT_FALSE(backend.status().failed);
        ASSERT_TRUE(backend.stop());ASSERT_TRUE(backend.close());
    }
}
TEST(AsioTransport, InputOnlyStreamsDoNotProbeOrCallOutputReady) {
    DriverFixture fixture;ASIO backend;fixture.readyResult=ASE_OK;
    ASSERT_TRUE(backend.open({driverId,Direction::Input,1,0,48000,Format::Float32,256}));ASSERT_TRUE(backend.start());
    fixture.callbacks.bufferSwitch(0,ASIOTrue);EXPECT_EQ(fixture.readyCalls,0u);
    ASSERT_TRUE(backend.stop());ASSERT_TRUE(backend.close());
}
TEST(AsioTransport, APreviouslySupportedOutputReadyFailureDisablesDspAndReportsFailed) {
    DriverFixture fixture;TransportCalls calls;ASIO backend;fixture.readyResult=ASE_OK;
    ASSERT_TRUE(backend.setProcessFunction(produce,&calls));ASSERT_TRUE(backend.open(transportConfig()));ASSERT_TRUE(backend.start());
    fixture.readyResult=ASE_HWMalfunction;fixture.callbacks.bufferSwitch(0,ASIOTrue);
    EXPECT_TRUE(backend.status().failed);fixture.callbacks.bufferSwitch(1,ASIOFalse);
    EXPECT_EQ(calls.calls,1u);EXPECT_EQ(fixture.readyCalls,2u);ASSERT_TRUE(backend.stop());ASSERT_TRUE(backend.close());
}
TEST(AsioTransport, FailedProcessingCanBeStoppedAndRequiresReopeningForNewBuffers) {
    DriverFixture fixture;TransportCalls calls;ASIO backend;
    ASSERT_TRUE(backend.setProcessFunction(produce,&calls));ASSERT_TRUE(backend.open(transportConfig()));ASSERT_TRUE(backend.start());
    fixture.callbacks.bufferSwitch(-1,ASIOTrue);EXPECT_TRUE(backend.status().failed);ASSERT_TRUE(backend.stop());
    EXPECT_EQ(backend.start().error(),ErrorType::ConfigurationFailed);ASSERT_TRUE(backend.close());
    ASSERT_TRUE(backend.open(transportConfig()));ASSERT_TRUE(backend.start());fixture.callbacks.bufferSwitch(0,ASIOTrue);
    EXPECT_EQ(calls.calls,1u);ASSERT_TRUE(backend.stop());ASSERT_TRUE(backend.close());
}
TEST(AsioTransport, DestructionStopsDisposesAndReleasesEvenAfterPersistentStopFailure) {
    DriverFixture fixture;TransportCalls calls;fixture.runAudioThread=true;
    {
        ASIO backend;ASSERT_TRUE(backend.setProcessFunction(produce,&calls));ASSERT_TRUE(backend.open(transportConfig()));
        ASSERT_TRUE(backend.start());ASSERT_TRUE(awaitTransport([&]{return calls.calls.load()>=2;}));fixture.stopResult=ASE_HWMalfunction;
    }
    const auto count=calls.calls.load();fixture.callbacks.bufferSwitch(0,ASIOTrue);
    EXPECT_EQ(calls.calls,count);EXPECT_FALSE(fixture.nativeRunning);EXPECT_EQ(fixture.live,0u);
    EXPECT_EQ(fixture.stops,2u);EXPECT_EQ(fixture.bufferDisposals,1u);EXPECT_TRUE(fixture.storage.empty());
}
TEST(AsioTransport, DiscoveryDuringRunningUsesCachedCapabilitiesWithoutNativeControlQueries) {
    DriverFixture fixture;ASIO backend;ASSERT_TRUE(backend.open(transportConfig()));ASSERT_TRUE(backend.start());
    const auto channels=fixture.channelQueries,buffers=fixture.bufferQueries,rates=fixture.getCalls;
    const auto formats=fixture.infoRequests.size();ASSERT_EQ(backend.getEndPoints().size(),1u);
    EXPECT_EQ(fixture.channelQueries,channels);EXPECT_EQ(fixture.bufferQueries,buffers);EXPECT_EQ(fixture.getCalls,rates);
    EXPECT_EQ(fixture.infoRequests.size(),formats);EXPECT_EQ(fixture.inits,1u);
    ASSERT_TRUE(backend.stop());ASSERT_TRUE(backend.close());
}
}
