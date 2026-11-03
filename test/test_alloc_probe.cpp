#include "utils/rt_test_utils.hpp"
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <new>
#include <thread>
#include <gtest/gtest.h>

TEST(AllocationProbe, DetectsScalarArrayNothrowAndAlignedCppAllocations) {
    alloc_probe::ignoreCurrentThread();
    std::atomic<bool> ready{false},go{false},done{false},finish{false},bad{false};
    std::thread worker([&] {
        ready = true; while (!go.load()) std::this_thread::yield();
        void* p[]{::operator new(16),::operator new[](16),::operator new(16,std::nothrow),
            ::operator new[](16,std::nothrow),::operator new(64,std::align_val_t{64}),
            ::operator new[](64,std::align_val_t{64},std::nothrow)};
        for (auto* ptr : p) { if (!ptr) bad = true; else std::memset(ptr,1,16); }
        if (reinterpret_cast<std::uintptr_t>(p[4]) % 64 || reinterpret_cast<std::uintptr_t>(p[5]) % 64) bad = true;
        ::operator delete(p[0]); ::operator delete[](p[1]); ::operator delete(p[2],std::nothrow);
        ::operator delete[](p[3],std::nothrow); ::operator delete(p[4],std::align_val_t{64});
        ::operator delete[](p[5],std::align_val_t{64},std::nothrow);
        done = true; while (!finish.load()) std::this_thread::yield();
    });
    while (!ready.load()) std::this_thread::yield();
    alloc_probe::arm();
    void* control = ::operator new(32); ::operator delete(control); // ignored control thread
    go = true; while (!done.load()) std::this_thread::yield();
    const auto count = alloc_probe::disarm(); finish = true; worker.join();
    EXPECT_EQ(count,6u); EXPECT_FALSE(bad.load());
}
TEST(AllocationProbe, DetectsLibcOperationsOnlyOnMarkedAudioThreads) {
#if defined(__GLIBC__)
    std::atomic<bool> ready{false},go{false},done{false},finish{false};
    std::thread worker([&] {
        alloc_probe::markAudioThread(); ready = true;
        while (!go.load()) std::this_thread::yield();
        void* (*volatile allocate)(std::size_t) = std::malloc;
        void* (*volatile zero)(std::size_t,std::size_t) = std::calloc;
        void* (*volatile resize)(void*,std::size_t) = std::realloc;
        void (*volatile release)(void*) = std::free;
        void* p = allocate(16); void* q = zero(2,16); p = resize(p,32); release(p); release(q);
        done = true; while (!finish.load()) std::this_thread::yield();
    });
    while (!ready.load()) std::this_thread::yield();
    alloc_probe::armAudio();
    void* control = std::malloc(16); std::free(control); // unmarked control thread
    go = true; while (!done.load()) std::this_thread::yield();
    const auto count = alloc_probe::disarmAudio(); finish = true; worker.join();
    EXPECT_EQ(count,5u);
#else
    GTEST_SKIP() << "interposition malloc réservée à glibc";
#endif
}
