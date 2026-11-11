#include <alsa/asoundlib.h>
#include <atomic>
#include <chrono>
#include <expected>
#include <memory>
#include <thread>
#include <gtest/gtest.h>
#include "../../../examples/dsp/gain.hpp"
import mka.audio.backend.alsa;
using namespace mka::audio::core;
TEST(AlsaEndpointSwitch, NativeNullAliasesCanBeClosedReopenedAndUseNewDspContext) {
    ASSERT_GE(snd_config_update(),0);
    const char text[]="pcm.mka_switch_a { type null }\npcm.mka_switch_b { type null }\n";
    snd_input_t* input=nullptr;ASSERT_GE(snd_input_buffer_open(&input,text,sizeof(text)-1),0);
    const auto loaded=snd_config_load(snd_config,input);snd_input_close(input);ASSERT_GE(loaded,0);
    struct Remove {
        ~Remove(){for(const char* name:{"pcm.mka_switch_a","pcm.mka_switch_b"}){snd_config_t* node=nullptr;if(snd_config_search(snd_config,name,&node)>=0)snd_config_delete(node);}}
    } remove;
    auto node=std::make_unique<mka_example::Gain>();ALSA backend;
    auto bind=[&]{return backend.setProcessFunction(+[](void* user,const AudioProcessContext& context) noexcept {static_cast<mka_example::Gain*>(user)->process(context);},node.get());};
    EndpointConfig config{"mka_switch_a",Direction::Output,0,2,48000,Format::Float32,256};
    for(const char* id:{"mka_switch_a","mka_switch_b"}){
        config.id=id;node=std::make_unique<mka_example::Gain>();ASSERT_TRUE(bind());
        ASSERT_TRUE(backend.open(config));ASSERT_TRUE(backend.start());
        const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        while(!node->calls.load() && !backend.status().failed && std::chrono::steady_clock::now()<end)std::this_thread::yield();
        ASSERT_TRUE(backend.stop());ASSERT_TRUE(backend.close());EXPECT_GT(node->calls.load(),0u);EXPECT_FALSE(backend.status().failed);
        node.reset();
    }
}
