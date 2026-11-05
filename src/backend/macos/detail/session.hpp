#pragma once
#include "devices.hpp"
#include <expected>
#include <chrono>
#include <thread>

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
        if(!config.rate)return std::unexpected{Failure::Rate};
        if(!config.frames)return std::unexpected{Failure::Buffer};
        config_=config;device_=selected;
        struct Rollback {Session& self;bool committed=false;~Rollback(){if(!committed)(void)self.close();}} rollback{*this};
        if(auto result=configure();!result)return result;
        // Rate changes can alter stream layouts; never retain stale discovery.
        for(bool input:{true,false}){
            if(!(input?config.input:config.output))continue;
            auto caps=capabilities(api_,device_,input?kAudioObjectPropertyScopeInput:kAudioObjectPropertyScopeOutput);
            if(!caps)return std::unexpected{Failure::Format};
            if(caps->channels!=(input?config.inputs:config.outputs))return std::unexpected{Failure::Channels};
        }
        opened_=true;rollback.committed=true;
        return {};
    }
    Result close() noexcept {
        restore();opened_=false;device_=kAudioObjectUnknown;return {};
    }
private:
    template<class T>
    bool setExact(AudioObjectPropertySelector selector,T value) noexcept {
        const auto address=property(selector);Boolean allowed=false;
        if(api_.settable(device_,&address,&allowed)!=noErr || !allowed)return false;
        if(api_.set(device_,&address,0,nullptr,sizeof(T),&value)!=noErr)return false;
        const auto until=std::chrono::steady_clock::now()+std::chrono::milliseconds(250);
        do {
            T actual{};
            if(read(api_,device_,selector,actual) && actual==value)return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }while(std::chrono::steady_clock::now()<until);
        return false;
    }
    Result configure() {
        Float64 rate=0;UInt32 frames=0;
        if(!read(api_,device_,kAudioDevicePropertyNominalSampleRate,rate) || !integralRate(rate))return std::unexpected{Failure::Rate};
        if(!read(api_,device_,kAudioDevicePropertyBufferFrameSize,frames) || !frames)return std::unexpected{Failure::Buffer};
        if(rate!=config_.rate){
            auto ranges=arrayProperty<AudioValueRange>(api_,device_,kAudioDevicePropertyAvailableNominalSampleRates);
            if(!ranges || std::none_of(ranges->begin(),ranges->end(),[&](const auto& r){return contains(r,config_.rate);}))return std::unexpected{Failure::Rate};
            oldRate_=rate;
            if(!setExact(kAudioDevicePropertyNominalSampleRate,static_cast<Float64>(config_.rate)))return std::unexpected{Failure::Rate};
        }
        // Re-read frames after changing rate; the HAL may have changed it too.
        if(!read(api_,device_,kAudioDevicePropertyBufferFrameSize,frames))return std::unexpected{Failure::Buffer};
        if(frames!=config_.frames){
            AudioValueRange range{};
            if(!read(api_,device_,kAudioDevicePropertyBufferFrameSizeRange,range) || !contains(range,config_.frames))return std::unexpected{Failure::Buffer};
            oldFrames_=frames;
            if(!setExact(kAudioDevicePropertyBufferFrameSize,config_.frames))return std::unexpected{Failure::Buffer};
        }
        Float64 actualRate=0;UInt32 actualFrames=0;
        if(!read(api_,device_,kAudioDevicePropertyNominalSampleRate,actualRate) || actualRate!=config_.rate)return std::unexpected{Failure::Rate};
        if(!read(api_,device_,kAudioDevicePropertyBufferFrameSize,actualFrames) || actualFrames!=config_.frames)return std::unexpected{Failure::Buffer};
        return {};
    }
    void restore() noexcept {
        // Do not overwrite a setting that another HAL client has changed since open.
        UInt32 frames=0;Float64 rate=0;
        if(oldFrames_ && read(api_,device_,kAudioDevicePropertyBufferFrameSize,frames) && frames==config_.frames)
            (void)setExact(kAudioDevicePropertyBufferFrameSize,*oldFrames_);
        if(oldRate_ && read(api_,device_,kAudioDevicePropertyNominalSampleRate,rate) && rate==config_.rate)
            (void)setExact(kAudioDevicePropertyNominalSampleRate,*oldRate_);
        oldFrames_.reset();oldRate_.reset();
    }
    std::optional<Float64> oldRate_;
    std::optional<UInt32> oldFrames_;
    const Api& api_;
    Config config_;
    AudioObjectID device_=kAudioObjectUnknown;
    bool opened_=false;
};
}
