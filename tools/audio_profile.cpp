// Opt-in diagnostics only. No production Backend ABI or callback policy changes.
#include <mka/audio/build_config.hpp>
#include <charconv>
#include <ctime>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include "../test/diagnostics/metrics.hpp"
import mka.audio.backend.abstract;
#if MKA_BUILD_ALSA
import mka.audio.backend.alsa;
#endif
#if MKA_BUILD_JACK
import mka.audio.backend.jack;
#endif
#if MKA_BUILD_PIPEWIRE
import mka.audio.backend.pipewire;
#endif
#if MKA_BUILD_PULSEAUDIO
import mka.audio.backend.pulseaudio;
#endif
#if MKA_BUILD_WASAPI
import mka.audio.backend.wasapi;
#endif
#if MKA_BUILD_COREAUDIO
import mka.audio.backend.coreaudio;
#endif
using namespace mka::audio::core;
static std::unique_ptr<Backend> makeBackend(const std::string& name){
#if MKA_BUILD_ALSA
    if(name=="alsa")return std::make_unique<ALSA>();
#endif
#if MKA_BUILD_JACK
    if(name=="jack")return std::make_unique<JACK>();
#endif
#if MKA_BUILD_PIPEWIRE
    if(name=="pipewire")return std::make_unique<PipeWire>();
#endif
#if MKA_BUILD_PULSEAUDIO
    if(name=="pulseaudio")return std::make_unique<PulseAudio>();
#endif
#if MKA_BUILD_WASAPI
    if(name=="wasapi")return std::make_unique<Wasapi>();
#endif
#if MKA_BUILD_COREAUDIO
    if(name=="coreaudio")return std::make_unique<CoreAudio>();
#endif
    return {};
}
static void quote(std::ostream& out,const std::string& value){
    out<<'"';for(unsigned char c:value){
        if(c=='"' || c=='\\')out<<'\\'<<c;
        else if(c<32){constexpr char hex[]="0123456789abcdef";out<<"\\u00"<<hex[c>>4]<<hex[c&15];}
        else out<<c;
    }out<<'"';
}
static bool number(const std::string& value,std::uint32_t& result){
    auto [end,error]=std::from_chars(value.data(),value.data()+value.size(),result);
    return error==std::errc{} && end==value.data()+value.size() && result>0;
}
int main(int argc,char** argv){
    std::string backendName,device,path;std::uint32_t seconds=2,frames=256,channels=2;
    bool input=false;
    for(int i=1;i<argc;++i){
        std::string key=argv[i];
        if(key=="--input"){input=true;continue;}
        if(i+1>=argc){std::cerr<<"Expected --backend/--device/--seconds/--frames/--channels/--json value, or --input\n";return 2;}
        std::string value=argv[++i];
        if(key=="--backend")backendName=value;else if(key=="--device")device=value;
        else if(key=="--json")path=value;
        else if(key=="--seconds"){if(!number(value,seconds) || seconds>86400)return 2;}
        else if(key=="--frames"){if(!number(value,frames))return 2;}
        else if(key=="--channels"){if(!number(value,channels))return 2;}
        else {std::cerr<<"Unknown option: "<<key<<'\n';return 2;}
    }
    mka_audit::Recorder recorder; // Outlives the backend even when native stop fails.
    auto backend=makeBackend(backendName);
    if(!backend){std::cerr<<"Backend is not compiled: "<<backendName<<'\n';return 2;}
    if(!backend->setProcessFunction(+[](void* user,const AudioProcessContext& context) noexcept {
        const auto begin=mka_audit::now();
        for(std::uint32_t ch=0;ch<context.output.count;++ch)
            for(std::uint32_t i=0;i<context.frames;++i)context.output.channels[ch][i]=0;
        static_cast<mka_audit::Recorder*>(user)->record(begin,mka_audit::now(),context.frames);
    },&recorder))return 1;
    EndpointConfig config{device,input?Direction::Input:Direction::Output,input?channels:0,input?0:channels,
        48000,backendName=="alsa"?Format::Int32:Format::Float32,frames};
    const auto openBegin=mka_audit::now();const auto opened=backend->open(config);const auto openEnd=mka_audit::now();
    if(!opened){std::cerr<<"Open failed, ErrorType="<<static_cast<int>(opened.error())<<'\n';return 1;}
    const auto cpuBegin=std::clock();const auto begin=mka_audit::now();const auto started=backend->start();const auto startEnd=mka_audit::now();
    if(!started){(void)backend->close();std::cerr<<"Start failed\n";return 1;}
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(seconds);
    while(std::chrono::steady_clock::now()<deadline && !backend->status().failed)std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const auto stopBegin=mka_audit::now();const auto stopped=backend->stop();const auto end=mka_audit::now();const auto cpuEnd=std::clock();
    const auto status=backend->status();const auto closed=backend->close();
    if(!stopped || !closed){std::cerr<<"Stop/close failed\n";return 1;}
    const auto service=mka_audit::serviceTime(recorder);
    std::ofstream file;if(!path.empty()){file.open(path);if(!file)return 2;}
    std::ostream& out=path.empty()?std::cout:file;
    out<<"{\"backend\":";quote(out,backendName);out<<",\"device\":";quote(out,device);
    out<<",\"sample_rate\":48000,\"requested_frames\":"<<frames
       <<",\"requested_period_ns\":"<<static_cast<double>(frames)*1e9/48000
       <<",\"open_ns\":"<<openEnd-openBegin<<",\"start_ns\":"<<startEnd-begin<<",\"stop_ns\":"<<end-stopBegin
       <<",\"wall_ns\":"<<end-begin<<",\"process_cpu_percent_one_core\":";
    if(cpuBegin==std::clock_t(-1) || cpuEnd<cpuBegin)out<<"null";
    else out<<100.0*static_cast<double>(cpuEnd-cpuBegin)/CLOCKS_PER_SEC/(static_cast<double>(end-begin)/1e9);
    out<<",\"callbacks\":"<<recorder.calls.load()<<",\"frames\":"<<recorder.frames.load()<<",\"timing_samples\":"<<recorder.stored
       <<",\"dropped_timing_samples\":"<<recorder.dropped<<",\"service_mean_ns\":"<<service.mean<<",\"service_p99_ns\":"<<service.p99
       <<",\"xruns\":"<<status.xruns<<",\"failed\":"<<(status.failed?"true":"false")<<",\"realtime\":"<<static_cast<int>(status.realtime)<<"}\n";
    return !out || status.failed || !recorder.calls.load() || !recorder.frames.load()?1:0;
}
