#include <gtest/gtest.h>
#include "fake_devices.hpp"
#include "../../src/backend/windows/detail/wasapi_session.hpp"
using namespace wasapi_test;
namespace {
std::atomic<int> handles{0};bool failEvent=false;
HANDLE WINAPI createEvent(LPSECURITY_ATTRIBUTES attrs,BOOL manual,BOOL initial,LPCWSTR name) {
    if(failEvent){SetLastError(ERROR_NOT_ENOUGH_MEMORY);return nullptr;}
    auto result=CreateEventW(attrs,manual,initial,name);if(result)++handles;return result;
}
BOOL WINAPI closeEvent(HANDLE event){--handles;return CloseHandle(event);}
EventApi events(){EventApi api;api.create=createEvent;api.close=closeEvent;return api;}
}
TEST_F(DevicesFixture, RegistersAnAutoResetAudioEventAndObtainsTheNativeRenderService) {
    auto d=add(L"output",eRender);ControlThread control(api());Session session(control,events());failEvent=false;
    ASSERT_EQ(session.open({"output",eRender,2,48000,256}),S_OK);
    ASSERT_NE(session.audioEvent(),nullptr);EXPECT_EQ(d->client->audioEvent,session.audioEvent());
    EXPECT_EQ(d->client->eventCalls,1u);EXPECT_EQ(d->client->renderServiceCalls,1u);
    SetEvent(session.audioEvent());EXPECT_EQ(WaitForSingleObject(session.audioEvent(),0),WAIT_OBJECT_0);
    EXPECT_EQ(WaitForSingleObject(session.audioEvent(),0),WAIT_TIMEOUT);
    EXPECT_EQ(d->client->render->gets,0u); // open never invokes processing
    EXPECT_EQ(session.close(),S_OK);EXPECT_EQ(handles.load(),0);d.Reset();
}
TEST_F(DevicesFixture, EventAndServiceFailuresReleaseHandlesAndAllowRetry) {
    auto d=add(L"output",eRender);ControlThread control(api());Session session(control,events());
    failEvent=true;EXPECT_EQ(session.open({"output",eRender,2,48000,256}),HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_MEMORY));EXPECT_EQ(handles.load(),0);
    failEvent=false;d->client->eventResult=E_FAIL;EXPECT_EQ(session.open({"output",eRender,2,48000,256}),E_FAIL);EXPECT_EQ(handles.load(),0);
    d->client->eventResult=S_OK;d->client->renderServiceResult=E_NOINTERFACE;
    EXPECT_EQ(session.open({"output",eRender,2,48000,256}),E_NOINTERFACE);EXPECT_EQ(handles.load(),0);
    d->client->renderServiceResult=S_OK;EXPECT_EQ(session.open({"output",eRender,2,48000,256}),S_OK);
    EXPECT_EQ(session.close(),S_OK);EXPECT_EQ(handles.load(),0);d.Reset();
}
