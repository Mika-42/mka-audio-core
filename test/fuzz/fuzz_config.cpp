#include "config_case.hpp"
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,std::size_t size){
    (void)mka_fuzz::configuration(data,size);return 0;
}
