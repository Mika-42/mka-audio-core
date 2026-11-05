#include <gtest/gtest.h>
#include "fake_devices.hpp"
#include <chrono>
#include <future>
#include "../../examples/dsp/gain.hpp"
import mka.audio.backend.wasapi;
#include "public_fixture.hpp"
using namespace wasapi_test;
using namespace mka::audio::core;
namespace {
struct Probe {
    std::atomic<unsigned> calls{0},bad{0},failedEvents{0},xrunEvents{0};
    std::atomic<DWORD> audioThread{0},eventThread{0};
};
void process(void* user,const AudioProcessContext& c) noexcept {
    auto& p=*static_cast<Probe*>(user);p.audioThread=GetCurrentThreadId();
    if(!c.frames || c.input.count || c.input.channels || c.output.count!=2 || !c.output.channels)++p.bad;
    else for(UINT32 ch=0;ch<c.output.count;++ch)for(UINT32 i=0;i<c.frames;++i){if(c.output.channels[ch][i]!=0)++p.bad;c.output.channels[ch][i]=0.0f;}
    ++p.calls;
}
void event(void* user,const Event& e) noexcept {
    auto& p=*static_cast<Probe*>(user);p.eventThread=GetCurrentThreadId();
    if(e.type==EventType::Failed)++p.failedEvents;else ++p.xrunEvents;
}
template<class P>bool waitFor(P predicate){auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(!predicate()){if(std::chrono::steady_clock::now()>end)return false;std::this_thread::yield();}return true;}
}
TEST_F(PublicFixture, PublicDiscoveryPreservesDirectionalIdentityAndPortableCapabilities) {
    add(L"output",eRender);add(L"input",eCapture);Wasapi backend;
    const auto endpoints=backend.getEndPoints();ASSERT_EQ(endpoints.size(),2u);
    EXPECT_EQ(endpoints[0].id,"output");ASSERT_TRUE(endpoints[0].output);EXPECT_FALSE(endpoints[0].input);
    EXPECT_EQ(endpoints[0].output->minChannels,2u);EXPECT_EQ(endpoints[0].output->maxChannels,2u);
    EXPECT_EQ(endpoints[0].output->formats,std::vector{Format::Float32});EXPECT_EQ(endpoints[0].output->sampleRates,std::vector<SampleRate>{48000});
    EXPECT_EQ(endpoints[0].output->bufferSizes,std::vector<BufferSize>{480});
    EXPECT_TRUE(endpoints[1].input);EXPECT_FALSE(endpoints[1].output);
}
TEST_F(PublicFixture, PublicErrorMappingKeepsClosedStateReusable) {
    auto d=add(L"output",eRender);Wasapi backend;
    auto expect=[&](ErrorType error){const auto result=backend.open(config());ASSERT_FALSE(result);EXPECT_EQ(result.error(),error);};
    createResult=E_ACCESSDENIED;expect(ErrorType::EndpointUnavailable);createResult=S_OK;
    d->activateResult=AUDCLNT_E_DEVICE_INVALIDATED;expect(ErrorType::EndpointUnavailable);d->activateResult=S_OK;
    d->client->supportResult=S_FALSE;expect(ErrorType::FormatNotSupported);d->client->supportResult=S_OK;
    d->client->initializeResult=AUDCLNT_E_BUFFER_SIZE_ERROR;expect(ErrorType::BufferSizeNotSupported);
    d->client->initializeResult=E_OUTOFMEMORY;expect(ErrorType::ConfigurationFailed);d->client->initializeResult=S_OK;
    auto duplex=config();duplex.direction=Direction::Duplex;duplex.inputChannels=2;
    EXPECT_EQ(backend.open(duplex).error(),ErrorType::ConfigurationFailed);
    ASSERT_TRUE(backend.open(config()));EXPECT_EQ(backend.open(config()).error(),ErrorType::InvalidState);ASSERT_TRUE(backend.close());d.Reset();
}
TEST_F(PublicFixture, PublicStatusAndOffAudioEventsExposeXrunsAndOneTerminalFailure) {
    auto d=add(L"output",eRender);Probe p;Wasapi backend;
    ASSERT_TRUE(backend.setProcessFunction(process,&p));ASSERT_TRUE(backend.setEventHandler(event,&p));
    ASSERT_TRUE(backend.open(config()));ASSERT_TRUE(backend.start());
    EXPECT_EQ(backend.setProcessFunction(nullptr).error(),ErrorType::InvalidState);
    d->client->render->getResult=AUDCLNT_E_BUFFER_ERROR;SetEvent(d->client->audioEvent);
    ASSERT_TRUE(waitFor([&]{return backend.status().xruns>0 && p.xrunEvents.load()>0;}));
    d->client->render->getResult=S_OK;SetEvent(d->client->audioEvent);ASSERT_TRUE(waitFor([&]{return p.calls.load()>0;}));
    EXPECT_EQ(p.bad.load(),0u);EXPECT_NE(p.audioThread.load(),GetCurrentThreadId());EXPECT_NE(backend.status().realtime,RealtimeState::Unknown);
    d->client->paddingResult=AUDCLNT_E_DEVICE_INVALIDATED;
    ASSERT_TRUE(waitFor([&]{return backend.status().failed && p.failedEvents.load()==1;}));
    EXPECT_NE(p.eventThread.load(),p.audioThread.load());const auto calls=p.calls.load();
    ASSERT_TRUE(backend.stop());ASSERT_TRUE(backend.close());EXPECT_EQ(p.calls.load(),calls);d.Reset();
}
TEST_F(PublicFixture, CallerStaAndOtherControlThreadsCanUseTheSamePortableBackend) {
    add(L"output",eRender);
    const auto initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    ASSERT_TRUE(SUCCEEDED(initialized));
    struct Balance{~Balance(){CoUninitialize();}}balance;
    Wasapi backend;ASSERT_TRUE(backend.open(config()));
    auto operations=std::async(std::launch::async,[&]{if(!backend.start())return false;if(!backend.stop())return false;return backend.close().has_value();});
    EXPECT_TRUE(operations.get());
}
class WasapiPublicStress:public PublicFixture{};
TEST_F(WasapiPublicStress, ThousandCompleteCyclesFreeThePreviousCallbackContextAfterEachStop) {
    auto d=add(L"output",eRender);std::unique_ptr<Probe> context;Wasapi backend;
    for(unsigned cycle=0;cycle<1000;++cycle){
        SCOPED_TRACE(cycle);context=std::make_unique<Probe>();
        ASSERT_TRUE(backend.setProcessFunction(process,context.get()));ASSERT_TRUE(backend.open(config()));ASSERT_TRUE(backend.start());
        SetEvent(d->client->audioEvent);ASSERT_TRUE(waitFor([&]{return context->calls.load()>0;}));
        ASSERT_TRUE(backend.stop());ASSERT_TRUE(backend.close());EXPECT_EQ(context->bad.load(),0u);context.reset();
    }
    EXPECT_EQ(d->client->starts,1000u);EXPECT_EQ(d->client->stops,1000u);d.Reset();
}

