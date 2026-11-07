#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <vector>
namespace mka_audit {
inline std::uint64_t now() noexcept {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
struct Sample {std::uint64_t begin,end;std::uint32_t frames;};
// Single audio writer, inspected only after stop(). All storage is reserved first.
struct Recorder {
    std::vector<Sample> samples;
    std::size_t stored=0;
    std::uint64_t dropped=0;
    std::atomic<std::uint64_t> calls{0},frames{0};
    explicit Recorder(std::size_t capacity=100000):samples(capacity){}
    void record(std::uint64_t begin,std::uint64_t end,std::uint32_t count) noexcept {
        if(stored<samples.size())samples[stored++]={begin,end,count};else ++dropped;
        frames.fetch_add(count,std::memory_order_relaxed);
        calls.fetch_add(1,std::memory_order_release);
    }
};
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
struct Distribution {double mean=0,p50=0,p99=0,max=0;std::size_t count=0;};
inline Distribution distribution(std::vector<double> values) {
    if(values.empty())return {};
    double sum=0;for(double value:values)sum+=value;
    std::sort(values.begin(),values.end());
    auto percentile=[&](double p){return values[static_cast<std::size_t>(std::ceil(p*values.size()))-1];};
    return {sum/values.size(),percentile(0.5),percentile(0.99),values.back(),values.size()};
}
inline Distribution serviceTime(const Recorder& recorder) {
    std::vector<double> values;values.reserve(recorder.stored);
    for(std::size_t i=0;i<recorder.stored;++i){const auto& s=recorder.samples[i];if(s.end>=s.begin)values.push_back(static_cast<double>(s.end-s.begin));}
    return distribution(std::move(values));
}
}
