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
    ASIOError start() override;
    ASIOError stop() override;
    ASIOError getChannels(long* input, long* output) override;
    ASIOError getLatencies(long* input, long* output) override { *input = 64; *output = 64; return ASE_OK; }
    ASIOError getBufferSize(long* min, long* max, long* preferred, long* granularity) override;
    ASIOError canSampleRate(ASIOSampleRate) override;
    ASIOError getSampleRate(ASIOSampleRate*) override;
    ASIOError setSampleRate(ASIOSampleRate) override;
    ASIOError getClockSources(ASIOClockSource*, long* count) override { *count = 0; return ASE_OK; }
    ASIOError setClockSource(long) override { return ASE_NotPresent; }
    ASIOError getSamplePosition(ASIOSamples*, ASIOTimeStamp*) override;
    ASIOError getChannelInfo(ASIOChannelInfo*) override;
    ASIOError createBuffers(ASIOBufferInfo*, long, long, ASIOCallbacks*) override;
    ASIOError disposeBuffers() override;
    ASIOError controlPanel() override { return ASE_NotPresent; }
    ASIOError future(long, void*) override { return ASE_InvalidParameter; }
    ASIOError outputReady() override;
private:
    void stopWorker() noexcept { worker_.request_stop(); if (worker_.joinable()) worker_.join(); }
    DriverFixture& owner_;
    std::atomic<ULONG> references_{1};
    std::jthread worker_;
};
struct DriverFixture {
    inline static DriverFixture* current = nullptr;
    LoaderApi api;
    std::vector<Device> registrations{{driverId, "Test ASIO"}};
    HRESULT creation = S_OK, apartment = S_OK;
    ASIOError channelsResult = ASE_OK;
    long inputs = 2, outputs = 2;
    unsigned channelQueries = 0;
    DWORD channelThread = 0;
    ASIOError buffersResult = ASE_OK;
    long minimum = 64, maximum = 1024, preferred = 256, granularity = -1;
    unsigned bufferQueries = 0;
    DWORD bufferThread = 0;
    ASIOError infoResult = ASE_OK;
    ASIOSampleType nativeFormat = ASIOSTFloat32LSB;
    std::vector<ASIOSampleType> inputFormats, outputFormats;
    std::vector<std::pair<bool, long>> infoRequests;
    bool wrongChannel = false, wrongDirection = false;
    DWORD infoThread = 0;
    ASIOSampleRate currentRate = 48000;
    std::vector<ASIOSampleRate> supportedRates{44100,48000,88200,96000,176400,192000};
    std::vector<ASIOSampleRate> rateRequests, setRequests, reportedRates;
    ASIOError canResult = ASE_OK, getResult = ASE_OK, setResult = ASE_OK;
    bool ignoreRateSet = false, mutateOnRateError = false;
    unsigned getCalls = 0, failGetNumber = 0;
    DWORD rateThread = 0;
    ASIOSampleRate channelsAtRate = 0, buffersAtRate = 0, infoAtRate = 0;
    ASIOError createBuffersResult = ASE_OK, disposeResult = ASE_OK;
    unsigned bufferCreations = 0, bufferDisposals = 0;
    DWORD createBuffersThread = 0, disposeThread = 0;
    long createdFrames = 0;
    ASIOCallbacks callbacks{};
    std::vector<ASIOBufferInfo> createdBuffers;
    std::vector<std::vector<std::byte>> storage;
    bool nullHalf = false, aliasedHalves = false, aliasedChannels = false;
    bool changedDescriptor = false, changedBufferDirection = false, wrappingPointer = false, fireColdCallbacks = false;
    ASIOError positionResult = ASE_SPNotAdvancing;
    ASIOSamples reportedPosition{};
    ASIOTimeStamp reportedTimestamp{};
    std::atomic<unsigned> positionCalls{0};
    std::atomic<DWORD> positionThread{0};
    ASIOError startResult = ASE_OK, stopResult = ASE_OK;
    std::atomic<ASIOError> readyResult{ASE_NotPresent};
    std::atomic<unsigned> starts{0}, stops{0}, readyCalls{0};
    std::atomic<DWORD> startThread{0}, stopThread{0}, readyThread{0};
    std::atomic<bool> nativeRunning{false};
    std::atomic<float> readyObservedSample{0};
    std::atomic<unsigned> currentHalf{0};
    bool fireStartCallback = false, partialStart = false, runAudioThread = false;
    ASIOSampleRate changeRateOnStart = 0;
    bool throwOnStop = false, throwOnDispose = false, throwOnSetRate = false, throwOnRelease = false;
    bool throwOnCreate = false, throwOnInfo = false, throwOnInit = false;
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
        EXPECT_TRUE(CallbackRoute::empty());
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
        const bool throws = owner_.throwOnRelease;
        stopWorker(); owner_.nativeRunning = false;
        owner_.storage.clear(); owner_.createdBuffers.clear();
        owner_.releaseThread = GetCurrentThreadId(); --owner_.live; ++owner_.destroyed; delete this;
        if (throws) throw std::runtime_error("mock Release failure after releasing ownership");
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
    if (owner_.throwOnInit) throw std::bad_alloc{};
    return owner_.acceptInit ? ASIOTrue : ASIOFalse;
}
inline void FakeDriver::getDriverName(char* name) { std::memcpy(name, "Test ASIO", 10); }
inline ASIOError FakeDriver::getChannels(long* input, long* output) {
    owner_.channelsAtRate = owner_.currentRate;
    ++owner_.channelQueries; owner_.channelThread = GetCurrentThreadId();
    *input = owner_.inputs; *output = owner_.outputs; return owner_.channelsResult;
}
inline ASIOError FakeDriver::getBufferSize(long* min, long* max, long* preferred, long* granularity) {
    owner_.buffersAtRate = owner_.currentRate;
    ++owner_.bufferQueries; owner_.bufferThread = GetCurrentThreadId();
    *min = owner_.minimum; *max = owner_.maximum; *preferred = owner_.preferred;
    *granularity = owner_.granularity; return owner_.buffersResult;
}
inline ASIOError FakeDriver::getChannelInfo(ASIOChannelInfo* info) {
    if (owner_.throwOnInfo) throw std::bad_alloc{};
    owner_.infoAtRate = owner_.currentRate;
    owner_.infoThread = GetCurrentThreadId();
    const bool input = info->isInput == ASIOTrue;
    owner_.infoRequests.push_back({input, info->channel});
    const auto& formats = input ? owner_.inputFormats : owner_.outputFormats;
    info->type = info->channel >= 0 && static_cast<std::size_t>(info->channel) < formats.size()
        ? formats[info->channel] : owner_.nativeFormat;
    if (owner_.wrongChannel) ++info->channel;
    if (owner_.wrongDirection) info->isInput = input ? ASIOFalse : ASIOTrue;
    return owner_.infoResult;
}
inline ASIOError FakeDriver::canSampleRate(ASIOSampleRate rate) {
    owner_.rateThread = GetCurrentThreadId(); owner_.rateRequests.push_back(rate);
    if (owner_.canResult != ASE_OK) return owner_.canResult;
    return std::find(owner_.supportedRates.begin(),owner_.supportedRates.end(),rate) != owner_.supportedRates.end()
        ? ASE_OK : ASE_NoClock;
}
inline ASIOError FakeDriver::getSampleRate(ASIOSampleRate* out) {
    owner_.rateThread = GetCurrentThreadId();
    const auto index = owner_.getCalls++;
    *out = owner_.reportedRates.empty() ? owner_.currentRate
        : owner_.reportedRates[std::min<std::size_t>(index,owner_.reportedRates.size()-1)];
    return !owner_.failGetNumber || owner_.getCalls == owner_.failGetNumber ? owner_.getResult : ASE_OK;
}
inline ASIOError FakeDriver::setSampleRate(ASIOSampleRate rate) {
    owner_.rateThread = GetCurrentThreadId(); owner_.setRequests.push_back(rate);
    if (owner_.throwOnSetRate) throw std::runtime_error("mock rate setter exception");
    if (!owner_.ignoreRateSet && (owner_.setResult == ASE_OK || owner_.mutateOnRateError)) owner_.currentRate = rate;
    return owner_.setResult;
}
inline ASIOError FakeDriver::createBuffers(ASIOBufferInfo* infos, long count, long frames, ASIOCallbacks* callbacks) {
    ++owner_.bufferCreations; owner_.createBuffersThread = GetCurrentThreadId(); owner_.createdFrames = frames;
    owner_.callbacks = *callbacks;
    if (count <= 0 || frames <= 0) return ASE_InvalidParameter;
    owner_.storage.resize(static_cast<std::size_t>(count) * 2);
    for (long index = 0; index < count; ++index) {
        const auto& formats = infos[index].isInput ? owner_.inputFormats : owner_.outputFormats;
        const auto type = infos[index].channelNum >= 0 && static_cast<std::size_t>(infos[index].channelNum) < formats.size()
            ? formats[infos[index].channelNum] : owner_.nativeFormat;
        const auto layout = channelLayout(type);
        if (!layout) return ASE_InvalidMode;
        for (std::size_t half = 0; half < 2; ++half) {
            auto& bytes = owner_.storage[static_cast<std::size_t>(index) * 2 + half];
            if (static_cast<std::size_t>(frames) > (std::numeric_limits<std::size_t>::max()-16) / layout->bytes)
                return ASE_NoMemory;
            bytes.assign(static_cast<std::size_t>(frames) * layout->bytes + 16, std::byte{0x5a});
            infos[index].buffers[half] = bytes.data() + 8;
        }
    }
    if (owner_.nullHalf) infos[0].buffers[1] = nullptr;
    if (owner_.aliasedHalves) infos[0].buffers[1] = infos[0].buffers[0];
    if (owner_.aliasedChannels && count > 1) infos[1].buffers[0] = infos[0].buffers[0];
    if (owner_.changedDescriptor) ++infos[0].channelNum;
    if (owner_.changedBufferDirection) infos[0].isInput = infos[0].isInput ? ASIOFalse : ASIOTrue;
    if (owner_.wrappingPointer) infos[0].buffers[0] = reinterpret_cast<void*>(std::numeric_limits<std::uintptr_t>::max()-1);
    owner_.createdBuffers.assign(infos,infos+count);
    if (owner_.throwOnCreate) throw std::bad_alloc{};
    if (owner_.fireColdCallbacks) {
        callbacks->bufferSwitch(0,ASIOTrue); callbacks->bufferSwitch(1,ASIOFalse);
        callbacks->sampleRateDidChange(owner_.currentRate);
        callbacks->bufferSwitchTimeInfo(nullptr,0,ASIOTrue);
    }
    return owner_.createBuffersResult;
}
inline ASIOError FakeDriver::disposeBuffers() {
    ++owner_.bufferDisposals; owner_.disposeThread = GetCurrentThreadId();
    if (owner_.throwOnDispose) throw std::runtime_error("mock DisposeBuffers exception");
    if (owner_.disposeResult == ASE_OK) {
        stopWorker(); owner_.nativeRunning = false; owner_.storage.clear(); owner_.createdBuffers.clear();
    }
    return owner_.disposeResult;
}
inline ASIOError FakeDriver::getSamplePosition(ASIOSamples* position,ASIOTimeStamp* timestamp) {
    ++owner_.positionCalls; owner_.positionThread = GetCurrentThreadId();
    *position = owner_.reportedPosition; *timestamp = owner_.reportedTimestamp; return owner_.positionResult;
}
inline ASIOError FakeDriver::start() {
    ++owner_.starts; owner_.startThread = GetCurrentThreadId();
    if (owner_.changeRateOnStart) owner_.currentRate = owner_.changeRateOnStart;
    if (owner_.fireStartCallback) owner_.callbacks.bufferSwitch(0,ASIOTrue);
    if (owner_.startResult == ASE_OK || owner_.partialStart) {
        owner_.nativeRunning = true;
        if (owner_.runAudioThread) worker_ = std::jthread([this](std::stop_token stop) {
            unsigned half = 0;
            while (!stop.stop_requested()) {
                owner_.currentHalf = half;
                owner_.callbacks.bufferSwitch(half,ASIOTrue);
                half ^= 1;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
    }
    return owner_.startResult;
}
inline ASIOError FakeDriver::stop() {
    ++owner_.stops; owner_.stopThread = GetCurrentThreadId();
    if (owner_.throwOnStop) throw std::runtime_error("mock Stop exception");
    if (owner_.stopResult == ASE_OK) { stopWorker(); owner_.nativeRunning = false; }
    return owner_.stopResult;
}
inline ASIOError FakeDriver::outputReady() {
    ++owner_.readyCalls; owner_.readyThread = GetCurrentThreadId();
    if (owner_.nativeFormat == ASIOSTFloat32LSB) for (const auto& info : owner_.createdBuffers) if (!info.isInput) {
        float value=0; std::memcpy(&value,info.buffers[owner_.currentHalf.load()],sizeof(value));
        owner_.readyObservedSample = value; break;
    }
    return owner_.readyResult.load();
}
}
