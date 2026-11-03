#include "../../src/backend/windows/detail/wasapi_control.hpp"
#include <atomic>
#include <gtest/gtest.h>
using namespace mka_wasapi_detail;
namespace {
std::atomic<unsigned> initialized{0}, uninitialized{0};
std::atomic<DWORD> initializationThread{0}, releaseThread{0};
HRESULT forced = S_OK;
HRESULT WINAPI initialize(void*, DWORD flags) {
    EXPECT_EQ(flags, static_cast<DWORD>(COINIT_MULTITHREADED));
    ++initialized; initializationThread = GetCurrentThreadId(); return forced;
}
void WINAPI uninitialize() { ++uninitialized; releaseThread = GetCurrentThreadId(); }
}
TEST(WasapiCom, BalancesSuccessfulAndAlreadyInitializedResultsOnTheOwningThread) {
    for (const auto result : {S_OK, S_FALSE}) {
        forced = result; initialized = 0; uninitialized = 0;
        {
            ControlThread control({initialize, uninitialize, &CoCreateInstance});
            EXPECT_EQ(control.initialization(), result);
            EXPECT_EQ(control.invoke([] { return GetCurrentThreadId(); }), initializationThread.load());
            EXPECT_NE(initializationThread.load(), GetCurrentThreadId());
            EXPECT_EQ(uninitialized.load(), 0u);
        }
        EXPECT_EQ(initialized.load(), 1u); EXPECT_EQ(uninitialized.load(), 1u);
        EXPECT_EQ(initializationThread.load(), releaseThread.load());
    }
}
TEST(WasapiCom, FailedInitializationNeverUninitializesOrRunsACommand) {
    for (const auto result : {E_OUTOFMEMORY, RPC_E_CHANGED_MODE, E_FAIL}) {
        forced = result; initialized = 0; uninitialized = 0;
        ControlThread control({initialize, uninitialize, &CoCreateInstance});
        EXPECT_EQ(control.initialization(), result);
        EXPECT_THROW(control.invoke([] {}), std::runtime_error);
        EXPECT_EQ(uninitialized.load(), 0u);
    }
}
TEST(WasapiCom, RealWindowsMtaIsIndependentOfTheCallingApartment) {
    ControlThread control;
    ASSERT_TRUE(SUCCEEDED(control.initialization()));
    control.invoke([] {
        APTTYPE type; APTTYPEQUALIFIER qualifier;
        ASSERT_EQ(CoGetApartmentType(&type, &qualifier), S_OK);
        EXPECT_EQ(type, APTTYPE_MTA);
    });
    EXPECT_THROW(control.invoke([] { throw std::runtime_error("operation"); }), std::runtime_error);
    EXPECT_EQ(control.invoke([&] { return control.invoke([] { return 42; }); }), 42);
}
