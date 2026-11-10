#include <gtest/gtest.h>
#include <expected>
#include "fake_driver.hpp"
#include "../rt_alloc.hpp"
import mka.audio.backend.asio;

namespace {
using namespace asio_test;
using namespace mka::audio::core;
TEST(AsioRt, ColdSdkCallbacksDoNotAllocateReleaseOrTouchUnstartedAudio) {
    DriverFixture fixture; ASIO backend;
    ASSERT_TRUE(backend.open({driverId,Direction::Duplex,1,2,48000,Format::Float32,256}));
    const auto input=fixture.storage[0]; const auto output=fixture.storage[2];
    std::size_t allocations=0,deallocations=0; long version=0; ASIOTime time{}; ASIOTime* returned=nullptr;
    {
        wasapi_rt::Scope scope;
        for (unsigned block=0;block<10000;++block) {
            fixture.callbacks.bufferSwitch(block%2,ASIOTrue);
            fixture.callbacks.bufferSwitch(-1,ASIOFalse);
            fixture.callbacks.sampleRateDidChange(48000);
            version=fixture.callbacks.asioMessage(kAsioEngineVersion,0,nullptr,nullptr);
            returned=fixture.callbacks.bufferSwitchTimeInfo(&time,block%2,ASIOFalse);
        }
        allocations=scope.news(); deallocations=scope.deletes();
    }
    EXPECT_EQ(allocations,0u); EXPECT_EQ(deallocations,0u); EXPECT_EQ(version,2); EXPECT_EQ(returned,&time);
    EXPECT_EQ(fixture.storage[0],input); EXPECT_EQ(fixture.storage[2],output); ASSERT_TRUE(backend.close());
}
TEST(AsioAllocationProbe, DetectsScalarArrayNothrowAndAlignedAllocationsAndReleases) {
    std::size_t allocations=0,deallocations=0;
    {
        wasapi_rt::Scope scope;
        auto* a=::operator new(16); ::operator delete(a);
        auto* b=::operator new[](16); ::operator delete[](b);
        auto* c=::operator new(16,std::nothrow); ::operator delete(c,std::nothrow);
        auto* d=::operator new(64,std::align_val_t(64)); ::operator delete(d,std::align_val_t(64));
        allocations=scope.news(); deallocations=scope.deletes();
    }
    EXPECT_EQ(allocations,4u); EXPECT_EQ(deallocations,4u);
}
}
