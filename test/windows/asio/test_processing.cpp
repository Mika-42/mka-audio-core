#include <gtest/gtest.h>
#include <atomic>
#include <expected>
#include "fake_driver.hpp"
import mka.audio.backend.asio;

namespace {
using namespace asio_test;
using namespace mka::audio::core;
struct Calls {
    std::atomic<unsigned> calls{0}, xruns{0}, failures{0};
    bool valid=true, zero=true, recurse=false;
};
ProcessingCallbacks counting(Calls& calls) {
    return {.user=&calls,
        .process=+[](void* user,const float* const* input,std::uint32_t inputs,
            float* const* output,std::uint32_t outputs,std::uint32_t frames) noexcept {
            auto& c=*static_cast<Calls*>(user); ++c.calls;
            c.valid &= inputs==1 && outputs==2 && frames==256 && input && output;
            for (std::uint32_t ch=0;ch<outputs;++ch) for (std::uint32_t frame=0;frame<frames;++frame) {
                c.zero &= output[ch][frame]==0; output[ch][frame]=0.25f;
            }
            if (c.recurse) DriverFixture::current->callbacks.bufferSwitch(1,ASIOTrue);
        },
        .xrun=+[](void* user) noexcept {++static_cast<Calls*>(user)->xruns;},
        .failed=+[](void* user) noexcept {++static_cast<Calls*>(user)->failures;}};
}
EndpointConfig processingConfig() { return {driverId,Direction::Duplex,1,2,48000,Format::Float32,256}; }
template<class Predicate> bool waitUntil(Predicate predicate) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while (!predicate()) {
        if (std::chrono::steady_clock::now()>=deadline) return false;
        std::this_thread::yield();
    }
    return true;
}
TEST(AsioProcessing, BothBufferIndicesDispatchExactlyOnceWithFixedPlanarViews) {
    DriverFixture fixture; Calls calls; ASIO backend;
    ASSERT_TRUE(backend.open(processingConfig()));
    const auto layouts=activeDriver->audioBuffers().layouts();
    const auto inputs=activeDriver->audioBuffers().inputChannels();
    const auto outputs=activeDriver->audioBuffers().outputChannels();
    const auto queries=fixture.infoRequests.size();
    activeDriver->configureProcessing(counting(calls)); ASSERT_TRUE(activeDriver->enableProcessing());
    fixture.callbacks.bufferSwitch(0,ASIOTrue); fixture.callbacks.bufferSwitch(1,ASIOFalse);
    EXPECT_EQ(calls.calls,2u); EXPECT_TRUE(calls.valid); EXPECT_TRUE(calls.zero);
    EXPECT_EQ(calls.xruns,0u); EXPECT_EQ(calls.failures,0u);
    EXPECT_EQ(activeDriver->audioBuffers().layouts(),layouts);
    EXPECT_EQ(activeDriver->audioBuffers().inputChannels(),inputs);
    EXPECT_EQ(activeDriver->audioBuffers().outputChannels(),outputs); EXPECT_EQ(fixture.infoRequests.size(),queries);
    ASSERT_TRUE(backend.close());
}
TEST(AsioProcessing, InvalidIndicesFailOnceWithoutCallingTheUserOrTouchingNativeAudio) {
    for (const long index : {-1L,2L,std::numeric_limits<long>::max()}) {
        DriverFixture fixture; Calls calls; ASIO backend;
        ASSERT_TRUE(backend.open(processingConfig())); const auto storage=fixture.storage;
        activeDriver->configureProcessing(counting(calls)); ASSERT_TRUE(activeDriver->enableProcessing());
        fixture.callbacks.bufferSwitch(index,ASIOTrue); fixture.callbacks.bufferSwitch(index,ASIOFalse);
        fixture.callbacks.bufferSwitch(0,ASIOTrue);
        EXPECT_EQ(calls.calls,0u); EXPECT_EQ(calls.failures,1u); EXPECT_EQ(fixture.storage,storage);
        EXPECT_FALSE(activeDriver->enableProcessing()); ASSERT_TRUE(backend.close());
    }
}
TEST(AsioProcessing, ARecursiveDriverCallbackIsDroppedWithoutReenteringTheUser) {
    DriverFixture fixture; Calls calls; ASIO backend; calls.recurse=true;
    ASSERT_TRUE(backend.open(processingConfig()));
    activeDriver->configureProcessing(counting(calls)); ASSERT_TRUE(activeDriver->enableProcessing());
    fixture.callbacks.bufferSwitch(0,ASIOTrue);
    EXPECT_EQ(calls.calls,1u); EXPECT_EQ(calls.xruns,1u); EXPECT_EQ(calls.failures,0u);
    ASSERT_TRUE(backend.close());
}
TEST(AsioProcessing, NullProcessingClearsEveryOutputChannelAndFrame) {
    DriverFixture fixture; ASIO backend; ASSERT_TRUE(backend.open(processingConfig()));
    for (auto* channel : activeDriver->audioBuffers().outputChannels()) std::fill_n(channel,256,0.75f);
    activeDriver->configureProcessing({}); ASSERT_TRUE(activeDriver->enableProcessing());
    fixture.callbacks.bufferSwitch(0,ASIOFalse);
    for (auto* channel : activeDriver->audioBuffers().outputChannels())
        for (unsigned frame=0;frame<256;++frame) EXPECT_FLOAT_EQ(channel[frame],0);
    ASSERT_TRUE(backend.close());
}
TEST(AsioProcessing, ConversionHooksSelectOnlyTheCurrentNativeHalf) {
    DriverFixture fixture; ASIO backend; ASSERT_TRUE(backend.open(processingConfig()));
    for (unsigned half=0;half<2;++half) {
        const float value=half? -0.25f:0.125f;
        auto* bytes=static_cast<std::byte*>(fixture.createdBuffers[0].buffers[half]);
        for (unsigned frame=0;frame<256;++frame) std::memcpy(bytes+frame*4,&value,4);
    }
    unsigned calls=0;
    ProcessingCallbacks callbacks{.user=&calls,
        .process=+[](void* user,const float* const* input,std::uint32_t,float* const* output,std::uint32_t,std::uint32_t frames) noexcept {
            ++*static_cast<unsigned*>(user);
            for (unsigned frame=0;frame<frames;++frame) {output[0][frame]=input[0][frame]; output[1][frame]=2*input[0][frame];}
        },
        .read=+[](ChannelLayout,const std::byte* bytes,std::size_t capacity,float* output,std::size_t frames) noexcept {
            if (capacity!=frames*4) return false; std::memcpy(output,bytes,capacity); return true;
        },
        .write=+[](ChannelLayout,std::byte* bytes,std::size_t capacity,const float* input,std::size_t frames) noexcept {
            if (capacity!=frames*4) return false; std::memcpy(bytes,input,capacity); return true;
        }};
    activeDriver->configureProcessing(callbacks); ASSERT_TRUE(activeDriver->enableProcessing());
    fixture.callbacks.bufferSwitch(0,ASIOTrue);
    for (unsigned channel=0;channel<2;++channel) {
        float value=0; std::memcpy(&value,fixture.createdBuffers[1+channel].buffers[0],4);
        EXPECT_FLOAT_EQ(value,channel?0.25f:0.125f);
        std::memcpy(&value,fixture.createdBuffers[1+channel].buffers[1],4); EXPECT_FLOAT_EQ(value,0);
    }
    fixture.callbacks.bufferSwitch(1,ASIOFalse);
    float value=0; std::memcpy(&value,fixture.createdBuffers[2].buffers[1],4); EXPECT_FLOAT_EQ(value,-0.5f);
    EXPECT_EQ(calls,2u); ASSERT_TRUE(backend.close());
}
TEST(AsioProcessing, AConversionFailureDisablesProcessingAndReportsFailureOnce) {
    for (const bool inputFailure : {true,false}) {
        DriverFixture fixture; Calls calls; ASIO backend; ASSERT_TRUE(backend.open(processingConfig()));
        auto callbacks=counting(calls);
        if (inputFailure) callbacks.read=+[](ChannelLayout,const std::byte*,std::size_t,float*,std::size_t) noexcept {return false;};
        else callbacks.write=+[](ChannelLayout,std::byte*,std::size_t,const float*,std::size_t) noexcept {return false;};
        activeDriver->configureProcessing(callbacks); ASSERT_TRUE(activeDriver->enableProcessing());
        fixture.callbacks.bufferSwitch(0,ASIOTrue); fixture.callbacks.bufferSwitch(1,ASIOFalse);
        EXPECT_EQ(calls.calls,inputFailure?0u:1u); EXPECT_EQ(calls.failures,1u); ASSERT_TRUE(backend.close());
    }
}
TEST(AsioProcessing, NewSessionsUseTheirOwnUserdataAndLateCallbacksAfterCloseDoNothing) {
    DriverFixture fixture; Calls first,second;
    for (auto* calls : {&first,&second}) {
        ASIO backend; ASSERT_TRUE(backend.open(processingConfig()));
        activeDriver->configureProcessing(counting(*calls)); ASSERT_TRUE(activeDriver->enableProcessing());
        fixture.callbacks.bufferSwitch(0,ASIOTrue); const auto saved=fixture.callbacks;
        ASSERT_TRUE(backend.close()); saved.bufferSwitch(1,ASIOFalse);
    }
    EXPECT_EQ(first.calls,1u); EXPECT_EQ(second.calls,1u); EXPECT_TRUE(CallbackRoute::empty());
}
TEST(AsioProcessing, ConcurrentCallbacksAreDroppedAndCloseWaitsForTheCurrentBlock) {
    DriverFixture fixture; ASIO backend; ASSERT_TRUE(backend.open(processingConfig()));
    struct Hold {std::atomic<bool> entered{false},release{false}; std::atomic<unsigned> calls{0},xruns{0};} hold;
    activeDriver->configureProcessing({.user=&hold,
        .process=+[](void* user,const float* const*,std::uint32_t,float* const*,std::uint32_t,std::uint32_t) noexcept {
            auto& h=*static_cast<Hold*>(user); ++h.calls; h.entered=true;
            // Deliberately held mock DSP: verify host retirement, not RT timing.
            while (!h.release.load()) std::this_thread::yield();
        },.xrun=+[](void* user) noexcept {++static_cast<Hold*>(user)->xruns;}});
    ASSERT_TRUE(activeDriver->enableProcessing()); const auto saved=fixture.callbacks;
    std::thread audio([&]{saved.bufferSwitch(0,ASIOTrue);});
    const bool entered=waitUntil([&]{return hold.entered.load();});
    if (!entered) {hold.release=true; audio.join(); FAIL()<<"mock callback did not enter"; return;}
    saved.bufferSwitch(1,ASIOFalse);
    std::atomic<bool> closed{false},result{false};
    std::thread closer([&]{result=bool(backend.close()); closed=true;});
    const bool retired=waitUntil([]{return CallbackRoute::empty();});
    std::this_thread::sleep_for(std::chrono::milliseconds(20)); const bool blocked=!closed.load();
    hold.release=true; audio.join(); closer.join();
    EXPECT_TRUE(retired); EXPECT_TRUE(blocked); EXPECT_TRUE(result); EXPECT_EQ(hold.calls,1u); EXPECT_EQ(hold.xruns,1u);
    saved.bufferSwitch(0,ASIOTrue); EXPECT_EQ(hold.calls,1u); EXPECT_EQ(fixture.live,0u);
}
}
