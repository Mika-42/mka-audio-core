#include <gtest/gtest.h>
#include "fake_devices.hpp"
#include "../../src/backend/windows/detail/wasapi_session.hpp"
using namespace wasapi_test;
namespace {
struct Probe {unsigned calls=0;const BYTE* bytes=nullptr;UINT32 frames=0;DWORD flags=0;};
void packet(void* user,const BYTE* bytes,UINT32 frames,DWORD flags) noexcept {
    auto& probe=*static_cast<Probe*>(user);++probe.calls;probe.bytes=bytes;probe.frames=frames;probe.flags=flags;
}
}
TEST_F(DevicesFixture, CapturesBorrowedPacketsAndSilentNullBuffersWithExactlyOneRelease) {
    auto d=add(L"input",eCapture);auto& capture=*d->client->capture.Get();
    capture.packets[0]={17,0};capture.packets[1]={9,AUDCLNT_BUFFERFLAGS_SILENT};capture.count=2;
    ControlThread control(api());Session session(control);ASSERT_EQ(session.open({"input",eCapture,2,48000,256}),S_OK);
    EXPECT_EQ(d->client->captureServiceCalls,1u);EXPECT_EQ(d->client->renderServiceCalls,0u);
    Probe probe;EXPECT_EQ(session.captureOnce(packet,&probe),S_OK);EXPECT_EQ(probe.calls,1u);EXPECT_NE(probe.bytes,nullptr);EXPECT_EQ(probe.frames,17u);
    EXPECT_EQ(session.captureOnce(packet,&probe),S_OK);EXPECT_EQ(probe.calls,2u);EXPECT_EQ(probe.bytes,nullptr);EXPECT_EQ(probe.frames,9u);
    EXPECT_EQ(capture.gets,2u);EXPECT_EQ(capture.releases,2u);EXPECT_EQ(session.captureOnce(packet,&probe),S_FALSE);d.Reset();
}
TEST_F(DevicesFixture, InvalidBorrowedDataNeverReachesTheCallbackButIsReleased) {
    auto d=add(L"input",eCapture);auto& capture=*d->client->capture.Get();
    ControlThread control(api());Session session(control);ASSERT_EQ(session.open({"input",eCapture,2,48000,256}),S_OK);
    Probe probe;
    capture.packets[0]={17,0};capture.count=1;capture.nullBuffer=true;
    EXPECT_EQ(session.captureOnce(packet,&probe),E_POINTER);EXPECT_EQ(capture.releases,1u);EXPECT_EQ(probe.calls,0u);
    capture.head=0;capture.nullBuffer=false;capture.packets[0]={481,0,17};
    EXPECT_EQ(session.captureOnce(packet,&probe),E_UNEXPECTED);EXPECT_EQ(capture.released,481u);EXPECT_EQ(probe.calls,0u);
    capture.head=0;capture.packets[0]={1,0x80000000u};
    EXPECT_EQ(session.captureOnce(packet,&probe),E_UNEXPECTED);EXPECT_EQ(probe.calls,0u);d.Reset();
}
TEST_F(DevicesFixture, NativeFailuresDoNotReleaseBuffersThatWereNeverAcquired) {
    auto d=add(L"input",eCapture);auto& capture=*d->client->capture.Get();capture.packets[0]={17,0};capture.count=1;
    ControlThread control(api());Session session(control);ASSERT_EQ(session.open({"input",eCapture,2,48000,256}),S_OK);
    Probe probe;capture.nextResult=AUDCLNT_E_DEVICE_INVALIDATED;
    EXPECT_EQ(session.captureOnce(packet,&probe),AUDCLNT_E_DEVICE_INVALIDATED);EXPECT_EQ(capture.gets,0u);EXPECT_EQ(capture.releases,0u);
    capture.nextResult=S_OK;capture.getResult=AUDCLNT_S_BUFFER_EMPTY;
    EXPECT_EQ(session.captureOnce(packet,&probe),S_FALSE);EXPECT_EQ(capture.releases,0u);
    capture.getResult=E_FAIL;EXPECT_EQ(session.captureOnce(packet,&probe),E_FAIL);EXPECT_EQ(capture.releases,0u);
    capture.getResult=S_OK;capture.releaseResult=E_FAIL;EXPECT_EQ(session.captureOnce(packet,&probe),E_FAIL);EXPECT_EQ(capture.releases,1u);d.Reset();
}
TEST_F(DevicesFixture, CaptureServiceFailureRollsBackAndRetryCanAcquireIt) {
    auto d=add(L"input",eCapture);ControlThread control(api());Session session(control);
    d->client->captureServiceResult=E_NOINTERFACE;EXPECT_EQ(session.open({"input",eCapture,2,48000,256}),E_NOINTERFACE);
    d->client->captureServiceResult=S_OK;EXPECT_EQ(session.open({"input",eCapture,2,48000,256}),S_OK);d.Reset();
}
