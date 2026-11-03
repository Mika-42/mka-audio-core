#pragma once

#include <cstdint>
#include <optional>
#include <spa/buffer/buffer.h>

// Internal native-buffer validation. No allocation, lock, or pointer dereference
// until the plane's address, chunk and bounds have been checked.
namespace mka::audio::core::detail {
inline bool writableFloatPlane(const spa_data& data) noexcept {
    return data.data && data.chunk
        && reinterpret_cast<std::uintptr_t>(data.data) % alignof(float) == 0;
}

inline std::optional<std::uint32_t> readableFloatFrames(const spa_data& data) noexcept {
    if (!writableFloatPlane(data) || (data.chunk->flags & SPA_CHUNK_FLAG_CORRUPTED) != 0
        || data.chunk->stride != sizeof(float)
        || data.chunk->offset % alignof(float) != 0
        || data.chunk->offset > data.maxsize
        || data.chunk->size > data.maxsize - data.chunk->offset
        || data.chunk->size % sizeof(float) != 0) return std::nullopt;
    return data.chunk->size / sizeof(float);
}
}
