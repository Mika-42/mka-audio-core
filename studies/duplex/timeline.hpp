#pragma once
#include <cmath>
#include <cstdint>
#include <optional>

namespace mka_study {
struct Stamp {std::int64_t nanoseconds=0;std::uint64_t ticks=0;std::uint32_t numerator=0,denominator=0;};
// Compare at one monotonic time; this measures alignment, not clock identity.
inline std::optional<long double> frameOffset(Stamp input,Stamp output) noexcept {
    constexpr std::uint64_t exactLimit=(std::uint64_t{1}<<53)-1;
    if(input.nanoseconds<=0 || output.nanoseconds<=0 || !input.numerator || !input.denominator
        || input.numerator!=output.numerator || input.denominator!=output.denominator
        || input.ticks>exactLimit || output.ticks>exactLimit)return std::nullopt;
    const auto delta=static_cast<long double>(output.nanoseconds)-input.nanoseconds;
    if(std::abs(delta)>2'000'000'000.L)return std::nullopt;
    const auto elapsed=delta*input.denominator/(static_cast<long double>(input.numerator)*1'000'000'000.L);
    return static_cast<long double>(output.ticks)-input.ticks-elapsed;
}
inline float marker(unsigned channel,std::uint64_t frame) noexcept {
    const float value=static_cast<float>(frame%251+1)/1024.0f;return channel?-2.0f*value:value;
}
struct SignalWitness {
    bool started=false;unsigned previous=0;
    std::uint64_t matches=0,errors=0;
    void sample(float left,float right) noexcept {
        bool valid=left>0 && left<=251.0f/1024 && right==-2*left;
        unsigned slot=0;
        if(valid){slot=static_cast<unsigned>(left*1024);valid=left==static_cast<float>(slot)/1024 && slot>0;}
        if(!started && !valid)return; // Initial native silence is not signal evidence.
        if(!valid || (started && slot!=previous%251+1)){++errors;return;}
        started=true;previous=slot;++matches;
    }
};
}
