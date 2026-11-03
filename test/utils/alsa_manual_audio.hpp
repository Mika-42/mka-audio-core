#pragma once
// Test-only helpers; include after importing mka.audio.backend.alsa.
#include <alsa/asoundlib.h>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <numbers>
#include <print>
#include <string>
#include <thread>
#include <vector>

namespace alsa_manual {
using namespace mka::audio::core;
inline constexpr SampleRate rate=48000;
inline constexpr BufferSize period=512;
inline constexpr std::uint32_t channels=2;
inline std::string device(bool input) {
    const char* name=std::getenv(input?"MKA_TEST_ALSA_INPUT_DEVICE":"MKA_TEST_ALSA_OUTPUT_DEVICE");
    if(name && *name)return name;
    if(!input){name=std::getenv("MKA_TEST_ALSA_DEVICE");if(name && *name)return name;}
    return input?"hw:2,0":"hw:1,0";
}
inline EndpointConfig config(bool input) {
    return {device(input),input?Direction::Input:Direction::Output,input?channels:0,input?0:channels,rate,Format::Int32,period};
}
inline const char* errorName(ErrorType error) noexcept {
    switch(error){
        case ErrorType::InvalidState:return "InvalidState";
        case ErrorType::EndpointUnavailable:return "EndpointUnavailable";
        case ErrorType::FormatNotSupported:return "FormatNotSupported";
        case ErrorType::ChannelsNotSupported:return "ChannelsNotSupported";
        case ErrorType::SampleRateNotSupported:return "SampleRateNotSupported";
        case ErrorType::BufferSizeNotSupported:return "BufferSizeNotSupported";
        case ErrorType::ConfigurationFailed:return "ConfigurationFailed";
    }
    return "Unknown";
}
inline Result open(ALSA& backend,const EndpointConfig& cfg) {
    std::println("ALSA {}: {} | {} Hz | {} canaux | S32 | periode {}",
        cfg.direction==Direction::Input?"capture":"lecture",cfg.id,cfg.sampleRate,
        cfg.direction==Direction::Input?cfg.inputChannels:cfg.outputChannels,cfg.bufferSize);
    auto result=backend.open(cfg);
    if(!result){
        std::println(stderr,"ALSA open a echoue: {}",errorName(result.error()));
        snd_pcm_t* pcm=nullptr;
        const int native=snd_pcm_open(&pcm,cfg.id.c_str(),cfg.direction==Direction::Input?SND_PCM_STREAM_CAPTURE:SND_PCM_STREAM_PLAYBACK,SND_PCM_NONBLOCK);
        if(native<0){
            std::println(stderr,"Diagnostic natif {}: {} ({})",cfg.id,snd_strerror(native),native);
            if(native==-EBUSY)std::println(stderr,"Le PCM hw est exclusif : un autre client (PipeWire/PulseAudio/autre application) le detient. Identifier ce client avec fuser -v /dev/snd/pcmC*D* avant de relancer.");
            if(native==-EACCES)std::println(stderr,"Verifier les droits et ACL du peripherique /dev/snd.");
        }else{
            std::println(stderr,"Le PCM est accessible ; l'echec survient pendant sa configuration. Verifier les contraintes exactes format/canaux/frequence/periode avec aplay/arecord --dump-hw-params.");
            snd_pcm_close(pcm);
        }
    }
    return result;
}
template<class Predicate>
bool wait(ALSA& backend,Predicate done,std::chrono::milliseconds timeout) {
    const auto end=std::chrono::steady_clock::now()+timeout;
    while(!done()){
        if(backend.status().failed || std::chrono::steady_clock::now()>=end)return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return true;
}
struct Tone {
    std::atomic<std::size_t> frames{0};
    static constexpr std::size_t signalFrames=static_cast<std::size_t>(rate)*3;
    static constexpr std::size_t total=signalFrames+rate/5; // Let queued audio play before stop() drops the PCM.
    double phase=0;
    void process(const AudioProcessContext& ctx) noexcept {
        const auto offset=frames.load(std::memory_order_relaxed);
        const auto remaining=offset<total?total-offset:0;
        const auto count=std::min<std::size_t>(ctx.frames,remaining);
        constexpr auto fade=rate/50; // 20 ms, no control work in the callback
        for(std::size_t i=0;i<ctx.frames;++i){
            float value=0;
            if(i<count && offset+i<signalFrames){
                const auto position=offset+i;
                const auto edge=std::min({position,signalFrames-1-position,static_cast<std::size_t>(fade)});
                value=static_cast<float>(0.2*std::sin(phase)*static_cast<double>(edge)/fade);
                phase+=2*std::numbers::pi*440.0/rate;if(phase>=2*std::numbers::pi)phase-=2*std::numbers::pi;
            }
            for(std::uint32_t ch=0;ch<ctx.output.count;++ch)ctx.output.channels[ch][i]=value;
        }
        frames.store(offset+count,std::memory_order_release);
    }
};
struct Clip {
    const std::size_t capacity;
    std::vector<std::vector<float>> samples;
    std::atomic<std::size_t> recorded{0},played{0};
    std::size_t warmup=rate/5;
    const std::size_t lead=rate/5,tail=rate/5;
    explicit Clip(std::size_t frames=static_cast<std::size_t>(rate)*5):capacity(frames),samples(channels,std::vector<float>(frames,0.0f)){}
    void record(const AudioProcessContext& ctx) noexcept {
        const auto skip=std::min<std::size_t>(warmup,ctx.frames);warmup-=skip;
        const auto offset=recorded.load(std::memory_order_relaxed);
        const auto available=offset<capacity?capacity-offset:0;
        const auto count=std::min<std::size_t>(ctx.frames-skip,available);
        for(std::uint32_t ch=0;ch<ctx.input.count && ch<samples.size();++ch)
            for(std::size_t i=0;i<count;++i)samples[ch][offset+i]=ctx.input.channels[ch][skip+i];
        recorded.store(offset+count,std::memory_order_release);
    }
    void play(const AudioProcessContext& ctx) noexcept {
        const auto offset=played.load(std::memory_order_relaxed);
        for(std::uint32_t ch=0;ch<ctx.output.count;++ch){
            const auto& source=samples[ch<samples.size()?ch:0];
            for(std::size_t i=0;i<ctx.frames;++i){
                const auto position=offset+i;
                ctx.output.channels[ch][i]=position>=lead && position-lead<capacity?source[position-lead]:0.0f;
            }
        }
        played.store(offset+ctx.frames,std::memory_order_release);
    }
    void fade() {
        const auto count=std::min<std::size_t>(rate/50,capacity/2);
        for(auto& channel:samples)for(std::size_t i=0;i<count;++i){
            const auto gain=static_cast<float>(0.5*(1-std::cos(std::numbers::pi*static_cast<double>(i)/count)));
            channel[i]*=gain;channel[capacity-1-i]*=gain;
        }
    }
    struct Level{float peak=0;double rms=0;};
    Level level() const {
        Level level;double sum=0;std::size_t count=0;
        const auto frames=std::min(recorded.load(std::memory_order_acquire),capacity);
        for(const auto& channel:samples)for(std::size_t i=0;i<frames;++i){
            level.peak=std::max(level.peak,std::abs(channel[i]));sum+=static_cast<double>(channel[i])*channel[i];++count;
        }
        if(count)level.rms=std::sqrt(sum/static_cast<double>(count));
        return level;
    }
};
}
