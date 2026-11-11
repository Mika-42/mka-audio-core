#include <gtest/gtest.h>
#include <expected>
#include "fake_driver.hpp"
#include "../rt_alloc.hpp"
import mka.audio.backend.asio;
import mka.audio.convert;
#include "conversion_helpers.hpp"

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
TEST(AsioRt, PreparedUserProcessingAndNullCallbacksDoNotAllocateOrRelease) {
    DriverFixture fixture; ASIO backend;
    ASSERT_TRUE(backend.open({driverId,Direction::Duplex,1,2,48000,Format::Float32,256}));
    unsigned calls=0;
    activeDriver->configureProcessing({.user=&calls,
        .process=+[](void* user,const float* const*,std::uint32_t,float* const* outputs,std::uint32_t channels,std::uint32_t frames) noexcept {
            ++*static_cast<unsigned*>(user);
            for (unsigned ch=0;ch<channels;++ch) for (unsigned frame=0;frame<frames;++frame) outputs[ch][frame]=0.25f;
        }});
    ASSERT_TRUE(activeDriver->enableProcessing()); std::size_t allocations=0,deallocations=0;
    {wasapi_rt::Scope scope;for (unsigned block=0;block<10000;++block) fixture.callbacks.bufferSwitch(block%2,ASIOTrue);
        allocations=scope.news(); deallocations=scope.deletes();}
    EXPECT_EQ(calls,10000u); EXPECT_EQ(allocations,0u); EXPECT_EQ(deallocations,0u);
    activeDriver->retireProcessing(); activeDriver->configureProcessing({}); ASSERT_TRUE(activeDriver->enableProcessing());
    {wasapi_rt::Scope scope;for (unsigned block=0;block<10000;++block) fixture.callbacks.bufferSwitch(block%2,ASIOFalse);
        allocations=scope.news(); deallocations=scope.deletes();}
    EXPECT_EQ(allocations,0u); EXPECT_EQ(deallocations,0u); ASSERT_TRUE(backend.close());
}
TEST(AsioRt, TimeInfoAndFallbackPositionDiagnosticsDoNotAllocateOrRelease) {
    DriverFixture fixture; ASIO backend;
    ASSERT_TRUE(backend.open({driverId,Direction::Output,0,2,48000,Format::Float32,256}));
    activeDriver->configureProcessing({}); ASSERT_TRUE(activeDriver->enableProcessing());
    fixture.positionResult=ASE_OK; ASIOTime time{}; time.timeInfo.flags=kSamplePositionValid|kSystemTimeValid;
    std::size_t allocations=0,deallocations=0;
    {wasapi_rt::Scope scope;for (unsigned block=0;block<10000;++block) {
        time.timeInfo.samplePosition.lo=block; time.timeInfo.systemTime.lo=block*10;
        fixture.callbacks.bufferSwitchTimeInfo(&time,block%2,ASIOTrue);
        fixture.callbacks.bufferSwitch(block%2,ASIOFalse);
    } allocations=scope.news(); deallocations=scope.deletes();}
    EXPECT_EQ(allocations,0u); EXPECT_EQ(deallocations,0u); EXPECT_EQ(fixture.positionCalls,10000u);
    ASSERT_TRUE(backend.close());
}
TEST(AsioRt, EveryNativeInputLayoutConvertsWithoutCxxAllocationOrRelease) {
    DriverFixture fixture; unsigned calls=0; ASIO backend;
    const ASIOSampleType types[]{ASIOSTInt16LSB,ASIOSTInt16MSB,ASIOSTInt24LSB,ASIOSTInt24MSB,
        ASIOSTInt32LSB24,ASIOSTInt32MSB24,ASIOSTInt32LSB,ASIOSTInt32MSB,
        ASIOSTFloat32LSB,ASIOSTFloat32MSB,ASIOSTFloat64LSB,ASIOSTFloat64MSB};
    for (const auto type : types) {
        fixture.nativeFormat=type; const auto layout=*channelLayout(type); Format format=Format::Float32;
        switch (layout.sample) {
            case Sample::i16:format=Format::Int16;break; case Sample::i24:format=Format::Int24;break;
            case Sample::i32:format=Format::Int32;break; case Sample::f32:format=Format::Float32;break;
            case Sample::f64:format=Format::Float64;break;
        }
        ASSERT_TRUE(backend.open({driverId,Direction::Input,1,0,48000,format,256}));
        activeDriver->configureProcessing({.user=&calls,
            .process=+[](void* user,const float* const*,std::uint32_t,float* const*,std::uint32_t,std::uint32_t) noexcept {
                ++*static_cast<unsigned*>(user);
            },.read=&readSamples<Conversion>});
        ASSERT_TRUE(activeDriver->enableProcessing()); std::size_t allocations=0,deallocations=0;
        {wasapi_rt::Scope scope;for (unsigned block=0;block<1000;++block) fixture.callbacks.bufferSwitch(block%2,ASIOTrue);
            allocations=scope.news();deallocations=scope.deletes();}
        EXPECT_EQ(allocations,0u);EXPECT_EQ(deallocations,0u);ASSERT_TRUE(backend.close());
    }
    EXPECT_EQ(calls,12000u);
}
TEST(AsioRt, EveryNativeOutputLayoutConvertsWithoutCxxAllocationOrRelease) {
    DriverFixture fixture; ASIO backend;
    const ASIOSampleType types[]{ASIOSTInt16LSB,ASIOSTInt16MSB,ASIOSTInt24LSB,ASIOSTInt24MSB,
        ASIOSTInt32LSB24,ASIOSTInt32MSB24,ASIOSTInt32LSB,ASIOSTInt32MSB,
        ASIOSTFloat32LSB,ASIOSTFloat32MSB,ASIOSTFloat64LSB,ASIOSTFloat64MSB};
    for (const auto type : types) {
        fixture.nativeFormat=type; const auto layout=*channelLayout(type); Format format=Format::Float32;
        switch (layout.sample) {
            case Sample::i16:format=Format::Int16;break;case Sample::i24:format=Format::Int24;break;
            case Sample::i32:format=Format::Int32;break;case Sample::f32:format=Format::Float32;break;
            case Sample::f64:format=Format::Float64;break;
        }
        ASSERT_TRUE(backend.open({driverId,Direction::Output,0,2,48000,format,256}));
        activeDriver->configureProcessing({.process=+[](void*,const float* const*,std::uint32_t,float* const* output,std::uint32_t channels,std::uint32_t frames) noexcept {
            for (unsigned channel=0;channel<channels;++channel) for (unsigned frame=0;frame<frames;++frame) output[channel][frame]=0.25f;
        },.write=&writeSamples<Conversion>}); ASSERT_TRUE(activeDriver->enableProcessing());
        std::size_t allocations=0,deallocations=0;
        {wasapi_rt::Scope scope;for (unsigned block=0;block<1000;++block) fixture.callbacks.bufferSwitch(block%2,ASIOTrue);
            allocations=scope.news();deallocations=scope.deletes();}
        EXPECT_EQ(allocations,0u);EXPECT_EQ(deallocations,0u);ASSERT_TRUE(backend.close());
    }
}
TEST(AsioRt, PublicNativeTransportAndOptionalOutputReadyDoNotAllocateOrRelease) {
    DriverFixture fixture;unsigned calls=0;ASIO backend;fixture.readyResult=ASE_OK;
    ASSERT_TRUE(backend.setProcessFunction(+[](void* user,const AudioProcessContext& context) noexcept {
        for (unsigned channel=0;channel<context.output.count;++channel)
            for (unsigned frame=0;frame<context.frames;++frame) context.output.channels[channel][frame]=0.25f;
        ++*static_cast<unsigned*>(user);
    },&calls));
    ASSERT_TRUE(backend.open({driverId,Direction::Output,0,2,48000,Format::Float32,256}));ASSERT_TRUE(backend.start());
    std::size_t allocations=0,deallocations=0;
    {wasapi_rt::Scope scope;for (unsigned block=0;block<10000;++block) fixture.callbacks.bufferSwitch(block%2,ASIOTrue);
        allocations=scope.news();deallocations=scope.deletes();}
    EXPECT_EQ(calls,10000u);EXPECT_EQ(fixture.readyCalls,10001u);EXPECT_EQ(allocations,0u);EXPECT_EQ(deallocations,0u);
    ASSERT_TRUE(backend.stop());ASSERT_TRUE(backend.close());
}
TEST(AsioRt, DriverNotificationsUseOnlyAtomicsAndDoNotAllocateOrRelease) {
    DriverFixture fixture;ASIO backend;
    ASSERT_TRUE(backend.open({driverId,Direction::Output,0,2,48000,Format::Float32,256}));ASSERT_TRUE(backend.start());
    std::size_t allocations=0,deallocations=0;
    {wasapi_rt::Scope scope;
        for (unsigned event=0;event<10000;++event) {
            fixture.callbacks.asioMessage(kAsioOverload,0,nullptr,nullptr);
            fixture.callbacks.sampleRateDidChange(48000);
            fixture.callbacks.asioMessage(kAsioSelectorSupported,kAsioResetRequest,nullptr,nullptr);
        }
        for (unsigned event=0;event<1000;++event) {
            fixture.callbacks.asioMessage(kAsioResetRequest,0,nullptr,nullptr);
            fixture.callbacks.sampleRateDidChange(0);
        }
        allocations=scope.news();deallocations=scope.deletes();
    }
    EXPECT_EQ(allocations,0u);EXPECT_EQ(deallocations,0u);EXPECT_EQ(backend.status().xruns,10000u);EXPECT_TRUE(backend.status().failed);
    ASSERT_TRUE(backend.stop());ASSERT_TRUE(backend.close());
}
}
