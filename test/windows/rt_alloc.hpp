#pragma once
#include <cstddef>
namespace wasapi_rt {
extern thread_local bool armed;
extern thread_local std::size_t allocations,deallocations;
struct Scope {
    bool previous=armed;std::size_t beforeAlloc=allocations,beforeFree=deallocations;
    Scope(){armed=true;}
    ~Scope(){armed=previous;}
    std::size_t news()const{return allocations-beforeAlloc;}
    std::size_t deletes()const{return deallocations-beforeFree;}
};
}
