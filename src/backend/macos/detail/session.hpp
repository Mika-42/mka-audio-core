#pragma once
#include "devices.hpp"
#include "process.hpp"
#include <memory>
#include <expected>
#include <initializer_list>
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
    ~Session(){
        if(!close() && state_){
            state_->accepting.store(false,std::memory_order_release);quiesce();
            // A failing HAL removal must not leave a dangling native user pointer.
            // Quarantine only on unrecoverable teardown failure; normal close frees all.
            (void)state_.release();
        }
    }
    Result open(const Config& config) {
        if(opened_)return std::unexpected{Failure::Configuration};
        if(device_ && !close())return std::unexpected{Failure::Configuration};
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
        state_=std::make_unique<ProcessState>();
        state_->allocate(config.inputs,config.outputs,config.frames);
        if(api_.create(device_,&ioProc,state_.get(),&proc_)!=noErr || !proc_)return std::unexpected{Failure::Configuration};
        if(auto result=listen();!result)return result;
        opened_=true;rollback.committed=true;
        return {};
    }
    Result start(Callbacks callbacks) {
        if(!opened_ || !proc_ || running_)return std::unexpected{Failure::Configuration};
        state_->invalidated.store(false,std::memory_order_release);
        UInt32 alive=0,frames=0;Float64 rate=0;
        if(!read(api_,device_,kAudioDevicePropertyDeviceIsAlive,alive) || !alive)return std::unexpected{Failure::Unavailable};
        if(!read(api_,device_,kAudioDevicePropertyNominalSampleRate,rate) || rate!=config_.rate)return std::unexpected{Failure::Rate};
        if(!read(api_,device_,kAudioDevicePropertyBufferFrameSize,frames) || frames!=config_.frames)return std::unexpected{Failure::Buffer};
        for(bool input:{true,false}){
            if(!(input?config_.input:config_.output))continue;
            auto caps=capabilities(api_,device_,input?kAudioObjectPropertyScopeInput:kAudioObjectPropertyScopeOutput);
            if(!caps)return std::unexpected{Failure::Format};
            if(caps->channels!=(input?config_.inputs:config_.outputs))return std::unexpected{Failure::Channels};
        }
        if(state_->invalidated.load(std::memory_order_acquire))return std::unexpected{Failure::Configuration};
        quiesce();state_->callbacks=callbacks;state_->failed.store(false,std::memory_order_release);
        state_->accepting.store(true,std::memory_order_release);
        running_=true; // Even a failed start may have partially activated the IOProc.
        if(api_.start(device_,proc_)!=noErr){
            state_->accepting.store(false,std::memory_order_release);
            if(api_.stop(device_,proc_)==noErr)running_=false;
            quiesce();return std::unexpected{Failure::Configuration};
        }
        if(state_->invalidated.load(std::memory_order_acquire))state_->fail();
        return {};
    }
    Result stop() noexcept {
        if(!state_)return {};
        state_->accepting.store(false,std::memory_order_release);
        if(running_){
            if(api_.stop(device_,proc_)!=noErr){quiesce();return std::unexpected{Failure::Configuration};}
            running_=false;
        }
        quiesce();return {};
    }
    Result close() noexcept {
        (void)stop();
        if(proc_){
            if(api_.destroy(device_,proc_)!=noErr)return std::unexpected{Failure::Configuration};
            proc_=nullptr;
        }
        running_=false;
        while(!listeners_.empty()){
            const auto& listener=listeners_.back();
            if(api_.remove(listener.object,&listener.address,&propertyChanged,state_.get())!=noErr)
                return std::unexpected{Failure::Configuration};
            listeners_.pop_back();
        }
        quiesce();state_.reset();restore();opened_=false;device_=kAudioObjectUnknown;return {};
    }
