#pragma once
#include "asio_devices.hpp"
#include "asio_control.hpp"
#include "asio_types.hpp"
#include "asio_buffer_limits.hpp"
#include "asio_channel_layout.hpp"
#include "asio_rates.hpp"
#include "asio_audio_buffers.hpp"
#include "asio_processing.hpp"
#include <asiosys.h>
#include <asio.h>
#include <iasiodrv.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <new>

namespace mka_asio_detail {
struct LoaderApi {
    std::vector<Device> (*devices)() = nativeDevices;
    ApartmentApi apartment;
    decltype(&CoCreateInstance) create = &CoCreateInstance;
};
#if defined(MKA_ASIO_TESTING)
inline std::atomic<const LoaderApi*> testLoaderOverride{nullptr};
#endif
inline LoaderApi loaderApi() noexcept {
#if defined(MKA_ASIO_TESTING)
    if (const auto* api = testLoaderOverride.load(std::memory_order_acquire)) return *api;
#endif
    return {};
}
inline std::optional<CLSID> driverClass(const std::string& id) {
    if (id.size() != 43 || id.compare(0, 5, "asio:") != 0) return {};
    const std::wstring text(id.begin() + 5, id.end());
    const auto normalized = asioClassId(text);
    if (!normalized) return {};
    CLSID clsid{};
    if (FAILED(CLSIDFromString(normalized->c_str(), &clsid))) return {};
    return clsid;
}

class Driver {
public:
    explicit Driver(LoaderApi api) : api_(api), control_(api.apartment) {}
    ~Driver() { (void)close(); }
    ASIOError open(const CLSID& clsid) {
        if (FAILED(control_.initialization()))
            return control_.initialization() == E_OUTOFMEMORY ? ASE_NoMemory : ASE_NotPresent;
        return control_.invoke([&]() -> ASIOError {
            void* pending = nullptr;
            const auto hr = api_.create(clsid, nullptr, CLSCTX_INPROC_SERVER, clsid, &pending);
            driver_ = static_cast<IASIO*>(pending);
            if (FAILED(hr) || !driver_) return hr == E_OUTOFMEMORY ? ASE_NoMemory : ASE_NotPresent;
            info_ = {};
            info_.asioVersion = 2;
            info_.sysRef = control_.window();
            // The SDK convenience ASIOInit forwards to these IASIO operations.
            // Own the interface directly instead of the SDK's global driver list.
            if (!driver_->init(info_.sysRef)) {
                driver_->getErrorMessage(info_.errorMessage);
                info_.errorMessage[sizeof(info_.errorMessage) - 1] = '\0';
                return ASE_NotPresent;
            }
            processor_.setPositionSource({driver_,+[](void* sdk,ASIOSamples* position,ASIOTimeStamp* timestamp) noexcept {
                return static_cast<IASIO*>(sdk)->getSamplePosition(position,timestamp);
            }});
            driver_->getDriverName(info_.name);
            info_.name[sizeof(info_.name) - 1] = '\0';
            info_.driverVersion = driver_->getDriverVersion();
            return ASE_OK;
        });
    }
    ASIOError close() noexcept {
        if (!driver_) return ASE_OK;
        retireProcessing();
        try {
            return control_.invoke([&]() -> ASIOError {
                ASIOError restored = ASE_OK;
                if (potentialActive_) {
                    restored = driver_->stop();
                    if (restored == ASE_OK) potentialActive_ = false;
                }
                running_ = false;
                if (buffersAttempted_) {
                    buffersAttempted_ = false;
                    buffersReady_ = false;
                    const auto disposed = driver_->disposeBuffers();
                    if (disposed == ASE_OK) potentialActive_ = false;
                    if (restored == ASE_OK) restored = disposed;
                }
                if (originalRate_) {
                    const auto rate = *originalRate_;
                    originalRate_.reset();
                    auto rateResult = driver_->setSampleRate(rate);
                    if (rateResult == ASE_OK) {
                        ASIOSampleRate actual = 0;
                        rateResult = driver_->getSampleRate(&actual);
                        if (rateResult == ASE_OK && actual != rate) rateResult = ASE_NoClock;
                    }
                    if (restored == ASE_OK) restored = rateResult;
                }
                auto* owned = std::exchange(driver_, nullptr);
                owned->Release();
                return restored;
            });
        } catch (...) { return ASE_HWMalfunction; }
    }
    Failure validateRate(std::uint32_t requested) {
        if (!requested) return Failure::rate;
        return control_.invoke([&] {
            const ASIOSampleRate rate = requested; // every uint32_t is exact in binary64
            auto error = driver_->canSampleRate(rate);
            if (error != ASE_OK) return failure(error);
            ASIOSampleRate initial = 0;
            error = driver_->getSampleRate(&initial);
            if (error != ASE_OK) return failure(error);
            if (!portableRate(initial)) return Failure::rate;
            if (initial != rate) {
                originalRate_ = initial; // also roll back a partially failing setter
                error = driver_->setSampleRate(rate);
                if (error != ASE_OK)
                    return error == ASE_InvalidMode || error == ASE_InvalidParameter ? Failure::rate : failure(error);
            }
            ASIOSampleRate actual = 0;
            error = driver_->getSampleRate(&actual);
            if (error != ASE_OK) return failure(error);
            if (actual != rate || !portableRate(actual)) return Failure::rate;
            rates_ = {requested};
            requestedRate_ = requested;
            return Failure::none;
        });
    }
    void queryRateCandidates() {
        control_.invoke([&] {
            for (const auto rate : {44100u, 48000u, 88200u, 96000u, 176400u, 192000u})
                if (driver_->canSampleRate(rate) == ASE_OK) rates_.push_back(rate);
            ASIOSampleRate current = 0;
            if (driver_->getSampleRate(&current) == ASE_OK)
                if (const auto rate = portableRate(current); rate && driver_->canSampleRate(current) == ASE_OK)
                    rates_.push_back(*rate);
            std::sort(rates_.begin(), rates_.end());
            rates_.erase(std::unique(rates_.begin(), rates_.end()), rates_.end());
        });
    }
    const std::vector<std::uint32_t>& sampleRates() const noexcept { return rates_; }
    Failure allocateBuffers(const Request& request) {
        if (const auto result = audio_.prepare(request, inputs_, outputs_); result != Failure::none) return result;
        processor_.setExpectedRate(request.rate);
        return control_.invoke([&] {
            buffersAttempted_ = true;
            const auto error = driver_->createBuffers(audio_.native().data(), static_cast<long>(audio_.native().size()),
                static_cast<long>(request.frames), &callbacks_);
            if (error != ASE_OK) return error == ASE_InvalidMode ? Failure::buffer : failure(error);
            if (const auto result = audio_.validateNative(); result != Failure::none) return result;
            audio_.primeOutputs();
            buffersReady_ = CallbackRoute::publish(processor_);
            return buffersReady_ ? Failure::none : Failure::configuration;
        });
    }
    ASIOError start(const ProcessingCallbacks& callbacks) noexcept {
        if (!driver_ || !buffersReady_ || running_) return ASE_InvalidMode;
        if (processor_.failed()) {
            if (callbacks.failed) callbacks.failed(callbacks.user);
            return ASE_InvalidMode;
        }
        retireProcessing();
        try {
            return control_.invoke([&]() -> ASIOError {
                if (potentialActive_) {
                    const auto stopped = driver_->stop();
                    if (stopped != ASE_OK) return stopped;
                    potentialActive_ = false;
                }
                ASIOSampleRate actual = 0;
                auto error = driver_->getSampleRate(&actual);
                if (error != ASE_OK) return error;
                if (actual != requestedRate_) return ASE_NoClock;
                audio_.primeOutputs();
                configureProcessing(callbacks);
                processor_.clearTiming();
                const bool ready = audio_.outputs() && driver_->outputReady() == ASE_OK;
                processor_.setOutputReady({driver_,+[](void* sdk) noexcept {
                    return static_cast<IASIO*>(sdk)->outputReady();
                }},ready);
                if (!CallbackRoute::publish(processor_)) return ASE_InvalidMode;
                potentialActive_ = true;
                error = driver_->start();
                if (error == ASE_OK) {
                    actual = 0;
                    error = driver_->getSampleRate(&actual);
                    if (error == ASE_OK && actual != requestedRate_) error = ASE_NoClock;
                }
                if (error == ASE_OK && processor_.enable()) { running_ = true; return ASE_OK; }
                retireProcessing();
                potentialActive_ = driver_->stop() != ASE_OK;
                return error == ASE_OK ? ASE_InvalidMode : error;
            });
        } catch (...) { retireProcessing(); return ASE_HWMalfunction; }
    }
    ASIOError stop() noexcept {
        if (!driver_) return ASE_NotPresent;
        retireProcessing();
        try {
            return control_.invoke([&]() -> ASIOError {
                if (!potentialActive_) { running_ = false; return ASE_OK; }
                const auto error = driver_->stop();
                if (error == ASE_OK) { potentialActive_ = false; running_ = false; }
                return error;
            });
        } catch (...) { return ASE_HWMalfunction; }
    }
    const AudioBuffers& audioBuffers() const noexcept { return audio_; }
    TimingSnapshot timing() const noexcept { return processor_.timing(); }
    void configureProcessing(ProcessingCallbacks callbacks) noexcept { processor_.configure(callbacks); }
    bool enableProcessing() noexcept {
        return CallbackRoute::publish(processor_) && processor_.enable();
    }
    void retireProcessing() noexcept { processor_.disable(); CallbackRoute::retire(processor_); }
    Failure queryChannels() {
        return control_.invoke([&] {
            long inputs = -1, outputs = -1;
            const auto error = driver_->getChannels(&inputs, &outputs);
            if (error != ASE_OK) return failure(error);
            static_assert(sizeof(long) == sizeof(std::int32_t));
            if (inputs < 0 || outputs < 0) return Failure::channels;
            channels_ = ChannelCounts{static_cast<std::uint32_t>(inputs), static_cast<std::uint32_t>(outputs)};
            return Failure::none;
        });
    }
    Failure validateChannels(const Request& request) {
        if (const auto result = queryChannels(); result != Failure::none) return result;
        const bool shape = request.flow == Flow::input ? request.inputs > 0 && request.outputs == 0
            : request.flow == Flow::output ? request.inputs == 0 && request.outputs > 0
            : request.flow == Flow::duplex && request.inputs > 0 && request.outputs > 0;
        if (!shape || request.inputs > channels_->inputs || request.outputs > channels_->outputs)
            return Failure::channels;
        return Failure::none;
    }
    std::optional<ChannelCounts> channels() const noexcept { return channels_; }
    Failure queryBuffers() {
        return control_.invoke([&] {
            long minimum = -1, maximum = -1, preferred = -1, granularity = -2;
            const auto error = driver_->getBufferSize(&minimum, &maximum, &preferred, &granularity);
            if (error != ASE_OK) return failure(error);
            const auto limits = BufferLimits::fromNative(minimum, maximum, preferred, granularity);
            if (!limits) return Failure::buffer;
            buffers_ = limits;
            return Failure::none;
        });
    }
    Failure validateBuffers(std::uint32_t frames) {
        if (const auto result = queryBuffers(); result != Failure::none) return result;
        return buffers_->contains(frames) ? Failure::none : Failure::buffer;
    }
    std::vector<std::uint32_t> bufferSizes() const { return buffers_ ? buffers_->snapshot() : std::vector<std::uint32_t>{}; }
    Failure readLayout(bool input, std::uint32_t channel, std::optional<ChannelLayout>& out) {
        return control_.invoke([&] {
            ASIOChannelInfo info{};
            info.channel = static_cast<long>(channel);
            info.isInput = input ? ASIOTrue : ASIOFalse;
            const auto error = driver_->getChannelInfo(&info);
            if (error != ASE_OK) return failure(error);
            if (info.channel != static_cast<long>(channel) || info.isInput != (input ? ASIOTrue : ASIOFalse))
                return Failure::channels;
            out = channelLayout(info.type);
            return out ? Failure::none : Failure::format;
        });
    }
    Failure validateFormats(const Request& request) {
        auto read = [&](bool input, std::uint32_t count, std::vector<ChannelLayout>& layouts) {
            layouts.reserve(count);
            for (std::uint32_t channel = 0; channel < count; ++channel) {
                std::optional<ChannelLayout> layout;
                if (const auto result = readLayout(input, channel, layout); result != Failure::none) return result;
                if (layout->sample != request.sample) return Failure::format;
                layouts.push_back(*layout);
            }
            return Failure::none;
        };
        if (const auto result = read(true, request.inputs, inputs_); result != Failure::none) return result;
        if (const auto result = read(false, request.outputs, outputs_); result != Failure::none) return result;
        if (!inputs_.empty()) inputFormat_ = inputs_[0].sample;
        if (!outputs_.empty()) outputFormat_ = outputs_[0].sample;
        return Failure::none;
    }
    void queryFormatCandidates() {
        // Core selects channels starting at zero. The first channel determines
        // the candidate format; open validates every requested channel. Avoid
        // probing/allocating an unbounded number of advertised hardware ports.
        std::optional<ChannelLayout> layout;
        if (channels_->inputs && readLayout(true, 0, layout) == Failure::none) inputFormat_ = layout->sample;
        layout.reset();
        if (channels_->outputs && readLayout(false, 0, layout) == Failure::none) outputFormat_ = layout->sample;
    }
    std::optional<Sample> inputFormat() const noexcept { return inputFormat_; }
    std::optional<Sample> outputFormat() const noexcept { return outputFormat_; }
    const std::vector<ChannelLayout>& inputLayouts() const noexcept { return inputs_; }
    const std::vector<ChannelLayout>& outputLayouts() const noexcept { return outputs_; }
    std::string id;
    DWORD thread() const noexcept { return control_.thread(); }
    HWND window() const noexcept { return control_.window(); }
    const ASIODriverInfo& information() const noexcept { return info_; }
private:
    LoaderApi api_;
    ControlThread control_;
    IASIO* driver_ = nullptr;
    ASIODriverInfo info_{};
    std::optional<ChannelCounts> channels_;
    std::optional<BufferLimits> buffers_;
    std::optional<Sample> inputFormat_, outputFormat_;
    std::vector<ChannelLayout> inputs_, outputs_;
    std::optional<ASIOSampleRate> originalRate_;
    std::vector<std::uint32_t> rates_;
    AudioBuffers audio_;
    AudioProcessor processor_{audio_};
    ASIOCallbacks callbacks_{&sdkBufferSwitch, &sdkRateChanged, &sdkMessage, &sdkTimeInfo};
    bool buffersAttempted_ = false;
    bool buffersReady_ = false, running_ = false, potentialActive_ = false;
    std::uint32_t requestedRate_ = 0;
};

inline std::mutex driverOwnership;
inline Driver* activeDriver = nullptr;

inline ASIOError openDriver(const std::string& requestedId, void** out) noexcept {
    if (!out) return ASE_InvalidParameter;
    *out = nullptr;
    try {
        std::lock_guard lock(driverOwnership);
        if (activeDriver) return ASE_NotPresent;
        const auto clsid = driverClass(requestedId);
        if (!clsid) return ASE_NotPresent;
        std::array<wchar_t, 39> text{};
        if (StringFromGUID2(*clsid, text.data(), static_cast<int>(text.size())) != 39) return ASE_NotPresent;
        auto canonical = "asio:" + asioUtf8(std::wstring(text.data()));
        for (auto& ch : canonical) if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
        const auto api = loaderApi();
        const auto devices = api.devices ? api.devices() : std::vector<Device>{};
        if (std::none_of(devices.begin(), devices.end(), [&](const auto& device) { return device.id == canonical; })) return ASE_NotPresent;
        auto pending = std::make_unique<Driver>(api);
        pending->id = canonical;
        const auto error = pending->open(*clsid);
        if (error != ASE_OK) return error;
        activeDriver = pending.release();
        *out = activeDriver;
        return ASE_OK;
    } catch (const std::bad_alloc&) { return ASE_NoMemory; }
    catch (...) { return ASE_HWMalfunction; }
}
inline ASIOError closeDriver(void** state) noexcept {
    if (!state || !*state) return ASE_OK;
    try {
        std::lock_guard lock(driverOwnership);
        auto* value = std::exchange(*state, nullptr);
        if (value != activeDriver) return ASE_InvalidParameter;
        std::unique_ptr<Driver> owned(std::exchange(activeDriver, nullptr));
        return owned->close();
    } catch (...) { return ASE_HWMalfunction; }
}

// Capability probes temporarily initialize a driver without starting audio.
// While a session owns a driver, use its immutable cached counts and leave all
// other registrations unprobed. The metadata-only registry scan stays separate.
inline std::vector<Device> nativeCapableDevices() {
    const auto api = loaderApi();
    auto devices = api.devices ? api.devices() : std::vector<Device>{};
    std::lock_guard lock(driverOwnership);
    for (auto& device : devices) {
        if (activeDriver) {
            if (activeDriver->id == device.id) {
                device.channels = activeDriver->channels();
                device.bufferSizes = activeDriver->bufferSizes();
                device.inputFormat = activeDriver->inputFormat();
                device.outputFormat = activeDriver->outputFormat();
                device.sampleRates = activeDriver->sampleRates();
            }
            continue;
        }
        try {
            const auto clsid = driverClass(device.id);
            if (!clsid) continue;
            Driver probe(api);
            if (probe.open(*clsid) == ASE_OK && probe.queryChannels() == Failure::none) {
                device.channels = probe.channels();
                if (probe.queryBuffers() == Failure::none) device.bufferSizes = probe.bufferSizes();
                probe.queryFormatCandidates();
                device.inputFormat = probe.inputFormat();
                device.outputFormat = probe.outputFormat();
                probe.queryRateCandidates();
                device.sampleRates = probe.sampleRates();
            }
        } catch (...) { device.channels.reset(); }
    }
    return devices;
}
}
