#include <gtest/gtest.h>
#include "fake_devices.hpp"
#include "../../src/backend/windows/detail/wasapi_session.hpp"
#include <chrono>
import mka.audio.convert;
#include "planar_callbacks.hpp"
using namespace wasapi_test;
namespace {
struct Probe {std::atomic<unsigned> calls{0},failures{0},xruns{0};};
void process(void* user,const float* const*,UINT32,float* const*,UINT32,UINT32) noexcept {++static_cast<Probe*>(user)->calls;}
Callbacks callbacks(Probe& p){auto cb=converters();cb.user=&p;cb.process=process;cb.failed=+[](void* p)noexcept{++static_cast<Probe*>(p)->failures;};cb.xrun=+[](void* p)noexcept{++static_cast<Probe*>(p)->xruns;};return cb;}
template<class P>bool waitFor(P predicate){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);while(!predicate()){if(std::chrono::steady_clock::now()>end)return false;std::this_thread::yield();}return true;}
}
TEST_F(DevicesFixture, EveryRenderInvalidationDisablesFurtherCallbacksAndStillAllowsStopCloseReopen) {
    auto d=add(L"output",eRender);Probe p;ControlThread control(api());Session session(control);
    for(int stage=0;stage<3;++stage) {
        p.calls=0;p.failures=0;
        ASSERT_EQ(session.open({"output",eRender,2,48000,256}),S_OK);ASSERT_EQ(session.start(callbacks(p)),S_OK);
        if(stage==0)d->client->paddingResult=AUDCLNT_E_DEVICE_INVALIDATED;
        if(stage==1)d->client->render->getResult=AUDCLNT_E_RESOURCES_INVALIDATED;
        if(stage==2)d->client->render->releaseResult=AUDCLNT_E_SERVICE_NOT_RUNNING;
        SetEvent(session.audioEvent());ASSERT_TRUE(waitFor([&]{return p.failures.load()==1;}));
        EXPECT_TRUE(session.failed());const auto calls=p.calls.load();
        SetEvent(session.audioEvent());EXPECT_EQ(p.calls.load(),calls);
        d->client->stopResult=AUDCLNT_E_DEVICE_INVALIDATED;d->client->resetResult=AUDCLNT_E_DEVICE_INVALIDATED;
        EXPECT_EQ(session.stop(),S_OK);EXPECT_FALSE(session.hasWorker());EXPECT_EQ(session.close(),S_OK);
        d->client->paddingResult=S_OK;d->client->render->getResult=S_OK;d->client->render->releaseResult=S_OK;
        d->client->stopResult=S_OK;d->client->resetResult=S_OK;
    }
    ASSERT_EQ(session.open({"output",eRender,2,48000,256}),S_OK);ASSERT_EQ(session.start(callbacks(p)),S_OK);
    EXPECT_FALSE(session.failed());EXPECT_EQ(session.lastFailure(),S_OK);EXPECT_EQ(session.stop(),S_OK);d.Reset();
}
TEST_F(DevicesFixture, CaptureInvalidationIsDetectedEvenWhenTheDeviceStopsSignalingEvents) {
    auto d=add(L"input",eCapture);Probe p;ControlThread control(api());Session session(control);
    ASSERT_EQ(session.open({"input",eCapture,2,48000,256}),S_OK);ASSERT_EQ(session.start(callbacks(p)),S_OK);
    d->client->capture->nextResult=AUDCLNT_E_DEVICE_INVALIDATED;
    // No SetEvent: the bounded health wait must still notice the lost device.
    ASSERT_TRUE(waitFor([&]{return p.failures.load()==1;}));EXPECT_EQ(p.calls.load(),0u);
    EXPECT_EQ(session.lastFailure(),AUDCLNT_E_DEVICE_INVALIDATED);EXPECT_EQ(session.stop(),S_OK);d.Reset();
}
TEST_F(DevicesFixture, CaptureGetAndReleaseFailuresAreTerminalAndReportedExactlyOnce) {
    auto d=add(L"input",eCapture);Probe p;ControlThread control(api());Session session(control);
    for(int stage=0;stage<2;++stage){
        p.calls=0;p.failures=0;auto& input=*d->client->capture.Get();input.head=0;input.count=1;input.packets[0]={7,0};
        ASSERT_EQ(session.open({"input",eCapture,2,48000,256}),S_OK);ASSERT_EQ(session.start(callbacks(p)),S_OK);
        if(stage==0)input.getResult=AUDCLNT_E_DEVICE_INVALIDATED;else input.releaseResult=AUDCLNT_E_DEVICE_INVALIDATED;
        SetEvent(session.audioEvent());ASSERT_TRUE(waitFor([&]{return p.failures.load()==1;}));
        EXPECT_EQ(p.calls.load(),stage==0?0u:1u);EXPECT_EQ(session.stop(),S_OK);EXPECT_EQ(session.close(),S_OK);
        input.getResult=S_OK;input.releaseResult=S_OK;
    }
    d.Reset();
}
TEST_F(DevicesFixture, TemporaryNativeBufferErrorsCountAsXrunsWithoutPoisoningTheStream) {
    auto d=add(L"output",eRender);Probe p;ControlThread control(api());Session session(control);
    ASSERT_EQ(session.open({"output",eRender,2,48000,256}),S_OK);ASSERT_EQ(session.start(callbacks(p)),S_OK);
    d->client->render->getResult=AUDCLNT_E_BUFFER_ERROR;SetEvent(session.audioEvent());
    ASSERT_TRUE(waitFor([&]{return p.xruns.load()>0;}));EXPECT_FALSE(session.failed());EXPECT_EQ(p.failures.load(),0u);
    d->client->render->getResult=S_OK;SetEvent(session.audioEvent());ASSERT_TRUE(waitFor([&]{return p.calls.load()>0;}));
    EXPECT_EQ(session.stop(),S_OK);d.Reset();
}
TEST_F(DevicesFixture, CaptureDiscontinuityCountsOneXrunAndContinuesWithValidData) {
    auto d=add(L"input",eCapture);Probe p;ControlThread control(api());Session session(control);
    auto& input=*d->client->capture.Get();input.packets[0]={7,AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY|AUDCLNT_BUFFERFLAGS_SILENT};input.count=1;
    ASSERT_EQ(session.open({"input",eCapture,2,48000,256}),S_OK);ASSERT_EQ(session.start(callbacks(p)),S_OK);SetEvent(session.audioEvent());
    ASSERT_TRUE(waitFor([&]{return p.calls.load()==1;}));EXPECT_EQ(p.xruns.load(),1u);EXPECT_FALSE(session.failed());EXPECT_EQ(session.stop(),S_OK);d.Reset();
}
