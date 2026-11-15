#pragma once
#include <cstddef>
#include <cstdint>
namespace mka_fuzz {
struct Observation {int error;unsigned nativeCalls;};
Observation configuration(const std::uint8_t* data,std::size_t size);
}
