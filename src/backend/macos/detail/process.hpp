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
    void process(const AudioBufferList*,AudioBufferList* out) noexcept {
        silence(out);
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
