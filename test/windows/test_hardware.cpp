#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <expected>
#include <thread>
import mka.audio.backend.wasapi;
namespace {
using namespace mka::audio::core;
struct Probe{std::atomic<unsigned>calls{0},bad{0};bool capture=false;};
void process(void* user,const AudioProcessContext& c) noexcept {
    auto& p=*static_cast<Probe*>(user);
    if(!c.frames)++p.bad;
    else if(p.capture){if(!c.input.count || !c.input.channels || c.output.count || c.output.channels)++p.bad;else{[[maybe_unused]]volatile float value=c.input.channels[0][0];}}
    else {if(c.input.count || c.input.channels || !c.output.count || !c.output.channels)++p.bad;else for(std::uint32_t ch=0;ch<c.output.count;++ch)for(std::uint32_t i=0;i<c.frames;++i)c.output.channels[ch][i]=0.0f;}
    ++p.calls;
}
void exercise(bool capture){
    Probe p;p.capture=capture;Wasapi backend;
    const auto devices=backend.getEndPoints();
    const Endpoint* chosen=nullptr;
    for(const auto& d:devices)if(capture?d.input.has_value():d.output.has_value()){chosen=&d;break;}
    if(!chosen)GTEST_SKIP()<<"No active WASAPI "<<(capture?"capture":"render")<<" endpoint/audio service on this Windows runner";
    const auto& caps=capture?*chosen->input:*chosen->output;
    ASSERT_FALSE(caps.formats.empty());ASSERT_FALSE(caps.sampleRates.empty());
    EndpointConfig config{chosen->id,capture?Direction::Input:Direction::Output,
        capture?caps.maxChannels:0,capture?0:caps.maxChannels,caps.sampleRates[0],caps.formats[0],caps.bufferSizes.empty()?256:caps.bufferSizes[0]};
    const auto opened=backend.open(config);
    if(!opened && opened.error()==ErrorType::EndpointUnavailable)GTEST_SKIP()<<"The selected endpoint became unavailable before open";
    ASSERT_TRUE(opened);ASSERT_TRUE(backend.setProcessFunction(process,&p));ASSERT_TRUE(backend.start());
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!p.calls.load() && !backend.status().failed && std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(2));
    EXPECT_GT(p.calls.load(),0u);EXPECT_EQ(p.bad.load(),0u);ASSERT_TRUE(backend.stop());ASSERT_TRUE(backend.close());
}
}
TEST(WasapiHardware, RenderOnAnActualEndpointOrExplicitlySkipUnavailableHardware){exercise(false);}
TEST(WasapiHardware, CaptureOnAnActualEndpointOrExplicitlySkipUnavailableHardware){exercise(true);}
