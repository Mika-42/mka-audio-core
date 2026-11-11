#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include "asio_channel_layout.hpp"

namespace mka_asio_detail {
template<class Layout> inline Layout conversionLayout(ChannelLayout layout) noexcept {
    switch (layout.sample) {
        case Sample::i16: return Layout::S16;
        case Sample::i24: return layout.low24In32 ? Layout::S24In32 : Layout::S24Packed;
        case Sample::i32: return Layout::S32;
        case Sample::f32: return Layout::F32;
        case Sample::f64: return Layout::F64;
    }
    return Layout::F32; // caller has already rejected an invalid layout
}
inline bool sampleRegion(ChannelLayout layout,const void* native,std::size_t capacity,
                         const void* floats,std::size_t frames) noexcept {
    if (!validChannelLayout(layout)) return false;
    if (!frames) return true;
    if (!native || !floats || frames > std::numeric_limits<std::size_t>::max()/layout.bytes
        || frames > std::numeric_limits<std::size_t>::max()/sizeof(float)) return false;
    const auto bytes=frames*layout.bytes;
    return capacity>=bytes
        && bytes<=std::numeric_limits<std::uintptr_t>::max()-reinterpret_cast<std::uintptr_t>(native)
        && frames*sizeof(float)<=std::numeric_limits<std::uintptr_t>::max()-reinterpret_cast<std::uintptr_t>(floats);
}
template<class Convert>
bool readSamples(ChannelLayout layout,const std::byte* source,std::size_t capacity,float* output,std::size_t frames) noexcept {
    if (!sampleRegion(layout,source,capacity,output,frames)) return false;
    if (!frames) return true;
    const auto core=conversionLayout<typename Convert::Layout>(layout);
    if (!layout.bigEndian) {
        Convert::read(core,source,layout.bytes,output,frames);
    } else {
        std::array<std::byte,8> little{};
        for (std::size_t frame=0;frame<frames;++frame) {
            for (std::size_t byte=0;byte<layout.bytes;++byte)
                little[byte]=source[frame*layout.bytes+layout.bytes-1-byte];
            Convert::read(core,little.data(),layout.bytes,output+frame,1);
        }
    }
    return true;
}
}
