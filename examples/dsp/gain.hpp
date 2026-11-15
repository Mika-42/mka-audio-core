#pragma once
#include <atomic>
#include <cstdint>
#include <limits>

namespace mka_example {
// Application DSP, deliberately outside the Core library. Parameters are fixed
// before start; the node retains counters, never borrowed channel/sample pointers.
struct Gain {
    static constexpr float factor=0.5f;
    std::atomic<std::uint64_t> calls{0},frames{0};
    template<class Context>
    void process(const Context& context) noexcept {
        for(std::uint32_t ch=0;ch<context.output.count;++ch){
            float* destination=context.output.channels[ch];
            const float* source=ch<context.input.count?context.input.channels[ch]:nullptr;
            for(std::uint32_t i=0;i<context.frames;++i)destination[i]=source?source[i]*factor:0.0f;
        }
        frames.fetch_add(context.frames,std::memory_order_relaxed);
        calls.fetch_add(1,std::memory_order_release);
    }
};
static_assert(sizeof(float)==4 && std::numeric_limits<float>::is_iec559);
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
}