TEST_F(PublicFixture, FailedNativeStopAndResetKeepRunningGuardsUntilCleanupActuallySucceeds) {
    auto d=add(L"output",eRender);
    for(int stage=0;stage<2;++stage) {
        Wasapi backend;ASSERT_TRUE(backend.open(config()));ASSERT_TRUE(backend.start());
        const auto beforeStops=d->client->stops,beforeResets=d->client->resets;
        if(stage==0)d->client->stopResult=E_FAIL;else d->client->resetResult=E_FAIL;
        for(int attempt=0;attempt<2;++attempt) {
            const auto stopped=backend.stop();ASSERT_FALSE(stopped);EXPECT_EQ(stopped.error(),ErrorType::ConfigurationFailed);
            EXPECT_EQ(backend.setProcessFunction(nullptr).error(),ErrorType::InvalidState);
            EXPECT_EQ(backend.close().error(),ErrorType::InvalidState);
        }
        if(stage==0)EXPECT_EQ(d->client->stops,beforeStops+2);else EXPECT_EQ(d->client->resets,beforeResets+2);
        d->client->stopResult=S_OK;d->client->resetResult=S_OK;
        ASSERT_TRUE(backend.stop());ASSERT_TRUE(backend.setProcessFunction(nullptr));ASSERT_TRUE(backend.close());
    }
    d.Reset();
}

TEST_F(PublicFixture, SwitchesExactEndpointIdsAfterCloseAndReplacesFreedDspContext) {
    auto first=add(L"output-a",eRender);auto second=add(L"output-b",eRender);
    auto node=std::make_unique<mka_example::Gain>();Wasapi backend;
    auto bind=[&]{return backend.setProcessFunction(+[](void* user,const AudioProcessContext& context) noexcept {
        static_cast<mka_example::Gain*>(user)->process(context);
    },node.get());};
    ASSERT_TRUE(bind());ASSERT_TRUE(backend.open(config("output-a")));ASSERT_TRUE(backend.start());
    SetEvent(first->client->audioEvent);ASSERT_TRUE(waitFor([&]{return node->calls.load()>0;}));
    EXPECT_EQ(backend.open(config("output-b")).error(),ErrorType::InvalidState);
    ASSERT_TRUE(backend.stop());ASSERT_TRUE(backend.close());node.reset();
    EXPECT_EQ(backend.open(config("missing")).error(),ErrorType::EndpointUnavailable);
    node=std::make_unique<mka_example::Gain>();ASSERT_TRUE(bind());
    ASSERT_TRUE(backend.open(config("output-b")));ASSERT_TRUE(backend.start());
    SetEvent(second->client->audioEvent);ASSERT_TRUE(waitFor([&]{return node->calls.load()>0;}));
    ASSERT_TRUE(backend.stop());ASSERT_TRUE(backend.close());
    EXPECT_EQ(first->client->starts,1u);EXPECT_EQ(second->client->starts,1u);
    first.Reset();second.Reset();
}
