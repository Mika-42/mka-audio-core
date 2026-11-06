#include "format_case.hpp"
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,std::size_t size){mka_fuzz::formats(data,size);return 0;}
