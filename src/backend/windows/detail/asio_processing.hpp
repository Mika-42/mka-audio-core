#pragma once
#include <atomic>
#include <chrono>
#include <thread>
#include <type_traits>
#include "asio_audio_buffers.hpp"
#include "asio_timing.hpp"

namespace mka_asio_detail {
struct ProcessingCallbacks {
    void* user = nullptr;
    void (*process)(void*,const float* const*,std::uint32_t,float* const*,std::uint32_t,std::uint32_t) noexcept = nullptr;
    bool (*read)(ChannelLayout,const std::byte*,std::size_t,float*,std::size_t) noexcept = nullptr;
    bool (*write)(ChannelLayout,std::byte*,std::size_t,const float*,std::size_t) noexcept = nullptr;
    void (*xrun)(void*) noexcept = nullptr;
    void (*failed)(void*) noexcept = nullptr;
};
class AudioProcessor {
public:
    explicit AudioProcessor(AudioBuffers& buffers) : buffers_(buffers) {}
    void configure(ProcessingCallbacks callbacks) noexcept { callbacks_ = callbacks; }
    bool enable() noexcept {
        if (failed()) return false;
        enabled_.store(true,std::memory_order_release); return true;
    }
    void disable() noexcept { enabled_.store(false,std::memory_order_release); }
    bool failed() const noexcept { return failed_.load(std::memory_order_acquire); }
    void setPositionSource(PositionSource source) noexcept { positionSource_ = source; }
    TimingSnapshot timing() const noexcept { return timing_.snapshot(); }
    void process(long index,const ASIOTime* time = nullptr) noexcept {
        if (!enabled_.load(std::memory_order_acquire)) return;
        if (index != 0 && index != 1) { fail(); return; }
        if (processing_.test_and_set(std::memory_order_acquire)) {
            if (callbacks_.xrun) callbacks_.xrun(callbacks_.user);
            return;
        }
        struct Leave {
            std::atomic_flag& flag;
            ~Leave() { flag.clear(std::memory_order_release); }
        } leave{processing_};
        if (!enabled_.load(std::memory_order_acquire) || failed()) return;
        timing_.capture(time,positionSource_);
        const auto frames = buffers_.frames();
        for (auto* channel : buffers_.outputChannels()) std::fill_n(channel,frames,0.0f);
        if (callbacks_.read) for (std::uint32_t channel = 0; channel < buffers_.inputs(); ++channel) {
            const auto layout = buffers_.layouts()[channel];
            if (!callbacks_.read(layout,static_cast<const std::byte*>(buffers_.native()[channel].buffers[index]),
                static_cast<std::size_t>(frames)*layout.bytes,buffers_.inputData(channel),frames)) { fail(); return; }
        }
        if (callbacks_.process) callbacks_.process(callbacks_.user,buffers_.inputChannels().data(),buffers_.inputs(),
            buffers_.outputChannels().data(),buffers_.outputs(),frames);
        if (!enabled_.load(std::memory_order_acquire) || failed()) return;
        if (callbacks_.write) for (std::uint32_t channel = 0; channel < buffers_.outputs(); ++channel) {
            const auto descriptor = static_cast<std::size_t>(buffers_.inputs()) + channel;
            const auto layout = buffers_.layouts()[descriptor];
            if (!callbacks_.write(layout,static_cast<std::byte*>(buffers_.native()[descriptor].buffers[index]),
                static_cast<std::size_t>(frames)*layout.bytes,buffers_.outputChannels()[channel],frames)) { fail(); return; }
        }
    }
private:
    void fail() noexcept {
        disable();
        if (!failed_.exchange(true,std::memory_order_acq_rel) && callbacks_.failed) callbacks_.failed(callbacks_.user);
    }
    AudioBuffers& buffers_;
    ProcessingCallbacks callbacks_;
    PositionSource positionSource_;
    TimingDiagnostics timing_;
    std::atomic<bool> enabled_{false}, failed_{false};
    std::atomic_flag processing_ = ATOMIC_FLAG_INIT;
};

// SDK callbacks have no userdata. A single owner is published only after all
// resources exist. Increment BEFORE loading it: retirement clears the owner,
// then waits outside RT for every reader that could have observed it.
class CallbackRoute {
public:
    static bool publish(AudioProcessor& processor) noexcept {
        AudioProcessor* expected = nullptr;
        return owner_.compare_exchange_strong(expected,&processor,std::memory_order_seq_cst)
            || expected == &processor;
    }
    template<class Function> static void visit(Function&& function) noexcept {
        static_assert(std::is_nothrow_invocable_v<Function,AudioProcessor&>);
        inFlight_.fetch_add(1,std::memory_order_seq_cst);
        if (auto* owner = owner_.load(std::memory_order_seq_cst)) function(*owner);
        inFlight_.fetch_sub(1,std::memory_order_seq_cst);
    }
    static void retire(AudioProcessor& processor) noexcept {
        auto* expected = &processor;
        owner_.compare_exchange_strong(expected,nullptr,std::memory_order_seq_cst);
        while (inFlight_.load(std::memory_order_seq_cst)) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    static bool empty() noexcept { return owner_.load(std::memory_order_seq_cst) == nullptr; }
private:
    static_assert(std::atomic<AudioProcessor*>::is_always_lock_free && std::atomic<std::uint64_t>::is_always_lock_free);
    inline static std::atomic<AudioProcessor*> owner_{nullptr};
    inline static std::atomic<std::uint64_t> inFlight_{0};
};
inline void sdkBufferSwitch(long index, ASIOBool) noexcept {
    CallbackRoute::visit([&](AudioProcessor& processor) noexcept { processor.process(index); });
}
inline ASIOTime* sdkTimeInfo(ASIOTime* time,long index,ASIOBool) noexcept {
    CallbackRoute::visit([&](AudioProcessor& processor) noexcept { processor.process(index,time); });
    return time;
}
inline long sdkMessage(long selector,long value,void*,double*) noexcept {
    if (selector == kAsioEngineVersion) return 2;
    if (selector == kAsioSupportsTimeInfo) return 1;
    if (selector == kAsioSelectorSupported)
        return value == kAsioEngineVersion || value == kAsioSupportsTimeInfo;
    return 0;
}
}
