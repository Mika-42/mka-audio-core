#include <gtest/gtest.h>
#include "fake_devices.hpp"
#include "../../src/backend/windows/detail/wasapi_session.hpp"
#include <chrono>
#include <future>
import mka.audio.convert;
#include "planar_callbacks.hpp"
using namespace wasapi_test;
using namespace std::chrono_literals;
namespace {
template<class Predicate>bool waitFor(Predicate predicate){auto end=std::chrono::steady_clock::now()+3s;while(!predicate()){if(std::chrono::steady_clock::now()>end)return false;std::this_thread::yield();}return true;}
struct Probe {std::atomic<unsigned> calls{0};std::atomic<bool> release{true};};
void process(void* user,const float* const*,UINT32,float* const*,UINT32,UINT32) noexcept {
    auto& probe=*static_cast<Probe*>(user);++probe.calls;while(!probe.release.load()){}
}
}
TEST_F(DevicesFixture, StartsOnlyAfterNativeSuccessAndJoinsBeforeStopReturns) {
    auto d=add(L"output",eRender);Probe probe;ControlThread control(api());Session session(control);
    ASSERT_EQ(session.open({"output",eRender,2,48000,256}),S_OK);auto cb=converters();cb.user=&probe;cb.process=process;
    ASSERT_EQ(session.start(cb),S_OK);EXPECT_EQ(probe.calls.load(),0u);EXPECT_TRUE(session.hasWorker());
    SetEvent(session.audioEvent());ASSERT_TRUE(waitFor([&]{return probe.calls.load()>0;}));
    ASSERT_EQ(session.stop(),S_OK);EXPECT_FALSE(session.hasWorker());const auto count=probe.calls.load();
    SetEvent(session.audioEvent());EXPECT_EQ(probe.calls.load(),count);
    ASSERT_EQ(session.start(cb),S_OK);SetEvent(session.audioEvent());ASSERT_TRUE(waitFor([&]{return probe.calls.load()>count;}));
    ASSERT_EQ(session.stop(),S_OK);EXPECT_EQ(session.close(),S_OK);d.Reset();
}
TEST_F(DevicesFixture, NativeStartFailureJoinsThePreparedWorkerWithoutInvokingUserCode) {
    auto d=add(L"output",eRender);Probe probe;ControlThread control(api());Session session(control);
    ASSERT_EQ(session.open({"output",eRender,2,48000,256}),S_OK);auto cb=converters();cb.user=&probe;cb.process=process;
    d->client->startResult=E_FAIL;EXPECT_EQ(session.start(cb),E_FAIL);EXPECT_EQ(probe.calls.load(),0u);EXPECT_FALSE(session.hasWorker());
    d->client->startResult=S_OK;ASSERT_EQ(session.start(cb),S_OK);EXPECT_EQ(session.stop(),S_OK);d.Reset();
}
TEST_F(DevicesFixture, StopWaitsForAnInFlightCallbackBeforeItsContextCanBeFreed) {
    auto d=add(L"output",eRender);auto probe=std::make_unique<Probe>();ControlThread control(api());Session session(control);
    ASSERT_EQ(session.open({"output",eRender,2,48000,256}),S_OK);auto cb=converters();cb.user=probe.get();cb.process=process;
    probe->release=false;ASSERT_EQ(session.start(cb),S_OK);SetEvent(session.audioEvent());
    const bool entered=waitFor([&]{return probe->calls.load()!=0;});
    if(!entered){probe->release=true;FAIL()<<"callback did not start";}
    auto stopped=std::async(std::launch::async,[&]{return session.stop();});
    const auto status=stopped.wait_for(30ms);probe->release=true;
    EXPECT_EQ(status,std::future_status::timeout);ASSERT_EQ(stopped.get(),S_OK);
    EXPECT_FALSE(session.hasWorker());probe.reset();EXPECT_EQ(session.close(),S_OK);d.Reset();
}
TEST_F(DevicesFixture, CaptureDrainsBoundedPacketsAndDestroyingAnActiveSessionJoinsTheWorker) {
    auto d=add(L"input",eCapture);Probe probe;ControlThread control(api());
    {
        Session session(control);ASSERT_EQ(session.open({"input",eCapture,2,48000,256}),S_OK);
        d->client->capture->packets[0]={7,0};d->client->capture->packets[1]={9,AUDCLNT_BUFFERFLAGS_SILENT};d->client->capture->count=2;
        auto cb=converters();cb.user=&probe;cb.process=process;ASSERT_EQ(session.start(cb),S_OK);
        SetEvent(session.audioEvent());ASSERT_TRUE(waitFor([&]{return probe.calls.load()==2;}));
    }
    EXPECT_EQ(probe.calls.load(),2u);d.Reset();
}
