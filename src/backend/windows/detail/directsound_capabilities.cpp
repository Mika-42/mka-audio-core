#include "directsound_capabilities.hpp"

namespace mka_ds_detail {
std::vector<Device> nativeCapableDevices() {
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
