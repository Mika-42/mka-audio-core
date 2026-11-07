#include <gtest/gtest.h>
#include "fake_devices.hpp"
#include "../../src/backend/windows/detail/wasapi_session.hpp"
using namespace wasapi_test;
TEST_F(DevicesFixture, QueriesEveryExactNativeRepresentationWithoutRateOrChannelSubstitution) {
    auto d=add(L"output",eRender);ControlThread control(api());Session session(control);
    for (auto kind:{SampleKind::S16,SampleKind::S24Packed,SampleKind::S24Msb32,SampleKind::S32,SampleKind::F32,SampleKind::F64}) {
        ASSERT_EQ(session.open({"output",eRender,2,44100,256,kind}),S_OK);
        EXPECT_EQ(d->client->requestedMode,AUDCLNT_SHAREMODE_SHARED);
        EXPECT_EQ(d->client->requested.Format.nSamplesPerSec,44100u);
        EXPECT_EQ(d->client->requested.Format.nChannels,2u);
        EXPECT_EQ(sampleKind(session.nativeFormat().Format),kind);
        EXPECT_TRUE(validWaveFormat(session.nativeFormat().Format));
        ASSERT_EQ(session.close(),S_OK);
    }
    d.Reset();
}
TEST_F(DevicesFixture, ClosestMatchIsFreedAndRejectedInsteadOfSilentlyChangingTheRequest) {
    auto d=add(L"output",eRender);ControlThread control(api());Session session(control);
    for (auto result:{S_FALSE,AUDCLNT_E_UNSUPPORTED_FORMAT,E_FAIL}) {
        d->client->supportResult=result;
        EXPECT_EQ(session.open({"output",eRender,2,44100,256}),result==S_FALSE?AUDCLNT_E_UNSUPPORTED_FORMAT:result);
    }
    d->client->supportResult=S_OK;EXPECT_EQ(session.open({"output",eRender,2,44100,256}),S_OK);d.Reset();
}
TEST_F(DevicesFixture, Int24KeepsTheNativeMostSignificant24BitContainerWhenRequired) {
    auto d=add(L"output",eRender);auto& mix=d->client->mix;
    mix.SubFormat=KSDATAFORMAT_SUBTYPE_PCM;mix.Samples.wValidBitsPerSample=24;
    ControlThread control(api());Session session(control);
    ASSERT_EQ(session.open({"output",eRender,2,48000,256,SampleKind::S24Packed}),S_OK);
    EXPECT_EQ(sampleKind(session.nativeFormat().Format),SampleKind::S24Msb32);d.Reset();
}
TEST(WasapiFormat, RejectsByteCountOverflowInvalidKindsAndInconsistentSpeakerMasks) {
    EXPECT_FALSE(makeWaveFormat(SampleKind::F64,65535,48000));
    EXPECT_FALSE(makeWaveFormat(SampleKind::F64,2,std::numeric_limits<std::uint32_t>::max()));
    EXPECT_FALSE(makeWaveFormat(static_cast<SampleKind>(255),2,48000));
    EXPECT_FALSE(makeWaveFormat(SampleKind::F32,2,48000,SPEAKER_FRONT_CENTER));
    EXPECT_FALSE(makeWaveFormat(SampleKind::F32,0,48000));
}
