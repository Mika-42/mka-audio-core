#include <gtest/gtest.h>
#include "fake_devices.hpp"
using namespace wasapi_test;
TEST_F(DevicesFixture, CapabilitiesUseTheNativeMixChannelsRateFormatAndDefaultPeriod) {
    add(L"output",eRender);add(L"input",eCapture);ControlThread control(api());
    const auto devices=enumerateDevices(control);ASSERT_EQ(devices.size(),2u);
    for (const auto& d:devices) { EXPECT_EQ(d.channels,2u);EXPECT_EQ(d.rate,48000u);EXPECT_EQ(d.kind,SampleKind::F32);ASSERT_EQ(d.bufferSizes.size(),1u);EXPECT_EQ(d.bufferSizes[0],480u); }
}
TEST_F(DevicesFixture, ActivationAndMalformedOrFailedMixFormatsDoNotAdvertiseInventedCapabilities) {
    auto d=add(L"bad",eRender);ControlThread control(api());
    d->activateResult=E_ACCESSDENIED;EXPECT_TRUE(enumerateDevices(control).empty());d->activateResult=S_OK;
    d->client->mixResult=E_FAIL;EXPECT_TRUE(enumerateDevices(control).empty());d->client->mixResult=S_OK;
    d->client->mix.Format.cbSize=0;EXPECT_TRUE(enumerateDevices(control).empty());d->client->mix.Format.cbSize=22;
    d->client->mix.Format.nBlockAlign=1;EXPECT_TRUE(enumerateDevices(control).empty());
    d.Reset();
}
TEST_F(DevicesFixture, UnknownDevicePeriodLeavesTheLatencyHintsEmpty) {
    auto d=add(L"output",eRender);d->client->periodResult=E_FAIL;ControlThread control(api());
    const auto devices=enumerateDevices(control);ASSERT_EQ(devices.size(),1u);EXPECT_TRUE(devices[0].bufferSizes.empty());d.Reset();
}
TEST(WasapiCapabilities, PeriodRoundingRejectsZeroNegativeAndOverflow) {
    EXPECT_FALSE(periodFrames(0,48000));EXPECT_FALSE(periodFrames(-1,48000));EXPECT_FALSE(periodFrames(100000,0));
    EXPECT_FALSE(periodFrames(std::numeric_limits<REFERENCE_TIME>::max(),std::numeric_limits<std::uint32_t>::max()));
    EXPECT_EQ(periodFrames(1,48000),1u);EXPECT_EQ(periodFrames(100000,44100),441u);
}
TEST(WasapiCapabilities, RecognizesPackedAndPaddedInteger24AndRejectsUnknownSubformats) {
    auto client=makeFake<FakeAudioClient>();auto& f=client->mix;
    f.SubFormat=KSDATAFORMAT_SUBTYPE_PCM;f.Samples.wValidBitsPerSample=24;
    EXPECT_EQ(sampleKind(f.Format),SampleKind::S24Msb32);
    f.Format.wBitsPerSample=24;EXPECT_EQ(sampleKind(f.Format),SampleKind::S24Packed);
    f.SubFormat=GUID_NULL;EXPECT_FALSE(sampleKind(f.Format));
}
