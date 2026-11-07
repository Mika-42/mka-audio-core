#include <gtest/gtest.h>
#include "fake_devices.hpp"
#include "../../src/backend/windows/detail/wasapi_session.hpp"
using namespace wasapi_test;
TEST_F(DevicesFixture, InitializesSharedEventTransportWithExactFormatAndZeroPeriodicity) {
    auto d=add(L"output",eRender);d->client->bufferFrames=960;ControlThread control(api());Session session(control);
    ASSERT_EQ(session.open({"output",eRender,2,48000,256}),S_OK);
    EXPECT_EQ(d->client->initializedMode,AUDCLNT_SHAREMODE_SHARED);
    EXPECT_EQ(d->client->initializedFlags,AUDCLNT_STREAMFLAGS_EVENTCALLBACK);
    EXPECT_EQ(d->client->duration,53334);EXPECT_EQ(d->client->periodicity,0);
    EXPECT_EQ(d->client->initializedFormat.Format.nSamplesPerSec,48000u);
    EXPECT_EQ(d->client->initializedFormat.Format.nChannels,2u);
    EXPECT_EQ(session.capacity(),960u);d.Reset();
}
TEST_F(DevicesFixture, InitializationAndReadbackFailuresRollbackAndAllowRetry) {
    auto d=add(L"output",eRender);ControlThread control(api());Session session(control);
    d->client->initializeResult=AUDCLNT_E_BUFFER_SIZE_ERROR;
    EXPECT_EQ(session.open({"output",eRender,2,48000,256}),AUDCLNT_E_BUFFER_SIZE_ERROR);
    d->client->initializeResult=S_OK;d->client->bufferSizeResult=E_FAIL;
    EXPECT_EQ(session.open({"output",eRender,2,48000,256}),E_FAIL);
    d->client->bufferSizeResult=S_OK;d->client->bufferFrames=0;
    EXPECT_EQ(session.open({"output",eRender,2,48000,256}),AUDCLNT_E_BUFFER_SIZE_ERROR);
    d->client->bufferFrames=std::numeric_limits<UINT32>::max();
    EXPECT_EQ(session.open({"output",eRender,2,48000,256}),AUDCLNT_E_BUFFER_SIZE_ERROR);
    d->client->bufferFrames=480;EXPECT_EQ(session.open({"output",eRender,2,48000,256}),S_OK);d.Reset();
}
TEST(WasapiShared, DurationAndCapacityArithmeticRemainBoundedAtUint32Limits) {
    EXPECT_FALSE(frameDuration(0,48000));EXPECT_FALSE(frameDuration(256,0));
    EXPECT_EQ(frameDuration(1,48000),209);
    EXPECT_EQ(frameDuration(std::numeric_limits<std::uint32_t>::max(),1),42949672950000000LL);
    EXPECT_FALSE(validCapacity(0,2));EXPECT_FALSE(validCapacity(256,0));
    EXPECT_FALSE(validCapacity(std::numeric_limits<std::uint32_t>::max(),2));
    EXPECT_TRUE(validCapacity(4096,32));
}
