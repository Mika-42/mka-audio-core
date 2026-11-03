#include <gtest/gtest.h>
#include "fake_devices.hpp"
#include "../../src/backend/windows/detail/wasapi_session.hpp"
#include <array>
#include <cmath>
import mka.audio.convert;
#include "planar_callbacks.hpp"
using namespace wasapi_test;
namespace {
struct Probe {unsigned calls=0,bad=0;UINT32 expectedFrames=7;bool silent=false;};
void render(void* user,const float* const* input,UINT32 inputs,float* const* output,UINT32 outputs,UINT32 frames) noexcept {
    auto& p=*static_cast<Probe*>(user);++p.calls;
    if(input || inputs || !output || outputs!=2 || frames!=p.expectedFrames || output[0]==output[1]) {++p.bad;return;}
    for(UINT32 i=0;i<frames;++i) {if(output[0][i]!=0 || output[1][i]!=0)++p.bad;output[0][i]=0.25f;output[1][i]=-0.5f;}
}
void capture(void* user,const float* const* input,UINT32 inputs,float* const* output,UINT32 outputs,UINT32 frames) noexcept {
    auto& p=*static_cast<Probe*>(user);++p.calls;
    if(!input || inputs!=2 || output || outputs || frames!=p.expectedFrames || input[0]==input[1]) {++p.bad;return;}
    for(UINT32 i=0;i<frames;++i)
        if(std::abs(input[0][i]-(p.silent?0.0f:0.25f))>0.0001f || std::abs(input[1][i]-(p.silent?0.0f:-0.5f))>0.0001f)++p.bad;
}
}
TEST_F(DevicesFixture, EveryNativeLayoutProducesBoundedPlanarOutputAndPreservesUnusedNativeBytes) {
    auto d=add(L"output",eRender);ControlThread control(api());Session session(control);
    for(auto kind:{SampleKind::S16,SampleKind::S24Packed,SampleKind::S24Msb32,SampleKind::S32,SampleKind::F32,SampleKind::F64}) {
        ASSERT_EQ(session.open({"output",eRender,2,48000,256,kind}),S_OK);
        d->client->padding=473;auto& native=*d->client->render.Get();std::fill(native.bytes.begin(),native.bytes.end(),0xA5);
        Probe probe;auto cb=converters();cb.user=&probe;cb.process=render;
        ASSERT_EQ(session.renderOnce(cb),S_OK);EXPECT_EQ(probe.calls,1u);EXPECT_EQ(probe.bad,0u);
        EXPECT_EQ(native.released,7u);EXPECT_EQ(native.releaseFlags,0u);
        const auto width=session.nativeFormat().Format.wBitsPerSample/8;const auto stride=width*2;
        for(UINT32 channel=0;channel<2;++channel) {
            std::array<float,7> values{};readSamples<Conversion>(kind,native.bytes.data()+channel*width,stride,values.data(),7);
            for(float v:values)EXPECT_NEAR(v,channel?-0.5f:0.25f,0.0001f);
        }
        for(std::size_t i=7*stride;i<native.bytes.size();++i)ASSERT_EQ(native.bytes[i],0xA5);
        ASSERT_EQ(session.close(),S_OK);
    }
    d.Reset();
}
TEST_F(DevicesFixture, NativeRenderIsSilentWithoutCallbackAndRejectsBadPaddingOrPointers) {
    auto d=add(L"output",eRender);ControlThread control(api());Session session(control);
    ASSERT_EQ(session.open({"output",eRender,2,48000,256}),S_OK);auto& native=*d->client->render.Get();
    EXPECT_EQ(session.renderOnce({}),S_OK);EXPECT_EQ(native.releaseFlags,AUDCLNT_BUFFERFLAGS_SILENT);
    const auto gets=native.gets;d->client->padding=480;EXPECT_EQ(session.renderOnce({}),S_FALSE);EXPECT_EQ(native.gets,gets);
    d->client->padding=481;EXPECT_EQ(session.renderOnce({}),E_UNEXPECTED);EXPECT_EQ(native.gets,gets);
    d->client->padding=0;native.nullBuffer=true;EXPECT_EQ(session.renderOnce({}),E_POINTER);EXPECT_EQ(native.released,480u);d.Reset();
}
TEST_F(DevicesFixture, EveryCaptureLayoutHasReadOnlyPlanarInputAndSilentPacketsAreZero) {
    auto d=add(L"input",eCapture);ControlThread control(api());Session session(control);
    for(auto kind:{SampleKind::S16,SampleKind::S24Packed,SampleKind::S24Msb32,SampleKind::S32,SampleKind::F32,SampleKind::F64}) {
        ASSERT_EQ(session.open({"input",eCapture,2,48000,256,kind}),S_OK);
        auto& native=*d->client->capture.Get();native.head=0;native.count=2;
        native.packets[0]={7,0};native.packets[1]={7,AUDCLNT_BUFFERFLAGS_SILENT};
        const auto width=session.nativeFormat().Format.wBitsPerSample/8;
        std::array<float,7> left,right;left.fill(0.25f);right.fill(-0.5f);
        writeSamples<Conversion>(kind,native.bytes.data(),width*2,left.data(),7);
        writeSamples<Conversion>(kind,native.bytes.data()+width,width*2,right.data(),7);
        Probe p;auto cb=converters();cb.user=&p;cb.process=capture;
        EXPECT_EQ(session.processCapture(cb),S_OK);EXPECT_EQ(p.bad,0u);p.silent=true;
        EXPECT_EQ(session.processCapture(cb),S_OK);EXPECT_EQ(p.bad,0u);EXPECT_EQ(p.calls,2u);
        ASSERT_EQ(session.close(),S_OK);
    }
    d.Reset();
}
TEST(WasapiPlanar, NativeValid24BitsAreHighAlignedAndPaddingIsIgnored) {
    std::array<std::uint32_t,4> native{0x7FFFFFFFu,0x80000055u,0x400000AAu,0xFFFFFF12u};
    std::array<float,4> read{};
    readSamples<Conversion>(SampleKind::S24Msb32,reinterpret_cast<const BYTE*>(native.data()),4,read.data(),4);
    EXPECT_EQ(read[0],8388607.0f/8388608.0f);EXPECT_EQ(read[1],-1.0f);EXPECT_EQ(read[2],0.5f);EXPECT_EQ(read[3],-1.0f/8388608.0f);
    const std::array<float,4> input{1.0f,-1.0f,0.5f,0.0f};
    writeSamples<Conversion>(SampleKind::S24Msb32,reinterpret_cast<BYTE*>(native.data()),4,input.data(),4);
    EXPECT_EQ(native[0],0x7FFFFF00u);EXPECT_EQ(native[1],0x80000100u);EXPECT_EQ(native[2],0x40000000u);EXPECT_EQ(native[3],0u);
}
