#include <gtest/gtest.h>
#include "fake_api.hpp"
#include "../../examples/dsp/gain.hpp"
import mka.audio.backend.coreaudio;
using namespace mka_coreaudio_detail;

TEST(CoreAudioProcess, InterleavedCaptureAndStereoRenderKeepDistinctChannelsAndSilence) {
    mac_test::FakeApi fake;Session session(fake.api);auto cfg=mac_test::config();cfg.input=true;cfg.inputs=2;
    ASSERT_TRUE(session.open(cfg));
    struct State {int calls=0;bool valid=true;} state;
    ASSERT_TRUE(session.start({&state,+[](void* user,const float* const* input,UInt32 inputs,float* const* output,UInt32 outputs,UInt32 frames) noexcept {
        auto& s=*static_cast<State*>(user);++s.calls;s.valid &= inputs==2 && outputs==2 && frames==128;
        s.valid &= input[0]!=output[0] && input[0][0]==0.125f && input[1][0]==-0.25f;
        for(UInt32 ch=0;ch<outputs;++ch)for(UInt32 i=0;i<frames;++i){s.valid &= output[ch][i]==0;output[ch][i]=input[ch][i];}
    }}));
    mac_test::Buffer input(2,128),output(2,128,1);
    for(int i=0;i<256;i+=2){input.data[i]=0.125f;input.data[i+1]=-0.25f;}
    fake.pump(&input.list,&output.list);EXPECT_EQ(input.data,output.data);
    fake.pump(&input.list,&output.list);EXPECT_EQ(state.calls,2);EXPECT_TRUE(state.valid);
    ASSERT_TRUE(session.stop());ASSERT_TRUE(session.close());
}
TEST(CoreAudioProcess, PlanarMultipleNativeBuffersAreFlattenedAndOutputClearedWithoutCallback) {
    ProcessState state;state.allocate(2,2,4);
    float left[]{1,2,3,4},right[]{-1,-2,-3,-4};
    struct TwoBuffers {UInt32 count;AudioBuffer buffers[2];};
    TwoBuffers native{2,{{1,sizeof(left),left},{1,sizeof(right),right}}};
    // Same ABI layout as the SDK's trailing AudioBuffer array.
    auto* list=reinterpret_cast<AudioBufferList*>(&native);
    UInt32 frames=0;ASSERT_TRUE(state.frames(list,2,frames));EXPECT_EQ(frames,4u);
    state.readInput(list,frames);EXPECT_EQ(state.input[0][3],4);EXPECT_EQ(state.input[1][0],-1);
    mac_test::Buffer output(2,4,1);state.process(list,&output.list);
    EXPECT_TRUE(std::all_of(output.data.begin(),output.data.end(),[](float v){return v==0;}));
}
TEST(CoreAudioProcess, RejectsOversizedPartialNullAndMismatchedNativeBuffers) {
    ProcessState state;state.allocate(2,2,256);
    mac_test::Buffer input(2,256),output(2,256,1);UInt32 frames=0;
    input.list.mBuffers[0].mDataByteSize-=1;EXPECT_FALSE(state.frames(&input.list,2,frames));
    input.list.mBuffers[0].mDataByteSize+=1;input.list.mBuffers[0].mData=nullptr;EXPECT_FALSE(state.frames(&input.list,2,frames));
    input.list.mBuffers[0].mData=input.data.data();state.capacity=128;
    state.process(&input.list,&output.list);EXPECT_TRUE(state.failed.load());
    EXPECT_TRUE(std::all_of(output.data.begin(),output.data.end(),[](float v){return v==0;}));
    state.failed=false;state.capacity=256;input.list.mBuffers[0].mDataByteSize/=2;
    state.process(&input.list,&output.list);EXPECT_TRUE(state.failed.load());
}

TEST(CoreAudioProcess, PublicFloat32DuplexRunsTheApplicationDspOnBorrowedPlanarViews) {
    mac_test::FakeApi fake;mka_example::Gain dsp;mka::audio::core::CoreAudio backend;
    ASSERT_TRUE(backend.setProcessFunction(+[](void* user,const mka::audio::core::AudioProcessContext& context) noexcept {
        static_cast<mka_example::Gain*>(user)->process(context);
    },&dsp));
    using namespace mka::audio::core;
    ASSERT_TRUE(backend.open({"fake-duplex",Direction::Duplex,2,2,48000,Format::Float32,256}));ASSERT_TRUE(backend.start());
    mac_test::Buffer input(2,37),output(2,37,1);
    for(std::size_t i=0;i<input.data.size();i+=2){input.data[i]=0.25f;input.data[i+1]=-0.5f;}
    for(int cycle=0;cycle<3;++cycle){
        fake.pump(&input.list,&output.list);
        for(std::size_t i=0;i<output.data.size();i+=2){EXPECT_FLOAT_EQ(output.data[i],0.125f);EXPECT_FLOAT_EQ(output.data[i+1],-0.25f);}
    }
    ASSERT_TRUE(backend.stop());ASSERT_TRUE(backend.close());
    EXPECT_EQ(dsp.calls.load(),3u);EXPECT_EQ(dsp.frames.load(),111u);
}
