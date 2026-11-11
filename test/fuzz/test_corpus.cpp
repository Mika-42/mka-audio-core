#include <array>
#include <cstdint>
#include <vector>
#include <gtest/gtest.h>
#include "config_case.hpp"
static std::array<std::uint8_t,24> encode(std::array<std::uint32_t,6> fields){
    std::array<std::uint8_t,24> bytes{};
    for(std::size_t i=0;i<bytes.size();++i)bytes[i]=static_cast<std::uint8_t>(fields[i/4]>>(8*(i%4)));
    return bytes;
}
TEST(ConfigurationCorpus, NominalAndUint32BoundariesReachOnlySafeNativeHooks) {
    auto valid=encode({1,3,0,2,48000,256});EXPECT_EQ(mka_fuzz::configuration(valid.data(),valid.size()).nativeCalls,1u);
    for(auto boundary:{0u,1u,0x7fffffffu,0x80000000u,0xffffffffu}){
        for(std::size_t field=0;field<6;++field){
            std::array<std::uint32_t,6> fields{1,3,0,2,48000,256};fields[field]=boundary;
            auto bytes=encode(fields);const auto observation=mka_fuzz::configuration(bytes.data(),bytes.size());
            EXPECT_LE(observation.nativeCalls,1u);
        }
    }
    EXPECT_EQ(mka_fuzz::configuration(nullptr,0).nativeCalls,0u);
}
TEST(ConfigurationCorpus, TransportOverflowIsRejectedBeforeAnyNativeResource) {
    auto overflow=encode({1,3,0,2,48000,0xffffffffu});
    EXPECT_EQ(mka_fuzz::configuration(overflow.data(),overflow.size()).nativeCalls,0u);
}
