#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <condition_variable>
#include <atomic>
#include <functional>
#include <optional>
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

#if defined(MKA_WASAPI_TESTING)
// Only test builds can replace the native factory; no constructor/SDK parameter
// is added to Wasapi's portable public interface.
inline std::atomic<const ComApi*> testApiOverride{nullptr};
#endif
inline ComApi defaultComApi() noexcept {
#if defined(MKA_WASAPI_TESTING)
    if(const auto* api=testApiOverride.load(std::memory_order_acquire))return *api;
#endif
    return {};
}

// Every COM object is acquired and released in this persistent MTA. Public
// control calls may originate on another thread, including an application's STA.
class ControlThread {
public:
    explicit ControlThread(ComApi api = defaultComApi()) : api_(api) {
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
                Command* task = nullptr;
                {
                    std::unique_lock lock(mutex_);
                    wake_.wait(lock, [&] { return stop.stop_requested() || pending_; });
                    if (!pending_ && stop.stop_requested()) break;
                    task = pending_;
                    pending_ = nullptr;
                }
                task->execute();
                {
                    std::lock_guard lock(mutex_);
                    completed_ = true;
                }
                finished_.notify_one();
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
        static_assert(!std::is_reference_v<Return>);
        struct Work final : Command {
            Function& function;
            std::optional<std::conditional_t<std::is_void_v<Return>, bool, Return>> value;
            std::exception_ptr error;
            explicit Work(Function& f) : function(f) {}
            void execute() noexcept override {
                try {
                    if constexpr (std::is_void_v<Return>) { std::invoke(function); value = true; }
                    else value.emplace(std::invoke(function));
                } catch (...) { error = std::current_exception(); }
            }
        } task(function);
        // The synchronous command lives on its caller's stack. Teardown does
        // not depend on allocating a queue node or a packaged_task.
        std::lock_guard serial(serial_);
        std::unique_lock lock(mutex_);
        completed_ = false;
        pending_ = &task;
        wake_.notify_one();
        finished_.wait(lock, [&] { return completed_; });
        if (task.error) std::rethrow_exception(task.error);
        if constexpr (!std::is_void_v<Return>) return std::move(*task.value);
    }
private:
    struct Command { virtual void execute() noexcept = 0; };
    ComApi api_;
    HRESULT initialized_ = E_FAIL;
    std::mutex serial_, mutex_;
    std::condition_variable wake_, finished_;
    Command* pending_ = nullptr;
    bool completed_ = false;
    std::jthread worker_; // destroyed before synchronization and COM state
};
}
