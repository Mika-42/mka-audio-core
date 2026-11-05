#include <gtest/gtest.h>
#include "fake_api.hpp"
#include <thread>
import mka.audio.backend.coreaudio;
using namespace mka::audio::core;
using namespace mka_coreaudio_detail;
static EndpointConfig publicConfig(){return {"fake-duplex",Direction::Output,0,2,48000,Format::Float32,256};}
TEST(CoreAudioLifecycle, PublicFacadeLifecycleFailureRecoveryAndQuiescentStop) {
    mac_test::FakeApi fake;CoreAudio backend;std::atomic<int> calls=0;
    ASSERT_TRUE(backend.setProcessFunction(+[](void* user,const AudioProcessContext& ctx) noexcept {
        ++*static_cast<std::atomic<int>*>(user);if(ctx.output.count)ctx.output.channels[0][0]=0.125f;
    },&calls));
    ASSERT_TRUE(backend.open(publicConfig()));fake.failStart=1;
    EXPECT_FALSE(backend.start());ASSERT_TRUE(backend.start());
    mac_test::Buffer output(2,256,1);fake.pump(nullptr,&output.list);EXPECT_EQ(calls.load(),1);
    fake.failStop=1;EXPECT_FALSE(backend.stop());EXPECT_FALSE(backend.close());ASSERT_TRUE(backend.stop());
    fake.pump(nullptr,&output.list);EXPECT_EQ(calls.load(),1);EXPECT_EQ(output.data[0],0);
    fake.failDestroy=1;EXPECT_FALSE(backend.close());ASSERT_TRUE(backend.close());
    EXPECT_EQ(fake.creates,fake.destroys);EXPECT_TRUE(fake.listeners.empty());
}
TEST(CoreAudioLifecycle, StopWaitsForAnAlreadyExecutingCallback) {
    mac_test::FakeApi fake;CoreAudio backend;
    struct Gate {std::atomic<bool> entered=false,release=false;} gate;
    ASSERT_TRUE(backend.setProcessFunction(+[](void* user,const AudioProcessContext&) noexcept {
        auto& g=*static_cast<Gate*>(user);g.entered=true;while(!g.release.load())std::this_thread::yield();
    },&gate));
    ASSERT_TRUE(backend.open(publicConfig()));ASSERT_TRUE(backend.start());
    mac_test::Buffer output(2,256);std::thread audio([&]{fake.pump(nullptr,&output.list);});
    while(!gate.entered.load())std::this_thread::yield();
    std::atomic<bool> stopped=false;std::thread control([&]{stopped=bool(backend.stop());});
    std::this_thread::sleep_for(std::chrono::milliseconds(20));EXPECT_FALSE(stopped.load());
    gate.release=true;audio.join();control.join();EXPECT_TRUE(stopped.load());EXPECT_TRUE(backend.close());
}
TEST(CoreAudioLifecycle, FailureAndOverloadNotificationsMuteAudioAndAllowReopen) {
    mac_test::FakeApi fake;CoreAudio backend;ASSERT_TRUE(backend.open(publicConfig()));ASSERT_TRUE(backend.start());
    fake.event(kAudioDeviceProcessorOverload);EXPECT_EQ(backend.status().xruns,1u);
    fake.event(kAudioDevicePropertyNominalSampleRate);EXPECT_TRUE(backend.status().failed);
    mac_test::Buffer output(2,256,1);fake.pump(nullptr,&output.list);EXPECT_EQ(output.data[0],0);
    ASSERT_TRUE(backend.stop());ASSERT_TRUE(backend.close());
    ASSERT_TRUE(backend.open(publicConfig()));ASSERT_TRUE(backend.start());EXPECT_FALSE(backend.status().failed);
    fake.alive=0;fake.event(kAudioDevicePropertyDeviceIsAlive);EXPECT_TRUE(backend.status().failed);
    ASSERT_TRUE(backend.stop());ASSERT_TRUE(backend.close());
}
TEST(CoreAudioLifecycle, RemovalFailureCanBeRetriedBeforeFreeingNativeUserData) {
    mac_test::FakeApi fake;CoreAudio backend;ASSERT_TRUE(backend.open(publicConfig()));
    fake.failRemove=1;EXPECT_FALSE(backend.close());EXPECT_FALSE(fake.listeners.empty());
    EXPECT_TRUE(backend.close());EXPECT_TRUE(fake.listeners.empty());
}
TEST(CoreAudioLifecycle, ThousandCyclesReleaseEveryRegistrationAndPreserveUserData) {
    mac_test::FakeApi fake;CoreAudio backend;int calls=0;
    ASSERT_TRUE(backend.setProcessFunction(+[](void* u,const AudioProcessContext&) noexcept {++*static_cast<int*>(u);},&calls));
    mac_test::Buffer output(2,256);
    for(int i=0;i<1000;++i){
        ASSERT_TRUE(backend.open(publicConfig()));ASSERT_TRUE(backend.start());fake.pump(nullptr,&output.list);
        ASSERT_TRUE(backend.stop());ASSERT_TRUE(backend.close());
    }
    EXPECT_EQ(calls,1000);EXPECT_EQ(fake.creates,fake.destroys);EXPECT_TRUE(fake.listeners.empty());
}

TEST(CoreAudioLifecycle, PermissionDeniedStartAndRemovedDeviceStopAreMappedWithoutStrandingRunningState) {
    mac_test::FakeApi fake;CoreAudio backend;ASSERT_TRUE(backend.open(publicConfig()));
    fake.startStatus=kAudioDevicePermissionsError;
    const auto denied=backend.start();ASSERT_FALSE(denied);EXPECT_EQ(denied.error(),ErrorType::EndpointUnavailable);
    fake.startStatus=noErr;ASSERT_TRUE(backend.start());
    fake.alive=0;fake.event(kAudioDevicePropertyDeviceIsAlive);fake.stopStatus=kAudioHardwareBadDeviceError;
    EXPECT_TRUE(backend.stop());EXPECT_TRUE(backend.close());
}
TEST(CoreAudioLifecycle, FailedEventIsDeliveredOnceOffTheNativeCallbackThread) {
    mac_test::FakeApi fake;CoreAudio backend;
    struct Events {std::atomic<unsigned> calls=0;std::atomic<bool> wrongThread=false;std::thread::id native;} events;
    events.native=std::this_thread::get_id();
    ASSERT_TRUE(backend.setEventHandler(+[](void* user,const Event& event) noexcept {
        auto& state=*static_cast<Events*>(user);
        if(event.type==EventType::Failed){
            if(std::this_thread::get_id()==state.native)state.wrongThread=true;
            ++state.calls;
        }
    },&events));
    ASSERT_TRUE(backend.open(publicConfig()));ASSERT_TRUE(backend.start());
    fake.event(kAudioDevicePropertyNominalSampleRate);fake.event(kAudioDevicePropertyDeviceIsAlive);
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(!events.calls.load() && std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    ASSERT_TRUE(backend.stop());ASSERT_TRUE(backend.close());
    EXPECT_EQ(events.calls.load(),1u);EXPECT_FALSE(events.wrongThread.load());
}
