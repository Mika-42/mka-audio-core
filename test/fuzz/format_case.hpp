#pragma once
#include <cstddef>
#include <cstdint>
namespace mka_fuzz {
struct FormatObservation {bool valid;std::uint32_t frames,stride,offset;};
FormatObservation formats(const std::uint8_t* data,std::size_t size);
}
