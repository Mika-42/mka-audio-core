#pragma once
#include <atomic>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include <asiosys.h>
#include <asio.h>
#include "asio_devices.hpp"
#include "asio_driver.hpp"

namespace mka_asio_detail {
// Native adapters consume their state on close, including cleanup after an
// error. No SDK type is exposed by the exported Backend interface.
inline Failure nativeOpen(const Request& request, void** out) noexcept {
    const auto result = failure(openDriver(request.id, out));
    if (result != Failure::none) return result;
    try {
        std::lock_guard lock(driverOwnership);
        return static_cast<Driver*>(*out)->validateChannels(request);
    }
    catch (...) { return Failure::configuration; }
}
inline ASIOError unavailableOperation(void*) noexcept { return ASE_NotPresent; }
struct Api {
    std::vector<Device> (*devices)() = nativeCapableDevices;
    Failure (*open)(const Request&, void**) noexcept = nativeOpen;
    ASIOError (*start)(void*) noexcept = unavailableOperation;
    ASIOError (*stop)(void*) noexcept = unavailableOperation;
    ASIOError (*close)(void**) noexcept = closeDriver;
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
        const auto result = api_.open(request, &pending);
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
