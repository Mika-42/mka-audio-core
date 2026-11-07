#pragma once
#include "wasapi_format.hpp"
#include <cstring>
namespace mka_wasapi_detail {
template<class Layout> constexpr Layout sampleLayout(SampleKind kind) noexcept {
    switch(kind) {
        case SampleKind::S16:return Layout::S16;
        case SampleKind::S24Packed:return Layout::S24Packed;
        case SampleKind::S24Msb32:return Layout::S24In32;
        case SampleKind::S32:return Layout::S32;
        case SampleKind::F32:return Layout::F32;
        case SampleKind::F64:return Layout::F64;
    }
    return Layout::F32;
}
// WAVEFORMATEXTENSIBLE puts valid PCM bits at the MOST significant end of a
// container. ALSA S24In32 is low-aligned, so its conversion cannot be used raw.
template<class Convert>
void readSamples(SampleKind kind,const BYTE* bytes,std::size_t stride,float* out,std::uint32_t frames) noexcept {
    if(kind!=SampleKind::S24Msb32) {
        Convert::read(sampleLayout<typename Convert::Layout>(kind),reinterpret_cast<const std::byte*>(bytes),stride,out,frames);
        return;
    }
    for(std::uint32_t i=0;i<frames;++i) {
        std::uint32_t sample;std::memcpy(&sample,bytes+static_cast<std::size_t>(i)*stride,sizeof(sample));sample>>=8;
        Convert::read(Convert::Layout::S24In32,reinterpret_cast<const std::byte*>(&sample),sizeof(sample),out+i,1);
    }
}
template<class Convert>
void writeSamples(SampleKind kind,BYTE* bytes,std::size_t stride,const float* in,std::uint32_t frames) noexcept {
    if(kind!=SampleKind::S24Msb32) {
        Convert::write(sampleLayout<typename Convert::Layout>(kind),reinterpret_cast<std::byte*>(bytes),stride,in,frames);
        return;
    }
    for(std::uint32_t i=0;i<frames;++i) {
        std::uint32_t sample=0;
        Convert::write(Convert::Layout::S24In32,reinterpret_cast<std::byte*>(&sample),sizeof(sample),in+i,1);
        sample<<=8;std::memcpy(bytes+static_cast<std::size_t>(i)*stride,&sample,sizeof(sample));
    }
}
}
