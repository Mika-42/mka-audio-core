#include "format_case.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <vector>
import mka.audio.convert;
namespace mka_fuzz {
namespace {
using namespace mka::audio::convert;
void require(bool condition){if(!condition)std::abort();} // Active under NDEBUG.
template<class T>T load(const std::byte* memory){T value;std::memcpy(&value,memory,sizeof(value));return value;}
std::int64_t integer(Layout layout,const std::byte* memory,bool writing=false){
    unsigned bytes=layout==Layout::S16?2:layout==Layout::S24Packed?3:4;
    unsigned bits=(layout==Layout::S24Packed || (layout==Layout::S24In32 && !writing))?24:bytes*8;
    const bool little=layout==Layout::S24Packed || std::endian::native==std::endian::little;
    std::uint64_t value=0;
    for(unsigned i=0;i<bytes;++i)value|=static_cast<std::uint64_t>(std::to_integer<unsigned>(memory[i]))<<(8*(little?i:bytes-1-i));
    value&=(std::uint64_t{1}<<bits)-1;
    return value&(std::uint64_t{1}<<(bits-1))?static_cast<std::int64_t>(value)-static_cast<std::int64_t>(std::uint64_t{1}<<bits):static_cast<std::int64_t>(value);
}
float referenceRead(Layout layout,const std::byte* memory){
    if(layout==Layout::F32)return load<float>(memory);
    if(layout==Layout::F64){
        const auto value=load<double>(memory);
        if(std::isnan(value))return std::numeric_limits<float>::quiet_NaN();
        if(std::abs(static_cast<long double>(value))>std::numeric_limits<float>::max())return std::copysign(std::numeric_limits<float>::infinity(),value);
        return static_cast<float>(value);
    }
    const auto bits=layout==Layout::S16?16:layout==Layout::S32?32:24;
    return static_cast<float>(static_cast<long double>(integer(layout,memory))/(std::uint64_t{1}<<(bits-1)));
}
void verifyWrite(Layout layout,const std::byte* memory,float source){
    if(layout==Layout::F32){const float expected=std::isnan(source)?0.0f:source;require(std::bit_cast<std::uint32_t>(load<float>(memory))==std::bit_cast<std::uint32_t>(expected));return;}
    if(layout==Layout::F64){const double expected=std::isnan(source)?0.0:static_cast<double>(source);require(std::bit_cast<std::uint64_t>(load<double>(memory))==std::bit_cast<std::uint64_t>(expected));return;}
    const auto bits=layout==Layout::S16?16:layout==Layout::S32?32:24;
    const auto maximum=(std::int64_t{1}<<(bits-1))-1;
    const auto actual=integer(layout,memory,true);
    require(actual>=-maximum && actual<=maximum);
    const auto normalized=std::isnan(source)?0.L:std::clamp(static_cast<long double>(source),-1.L,1.L);
    // Independent numeric oracle: float intermediate rounding in S16/S24 can
    // contribute another half LSB; the S32 writer uses double precision.
    require(std::abs(static_cast<long double>(actual)-normalized*maximum)<=(bits==32?0.500001L:1.000001L));
}
}
FormatObservation formats(const std::uint8_t* data,std::size_t size){
    auto byte=[&](std::size_t i)->std::uint8_t{return i<size?data[i]:0;};
    const auto layout=static_cast<Layout>(byte(0));
    const auto frames=static_cast<std::uint32_t>((byte(1)|(std::uint32_t(byte(2))<<8))%258);
    const auto width=bytesPerSample(layout);const auto offset=std::uint32_t(byte(4)%16+1);
    const auto stride=std::uint32_t(byte(5)&1?0:width+byte(3)%32);
    if(!width){
        std::array<std::byte,32> raw{};std::array<float,3> values{.25f,-.5f,1};const auto saved=values;
        readChannel(layout,raw.data(),8,values.data(),3);writeChannel(layout,raw.data(),8,values.data(),3);
        require(values==saved && std::all_of(raw.begin(),raw.end(),[](auto b){return b==std::byte{};}));
        return {false,frames,stride,offset};
    }
    const auto extent=frames?std::size_t(frames-1)*stride+width:0;
    std::vector<std::byte> raw(offset+extent+16,std::byte{0x5a});
    auto payload=[&](std::size_t i)->std::uint8_t{return size>6?data[6+i%(size-6)]:0;};
    for(std::size_t i=0;i<extent;++i)raw[offset+i]=std::byte{payload(i)};
    const auto saved=raw;
    constexpr auto guard=std::uint32_t{0x4f123456};
    std::vector<float> decoded(frames+2,std::bit_cast<float>(guard));
    readChannel(layout,frames?raw.data()+offset:nullptr,stride,frames?decoded.data()+1:nullptr,frames);
    require(raw==saved && std::bit_cast<std::uint32_t>(decoded.front())==guard && std::bit_cast<std::uint32_t>(decoded.back())==guard);
    for(std::uint32_t i=0;i<frames;++i){
        const auto expected=referenceRead(layout,raw.data()+offset+std::size_t(i)*stride),actual=decoded[i+1];
        require(std::isnan(expected)?std::isnan(actual):actual==expected && (expected!=0 || std::signbit(actual)==std::signbit(expected)));
    }
    std::vector<float> source(frames+2,std::bit_cast<float>(guard));
    for(std::uint32_t i=0;i<frames;++i){std::uint32_t bits=0;for(unsigned b=0;b<4;++b)bits|=std::uint32_t(payload(i*4+b))<<(8*b);source[i+1]=std::bit_cast<float>(bits);}
    const auto original=source;
    std::vector<std::byte> encoded(offset+extent+16,std::byte{0xa5});std::vector<bool> touched(encoded.size(),false);
    writeChannel(layout,frames?encoded.data()+offset:nullptr,stride,frames?source.data()+1:nullptr,frames);
    require(std::memcmp(source.data(),original.data(),source.size()*sizeof(float))==0);
    for(std::uint32_t i=0;i<frames;++i){
        const auto position=offset+std::size_t(i)*stride;
        for(std::size_t b=0;b<width;++b)touched[position+b]=true;
        if(stride || i+1==frames)verifyWrite(layout,encoded.data()+position,source[i+1]);
    }
    for(std::size_t i=0;i<encoded.size();++i)if(!touched[i])require(encoded[i]==std::byte{0xa5});
    return {true,frames,stride,offset};
}
}
