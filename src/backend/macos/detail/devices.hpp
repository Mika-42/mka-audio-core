#pragma once
#include "api.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace mka_coreaudio_detail {
template<class T>
std::optional<std::vector<T>> arrayProperty(const Api& api,AudioObjectID object,
    AudioObjectPropertySelector selector,AudioObjectPropertyScope scope=kAudioObjectPropertyScopeGlobal) {
    const auto address=property(selector,scope);UInt32 bytes=0;
    if(api.size(object,&address,0,nullptr,&bytes)!=noErr || bytes%sizeof(T) || bytes>16*1024*1024)return std::nullopt;
    std::vector<T> values(bytes/sizeof(T));
    if(!bytes)return values;
    const auto capacity=bytes;
    if(api.get(object,&address,0,nullptr,&bytes,values.data())!=noErr || bytes>capacity || bytes%sizeof(T))return std::nullopt;
    values.resize(bytes/sizeof(T));return values;
}
inline std::optional<std::string> stringProperty(const Api& api,AudioObjectID object,AudioObjectPropertySelector selector) {
    CFStringRef value=nullptr;
    if(!read(api,object,selector,value) || !value)return std::nullopt;
    // CoreAudio string properties return an owned reference.
    struct Release {CFStringRef value;~Release(){CFRelease(value);}} release{value};
    const auto length=CFStringGetLength(value);
    for(CFIndex i=0;i<length;++i)if(CFStringGetCharacterAtIndex(value,i)==0)return std::nullopt;
    const auto bytes=CFStringGetMaximumSizeForEncoding(length,kCFStringEncodingUTF8);
    if(bytes<0 || bytes>1024*1024)return std::nullopt;
    std::string text(static_cast<std::size_t>(bytes)+1,'\0');
    if(!CFStringGetCString(value,text.data(),text.size(),kCFStringEncodingUTF8))return std::nullopt;
    text.resize(std::char_traits<char>::length(text.c_str()));
    if(text.empty())return std::nullopt;
    return text;
}
struct Device {AudioObjectID object;std::string uid,name;};
inline std::vector<Device> devices(const Api& api) {
    std::vector<Device> result;
    const auto ids=arrayProperty<AudioObjectID>(api,kAudioObjectSystemObject,kAudioHardwarePropertyDevices);
    if(!ids)return result;
    for(const auto id:*ids){
        UInt32 alive=0;
        if(!read(api,id,kAudioDevicePropertyDeviceIsAlive,alive) || !alive)continue;
        auto uid=stringProperty(api,id,kAudioDevicePropertyDeviceUID);
        auto name=stringProperty(api,id,kAudioObjectPropertyName);
        if(uid && name)result.push_back({id,std::move(*uid),std::move(*name)});
    }
    return result;
}
}

namespace mka_coreaudio_detail {
inline bool float32(const AudioStreamBasicDescription& format) noexcept {
    const auto flags=format.mFormatFlags;
    return format.mFormatID==kAudioFormatLinearPCM && (flags&kAudioFormatFlagIsFloat)
        && (flags&kAudioFormatFlagIsPacked) && !(flags&kAudioFormatFlagIsBigEndian)
        && format.mBitsPerChannel==32 && format.mChannelsPerFrame>0
        && format.mFramesPerPacket==1 && format.mBytesPerPacket==format.mBytesPerFrame
        && format.mBytesPerFrame==sizeof(float)*((flags&kAudioFormatFlagIsNonInterleaved)?1:format.mChannelsPerFrame);
}
inline std::optional<UInt32> channelCount(const Api& api,AudioObjectID object,AudioObjectPropertyScope scope) {
    const auto address=property(kAudioDevicePropertyStreamConfiguration,scope);UInt32 bytes=0;
    constexpr auto header=offsetof(AudioBufferList,mBuffers);
    if(api.size(object,&address,0,nullptr,&bytes)!=noErr || bytes<header || bytes>1024*1024)return std::nullopt;
    std::vector<std::max_align_t> storage((bytes+sizeof(std::max_align_t)-1)/sizeof(std::max_align_t));
    auto* list=reinterpret_cast<AudioBufferList*>(storage.data());const auto capacity=bytes;
    if(api.get(object,&address,0,nullptr,&bytes,list)!=noErr || bytes>capacity || bytes<header
        || list->mNumberBuffers>(bytes-header)/sizeof(AudioBuffer))return std::nullopt;
    UInt32 total=0;
    for(UInt32 i=0;i<list->mNumberBuffers;++i){
        if(list->mBuffers[i].mNumberChannels>1024-total)return std::nullopt;
        total+=list->mBuffers[i].mNumberChannels;
    }
    return total;
}
inline bool integralRate(Float64 value) noexcept {
    return std::isfinite(value) && value>=1 && value<=std::numeric_limits<UInt32>::max() && std::floor(value)==value;
}
inline bool contains(const AudioValueRange& range,Float64 value) noexcept {
    return std::isfinite(range.mMinimum) && std::isfinite(range.mMaximum)
        && range.mMinimum<=value && value<=range.mMaximum;
}
struct Capabilities {UInt32 channels=0;std::vector<UInt32> rates,buffers;};
inline std::optional<Capabilities> capabilities(const Api& api,AudioObjectID object,AudioObjectPropertyScope scope) {
    const auto count=channelCount(api,object,scope);
    if(!count || !*count)return std::nullopt;
    Float64 rate=0;
    if(!read(api,object,kAudioDevicePropertyNominalSampleRate,rate) || !integralRate(rate))return std::nullopt;
    const auto streams=arrayProperty<AudioStreamID>(api,object,kAudioDevicePropertyStreams,scope);
    if(!streams || streams->empty())return std::nullopt;
    UInt32 streamChannels=0;
    for(auto stream:*streams){
        AudioStreamBasicDescription format{};
        if(!read(api,stream,kAudioStreamPropertyVirtualFormat,format) || !float32(format)
            || format.mSampleRate!=rate || format.mChannelsPerFrame>*count-streamChannels)return std::nullopt;
        streamChannels+=format.mChannelsPerFrame;
    }
    if(streamChannels!=*count)return std::nullopt;
    UInt32 frames=0;AudioValueRange bufferRange{};
    if(!read(api,object,kAudioDevicePropertyBufferFrameSize,frames) || !frames)return std::nullopt;
    Capabilities caps{*count,{static_cast<UInt32>(rate)},{frames}};
    auto append=[](std::vector<UInt32>& values,UInt32 value){
        if(value && std::find(values.begin(),values.end(),value)==values.end())values.push_back(value);
    };
    if(auto ranges=arrayProperty<AudioValueRange>(api,object,kAudioDevicePropertyAvailableNominalSampleRates)){
        for(const auto& range:*ranges){
            for(auto candidate:{8000u,16000u,22050u,32000u,44100u,48000u,88200u,96000u,176400u,192000u})
                if(contains(range,candidate))append(caps.rates,candidate);
            if(integralRate(range.mMinimum))append(caps.rates,static_cast<UInt32>(range.mMinimum));
            if(integralRate(range.mMaximum))append(caps.rates,static_cast<UInt32>(range.mMaximum));
        }
    }
    if(read(api,object,kAudioDevicePropertyBufferFrameSizeRange,bufferRange)){
        for(auto candidate:{32u,64u,128u,256u,512u,1024u,2048u,4096u})
            if(contains(bufferRange,candidate))append(caps.buffers,candidate);
    }
    std::sort(caps.rates.begin(),caps.rates.end());std::sort(caps.buffers.begin(),caps.buffers.end());
    return caps;
}
}
