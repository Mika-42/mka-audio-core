#pragma once
#include <atomic>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include <asiosys.h>
#include <asio.h>
#include "asio_devices.hpp"

namespace mka_asio_detail {
enum class Failure { none, endpoint, format, channels, rate, buffer, configuration };
enum class Flow { input, output, duplex };
enum class Sample { i16, i24, i32, f32, f64 };
struct Request {
    std::string id;
    Flow flow;
    std::uint32_t inputs, outputs, rate, frames;
    Sample sample;
};
inline Failure failure(ASIOError error) noexcept {
    switch (error) {
        case ASE_OK: return Failure::none;
        case ASE_NotPresent: case ASE_HWMalfunction: return Failure::endpoint;
        case ASE_NoClock: return Failure::rate;
        default: return Failure::configuration;
    }
}

// Native adapters consume their state on close, including cleanup after an
// error. No SDK type is exposed by the exported Backend interface.
inline ASIOError unavailable(const Request&, void** out) noexcept { *out = nullptr; return ASE_NotPresent; }
inline ASIOError unavailableOperation(void*) noexcept { return ASE_NotPresent; }
inline ASIOError releaseEmpty(void** state) noexcept { *state = nullptr; return ASE_OK; }
struct Api {
    std::vector<Device> (*devices)() = nativeDevices;
    ASIOError (*open)(const Request&, void**) noexcept = unavailable;
    ASIOError (*start)(void*) noexcept = unavailableOperation;
    ASIOError (*stop)(void*) noexcept = unavailableOperation;
    ASIOError (*close)(void**) noexcept = releaseEmpty;
};
#if defined(MKA_ASIO_TESTING)
inline std::atomic<const Api*> testApiOverride{nullptr};
#endif
inline Api defaultApi() noexcept {
#if defined(MKA_ASIO_TESTING)
    if (const auto* api = testApiOverride.load(std::memory_order_acquire)) return *api;
#endif
    return {};
}

class Session {
public:
    Session() : api_(defaultApi()) {}
    ~Session() { (void)close(); }
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    std::vector<Device> devices() const { return api_.devices ? api_.devices() : std::vector<Device>{}; }
    Failure open(const Request& request) noexcept {
        if (state_ || !api_.open || !api_.close) return Failure::configuration;
        void* pending = nullptr;
        const auto error = api_.open(request, &pending);
        const auto result = failure(error);
        if (result != Failure::none || !pending) {
            if (pending) (void)api_.close(&pending);
            return result == Failure::none ? Failure::configuration : result;
        }
        state_ = pending;
        return Failure::none;
    }
    Failure start() noexcept {
        if (!state_ || !api_.start) return Failure::configuration;
        return failure(api_.start(state_));
    }
    Failure stop() noexcept {
        if (!state_) return Failure::none;
        if (!api_.stop) return Failure::configuration;
        return failure(api_.stop(state_));
    }
    Failure close() noexcept {
        if (!state_) return Failure::none;
        auto* owned = std::exchange(state_, nullptr);
        return failure(api_.close(&owned));
    }
private:
    const Api api_;
    void* state_ = nullptr;
};
}
