#pragma once
#include <atomic>
#include <cstdint>
#include "asio_types.hpp"

namespace mka_asio_detail {
struct TimingSnapshot {
    std::uint64_t samplePosition = 0, systemNanoseconds = 0;
    std::uint32_t valid = 0;
};
struct PositionSource {
    void* driver = nullptr;
    ASIOError (*get)(void*,ASIOSamples*,ASIOTimeStamp*) noexcept = nullptr;
};
template<class Words> inline std::uint64_t asioWords(Words words) noexcept {
    // Audited Windows SDK represents these values as two unsigned 32-bit words.
    static_assert(sizeof(words.hi) == 4 && sizeof(words.lo) == 4);
    return (static_cast<std::uint64_t>(words.hi) << 32) | static_cast<std::uint64_t>(words.lo);
}
class TimingDiagnostics {
public:
    void capture(const ASIOTime* time, PositionSource source) noexcept {
        TimingSnapshot value;
        if (time) {
            value.valid = static_cast<std::uint32_t>(time->timeInfo.flags) & (kSamplePositionValid | kSystemTimeValid);
            if (value.valid & kSamplePositionValid) value.samplePosition = asioWords(time->timeInfo.samplePosition);
            if (value.valid & kSystemTimeValid) value.systemNanoseconds = asioWords(time->timeInfo.systemTime);
        } else if (source.get) {
            ASIOSamples position{}; ASIOTimeStamp timestamp{};
            if (source.get(source.driver,&position,&timestamp) == ASE_OK) {
                value = {asioWords(position),asioWords(timestamp),kSamplePositionValid | kSystemTimeValid};
            }
        }
        // A single writer holds AudioProcessor's nonreentrant flag. SC atomics
        // make the pair coherent without a data race or an RT lock.
        sequence_.fetch_add(1,std::memory_order_seq_cst);
        position_.store(value.samplePosition,std::memory_order_seq_cst);
        timestamp_.store(value.systemNanoseconds,std::memory_order_seq_cst);
        valid_.store(value.valid,std::memory_order_seq_cst);
        sequence_.fetch_add(1,std::memory_order_seq_cst);
    }
    TimingSnapshot snapshot() const noexcept {
        for (;;) {
            const auto before = sequence_.load(std::memory_order_seq_cst);
            if (before & 1) continue;
            const TimingSnapshot value{position_.load(std::memory_order_seq_cst),timestamp_.load(std::memory_order_seq_cst),
                valid_.load(std::memory_order_seq_cst)};
            if (before == sequence_.load(std::memory_order_seq_cst)) return value;
        }
    }
private:
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free && std::atomic<std::uint32_t>::is_always_lock_free);
    std::atomic<std::uint64_t> sequence_{0},position_{0},timestamp_{0};
    std::atomic<std::uint32_t> valid_{0};
};
}
