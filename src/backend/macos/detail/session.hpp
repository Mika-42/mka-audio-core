#pragma once
#include "devices.hpp"
#include <expected>

namespace mka_coreaudio_detail {
enum class Failure {Unavailable,Format,Channels,Rate,Buffer,Configuration};
using Result=std::expected<void,Failure>;
struct Config {std::string uid;bool input=false,output=false;UInt32 inputs=0,outputs=0,rate=0,frames=0;};
class Session {
public:
    explicit Session(const Api& api=nativeApi()):api_(api){}
    Session(const Session&)=delete;
    Session& operator=(const Session&)=delete;
    ~Session(){(void)close();}
    Result open(const Config& config) {
        if(opened_ || device_)return std::unexpected{Failure::Configuration};
        if(config.uid.find('\0')!=std::string::npos)return std::unexpected{Failure::Unavailable};
        AudioObjectID selected=kAudioObjectUnknown;
        if(config.uid.empty()){
            if(!read(api_,kAudioObjectSystemObject,config.input&&!config.output?kAudioHardwarePropertyDefaultInputDevice:
                kAudioHardwarePropertyDefaultOutputDevice,selected))return std::unexpected{Failure::Unavailable};
        }else{
            for(const auto& device:devices(api_))if(device.uid==config.uid){selected=device.object;break;}
        }
        UInt32 alive=0;
        if(!selected || !read(api_,selected,kAudioDevicePropertyDeviceIsAlive,alive) || !alive)return std::unexpected{Failure::Unavailable};
        if(!config.input && !config.output)return std::unexpected{Failure::Configuration};
        for(bool input:{true,false}){
            if(!(input?config.input:config.output))continue;
            const auto scope=input?kAudioObjectPropertyScopeInput:kAudioObjectPropertyScopeOutput;
            const auto count=channelCount(api_,selected,scope);
            const auto requested=input?config.inputs:config.outputs;
            if(!count || !requested || *count!=requested)return std::unexpected{Failure::Channels};
            if(!capabilities(api_,selected,scope))return std::unexpected{Failure::Format};
        }
        // Commit only after discovery/direction validation; configuration is immutable.
        config_=config;device_=selected;opened_=true;
        return {};
    }
    Result close() noexcept {opened_=false;device_=kAudioObjectUnknown;return {};}
private:
    const Api& api_;
    Config config_;
    AudioObjectID device_=kAudioObjectUnknown;
    bool opened_=false;
};
}
