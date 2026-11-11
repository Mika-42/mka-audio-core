#pragma once
#include <optional>
#include "asio_types.hpp"

namespace mka_asio_detail {
struct ChannelLayout {
    Sample sample;
    std::uint32_t bytes;
    bool bigEndian;
    bool low24In32 = false;
    bool operator==(const ChannelLayout&) const = default;
};
inline bool validChannelLayout(ChannelLayout layout) noexcept {
    switch (layout.sample) {
        case Sample::i16: return layout.bytes == 2 && !layout.low24In32;
        case Sample::i24: return layout.low24In32 ? layout.bytes == 4 : layout.bytes == 3;
        case Sample::i32: case Sample::f32: return layout.bytes == 4 && !layout.low24In32;
        case Sample::f64: return layout.bytes == 8 && !layout.low24In32;
    }
    return false;
}
inline std::optional<ChannelLayout> channelLayout(ASIOSampleType type) noexcept {
    switch (type) {
        case ASIOSTInt16LSB: return ChannelLayout{Sample::i16, 2, false};
        case ASIOSTInt16MSB: return ChannelLayout{Sample::i16, 2, true};
        case ASIOSTInt24LSB: return ChannelLayout{Sample::i24, 3, false};
        case ASIOSTInt24MSB: return ChannelLayout{Sample::i24, 3, true};
        case ASIOSTInt32LSB24: return ChannelLayout{Sample::i24, 4, false, true};
        case ASIOSTInt32MSB24: return ChannelLayout{Sample::i24, 4, true, true};
        case ASIOSTInt32LSB: return ChannelLayout{Sample::i32, 4, false};
        case ASIOSTInt32MSB: return ChannelLayout{Sample::i32, 4, true};
        case ASIOSTFloat32LSB: return ChannelLayout{Sample::f32, 4, false};
        case ASIOSTFloat32MSB: return ChannelLayout{Sample::f32, 4, true};
        case ASIOSTFloat64LSB: return ChannelLayout{Sample::f64, 8, false};
        case ASIOSTFloat64MSB: return ChannelLayout{Sample::f64, 8, true};
        default: return {};
    }
}
}
