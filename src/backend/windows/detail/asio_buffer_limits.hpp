#pragma once
#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

namespace mka_asio_detail {
struct BufferLimits {
    std::uint32_t minimum, maximum, preferred;
    std::int32_t granularity;
    bool contains(std::uint32_t value) const noexcept {
        if (value < minimum || value > maximum) return false;
        if (granularity == 0) return value == minimum;
        if (granularity == -1) return value && !(value & (value - 1));
        return (value - minimum) % static_cast<std::uint32_t>(granularity) == 0;
    }
    static std::optional<BufferLimits> fromNative(long min, long max, long preferred, long granularity) noexcept {
        static_assert(sizeof(long) == sizeof(std::int32_t));
        if (min <= 0 || max < min || preferred < min || preferred > max || granularity < -1
            || (granularity == 0 && min != max)) return {};
        BufferLimits result{static_cast<std::uint32_t>(min), static_cast<std::uint32_t>(max),
            static_cast<std::uint32_t>(preferred), static_cast<std::int32_t>(granularity)};
        if (!result.contains(result.minimum) || !result.contains(result.maximum) || !result.contains(result.preferred)) return {};
        return result;
    }
    // Capabilities are indicative. Bound enumeration even for a 1-frame step
    // spanning LONG_MAX; always include the valid preferred size and maximum.
    std::vector<std::uint32_t> snapshot() const {
        std::vector<std::uint32_t> result;
        for (std::uint64_t value = minimum; value <= maximum && result.size() < 256;) {
            result.push_back(static_cast<std::uint32_t>(value));
            if (granularity == 0) break;
            value = granularity == -1 ? value * 2 : value + static_cast<std::uint32_t>(granularity);
        }
        result.push_back(preferred); result.push_back(maximum);
        std::sort(result.begin(), result.end());
        result.erase(std::unique(result.begin(), result.end()), result.end());
        return result;
    }
};
}
