#pragma once
#include <algorithm>
#include "directsound_devices.hpp"
#include "directsound_native.hpp"

namespace mka_ds_detail {
inline std::optional<Capabilities> probeCapabilities(const DeviceId& id) {
    NativeDevice native;
    if (native.initialize(id) != Failure::none || !native.capabilitiesValid()) return std::nullopt;
    Capabilities result;
    const auto append = [](auto& values, const auto value) {
        if (std::find(values.begin(), values.end(), value) == values.end()) values.push_back(value);
    };
    for (const auto channels : {1u, 2u})
        for (const auto rate : {44100u, 48000u, 96000u})
            for (const auto sample : {Sample::i16, Sample::i24, Sample::i32, Sample::f32, Sample::f64})
                for (const auto frames : {128u, 256u, 512u}) {
                    const Request request{{}, id.flow, id.flow == Flow::input ? channels : 0, id.flow == Flow::output ? channels : 0, rate, frames, sample};
                    if (!native.supports(request)) continue;
                    if (!result.minimumChannels) result.minimumChannels = channels;
                    result.maximumChannels = channels;
                    append(result.rates, rate); append(result.formats, sample); append(result.buffers, frames);
                }
    if (!result.maximumChannels) return std::nullopt;
    std::sort(result.rates.begin(), result.rates.end()); std::sort(result.buffers.begin(), result.buffers.end());
    return result;
}
inline std::vector<Device> nativeCapableDevices() {
    auto result = nativeDevices();
    for (auto& device : result) {
        const auto id = parseDeviceId(device.id);
        if (!id) continue;
        try {
            if (auto caps = probeCapabilities(*id)) {
                if (id->flow == Flow::output) device.output = std::move(caps);
                else device.input = std::move(caps);
            }
        } catch (...) { /* Keep metadata, without advertising an unverified tuple. */ }
    }
    return result;
}
}
