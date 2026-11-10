#pragma once
#include <gtest/gtest.h>
#include <cstring>
#include "../../../src/backend/windows/detail/asio_driver.hpp"

namespace asio_test {
using namespace mka_asio_detail;
inline constexpr const char* driverId = "asio:{aaaaaaaa-1111-2222-3333-444444444444}";
struct DriverFixture;
class FakeDriver final : public IASIO {
public:
    explicit FakeDriver(DriverFixture& owner);
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override;
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override;
    ASIOBool init(void* window) override;
    void getDriverName(char* name) override;
    long getDriverVersion() override { return 123; }
    void getErrorMessage(char* text) override { std::memcpy(text, "init refused", 13); }
    ASIOError start() override { return ASE_InvalidMode; }
    ASIOError stop() override { return ASE_OK; }
    ASIOError getChannels(long* input, long* output) override { *input = 2; *output = 2; return ASE_OK; }
    ASIOError getLatencies(long* input, long* output) override { *input = 64; *output = 64; return ASE_OK; }
    ASIOError getBufferSize(long* min, long* max, long* preferred, long* granularity) override {
        *min = 64; *max = 1024; *preferred = 256; *granularity = -1; return ASE_OK;
    }
    ASIOError canSampleRate(ASIOSampleRate) override { return ASE_OK; }
    ASIOError getSampleRate(ASIOSampleRate* out) override { *out = 48000; return ASE_OK; }
    ASIOError setSampleRate(ASIOSampleRate) override { return ASE_OK; }
    ASIOError getClockSources(ASIOClockSource*, long* count) override { *count = 0; return ASE_OK; }
    ASIOError setClockSource(long) override { return ASE_NotPresent; }
    ASIOError getSamplePosition(ASIOSamples*, ASIOTimeStamp*) override { return ASE_SPNotAdvancing; }
    ASIOError getChannelInfo(ASIOChannelInfo*) override { return ASE_InvalidParameter; }
    ASIOError createBuffers(ASIOBufferInfo*, long, long, ASIOCallbacks*) override { return ASE_InvalidMode; }
    ASIOError disposeBuffers() override { return ASE_OK; }
    ASIOError controlPanel() override { return ASE_NotPresent; }
    ASIOError future(long, void*) override { return ASE_InvalidParameter; }
    ASIOError outputReady() override { return ASE_NotPresent; }
private:
    DriverFixture& owner_;
    std::atomic<ULONG> references_{1};
};
struct DriverFixture {
    inline static DriverFixture* current = nullptr;
    LoaderApi api;
    std::vector<Device> registrations{{driverId, "Test ASIO"}};
    HRESULT creation = S_OK, apartment = S_OK;
    bool acceptInit = true, nullInterface = false, partialFailure = false, sta = false, validWindow = false;
    unsigned live = 0, destroyed = 0, creates = 0, inits = 0, comAttempts = 0, comReferences = 0, uninitializes = 0;
    DWORD creationThread = 0, initThread = 0, releaseThread = 0, uninitializeThread = 0;
    HWND lastWindow = nullptr;
    DriverFixture() {
        EXPECT_EQ(current, nullptr); current = this;
        api.devices = +[] { return current->registrations; };
        api.apartment.initialize = +[](void*, DWORD flags) -> HRESULT {
            auto& f = *current; ++f.comAttempts;
            EXPECT_EQ(flags, static_cast<DWORD>(COINIT_APARTMENTTHREADED));
            if (FAILED(f.apartment)) return f.apartment;
            const auto result = CoInitializeEx(nullptr, flags);
            if (SUCCEEDED(result)) ++f.comReferences;
            return SUCCEEDED(result) && f.apartment == S_FALSE ? S_FALSE : result;
        };
        api.apartment.uninitialize = +[] {
            auto& f = *current; ++f.uninitializes; f.uninitializeThread = GetCurrentThreadId(); CoUninitialize();
        };
        api.create = +[](REFCLSID clsid, LPUNKNOWN outer, DWORD context, REFIID iid, void** out) -> HRESULT {
            auto& f = *current; ++f.creates; f.creationThread = GetCurrentThreadId(); *out = nullptr;
            EXPECT_EQ(outer, nullptr); EXPECT_EQ(context, static_cast<DWORD>(CLSCTX_INPROC_SERVER));
            EXPECT_TRUE(IsEqualGUID(clsid, iid));
            if (FAILED(f.creation) && !f.partialFailure) return f.creation;
            if (f.nullInterface) return S_OK;
            auto* driver = new FakeDriver(f);
            if (f.partialFailure) { *out = static_cast<IASIO*>(driver); return f.creation; }
            const auto result = driver->QueryInterface(iid, out); driver->Release(); return result;
        };
        testLoaderOverride = &api;
    }
    ~DriverFixture() {
        testLoaderOverride = nullptr;
        EXPECT_EQ(live, 0u); EXPECT_EQ(comReferences, uninitializes);
        EXPECT_EQ(activeDriver, nullptr); current = nullptr;
    }
};
inline FakeDriver::FakeDriver(DriverFixture& owner) : owner_(owner) { ++owner_.live; }
inline HRESULT FakeDriver::QueryInterface(REFIID iid, void** out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    const auto clsid = driverClass(driverId);
    if (!IsEqualGUID(iid, __uuidof(IUnknown)) && (!clsid || !IsEqualGUID(iid, *clsid))) return E_NOINTERFACE;
    *out = static_cast<IASIO*>(this); AddRef(); return S_OK;
}
inline ULONG FakeDriver::Release() {
    const auto count = --references_;
    if (!count) {
        owner_.releaseThread = GetCurrentThreadId(); --owner_.live; ++owner_.destroyed; delete this;
    }
    return count;
}
inline ASIOBool FakeDriver::init(void* window) {
    ++owner_.inits; owner_.initThread = GetCurrentThreadId(); owner_.lastWindow = static_cast<HWND>(window);
    owner_.validWindow = window && IsWindow(owner_.lastWindow)
        && GetWindowThreadProcessId(owner_.lastWindow, nullptr) == GetCurrentThreadId();
    APTTYPE type{}; APTTYPEQUALIFIER qualifier{};
    owner_.sta = SUCCEEDED(CoGetApartmentType(&type, &qualifier))
        && (type == APTTYPE_STA || type == APTTYPE_MAINSTA);
    return owner_.acceptInit ? ASIOTrue : ASIOFalse;
}
inline void FakeDriver::getDriverName(char* name) { std::memcpy(name, "Test ASIO", 10); }
}
