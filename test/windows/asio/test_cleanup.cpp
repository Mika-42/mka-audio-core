#include <gtest/gtest.h>
#include <atomic>
#include <expected>
#include "fake_driver.hpp"
import mka.audio.backend.asio;

namespace {
using namespace asio_test;
using namespace mka::audio::core;
EndpointConfig cleanupConfig() {return {driverId,Direction::Output,0,2,48000,Format::Float32,256};}
template<class Predicate> bool awaitCleanup(Predicate predicate) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while (!predicate()) {if (std::chrono::steady_clock::now()>=deadline)return false;std::this_thread::yield();}
    return true;
}
TEST(AsioCleanup, CloseConsumesNativeResourcesOnceAndBalancesTheOwnerApartment) {
    DriverFixture fixture;ASIO backend;fixture.currentRate=44100;
    ASSERT_TRUE(backend.open(cleanupConfig()));ASSERT_TRUE(backend.start());ASSERT_TRUE(backend.stop());ASSERT_TRUE(backend.close());
    EXPECT_EQ(fixture.bufferDisposals,1u);EXPECT_EQ(fixture.destroyed,1u);EXPECT_EQ(fixture.live,0u);
    EXPECT_EQ(fixture.releaseThread,fixture.initThread);EXPECT_EQ(fixture.disposeThread,fixture.initThread);
    EXPECT_EQ(fixture.comReferences,fixture.uninitializes);EXPECT_FALSE(IsWindow(fixture.lastWindow));
    EXPECT_DOUBLE_EQ(fixture.currentRate,44100);EXPECT_TRUE(CallbackRoute::empty());
    EXPECT_EQ(backend.close().error(),ErrorType::InvalidState);EXPECT_EQ(fixture.bufferDisposals,1u);
}
TEST(AsioCleanup, EveryDisposeErrorStillRestoresTheClockAndReleasesTheInterface) {
    const std::pair<ASIOError,ErrorType> errors[]{{ASE_NoMemory,ErrorType::ConfigurationFailed},
        {ASE_HWMalfunction,ErrorType::EndpointUnavailable},{ASE_SUCCESS,ErrorType::ConfigurationFailed}};
    for (const auto& [error,expected] : errors) {
        DriverFixture fixture;ASIO backend;fixture.currentRate=44100;
        ASSERT_TRUE(backend.open(cleanupConfig()));fixture.disposeResult=error;
        EXPECT_EQ(backend.close().error(),expected);EXPECT_EQ(fixture.live,0u);EXPECT_EQ(fixture.destroyed,1u);
        EXPECT_TRUE(fixture.storage.empty());EXPECT_DOUBLE_EQ(fixture.currentRate,44100);
        EXPECT_EQ(fixture.comReferences,fixture.uninitializes);ASSERT_TRUE(backend.close());EXPECT_EQ(fixture.bufferDisposals,1u);
        fixture.disposeResult=ASE_OK;ASSERT_TRUE(backend.open(cleanupConfig()));ASSERT_TRUE(backend.close());
    }
}
TEST(AsioCleanup, UnexpectedSdkCleanupExceptionsDoNotSkipReferenceRelease) {
    bool DriverFixture::* flags[]{&DriverFixture::throwOnStop,&DriverFixture::throwOnDispose,
        &DriverFixture::throwOnSetRate,&DriverFixture::throwOnRelease};
    for (const auto flag : flags) {
        DriverFixture fixture;ASIO backend;fixture.currentRate=44100;
        ASSERT_TRUE(backend.open(cleanupConfig()));
        if (flag==&DriverFixture::throwOnStop) {
            // White-box native state: exercise forced close without advancing
            // Core to Running, so the error result itself can be inspected.
            fixture.runAudioThread=true;ASSERT_EQ(activeDriver->start({}),ASE_OK);
        }
        fixture.*flag=true;
        EXPECT_EQ(backend.close().error(),ErrorType::EndpointUnavailable);
        EXPECT_EQ(fixture.live,0u);EXPECT_EQ(fixture.destroyed,1u);EXPECT_FALSE(fixture.nativeRunning);
        EXPECT_TRUE(fixture.storage.empty());EXPECT_EQ(fixture.bufferDisposals,1u);
        EXPECT_EQ(fixture.releaseThread,fixture.initThread);EXPECT_EQ(fixture.comReferences,fixture.uninitializes);
        ASSERT_TRUE(backend.close());EXPECT_EQ(fixture.destroyed,1u);
    }
}
TEST(AsioCleanup, ExceptionsDuringInitInfoAndPartialBufferCreationRollBackWithoutLeaks) {
    bool DriverFixture::* flags[]{&DriverFixture::throwOnInit,&DriverFixture::throwOnInfo,&DriverFixture::throwOnCreate};
    for (const auto flag : flags) {
        DriverFixture fixture;ASIO backend;fixture.currentRate=44100;fixture.*flag=true;
        EXPECT_EQ(backend.open(cleanupConfig()).error(),ErrorType::ConfigurationFailed);
        EXPECT_EQ(fixture.live,0u);EXPECT_EQ(fixture.destroyed,1u);EXPECT_TRUE(fixture.storage.empty());
        EXPECT_EQ(fixture.bufferDisposals,flag==&DriverFixture::throwOnCreate?1u:0u);
        EXPECT_EQ(fixture.comReferences,fixture.uninitializes);EXPECT_DOUBLE_EQ(fixture.currentRate,44100);
        fixture.*flag=false;ASSERT_TRUE(backend.open(cleanupConfig()));ASSERT_TRUE(backend.close());
    }
}
TEST(AsioCleanup, TheFirstCleanupErrorWinsWhileAllLaterStagesAreAttempted) {
    DriverFixture fixture;ASIO backend;fixture.currentRate=44100;
    ASSERT_TRUE(backend.open(cleanupConfig()));fixture.disposeResult=ASE_NoMemory;fixture.setResult=ASE_NoClock;
    EXPECT_EQ(backend.close().error(),ErrorType::ConfigurationFailed);
    EXPECT_EQ(fixture.bufferDisposals,1u);EXPECT_EQ(fixture.setRequests.size(),2u);EXPECT_EQ(fixture.destroyed,1u);
    EXPECT_EQ(fixture.live,0u);EXPECT_TRUE(fixture.storage.empty());ASSERT_TRUE(backend.close());
}
TEST(AsioCleanup, EverySdkCallbackFormIsRetiredBeforeDisposalOrInterfaceRelease) {
    for (unsigned kind=0;kind<4;++kind) {
        DriverFixture fixture;
        struct Hold {std::atomic<bool> entered{false},release{false};std::atomic<unsigned> failures{0};} hold;
        ASIO backend;ASSERT_TRUE(backend.open(cleanupConfig()));
        activeDriver->configureProcessing({.user=&hold,.failed=+[](void* user) noexcept {
            auto& h=*static_cast<Hold*>(user);++h.failures;h.entered=true;
            // Deliberately held notification mock, never production RT code.
            while (!h.release.load()) std::this_thread::yield();
        }});ASSERT_TRUE(activeDriver->enableProcessing());
        const auto saved=fixture.callbacks;ASIOTime time{};time.timeInfo.flags=kClockSourceChanged;
        std::thread callback([&] {
            if (kind==0)saved.sampleRateDidChange(0);
            else if (kind==1)saved.asioMessage(kAsioResetRequest,0,nullptr,nullptr);
            else if (kind==2)saved.bufferSwitchTimeInfo(&time,0,ASIOTrue);
            else saved.bufferSwitch(-1,ASIOTrue);
        });
        const bool entered=awaitCleanup([&]{return hold.entered.load();});
        if (!entered) {hold.release=true;callback.join();FAIL()<<"notification did not enter";return;}
        std::atomic<bool> closed{false},result{false};
        std::thread closer([&]{result=bool(backend.close());closed=true;});
        const bool retired=awaitCleanup([]{return CallbackRoute::empty();});
        std::this_thread::sleep_for(std::chrono::milliseconds(20));const bool waiting=!closed.load();
        const auto disposed=fixture.bufferDisposals,destroyed=fixture.destroyed;
        hold.release=true;callback.join();closer.join();
        EXPECT_TRUE(retired);EXPECT_TRUE(waiting);EXPECT_EQ(disposed,0u);EXPECT_EQ(destroyed,0u);EXPECT_TRUE(result);
        EXPECT_EQ(fixture.bufferDisposals,1u);EXPECT_EQ(fixture.destroyed,1u);
        saved.bufferSwitch(0,ASIOTrue);saved.sampleRateDidChange(0);saved.asioMessage(kAsioResetRequest,0,nullptr,nullptr);
        EXPECT_EQ(saved.bufferSwitchTimeInfo(&time,0,ASIOTrue),&time);EXPECT_EQ(hold.failures,1u);
    }
}
TEST(AsioCleanup, DestroyingAClosedSecondInstanceCannotRetireTheRunningFirstInstance) {
    DriverFixture fixture;std::atomic<unsigned> calls{0};ASIO first;fixture.runAudioThread=true;
    ASSERT_TRUE(first.setProcessFunction(+[](void* user,const AudioProcessContext&) noexcept {
        ++*static_cast<std::atomic<unsigned>*>(user);
    },&calls));ASSERT_TRUE(first.open(cleanupConfig()));ASSERT_TRUE(first.start());
    ASSERT_TRUE(awaitCleanup([&]{return calls.load()>=2;}));
    {ASIO second;EXPECT_EQ(second.open(cleanupConfig()).error(),ErrorType::EndpointUnavailable);}
    const auto before=calls.load();ASSERT_TRUE(awaitCleanup([&]{return calls.load()>before;}));
    EXPECT_EQ(fixture.stops,0u);EXPECT_EQ(fixture.creates,1u);EXPECT_FALSE(first.status().failed);
    ASSERT_TRUE(first.stop());ASSERT_TRUE(first.close());
}
TEST(AsioCleanup, RepeatedOwnershipCyclesBalanceBuffersInterfacesWindowsAndComReferences) {
    DriverFixture fixture;ASIO backend;fixture.currentRate=44100;
    for (unsigned cycle=1;cycle<=50;++cycle) {
        ASSERT_TRUE(backend.open(cleanupConfig()));ASSERT_TRUE(backend.start());ASSERT_TRUE(backend.stop());ASSERT_TRUE(backend.close());
        EXPECT_EQ(fixture.bufferCreations,cycle);EXPECT_EQ(fixture.bufferDisposals,cycle);EXPECT_EQ(fixture.destroyed,cycle);
        EXPECT_EQ(fixture.live,0u);EXPECT_EQ(fixture.comReferences,fixture.uninitializes);EXPECT_FALSE(IsWindow(fixture.lastWindow));
        EXPECT_DOUBLE_EQ(fixture.currentRate,44100);EXPECT_TRUE(CallbackRoute::empty());
    }
}
}