private:
    struct Listener {AudioObjectID object;AudioObjectPropertyAddress address;};
    Result listen() {
        std::vector<Listener> pending;
        for(auto selector:std::initializer_list<AudioObjectPropertySelector>{kAudioDevicePropertyDeviceIsAlive,kAudioDevicePropertyNominalSampleRate,
            kAudioDevicePropertyBufferFrameSize,kAudioDeviceProcessorOverload})pending.push_back({device_,property(selector)});
        for(auto selector:{kAudioDevicePropertyClockDomain,kAudioDevicePropertyClockSource}){
            auto address=property(selector);
            if(api_.has(device_,&address))pending.push_back({device_,address});
        }
        for(bool input:{true,false}){
            if(!(input?config_.input:config_.output))continue;
            const auto scope=input?kAudioObjectPropertyScopeInput:kAudioObjectPropertyScopeOutput;
            pending.push_back({device_,property(kAudioDevicePropertyStreamConfiguration,scope)});
            auto streams=arrayProperty<AudioStreamID>(api_,device_,kAudioDevicePropertyStreams,scope);
            if(!streams)return std::unexpected{Failure::Configuration};
            for(auto stream:*streams)pending.push_back({stream,property(kAudioStreamPropertyVirtualFormat)});
        }
        listeners_.reserve(pending.size());
        for(const auto& listener:pending){
            if(api_.add(listener.object,&listener.address,&propertyChanged,state_.get())!=noErr)
                return std::unexpected{Failure::Configuration};
            listeners_.push_back(listener);
        }
        return {};
    }
    std::vector<Listener> listeners_;
    void quiesce() noexcept {
        while(state_ && state_->active.load(std::memory_order_acquire))std::this_thread::yield();
    }
    template<class T>
    bool setExact(AudioObjectPropertySelector selector,T value,T* observed=nullptr) noexcept {
        const auto address=property(selector);Boolean allowed=false;
        if(api_.settable(device_,&address,&allowed)!=noErr || !allowed)return false;
        if(api_.set(device_,&address,0,nullptr,sizeof(T),&value)!=noErr)return false;
        const auto until=std::chrono::steady_clock::now()+std::chrono::milliseconds(250);
        do {
            T actual{};
            if(read(api_,device_,selector,actual)){
                if(observed)*observed=actual;
                if(actual==value)return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }while(std::chrono::steady_clock::now()<until);
        return false;
    }
    Result configure() {
        Float64 rate=0;UInt32 frames=0;
        if(!read(api_,device_,kAudioDevicePropertyNominalSampleRate,rate) || !integralRate(rate))return std::unexpected{Failure::Rate};
        if(!read(api_,device_,kAudioDevicePropertyBufferFrameSize,frames) || !frames)return std::unexpected{Failure::Buffer};
        oldFrames_=frames;setFrames_=frames;
        if(rate!=config_.rate){
            auto ranges=arrayProperty<AudioValueRange>(api_,device_,kAudioDevicePropertyAvailableNominalSampleRates);
            if(!ranges || std::none_of(ranges->begin(),ranges->end(),[&](const auto& r){return contains(r,config_.rate);}))return std::unexpected{Failure::Rate};
            oldRate_=rate;setRate_=rate;
            if(!setExact(kAudioDevicePropertyNominalSampleRate,static_cast<Float64>(config_.rate),&*setRate_))return std::unexpected{Failure::Rate};
        }
        // Re-read frames after changing rate; the HAL may have changed it too.
        if(!read(api_,device_,kAudioDevicePropertyBufferFrameSize,frames))return std::unexpected{Failure::Buffer};
        setFrames_=frames;
        if(frames!=config_.frames){
            AudioValueRange range{};
            if(!read(api_,device_,kAudioDevicePropertyBufferFrameSizeRange,range) || !contains(range,config_.frames))return std::unexpected{Failure::Buffer};
            if(!setExact(kAudioDevicePropertyBufferFrameSize,config_.frames,&*setFrames_))return std::unexpected{Failure::Buffer};
        }
        Float64 actualRate=0;UInt32 actualFrames=0;
        if(!read(api_,device_,kAudioDevicePropertyNominalSampleRate,actualRate) || actualRate!=config_.rate)return std::unexpected{Failure::Rate};
        if(!read(api_,device_,kAudioDevicePropertyBufferFrameSize,actualFrames) || actualFrames!=config_.frames)return std::unexpected{Failure::Buffer};
        return {};
    }
    void restore() noexcept {
        // Restore values observed after our writes, including an inexact native
        // readback. Preserve a later setting selected by another HAL client.
        UInt32 frames=0;Float64 rate=0;
        const bool ownFrames=oldFrames_ && setFrames_
            && read(api_,device_,kAudioDevicePropertyBufferFrameSize,frames) && frames==*setFrames_;
        if(oldRate_ && setRate_ && read(api_,device_,kAudioDevicePropertyNominalSampleRate,rate) && rate==*setRate_)
            (void)setExact(kAudioDevicePropertyNominalSampleRate,*oldRate_);
        if(ownFrames)(void)setExact(kAudioDevicePropertyBufferFrameSize,*oldFrames_);
        oldFrames_.reset();oldRate_.reset();setFrames_.reset();setRate_.reset();
    }
    std::optional<Float64> oldRate_,setRate_;
    std::optional<UInt32> oldFrames_,setFrames_;
    std::unique_ptr<ProcessState> state_;
    AudioDeviceIOProcID proc_=nullptr;
    const Api& api_;
    Config config_;
    AudioObjectID device_=kAudioObjectUnknown;
    bool opened_=false,running_=false;
};
}
