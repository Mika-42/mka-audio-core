#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <memory>
#include <new>
#include <stdexcept>
#include <thread>
#include <vector>

// Include <chrono> and <thread> before importing mka.audio.backend.abstract,
// then include this fixture. GCC must see those headers before their module use.
// A resource-owning core fixture, with a real processing worker and the
// production event dispatcher.
namespace lifecycle_test {
using namespace mka::audio::core;

enum class Hook { None, Discover, Open, Start, Stop, Close };
enum class Exception { Allocation, Standard, NonStandard };

struct Counters {
    int opens = 0, starts = 0, stops = 0, closes = 0;
    std::atomic<int> resources{0}, workers{0};
};

class LifecycleBackend : public Backend {
public:
    explicit LifecycleBackend(Counters& counters) : counters_(counters) {}
    ~LifecycleBackend() override {
        stopDispatcher();
        quiesce();
    }

    void failAt(Hook hook, Exception exception = Exception::Standard) {
        fault_ = hook;
        exception_ = exception;
    }
    void* storedUser() const { return userData; }

protected:
    void throwAt(Hook hook) const {
        if (fault_ != hook) return;
        switch (exception_) {
            case Exception::Allocation: throw std::bad_alloc{};
            case Exception::Standard: throw std::runtime_error("injected hook failure");
            case Exception::NonStandard: throw 42;
        }
    }

    struct Resource {
        explicit Resource(Counters& counters) : counters(counters) { ++counters.resources; }
        ~Resource() { --counters.resources; }
        Counters& counters;
        std::array<float, 32> samples{};
        std::array<float*, 1> channels{samples.data()};
    };

    std::vector<Endpoint> getEndPoints_() const override {
        throwAt(Hook::Discover);
        return {Endpoint{.id = "test:output", .name = "Lifecycle fixture"}};
    }
    Result open_(const EndpointConfig&) override {
        ++counters_.opens;
        auto pending = std::make_unique<Resource>(counters_);
        throwAt(Hook::Open);
        resource_ = std::move(pending);
        return {};
    }
    Result start_() override {
        ++counters_.starts;
        processEnabled_.store(false);
        auto pending = std::jthread([this](std::stop_token stop) {
            ++counters_.workers;
            const AudioProcessContext context{
                .input = {nullptr, 0}, .output = {resource_->channels.data(), 1}, .frames = 32
            };
            while (!stop.stop_requested()) {
                if (!processEnabled_.load()) continue;
                std::fill(resource_->samples.begin(), resource_->samples.end(), 0.0f);
                if (callback) callback(userData, context);
            }
            --counters_.workers;
        });
        // A throwing hook owns rollback of its partial native resources. The
        // local jthread requests stop and joins without invoking user code.
        throwAt(Hook::Start);
        worker_ = std::move(pending);
        notifyXRun();
        processEnabled_.store(true);
        return {};
    }
    Result stop_() override {
        ++counters_.stops;
        throwAt(Hook::Stop);
        quiesce();
        return {};
    }
    Result close_() override {
        ++counters_.closes;
        throwAt(Hook::Close);
        resource_.reset();
        return {};
    }
    void quiesce() {
        if (worker_.joinable()) {
            worker_.request_stop();
            worker_.join();
        }
    }

    Counters& counters_;
    std::unique_ptr<Resource> resource_;
    std::jthread worker_;
    std::atomic<bool> processEnabled_{false};
    Hook fault_ = Hook::None;
    Exception exception_ = Exception::Standard;
};

template<class Predicate>
bool waitUntil(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::yield();
    }
    return true;
}
}
