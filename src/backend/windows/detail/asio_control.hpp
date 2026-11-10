#pragma once
#include <chrono>
#include <condition_variable>
#include <exception>
#include <functional>
#include <future>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>

namespace mka_asio_detail {
struct ApartmentApi {
    decltype(&CoInitializeEx) initialize = &CoInitializeEx;
    decltype(&CoUninitialize) uninitialize = &CoUninitialize;
};
class NativeEvent {
public:
    NativeEvent() : value_(CreateEventW(nullptr, FALSE, FALSE, nullptr)) {
        if (!value_) throw std::runtime_error("ASIO control event creation failed");
    }
    ~NativeEvent() { CloseHandle(value_); }
    NativeEvent(const NativeEvent&) = delete;
    NativeEvent& operator=(const NativeEvent&) = delete;
    HANDLE get() const noexcept { return value_; }
private:
    HANDLE value_;
};

// ASIO driver calls are serialized in an STA with a message pump and a valid,
// hidden top-level window. This thread is never used by the audio callback.
class ControlThread {
public:
    explicit ControlThread(ApartmentApi api = {}) : api_(api) {
        auto ready = ready_.get_future();
        worker_ = std::jthread([this](std::stop_token stop) { run(stop); });
        initialization_ = ready.get();
    }
    ~ControlThread() {
        worker_.request_stop();
        SetEvent(wake_.get());
        if (worker_.joinable()) worker_.join();
    }
    ControlThread(const ControlThread&) = delete;
    ControlThread& operator=(const ControlThread&) = delete;
    HRESULT initialization() const noexcept { return initialization_; }
    HWND window() const noexcept { return window_; }
    DWORD thread() const noexcept { return thread_; }

    template<class Function>
    auto invoke(Function&& function) -> std::invoke_result_t<Function> {
        if (FAILED(initialization_)) throw std::runtime_error("ASIO COM initialization failed");
        if (GetCurrentThreadId() == thread_) return std::invoke(std::forward<Function>(function));
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
        } work(function);
        std::lock_guard serial(serial_);
        std::unique_lock lock(mutex_);
        if (stopped_) throw std::runtime_error("ASIO control worker stopped");
        completed_ = false;
        pending_ = &work;
        SetEvent(wake_.get());
        finished_.wait(lock, [&] { return completed_ || stopped_; });
        if (!completed_) throw std::runtime_error("ASIO control command was interrupted");
        if (work.error) std::rethrow_exception(work.error);
        if constexpr (!std::is_void_v<Return>) return std::move(*work.value);
    }
private:
    struct Command { virtual void execute() noexcept = 0; };
    void run(std::stop_token stop) {
        thread_ = GetCurrentThreadId();
        const auto initialized = api_.initialize(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(initialized)) { ready_.set_value(initialized); return; }
        struct Balance {
            ApartmentApi& api;
            ~Balance() { api.uninitialize(); }
        } balance{api_};
        window_ = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"MKA ASIO control", WS_OVERLAPPED,
            0, 0, 1, 1, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!window_) {
            const auto error = GetLastError();
            ready_.set_value(error ? HRESULT_FROM_WIN32(error) : E_FAIL);
            return;
        }
        struct Window {
            HWND value;
            ~Window() { DestroyWindow(value); }
        } window{window_};
        ready_.set_value(initialized);
        const HANDLE wake = wake_.get();
        while (!stop.stop_requested()) {
            const auto result = MsgWaitForMultipleObjectsEx(1, &wake, 100, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            if (stop.stop_requested()) break;
            if (result == WAIT_OBJECT_0 + 1) {
                MSG message{};
                for (unsigned count = 0; count < 64 && !stop.stop_requested()
                     && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE); ++count) {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
            }
            Command* command = nullptr;
            {
                std::lock_guard lock(mutex_);
                command = std::exchange(pending_, nullptr);
            }
            if (command) {
                command->execute();
                { std::lock_guard lock(mutex_); completed_ = true; }
                finished_.notify_one();
            }
            // An unexpected wait failure still permits command/stop progress.
            if (result == WAIT_FAILED) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        { std::lock_guard lock(mutex_); stopped_ = true; }
        finished_.notify_all();
    }
    ApartmentApi api_;
    NativeEvent wake_;
    HRESULT initialization_ = E_FAIL;
    HWND window_ = nullptr;
    DWORD thread_ = 0;
    std::mutex mutex_, serial_;
    std::condition_variable finished_;
    Command* pending_ = nullptr;
    bool completed_ = false, stopped_ = false;
    std::promise<HRESULT> ready_;
    std::jthread worker_;
};
}
