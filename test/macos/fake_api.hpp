#pragma once
#include "../../src/backend/macos/detail/session.hpp"
#include <cstring>
#include <string>

namespace mac_test {
using namespace mka_coreaudio_detail;
struct FakeApi {
    inline static FakeApi* current=nullptr;
    Api api;
    UInt32 inputs=2,outputs=2,frames=256,alive=1;
    Float64 rate=48000;
    bool malformed=false,canSet=true,floatFormat=true,roundFrames=false;
    AudioObjectPropertySelector failGet=0;
    int failCreate=0,failStart=0,failStop=0,failDestroy=0,failRemove=0,failAddAt=0;
    int creates=0,destroys=0,starts=0,stops=0,adds=0,removes=0;
    AudioDeviceIOProc proc=nullptr;
    void* user=nullptr;
    struct Listener {AudioObjectID object;AudioObjectPropertyAddress address;AudioObjectPropertyListenerProc callback;void* user;};
    std::vector<Listener> listeners;
    FakeApi(){
        current=this;
        api.has=+[](AudioObjectID,const AudioObjectPropertyAddress*)->Boolean{return true;};
        api.size=&size;api.get=&get;api.set=&set;api.settable=&settable;
        api.create=&create;api.destroy=&destroy;api.start=&start;api.stop=&stop;
        api.add=&add;api.remove=&remove;
        testApi=&api;
    }
    ~FakeApi(){testApi=nullptr;current=nullptr;}
    template<class T>static OSStatus copy(UInt32* bytes,void* out,const T& value){
        if(*bytes<sizeof(T))return kAudioHardwareBadPropertySizeError;
        std::memcpy(out,&value,sizeof(T));*bytes=sizeof(T);return noErr;
    }
    static OSStatus size(AudioObjectID,const AudioObjectPropertyAddress* address,UInt32,const void*,UInt32* bytes){
        if(address->mSelector==current->failGet)return kAudioHardwareUnknownPropertyError;
        switch(address->mSelector){
            case kAudioDevicePropertyStreamConfiguration:*bytes=sizeof(AudioBufferList);break;
            case kAudioDevicePropertyAvailableNominalSampleRates:*bytes=sizeof(AudioValueRange);break;
            default:*bytes=sizeof(UInt32);break;
        }
        if(current->malformed)++*bytes;
        return noErr;
    }
    static OSStatus get(AudioObjectID object,const AudioObjectPropertyAddress* address,UInt32,const void*,UInt32* bytes,void* out){
        auto& f=*current;
        if(address->mSelector==f.failGet)return kAudioHardwareUnknownPropertyError;
        switch(address->mSelector){
            case kAudioHardwarePropertyDevices:
            case kAudioHardwarePropertyDefaultInputDevice:
            case kAudioHardwarePropertyDefaultOutputDevice:return copy(bytes,out,AudioObjectID{101});
            case kAudioDevicePropertyDeviceIsAlive:return copy(bytes,out,f.alive);
            case kAudioDevicePropertyDeviceUID:{
                CFStringRef value=CFStringCreateWithCString(nullptr,"fake-duplex",kCFStringEncodingUTF8);
                const auto status=copy(bytes,out,value);if(status)CFRelease(value);return status;
            }
            case kAudioObjectPropertyName:{
                CFStringRef value=CFStringCreateWithCString(nullptr,"Test Microphone",kCFStringEncodingUTF8);
                const auto status=copy(bytes,out,value);if(status)CFRelease(value);return status;
            }
            case kAudioDevicePropertyStreamConfiguration:{
                AudioBufferList list{};list.mNumberBuffers=1;
                list.mBuffers[0].mNumberChannels=address->mScope==kAudioObjectPropertyScopeInput?f.inputs:f.outputs;
                return copy(bytes,out,list);
            }
            case kAudioDevicePropertyStreams:return copy(bytes,out,AudioStreamID{address->mScope==kAudioObjectPropertyScopeInput?201u:202u});
            case kAudioStreamPropertyVirtualFormat:{
                const auto channels=object==201?f.inputs:f.outputs;
                AudioStreamBasicDescription format{};
                format.mSampleRate=f.rate;format.mFormatID=kAudioFormatLinearPCM;
                format.mFormatFlags=kAudioFormatFlagIsPacked|(f.floatFormat?kAudioFormatFlagIsFloat:kAudioFormatFlagIsSignedInteger);
                format.mBitsPerChannel=32;format.mChannelsPerFrame=channels;format.mFramesPerPacket=1;
                format.mBytesPerFrame=channels*4;format.mBytesPerPacket=format.mBytesPerFrame;
                return copy(bytes,out,format);
            }
            case kAudioDevicePropertyNominalSampleRate:return copy(bytes,out,f.rate);
            case kAudioDevicePropertyAvailableNominalSampleRates:return copy(bytes,out,AudioValueRange{44100,96000});
            case kAudioDevicePropertyBufferFrameSize:return copy(bytes,out,f.frames);
            case kAudioDevicePropertyBufferFrameSizeRange:return copy(bytes,out,AudioValueRange{32,2048});
            default:return kAudioHardwareUnknownPropertyError;
        }
    }
    static OSStatus settable(AudioObjectID,const AudioObjectPropertyAddress*,Boolean* value){*value=current->canSet;return noErr;}
    static OSStatus set(AudioObjectID,const AudioObjectPropertyAddress* address,UInt32,const void*,UInt32 bytes,const void* value){
        auto& f=*current;
        if(!f.canSet)return kAudioHardwareIllegalOperationError;
        if(address->mSelector==kAudioDevicePropertyNominalSampleRate && bytes==sizeof(Float64)){std::memcpy(&f.rate,value,bytes);return noErr;}
        if(address->mSelector==kAudioDevicePropertyBufferFrameSize && bytes==sizeof(UInt32)){
            std::memcpy(&f.frames,value,bytes);if(f.roundFrames)++f.frames;return noErr;
        }
        return kAudioHardwareUnknownPropertyError;
    }
    static OSStatus create(AudioObjectID,AudioDeviceIOProc proc,void* user,AudioDeviceIOProcID* id){
        auto& f=*current;++f.creates;
        if(f.failCreate-->0)return kAudioHardwareIllegalOperationError;
        f.proc=proc;f.user=user;*id=proc;return noErr;
    }
    static OSStatus destroy(AudioObjectID,AudioDeviceIOProcID){
        auto& f=*current;
        if(f.failDestroy-->0)return kAudioHardwareIllegalOperationError;
        ++f.destroys;f.proc=nullptr;f.user=nullptr;return noErr;
    }
    static OSStatus start(AudioObjectID,AudioDeviceIOProcID){++current->starts;return current->failStart-->0?kAudioHardwareIllegalOperationError:noErr;}
    static OSStatus stop(AudioObjectID,AudioDeviceIOProcID){++current->stops;return current->failStop-->0?kAudioHardwareIllegalOperationError:noErr;}
    static OSStatus add(AudioObjectID object,const AudioObjectPropertyAddress* address,AudioObjectPropertyListenerProc callback,void* user){
        auto& f=*current;++f.adds;
        if(f.adds==f.failAddAt)return kAudioHardwareIllegalOperationError;
        f.listeners.push_back({object,*address,callback,user});return noErr;
    }
    static OSStatus remove(AudioObjectID object,const AudioObjectPropertyAddress* address,AudioObjectPropertyListenerProc callback,void* user){
        auto& f=*current;
        if(f.failRemove-->0)return kAudioHardwareIllegalOperationError;
        for(auto i=f.listeners.begin();i!=f.listeners.end();++i)
            if(i->object==object && i->address.mSelector==address->mSelector && i->address.mScope==address->mScope && i->callback==callback && i->user==user){
                f.listeners.erase(i);++f.removes;return noErr;
            }
        return kAudioHardwareUnknownPropertyError;
    }
    void event(AudioObjectPropertySelector selector){
        for(const auto& listener:listeners)if(listener.address.mSelector==selector){listener.callback(listener.object,1,&listener.address,listener.user);return;}
    }
    void pump(const AudioBufferList* input,AudioBufferList* output){
        if(proc)proc(101,nullptr,input,nullptr,output,nullptr,user);
    }
};
inline Config config(){return {"fake-duplex",false,true,0,2,48000,256};}
struct Buffer {
    std::vector<float> data;
    AudioBufferList list{};
    Buffer(UInt32 channels,UInt32 frames,float value=0):data(channels*frames,value){
        list.mNumberBuffers=1;list.mBuffers[0]={channels,static_cast<UInt32>(data.size()*sizeof(float)),data.data()};
    }
};
}
