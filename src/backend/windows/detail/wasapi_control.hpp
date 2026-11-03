#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

namespace mka_wasapi_detail {
// Private seams retain the exact Windows ABI. No SDK type enters Backend's API.
struct ComApi {
    decltype(&CoInitializeEx) initialize = &CoInitializeEx;
    decltype(&CoUninitialize) uninitialize = &CoUninitialize;
    decltype(&CoCreateInstance) create = &CoCreateInstance;
};

// Every COM object is acquired and released in this persistent MTA. Public
// control calls may originate on another thread, including an application's STA.
class ControlThread {
public:
    explicit ControlThread(ComApi api = {}) : api_(api) {
        std::promise<HRESULT> ready;
        auto result = ready.get_future();
        worker_ = std::jthread([this, &ready](std::stop_token stop) {
            const HRESULT initialized = api_.initialize(nullptr, COINIT_MULTITHREADED);
            ready.set_value(initialized);
            if (FAILED(initialized)) return;
            // S_OK and S_FALSE both acquire one COM initialization reference.
            struct Balance {
                ComApi& api;
                ~Balance() { api.uninitialize(); }
            } balance{api_};
            for (;;) {
                std::function<void()> task;
                {
                    std::unique_lock lock(mutex_);
                    wake_.wait(lock, [&] { return stop.stop_requested() || !tasks_.empty(); });
                    if (tasks_.empty() && stop.stop_requested()) break;
                    task = std::move(tasks_.front());
                    tasks_.pop_front();
                }
                task(); // packaged_task forwards exceptions to its waiting caller
            }
        });
        initialized_ = result.get();
    }
    ~ControlThread() {
        {
            std::lock_guard lock(mutex_);
            worker_.request_stop();
        }
        wake_.notify_all();
        if (worker_.joinable()) worker_.join();
    }
    ControlThread(const ControlThread&) = delete;
    ControlThread& operator=(const ControlThread&) = delete;
    HRESULT initialization() const noexcept { return initialized_; }
    const ComApi& api() const noexcept { return api_; }

    template<class Function>
    auto invoke(Function&& function) -> std::invoke_result_t<Function> {
        if (FAILED(initialized_)) throw std::runtime_error("WASAPI control COM initialization failed");
        if (std::this_thread::get_id() == worker_.get_id())
            return std::invoke(std::forward<Function>(function));
        using Return = std::invoke_result_t<Function>;
        auto task = std::make_shared<std::packaged_task<Return()>>(std::forward<Function>(function));
        auto result = task->get_future();
        {
            std::lock_guard lock(mutex_);
            tasks_.emplace_back([task] { (*task)(); });
        }
        wake_.notify_one();
        return result.get();
    }
private:
    ComApi api_;
    HRESULT initialized_ = E_FAIL;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<std::function<void()>> tasks_;
    std::jthread worker_; // destroyed before synchronization and COM state
};
}
