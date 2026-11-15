#pragma once
#include "win32_control.hpp"
namespace mka_asio_detail {
using ApartmentApi = mka_windows_detail::ApartmentApi;
using NativeEvent = mka_windows_detail::NativeEvent;
class ControlThread : public mka_windows_detail::ControlThread {
public:
    explicit ControlThread(ApartmentApi api = {})
        : mka_windows_detail::ControlThread(api, COINIT_APARTMENTTHREADED, L"MKA ASIO control") {}
};
}
