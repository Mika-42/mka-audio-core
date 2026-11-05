#include <gtest/gtest.h>
#include "fake_api.hpp"
#include "rt_alloc.hpp"
import mka.audio.backend.coreaudio;
using namespace mka::audio::core;
TEST(CoreAudioRt, AllocationProbeDetectsItsOwnPositiveWitness) {
    mac_rt::begin();auto* p=::operator new(128);::operator delete(p);const auto operations=mac_rt::end();
    EXPECT_EQ(operations,2u);
}
TEST(CoreAudioRt, NativeCaptureRenderAndNotificationsDoNotAllocateOrFreeCppStorage) {
    mac_test::FakeApi fake;CoreAudio backend;
    EndpointConfig cfg{"fake-duplex",Direction::Duplex,2,2,48000,Format::Float32,256};
    struct State {int calls=0;bool clean=true;} state;
    ASSERT_TRUE(backend.setProcessFunction(+[](void* user,const AudioProcessContext& ctx) noexcept {
        auto& s=*static_cast<State*>(user);++s.calls;
        for(std::uint32_t ch=0;ch<ctx.output.count;++ch)for(std::uint32_t i=0;i<ctx.frames;++i){
            s.clean &= ctx.output.channels[ch][i]==0.0f;ctx.output.channels[ch][i]=ctx.input.channels[ch][i];
        }
    },&state));
    ASSERT_TRUE(backend.open(cfg));ASSERT_TRUE(backend.start());
    mac_test::Buffer input(2,256,0.125f),output(2,256,1);
    mac_rt::begin();
    for(int i=0;i<64;++i)fake.pump(&input.list,&output.list);
    fake.event(kAudioDeviceProcessorOverload);fake.event(kAudioDevicePropertyDeviceIsAlive);
    fake.pump(&input.list,&output.list);
    const auto operations=mac_rt::end();
    EXPECT_EQ(operations,0u);EXPECT_EQ(state.calls,64);EXPECT_TRUE(state.clean);EXPECT_EQ(output.data[0],0);
    ASSERT_TRUE(backend.stop());ASSERT_TRUE(backend.close());
}
