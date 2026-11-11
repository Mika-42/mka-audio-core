#pragma once
#include <vector>
#include "../../../src/backend/windows/detail/asio_convert.hpp"

namespace asio_test {
// Include after importing mka.audio.convert in the consuming test source.
struct Conversion {
    using Layout=mka::audio::convert::Layout;
    static void read(Layout layout,const std::byte* source,std::size_t stride,float* output,std::size_t frames) noexcept {
        mka::audio::convert::readChannel(layout,source,stride,output,frames);
    }
    static void write(Layout layout,std::byte* target,std::size_t stride,const float* input,std::size_t frames) noexcept {
        mka::audio::convert::writeChannel(layout,target,stride,input,frames);
    }
};
inline std::vector<std::byte> encodedSamples(const std::vector<std::uint64_t>& values,ChannelLayout layout) {
    std::vector<std::byte> result(values.size()*layout.bytes);
    for (std::size_t frame=0;frame<values.size();++frame) for (std::size_t byte=0;byte<layout.bytes;++byte)
        result[frame*layout.bytes+(layout.bigEndian?layout.bytes-1-byte:byte)]=std::byte((values[frame]>>(8*byte))&0xff);
    return result;
}
}
