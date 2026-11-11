#pragma once
#include <algorithm>
#include <atomic>
#include <iterator>
#include <limits>
#include <string_view>
#include "directsound_ids.hpp"

namespace mka_ds_detail {
struct DiscoveryApi {
    decltype(&DirectSoundEnumerateW) enumerateOutput = &DirectSoundEnumerateW;
    decltype(&DirectSoundCaptureEnumerateW) enumerateInput = &DirectSoundCaptureEnumerateW;
};
#if defined(MKA_DIRECTSOUND_TESTING)
inline std::atomic<const DiscoveryApi*> testDiscoveryOverride{nullptr};
#endif
inline DiscoveryApi defaultDiscoveryApi() noexcept {
#if defined(MKA_DIRECTSOUND_TESTING)
    if (const auto* api = testDiscoveryOverride.load(std::memory_order_acquire)) return *api;
#endif
    return {};
}
inline std::optional<std::string> utf8Name(const wchar_t* native) {
    if (!native) return std::nullopt;
    const std::wstring_view wide(native);
    if (wide.empty() || wide.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) return std::nullopt;
    const auto length = static_cast<int>(wide.size());
    const auto bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), length, nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return std::nullopt;
    std::string result(static_cast<std::size_t>(bytes), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), length, result.data(), bytes, nullptr, nullptr) != bytes)
        return std::nullopt;
    return result;
}
struct Enumeration {
    Flow flow;
    std::vector<Device> devices;
    bool failed = false;
};
// Synchronous metadata enumeration only. No native device is created or started.
inline BOOL CALLBACK collectDevice(LPGUID guid, LPCWSTR description, LPCWSTR, LPVOID context) noexcept {
    if (!context) return FALSE;
    auto& scan = *static_cast<Enumeration*>(context);
    if (scan.failed) return FALSE;
    try {
        const auto id = deviceId(guid, scan.flow);
        if (id.empty()) return TRUE; // A supplied zero GUID is not a physical identity.
        if (std::any_of(scan.devices.begin(), scan.devices.end(), [&](const Device& device) { return device.id == id; })) return TRUE;
        auto label = description && *description ? utf8Name(description)
            : std::optional<std::string>(guid ? id : scan.flow == Flow::input ? "Default DirectSound input" : "Default DirectSound output");
        if (!label) { scan.failed = true; return FALSE; }
        Device device{id, std::move(*label), std::nullopt, std::nullopt};
        // Presence identifies the direction, with no unverified channel/rate list.
        if (scan.flow == Flow::output) device.output.emplace();
        else if (scan.flow == Flow::input) device.input.emplace();
        scan.devices.push_back(std::move(device));
        return TRUE;
    } catch (...) {
        scan.failed = true;
        return FALSE; // Never unwind through the Windows callback ABI.
    }
}
inline std::vector<Device> enumerateDirection(decltype(&DirectSoundEnumerateW) enumerate, Flow flow) {
    if (!enumerate) return {};
    Enumeration scan{flow};
    // Windows can return success after the callback returned FALSE. Retain our
    // own failure flag so a callback failure cannot expose a partial snapshot.
    if (FAILED(enumerate(&collectDevice, &scan)) || scan.failed) return {};
    return std::move(scan.devices);
}
inline std::vector<Device> nativeDevices() {
    const auto api = defaultDiscoveryApi();
    auto result = enumerateDirection(api.enumerateOutput, Flow::output);
    auto input = enumerateDirection(api.enumerateInput, Flow::input);
    result.insert(result.end(), std::make_move_iterator(input.begin()), std::make_move_iterator(input.end()));
    return result;
}
}
