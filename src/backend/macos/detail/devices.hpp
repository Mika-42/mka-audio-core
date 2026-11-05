#pragma once
#include "api.hpp"
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
