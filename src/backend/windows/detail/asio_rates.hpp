#pragma once
#include <cmath>
#include <limits>
#include <optional>
#include "asio_types.hpp"

namespace mka_asio_detail {
inline std::optional<std::uint32_t> portableRate(ASIOSampleRate rate) noexcept {
    if (!std::isfinite(rate) || rate < 1 || rate > std::numeric_limits<std::uint32_t>::max()
        || std::trunc(rate) != rate) return {};
    return static_cast<std::uint32_t>(rate);
}
}
