#pragma once
#include "api.hpp"
#include <algorithm>
#include <cstring>
#include <vector>

namespace mka_coreaudio_detail {
struct Callbacks {
    void* user=nullptr;
    void (*process)(void*,const float* const*,UInt32,float* const*,UInt32,UInt32) noexcept=nullptr;
    void (*failed)(void*) noexcept=nullptr;
    void (*xrun)(void*) noexcept=nullptr;
};
struct ProcessState {
    std::atomic<bool> accepting{false},failed{false};
    std::atomic<unsigned> active{0};
    UInt32 inputs=0,outputs=0,capacity=0;
    Callbacks callbacks;
    std::vector<std::vector<float>> input,output;
    std::vector<const float*> inputPointers;
    std::vector<float*> outputPointers;
    void allocate(UInt32 in,UInt32 out,UInt32 frames) {
        inputs=in;outputs=out;capacity=frames;
        input.assign(in,std::vector<float>(frames));output.assign(out,std::vector<float>(frames));
        for(auto& channel:input)inputPointers.push_back(channel.data());
        for(auto& channel:output)outputPointers.push_back(channel.data());
    }
    void fail() noexcept {
        if(!failed.exchange(true,std::memory_order_acq_rel) && callbacks.failed)callbacks.failed(callbacks.user);
    }
    static void silence(AudioBufferList* buffers) noexcept {
        if(!buffers || buffers->mNumberBuffers>1024)return;
        for(UInt32 i=0;i<buffers->mNumberBuffers;++i){
            auto& buffer=buffers->mBuffers[i];
            if(buffer.mData)std::memset(buffer.mData,0,buffer.mDataByteSize);
        }
    }
    // HAL buffers may be interleaved per stream or planar. Flatten their channels
    // into independent Float32 scratch buffers; never lend an interleaved pointer.
    bool frames(const AudioBufferList* list,UInt32 channels,UInt32& count) const noexcept {
        if(!list || !list->mNumberBuffers || list->mNumberBuffers>1024)return false;
        UInt32 total=0;count=0;
        for(UInt32 b=0;b<list->mNumberBuffers;++b){
            const auto& buffer=list->mBuffers[b];
            if(!buffer.mNumberChannels)continue;
            if(buffer.mNumberChannels>channels-total || !buffer.mData)return false;
            const auto stride=sizeof(float)*buffer.mNumberChannels;
            if(buffer.mDataByteSize%stride)return false;
            const auto available=static_cast<UInt32>(buffer.mDataByteSize/stride);
            if(!available || available>capacity || (count && count!=available))return false;
            count=available;total+=buffer.mNumberChannels;
        }
        return total==channels && count>0;
    }
    void readInput(const AudioBufferList* list,UInt32 count) noexcept {
        UInt32 channel=0;
        for(UInt32 b=0;b<list->mNumberBuffers;++b){
            const auto& buffer=list->mBuffers[b];
            const auto* data=static_cast<const float*>(buffer.mData);
            for(UInt32 ch=0;ch<buffer.mNumberChannels;++ch,++channel)
                for(UInt32 i=0;i<count;++i)input[channel][i]=data[i*buffer.mNumberChannels+ch];
        }
    }
    void process(const AudioBufferList* in,AudioBufferList* out) noexcept {
        silence(out);
        UInt32 count=0;
        if(inputs && !frames(in,inputs,count)){fail();return;}
        if(inputs)readInput(in,count);
        if(callbacks.process && !outputs && count)
            callbacks.process(callbacks.user,inputPointers.data(),inputs,nullptr,0,count);
    }
};
inline OSStatus ioProc(AudioObjectID,const AudioTimeStamp*,const AudioBufferList* input,
    const AudioTimeStamp*,AudioBufferList* output,const AudioTimeStamp*,void* user) noexcept {
    auto& state=*static_cast<ProcessState*>(user);
    state.active.fetch_add(1,std::memory_order_acq_rel);
    if(state.accepting.load(std::memory_order_acquire) && !state.failed.load(std::memory_order_acquire))state.process(input,output);
    else ProcessState::silence(output);
    state.active.fetch_sub(1,std::memory_order_release);
    return noErr;
}
}
