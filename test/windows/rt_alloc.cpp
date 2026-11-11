#include "rt_alloc.hpp"
#include <cstdlib>
#include <malloc.h>
#include <new>
namespace wasapi_rt {
thread_local bool armed=false;
thread_local std::size_t allocations=0,deallocations=0;
}
namespace {
void* allocate(std::size_t size){if(wasapi_rt::armed)++wasapi_rt::allocations;if(auto* p=std::malloc(size?size:1))return p;throw std::bad_alloc{};}
void release(void* p)noexcept{if(p&&wasapi_rt::armed)++wasapi_rt::deallocations;std::free(p);}
void* aligned(std::size_t size,std::align_val_t alignment){if(wasapi_rt::armed)++wasapi_rt::allocations;if(auto* p=_aligned_malloc(size?size:1,static_cast<std::size_t>(alignment)))return p;throw std::bad_alloc{};}
void alignedRelease(void* p)noexcept{if(p&&wasapi_rt::armed)++wasapi_rt::deallocations;_aligned_free(p);}
}
void* operator new(std::size_t n){return allocate(n);}void* operator new[](std::size_t n){return allocate(n);}
void operator delete(void* p)noexcept{release(p);}void operator delete[](void* p)noexcept{release(p);}
void operator delete(void* p,std::size_t)noexcept{release(p);}void operator delete[](void* p,std::size_t)noexcept{release(p);}
void* operator new(std::size_t n,const std::nothrow_t&)noexcept{try{return allocate(n);}catch(...){return nullptr;}}
void* operator new[](std::size_t n,const std::nothrow_t&)noexcept{try{return allocate(n);}catch(...){return nullptr;}}
void operator delete(void* p,const std::nothrow_t&)noexcept{release(p);}void operator delete[](void* p,const std::nothrow_t&)noexcept{release(p);}
void* operator new(std::size_t n,std::align_val_t a){return aligned(n,a);}void* operator new[](std::size_t n,std::align_val_t a){return aligned(n,a);}
void operator delete(void* p,std::align_val_t)noexcept{alignedRelease(p);}void operator delete[](void* p,std::align_val_t)noexcept{alignedRelease(p);}
void operator delete(void* p,std::size_t,std::align_val_t)noexcept{alignedRelease(p);}void operator delete[](void* p,std::size_t,std::align_val_t)noexcept{alignedRelease(p);}
void* operator new(std::size_t n,std::align_val_t a,const std::nothrow_t&)noexcept{try{return aligned(n,a);}catch(...){return nullptr;}}
void* operator new[](std::size_t n,std::align_val_t a,const std::nothrow_t&)noexcept{try{return aligned(n,a);}catch(...){return nullptr;}}
void operator delete(void* p,std::align_val_t,const std::nothrow_t&)noexcept{alignedRelease(p);}void operator delete[](void* p,std::align_val_t,const std::nothrow_t&)noexcept{alignedRelease(p);}
