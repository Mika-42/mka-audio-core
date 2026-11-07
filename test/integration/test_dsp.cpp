#include <algorithm>
#include <array>
#include <cstdint>
#include <expected>
#include <type_traits>
#include <vector>
#include <gtest/gtest.h>
#include "../../examples/dsp/gain.hpp"
import mka.audio.backend.abstract;
using namespace mka::audio::core;
TEST(DspBuffers, ProcessesBorrowedViewsDirectlyWithoutChangingInputOrWritingPastFrames) {
    static_assert(std::is_same_v<decltype(InputBuffer{}.channels),const float* const*>);
    static_assert(std::is_same_v<decltype(OutputBuffer{}.channels),float* const*>);
    for(std::uint32_t frames:{0u,1u,3u,17u,63u,64u}){
        std::array<float,66> left,right,first,second,third;
        left.fill(0.25f);right.fill(-0.5f);first.fill(1234);second.fill(1234);third.fill(1234);
        const auto originalLeft=left,originalRight=right;
        const float* inputs[]{left.data()+1,right.data()+1};
        float* outputs[]{first.data()+1,second.data()+1,third.data()+1};
        const auto firstInput=inputs[0];const auto firstOutput=outputs[0];
        mka_example::Gain dsp;
        dsp.process(AudioProcessContext{{inputs,2},{outputs,3},frames});
        EXPECT_EQ(inputs[0],firstInput);EXPECT_EQ(outputs[0],firstOutput);
        EXPECT_EQ(left,originalLeft);EXPECT_EQ(right,originalRight);
        for(std::uint32_t i=0;i<frames;++i){EXPECT_FLOAT_EQ(first[i+1],0.125f);EXPECT_FLOAT_EQ(second[i+1],-0.25f);EXPECT_FLOAT_EQ(third[i+1],0);}
        for(const auto* channel:{&first,&second,&third}){
            EXPECT_FLOAT_EQ(channel->front(),1234);
            for(std::size_t i=frames+1;i<channel->size();++i)EXPECT_FLOAT_EQ((*channel)[i],1234);
        }
        EXPECT_EQ(dsp.calls.load(),1u);EXPECT_EQ(dsp.frames.load(),frames);
    }
}
TEST(DspBuffers, MissingInputAndOutputDirectionsRespectNullTablesAndProduceSilence) {
    mka_example::Gain dsp;std::array<float,8> samples;samples.fill(1);
    float* outputs[]{samples.data()};
    dsp.process(AudioProcessContext{{nullptr,0},{outputs,1},8});
    EXPECT_TRUE(std::all_of(samples.begin(),samples.end(),[](float value){return value==0;}));
    dsp.process(AudioProcessContext{{nullptr,0},{nullptr,0},0});
    const float* inputs[]{samples.data()};
    dsp.process(AudioProcessContext{{inputs,1},{nullptr,0},8});
    EXPECT_EQ(dsp.calls.load(),3u);EXPECT_EQ(dsp.frames.load(),16u);
}
