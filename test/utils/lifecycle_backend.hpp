#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

// Include after importing mka.audio.backend.abstract. A resource-owning core
// fixture, with a real processing worker and the production event dispatcher.
namespace lifecycle_test {
using namespace mka::audio::core;

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

protected:
    struct Resource {
        explicit Resource(Counters& counters) : counters(counters) { ++counters.resources; }
        ~Resource() { --counters.resources; }
        Counters& counters;
        std::array<float, 32> samples{};
        std::array<float*, 1> channels{samples.data()};
    };

    std::vector<Endpoint> getEndPoints_() const override { return {}; }
    Result open_(const EndpointConfig&) override {
        ++counters_.opens;
        resource_ = std::make_unique<Resource>(counters_);
        return {};
    }
    Result start_() override {
        ++counters_.starts;
        // Notification precedes dispatcher creation; no timer is needed by tests.
        notifyXRun();
        worker_ = std::jthread([this](std::stop_token stop) {
            ++counters_.workers;
            const AudioProcessContext context{
                .input = {nullptr, 0}, .output = {resource_->channels.data(), 1}, .frames = 32
            };
            while (!stop.stop_requested()) {
                std::fill(resource_->samples.begin(), resource_->samples.end(), 0.0f);
                if (callback) callback(userData, context);
            }
            --counters_.workers;
        });
        return {};
    }
    Result stop_() override {
        ++counters_.stops;
        quiesce();
        return {};
    }
    Result close_() override {
        ++counters_.closes;
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
