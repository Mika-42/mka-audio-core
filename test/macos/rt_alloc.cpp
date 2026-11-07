#include "rt_alloc.hpp"
#include <cstdlib>
#include <new>
namespace {
thread_local bool tracking=false;
thread_local std::size_t operations=0;
void record() noexcept {if(tracking)++operations;}
void* allocate(std::size_t size,std::size_t alignment=alignof(std::max_align_t)) {
    record();void* p=nullptr;
    if(alignment<=alignof(std::max_align_t))p=std::malloc(size?size:1);
    else if(posix_memalign(&p,alignment,size?size:1)!=0)p=nullptr;
    if(!p)throw std::bad_alloc();return p;
}
void release(void* p) noexcept {if(p)record();std::free(p);}
}
namespace mac_rt {
void begin() noexcept {operations=0;tracking=true;}
std::size_t end() noexcept {tracking=false;return operations;}
}
void* operator new(std::size_t n){return allocate(n);}
void* operator new[](std::size_t n){return allocate(n);}
void operator delete(void* p) noexcept {release(p);}
void operator delete[](void* p) noexcept {release(p);}
void operator delete(void* p,std::size_t) noexcept {release(p);}
void operator delete[](void* p,std::size_t) noexcept {release(p);}
void* operator new(std::size_t n,std::align_val_t a){return allocate(n,static_cast<std::size_t>(a));}
void* operator new[](std::size_t n,std::align_val_t a){return allocate(n,static_cast<std::size_t>(a));}
void operator delete(void* p,std::align_val_t) noexcept {release(p);}
void operator delete[](void* p,std::align_val_t) noexcept {release(p);}
void operator delete(void* p,std::size_t,std::align_val_t) noexcept {release(p);}
void operator delete[](void* p,std::size_t,std::align_val_t) noexcept {release(p);}
