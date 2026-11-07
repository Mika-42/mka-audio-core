// Real ALSA null mmap/params; inject/inspect known F32 sample data only.
#include <alsa/asoundlib.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <expected>
#include <thread>
#include <gtest/gtest.h>
#include "../../../examples/dsp/gain.hpp"
import mka.audio.backend.alsa;
namespace {
using namespace mka::audio::core;
enum class Mode {Capture,Render};
std::atomic<Mode> mode{Mode::Capture};
bool interleaved=false;
thread_local const snd_pcm_channel_area_t* areas=nullptr;
thread_local std::size_t captureCursor=0;
std::atomic<std::size_t> checked=0;
std::atomic<bool> bad=false;
bool signalStarted=false;
constexpr std::size_t length=1024;
float signal(unsigned channel,std::size_t frame){return channel==0?0.125f+static_cast<float>(frame%4)*0.03125f:-0.25f-static_cast<float>(frame%8)*0.015625f;}
struct Pipeline {
    mka_example::Gain dsp;
    std::array<std::array<float,length>,2> result{};
    std::atomic<std::size_t> captured=0,played=0;
};
template<class P>bool waitFor(P predicate){
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(!predicate() && std::chrono::steady_clock::now()<end)std::this_thread::yield();
    return predicate();
}
}
extern "C" {
int __real_snd_pcm_hw_params_set_access(snd_pcm_t*,snd_pcm_hw_params_t*,snd_pcm_access_t);
int __wrap_snd_pcm_hw_params_set_access(snd_pcm_t* pcm,snd_pcm_hw_params_t* params,snd_pcm_access_t access){
    if(interleaved && access!=SND_PCM_ACCESS_MMAP_INTERLEAVED)return -EINVAL;
    return __real_snd_pcm_hw_params_set_access(pcm,params,access);
}
int __real_snd_pcm_mmap_begin(snd_pcm_t*,const snd_pcm_channel_area_t**,snd_pcm_uframes_t*,snd_pcm_uframes_t*);
int __wrap_snd_pcm_mmap_begin(snd_pcm_t* pcm,const snd_pcm_channel_area_t** mapped,snd_pcm_uframes_t* offset,snd_pcm_uframes_t* count){
    const auto status=__real_snd_pcm_mmap_begin(pcm,mapped,offset,count);areas=status>=0?*mapped:nullptr;
    if(areas){
        *count=std::min<snd_pcm_uframes_t>(*count,37);
        if(mode.load()==Mode::Capture){
            for(unsigned ch=0;ch<2;++ch)for(snd_pcm_uframes_t i=0;i<*count;++i){
                auto* destination=static_cast<std::byte*>(areas[ch].addr)+areas[ch].first/8+(*offset+i)*(areas[ch].step/8);
                const auto value=signal(ch,captureCursor+i);std::memcpy(destination,&value,sizeof(value));
            }
            captureCursor+=*count;
        }
    }
    return status;
}
snd_pcm_sframes_t __real_snd_pcm_mmap_commit(snd_pcm_t*,snd_pcm_uframes_t,snd_pcm_uframes_t);
snd_pcm_sframes_t __wrap_snd_pcm_mmap_commit(snd_pcm_t* pcm,snd_pcm_uframes_t offset,snd_pcm_uframes_t count){
    if(areas && mode.load()==Mode::Render){
        for(snd_pcm_uframes_t i=0;i<count;++i){
            std::array<float,2> values{};
            for(unsigned ch=0;ch<2;++ch){
                const auto* source=static_cast<const std::byte*>(areas[ch].addr)+areas[ch].first/8+(offset+i)*(areas[ch].step/8);
                std::memcpy(&values[ch],source,sizeof(float));
            }
            if(!signalStarted && (values[0]!=0 || values[1]!=0))signalStarted=true;
            const auto position=checked.load();
            if(signalStarted && position<length){
                for(unsigned ch=0;ch<2;++ch)if(values[ch]!=signal(ch,position)*mka_example::Gain::factor)bad=true;
                checked.store(position+1);
            }else if(values[0]!=0 || values[1]!=0)bad=true;
        }
    }
    return __real_snd_pcm_mmap_commit(pcm,offset,count);
}
}
TEST(AlsaDspPipeline, Float32CaptureGainAndNativeReplayPreserveBothChannelSignals) {
    for(bool layout:{false,true}){
        interleaved=layout;checked=0;bad=false;signalStarted=false;mode=Mode::Capture;
        Pipeline state;
        {
            ALSA capture;
            ASSERT_TRUE(capture.setProcessFunction(+[](void* user,const AudioProcessContext& context) noexcept {
                auto& p=*static_cast<Pipeline*>(user);const auto offset=p.captured.load();
                const auto count=static_cast<std::uint32_t>(std::min<std::size_t>(context.frames,length-offset));
                float* outputs[]{p.result[0].data()+offset,p.result[1].data()+offset};
                if(count)p.dsp.process(AudioProcessContext{context.input,{outputs,2},count});
                p.captured.store(offset+count,std::memory_order_release);
            },&state));
            ASSERT_TRUE(capture.open({"null",Direction::Input,2,0,48000,Format::Float32,256}));ASSERT_TRUE(capture.start());
            EXPECT_TRUE(waitFor([&]{return state.captured.load()==length;}));
            ASSERT_TRUE(capture.stop());ASSERT_TRUE(capture.close());EXPECT_FALSE(capture.status().failed);
        }
        ASSERT_EQ(state.captured.load(),length);
        for(unsigned ch=0;ch<2;++ch)for(std::size_t i=0;i<length;++i)ASSERT_FLOAT_EQ(state.result[ch][i],signal(ch,i)*mka_example::Gain::factor);
        mode=Mode::Render;
        {
            ALSA render;
            ASSERT_TRUE(render.setProcessFunction(+[](void* user,const AudioProcessContext& context) noexcept {
                auto& p=*static_cast<Pipeline*>(user);const auto offset=p.played.load();
                for(unsigned ch=0;ch<context.output.count;++ch)for(std::uint32_t i=0;i<context.frames;++i)
                    context.output.channels[ch][i]=offset+i<length?p.result[ch][offset+i]:0.0f;
                p.played.store(offset+context.frames,std::memory_order_release);
            },&state));
            ASSERT_TRUE(render.open({"null",Direction::Output,0,2,48000,Format::Float32,256}));ASSERT_TRUE(render.start());
            EXPECT_TRUE(waitFor([]{return checked.load()==length;}));ASSERT_TRUE(render.stop());ASSERT_TRUE(render.close());
            EXPECT_FALSE(render.status().failed);
        }
        EXPECT_EQ(checked.load(),length);EXPECT_FALSE(bad.load());EXPECT_EQ(state.dsp.frames.load(),length);
    }
}
