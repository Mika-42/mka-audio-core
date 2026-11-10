#include <gtest/gtest.h>
#include <atomic>
#include "../../../src/backend/windows/detail/asio_control.hpp"

namespace {
using namespace mka_asio_detail;
std::atomic<unsigned> messages{0};
WNDPROC original = nullptr;
LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_APP + 1) { ++messages; return 0; }
    return CallWindowProcW(original, window, message, wparam, lparam);
}
TEST(AsioControl, CommandsExceptionsAndNestedCallsRemainUsable) {
    ControlThread control;
    ASSERT_TRUE(SUCCEEDED(control.initialization()));
    EXPECT_NE(control.thread(), GetCurrentThreadId());
    EXPECT_EQ(control.invoke([&] { return GetCurrentThreadId(); }), control.thread());
    EXPECT_THROW(control.invoke([]() -> int { throw std::runtime_error("test"); }), std::runtime_error);
    EXPECT_EQ(control.invoke([&] { return control.invoke([] { return 42; }); }), 42);
    bool called = false; control.invoke([&] { called = true; }); EXPECT_TRUE(called);
}

TEST(AsioControl, TheOwnerPumpsWindowMessagesAndDestroysItsHiddenWindow) {
    HWND window = nullptr; messages = 0;
    {
        ControlThread control; ASSERT_TRUE(SUCCEEDED(control.initialization())); window = control.window();
        control.invoke([&] {
            original = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window, GWLP_WNDPROC));
            SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(windowProcedure));
        });
        ASSERT_TRUE(PostMessageW(window, WM_APP + 1, 0, 0));
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!messages.load() && std::chrono::steady_clock::now() < end) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        EXPECT_EQ(messages.load(), 1u);
    }
    EXPECT_FALSE(IsWindow(window));
}
}
