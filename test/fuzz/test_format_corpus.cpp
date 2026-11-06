#include <array>
#include <cstdint>
#include <vector>
#include <gtest/gtest.h>
#include "format_case.hpp"
TEST(FormatCorpus, EveryLayoutExercisesZeroOddMaximumStridedAndRepeatedAddressCases) {
    const std::array<std::uint32_t,10> bits{0,0x80000000,0x3f000000,0xbf000000,0x3f800000,0xbf800000,0x7f800000,0xff800000,0x7fc00001,1};
    for(unsigned layout=0;layout<6;++layout)for(unsigned frames:{0u,1u,17u,257u})for(unsigned repeat:{0u,1u}){
        std::vector<std::uint8_t> bytes{std::uint8_t(layout),std::uint8_t(frames),std::uint8_t(frames>>8),31,15,std::uint8_t(repeat)};
        for(auto word:bits)for(unsigned b=0;b<4;++b)bytes.push_back(static_cast<std::uint8_t>(word>>(8*b)));
        auto observed=mka_fuzz::formats(bytes.data(),bytes.size());EXPECT_TRUE(observed.valid);EXPECT_EQ(observed.frames,frames);EXPECT_EQ(observed.offset,16u);
        EXPECT_EQ(observed.stride==0,repeat!=0);
    }
}
TEST(FormatCorpus, InvalidLayoutsAndShortInputsCannotReachAConversionLoop) {
    const std::array<std::uint8_t,6> invalid{255,255,255,255,255,255};
    EXPECT_FALSE(mka_fuzz::formats(invalid.data(),invalid.size()).valid);
    const auto empty=mka_fuzz::formats(nullptr,0);EXPECT_TRUE(empty.valid);EXPECT_EQ(empty.frames,0u);
    for(std::size_t size=1;size<invalid.size();++size)EXPECT_FALSE(mka_fuzz::formats(invalid.data(),size).valid);
}
