#include <array>
#include <gtest/gtest.h>
#include "../utils/rt_test_utils.hpp"
#include "../../examples/dsp/gain.hpp"
import mka.audio.process;
TEST(DspRt, DirectProcessingDoesNotAllocateOrFreeCppOrLibcStorage) {
    mka_example::Gain dsp;std::array<float,64> input{},output{};
    const float* inputs[]{input.data()};float* outputs[]{output.data()};
    const mka::audio::core::AudioProcessContext context{{inputs,1},{outputs,1},64};
    alloc_probe::markAudioThread();dsp.process(context);
    alloc_probe::armAudio();
    for(int call=0;call<64;++call)dsp.process(context);
    const auto operations=alloc_probe::disarmAudio();
    EXPECT_EQ(operations,0u);EXPECT_EQ(dsp.calls.load(),65u);
}
