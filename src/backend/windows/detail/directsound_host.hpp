#pragma once
#include <atomic>
#include <utility>
#include "directsound_types.hpp"
#include "directsound_devices.hpp"

namespace mka_ds_detail {
// Subsequent native buffer cards replace these unavailable transport adapters.
// Until a native stream exists, the production backend cannot report Open.
inline Failure nativeOpen(const Request&, void** state) noexcept {
    *state = nullptr;
    return Failure::endpoint;
}
inline Failure nativeStart(void*, const ProcessingCallbacks&) noexcept { return Failure::configuration; }
inline Failure nativeStop(void*) noexcept { return Failure::none; }
inline Failure nativeClose(void** state) noexcept { *state = nullptr; return Failure::none; }
struct Api {
    std::vector<Device> (*devices)() = nativeDevices;
    Failure (*open)(const Request&, void**) noexcept = nativeOpen;
    Failure (*start)(void*, const ProcessingCallbacks&) noexcept = nativeStart;
    Failure (*stop)(void*) noexcept = nativeStop;
    // Must consume the owned state even if cleanup reports an error.
    Failure (*close)(void**) noexcept = nativeClose;
};
#if defined(MKA_DIRECTSOUND_TESTING)
inline std::atomic<const Api*> testApiOverride{nullptr};
#endif
inline Api defaultApi() noexcept {
#if defined(MKA_DIRECTSOUND_TESTING)
    if (const auto* api = testApiOverride.load(std::memory_order_acquire)) return *api;
#endif
    return {};
}

// Owns one native session. Calls are serialized by Backend's control mutex.
// Stop/close are non-RT and the native close adapter must quiesce its worker.
class Session {
public:
    Session() : api_(defaultApi()) {}
    ~Session() { (void)stop(); (void)close(); }
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    std::vector<Device> devices() const { return api_.devices ? api_.devices() : std::vector<Device>{}; }
    Failure open(const Request& request) noexcept {
        if (state_ || !api_.open || !api_.close) return Failure::configuration;
        void* pending = nullptr;
        const auto result = api_.open(request, &pending);
        if (result != Failure::none || !pending) {
            if (pending) (void)api_.close(&pending);
            return result == Failure::none ? Failure::configuration : result;
        }
        state_ = pending;
        return Failure::none;
    }
    Failure start(const ProcessingCallbacks& callbacks) noexcept {
        if (!state_ || running_ || !api_.start) return Failure::configuration;
        const auto result = api_.start(state_, callbacks);
        if (result == Failure::none) running_ = true;
        return result;
    }
    Failure stop() noexcept {
        if (!state_ || !running_) return Failure::none;
        if (!api_.stop) return Failure::configuration;
        const auto result = api_.stop(state_);
        if (result == Failure::none) running_ = false;
        return result;
    }
    Failure close() noexcept {
        if (!state_) return Failure::none;
        auto* owned = std::exchange(state_, nullptr);
        running_ = false;
        return api_.close(&owned);
    }
private:
    const Api api_;
    void* state_ = nullptr;
    bool running_ = false;
};
}
