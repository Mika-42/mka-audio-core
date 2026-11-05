#include <gtest/gtest.h>
#include "fake_api.hpp"
import mka.audio.backend.coreaudio;

using namespace mka_coreaudio_detail;
TEST(CoreAudioDevices, NativeUidNameDirectionsAndConservativeCapabilities) {
    mac_test::FakeApi fake;
    mka::audio::core::CoreAudio backend;
    const auto endpoints=backend.getEndPoints();
    ASSERT_EQ(endpoints.size(),1u);
    EXPECT_EQ(endpoints[0].id,"fake-duplex");EXPECT_EQ(endpoints[0].name,"Test Microphone");
    ASSERT_TRUE(endpoints[0].input);ASSERT_TRUE(endpoints[0].output);
    EXPECT_EQ(endpoints[0].input->minChannels,2u);EXPECT_EQ(endpoints[0].output->maxChannels,2u);
    EXPECT_EQ(endpoints[0].output->formats,std::vector{mka::audio::core::Format::Float32});
    EXPECT_NE(std::find(endpoints[0].output->sampleRates.begin(),endpoints[0].output->sampleRates.end(),48000u),endpoints[0].output->sampleRates.end());
}
TEST(CoreAudioDevices, FailedMalformedAndDeadDeviceQueriesDoNotInventCapabilities) {
    mac_test::FakeApi fake;
    fake.failGet=kAudioHardwarePropertyDevices;EXPECT_TRUE(devices(fake.api).empty());
    fake.failGet=0;fake.malformed=true;EXPECT_TRUE(devices(fake.api).empty());
    fake.malformed=false;fake.alive=0;EXPECT_TRUE(devices(fake.api).empty());
    fake.alive=1;fake.floatFormat=false;EXPECT_FALSE(capabilities(fake.api,101,kAudioObjectPropertyScopeOutput));
    fake.floatFormat=true;fake.failGet=kAudioDevicePropertyStreamConfiguration;
    EXPECT_FALSE(capabilities(fake.api,101,kAudioObjectPropertyScopeInput));
}
TEST(CoreAudioDevices, WrongDirectionAbsentUidAndZeroValuesAreRejected) {
    mac_test::FakeApi fake;Session session(fake.api);auto cfg=mac_test::config();
    cfg.uid="absent";EXPECT_EQ(session.open(cfg).error(),Failure::Unavailable);
    cfg.uid=std::string("fake-duplex\0hidden",18);EXPECT_EQ(session.open(cfg).error(),Failure::Unavailable);
    cfg=mac_test::config();fake.outputs=0;EXPECT_EQ(session.open(cfg).error(),Failure::Channels);
    fake.outputs=2;cfg.rate=0;EXPECT_EQ(session.open(cfg).error(),Failure::Rate);
    cfg.rate=48000;cfg.frames=0;EXPECT_EQ(session.open(cfg).error(),Failure::Buffer);
    cfg.frames=256;cfg.uid="";EXPECT_TRUE(session.open(cfg));EXPECT_TRUE(session.close());
}
TEST(CoreAudioDevices, ExactRatesFramesAndNativeReadbacksWithRetry) {
    mac_test::FakeApi fake;Session session(fake.api);auto cfg=mac_test::config();
    cfg.rate=192000;EXPECT_EQ(session.open(cfg).error(),Failure::Rate);
    cfg.rate=44100;cfg.frames=512;ASSERT_TRUE(session.open(cfg));
    EXPECT_EQ(fake.rate,44100);EXPECT_EQ(fake.frames,512u);ASSERT_TRUE(session.close());
    EXPECT_EQ(fake.rate,48000);EXPECT_EQ(fake.frames,256u);
    fake.canSet=false;EXPECT_EQ(session.open(cfg).error(),Failure::Rate);fake.canSet=true;
    cfg.rate=48000;fake.roundFrames=true;EXPECT_EQ(session.open(cfg).error(),Failure::Buffer);
    fake.roundFrames=false;ASSERT_TRUE(session.open(cfg));EXPECT_TRUE(session.close());
}
TEST(CoreAudioDevices, NativeRegistrationFailuresRollBackAndLeaveReusableInstance) {
    mac_test::FakeApi fake;Session session(fake.api);
    fake.failCreate=1;EXPECT_FALSE(session.open(mac_test::config()));EXPECT_EQ(fake.destroys,0);
    fake.failAddAt=3;EXPECT_FALSE(session.open(mac_test::config()));EXPECT_TRUE(fake.listeners.empty());EXPECT_EQ(fake.destroys,1);
    ASSERT_TRUE(session.open(mac_test::config()));EXPECT_TRUE(session.close());EXPECT_TRUE(fake.listeners.empty());
}
