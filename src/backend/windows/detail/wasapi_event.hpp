#pragma once
#include "wasapi_control.hpp"
#include <avrt.h>
namespace mka_wasapi_detail {
struct EventApi {
    decltype(&CreateEventW) create = &CreateEventW;
    decltype(&CloseHandle) close = &CloseHandle;
    decltype(&SetEvent) signal = &SetEvent;
    decltype(&WaitForMultipleObjects) wait = &WaitForMultipleObjects;
    decltype(&ResetEvent) reset = &ResetEvent;
    decltype(&AvSetMmThreadCharacteristicsW) enterAudio = &AvSetMmThreadCharacteristicsW;
    decltype(&AvRevertMmThreadCharacteristics) leaveAudio = &AvRevertMmThreadCharacteristics;
};
class UniqueEvent {
public:
    UniqueEvent() = default;
    UniqueEvent(HANDLE handle, decltype(&CloseHandle) close) : handle_(handle), close_(close) {}
    ~UniqueEvent() { reset(); }
    UniqueEvent(const UniqueEvent&) = delete;
    UniqueEvent& operator=(const UniqueEvent&) = delete;
    UniqueEvent(UniqueEvent&& other) noexcept : handle_(std::exchange(other.handle_,nullptr)),close_(other.close_) {}
    UniqueEvent& operator=(UniqueEvent&& other) noexcept {
        if (this != &other) {reset();handle_=std::exchange(other.handle_,nullptr);close_=other.close_;}return *this;
    }
    void reset() noexcept { if(handle_) {close_(handle_);handle_=nullptr;} }
    HANDLE get() const noexcept { return handle_; }
private:
    HANDLE handle_ = nullptr;
    decltype(&CloseHandle) close_ = &CloseHandle;
};
inline HRESULT lastEventError() noexcept { const auto error=GetLastError();return error?HRESULT_FROM_WIN32(error):E_FAIL; }
}
