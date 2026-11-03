#pragma once
#include "../../src/backend/windows/detail/wasapi_convert.hpp"
// Include after importing mka.audio.convert.
namespace wasapi_test {
struct Conversion {
    using Layout=mka::audio::convert::Layout;
    static void read(Layout l,const std::byte* bytes,std::size_t stride,float* out,std::size_t frames) noexcept {
        mka::audio::convert::readChannel(l,bytes,stride,out,frames);
    }
    static void write(Layout l,std::byte* bytes,std::size_t stride,const float* in,std::size_t frames) noexcept {
        mka::audio::convert::writeChannel(l,bytes,stride,in,frames);
    }
};
inline mka_wasapi_detail::Callbacks converters() {
    mka_wasapi_detail::Callbacks result;
    result.read=&mka_wasapi_detail::readSamples<Conversion>;result.write=&mka_wasapi_detail::writeSamples<Conversion>;
    return result;
}
}
