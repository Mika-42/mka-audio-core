#include <gtest/gtest.h>
#include "fake_devices.hpp"
#include "../../src/backend/windows/detail/wasapi_session.hpp"
using namespace wasapi_test;
TEST_F(DevicesFixture, ReservesTheExactRequestedEndpointAndReopensAfterClose) {
    add(L"output",eRender);add(L"input",eCapture);ControlThread control(api());Session session(control);
    EXPECT_EQ(session.open({"output",eRender,2,48000,256,SampleKind::F32}),S_OK);
    EXPECT_EQ(session.close(),S_OK);
    EXPECT_EQ(session.open({"input",eCapture,2,48000,256,SampleKind::F32}),S_OK);
    EXPECT_EQ(session.close(),S_OK);
    EXPECT_EQ(session.open({"",eRender,2,48000,256,SampleKind::F32}),S_OK);
}
TEST_F(DevicesFixture, MissingWrongDirectionAndNativeActivationFailuresDoNotPoisonRetry) {
    auto device=add(L"output",eRender);ControlThread control(api());Session session(control);
    EXPECT_EQ(session.open({"missing",eRender,2,48000,256}),HRESULT_FROM_WIN32(ERROR_NOT_FOUND));
    EXPECT_EQ(session.open({"output",eCapture,2,48000,256}),AUDCLNT_E_WRONG_ENDPOINT_TYPE);
    device->state=DEVICE_STATE_DISABLED;
    EXPECT_EQ(session.open({"output",eRender,2,48000,256}),AUDCLNT_E_DEVICE_INVALIDATED);
    device->state=DEVICE_STATE_ACTIVE;
    device->activateResult=E_ACCESSDENIED;
    EXPECT_EQ(session.open({"output",eRender,2,48000,256}),E_ACCESSDENIED);
    device->activateResult=S_OK;EXPECT_EQ(session.open({"output",eRender,2,48000,256}),S_OK);
    EXPECT_TRUE(FAILED(session.open({"output",eRender,2,48000,256})));
    EXPECT_EQ(session.close(),S_OK);device.Reset();
}
TEST_F(DevicesFixture, InvalidCountsAndMalformedIdsAreRejectedBeforeCommit) {
    add(L"output",eRender);ControlThread control(api());Session session(control);
    for (auto request : {Request{"output",eRender,0,48000,256},Request{"output",eRender,65536,48000,256},
        Request{"output",eRender,2,0,256},Request{"output",eRender,2,48000,0}})
        EXPECT_EQ(session.open(request),E_INVALIDARG);
    EXPECT_EQ(session.open({std::string("output\0other",12),eRender,2,48000,256}),HRESULT_FROM_WIN32(ERROR_NOT_FOUND));
    EXPECT_EQ(session.open({"output",eRender,2,48000,256}),S_OK);
}
