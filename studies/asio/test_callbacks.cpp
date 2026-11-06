#include <asiosys.h>
#include <asio.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <expected>
#include <type_traits>
#include <gtest/gtest.h>
#include "../../examples/dsp/gain.hpp"
#if MKA_STUDY_ALLOCATOR_PROBE
#include "../../test/windows/rt_alloc.hpp"
#endif
import mka.audio.backend.abstract;
import mka.audio.convert;

namespace {
using namespace mka::audio::core;
using mka::audio::convert::Layout;
static_assert(sizeof(long)==4 && sizeof(ASIOBool)==4 && sizeof(ASIOSampleRate)==8);
static_assert(std::is_same_v<decltype(ASIOCallbacks::bufferSwitch),void(*)(long,ASIOBool)>);
static_assert(std::is_same_v<decltype(ASIOCallbacks::bufferSwitchTimeInfo),ASIOTime*(*)(ASIOTime*,long,ASIOBool)>);
Result layout(ASIOSampleType type,Layout& result) noexcept {
    switch(type){
        case ASIOSTInt16LSB:result=Layout::S16;break;
        case ASIOSTInt24LSB:result=Layout::S24Packed;break;
        case ASIOSTInt32LSB:result=Layout::S32;break;
        case ASIOSTFloat32LSB:result=Layout::F32;break;
        case ASIOSTFloat64LSB:result=Layout::F64;break;
        default:return std::unexpected{ErrorType::FormatNotSupported};
    }
    return {};
}
// Synchronous callback ABI probe, not a driver host. The SDK callback table has
// no user pointer; a production host needs an exclusive routing/quiescence policy.
struct Probe {
    static inline Probe* current=nullptr;
    std::array<std::array<float,19>,2> input{},output{};
    mka_example::Gain gain;
    std::atomic<bool> failed{false},resetRequested{false};
    Probe(){EXPECT_EQ(current,nullptr);current=this;for(auto& p:output)p.fill(99);}
    ~Probe(){current=nullptr;}
    static void switched(long index,ASIOBool) noexcept {
        if(!current)return;
        auto& p=*current;
        if(index<0 || index>1){p.failed=true;return;}
        const float* in[]{p.input[index].data()+1};float* out[]{p.output[index].data()+1};
        p.gain.process(AudioProcessContext{{in,1},{out,1},17});
    }
    static void rateChanged(ASIOSampleRate) noexcept {if(current)current->resetRequested=true;}
    static long message(long selector,long value,void*,double*) noexcept {
        if(selector==kAsioSelectorSupported)return value==kAsioResetRequest || value==kAsioSupportsTimeInfo;
        if(selector==kAsioEngineVersion)return 2;
        if(selector==kAsioSupportsTimeInfo)return 1;
        if(selector==kAsioResetRequest && current){current->resetRequested=true;return 1;}
        return 0;
    }
    static ASIOTime* timed(ASIOTime* time,long index,ASIOBool direct) noexcept {
        if(!time){if(current)current->failed=true;return nullptr;}
        switched(index,direct);return time;
    }
    static ASIOCallbacks callbacks(){return {switched,rateChanged,message,timed};}
};
TEST(AsioAssessment, SdkCallbacksSelectBothNativeBufferHalvesAndBorrowCoreViews) {
    Probe p;auto callbacks=Probe::callbacks();p.input[0].fill(0.25f);p.input[1].fill(-0.5f);
    callbacks.bufferSwitch(0,ASIOTrue);ASIOTime time{};
    EXPECT_EQ(callbacks.bufferSwitchTimeInfo(&time,1,ASIOTrue),&time);
    for(unsigned half=0;half<2;++half){
        EXPECT_EQ(p.output[half].front(),99);EXPECT_EQ(p.output[half].back(),99);
        for(unsigned i=1;i<=17;++i)EXPECT_FLOAT_EQ(p.output[half][i],half?-.25f:.125f);
    }
    EXPECT_EQ(p.gain.frames.load(),34u);EXPECT_FALSE(p.failed.load());
}
TEST(AsioAssessment, InvalidIndicesAndMissingTimeDoNotTouchBuffers) {
    Probe p;auto callbacks=Probe::callbacks();const auto before=p.output;
    callbacks.bufferSwitch(-1,ASIOFalse);callbacks.bufferSwitch(2,ASIOFalse);
    EXPECT_EQ(callbacks.bufferSwitchTimeInfo(nullptr,0,ASIOFalse),nullptr);
    EXPECT_EQ(p.output,before);EXPECT_EQ(p.gain.calls.load(),0u);EXPECT_TRUE(p.failed.load());
}
TEST(AsioAssessment, RateLossAndResetAreDeferredOutsideTheNativeCallback) {
    Probe p;auto c=Probe::callbacks();c.sampleRateDidChange(0);
    EXPECT_TRUE(p.resetRequested.load());p.resetRequested=false;
    EXPECT_EQ(c.asioMessage(kAsioResetRequest,0,nullptr,nullptr),1);EXPECT_TRUE(p.resetRequested.load());
    EXPECT_EQ(c.asioMessage(kAsioSelectorSupported,kAsioSupportsTimeInfo,nullptr,nullptr),1);
    EXPECT_EQ(c.asioMessage(0x7fffffff,0,nullptr,nullptr),0);
}
#if MKA_STUDY_ALLOCATOR_PROBE
TEST(AsioAssessment, NativeCallbackWorkDoesNotAllocateOrFree) {
    Probe p;auto c=Probe::callbacks();ASIOTime time{};std::size_t allocated=0,freed=0;
    {
        wasapi_rt::Scope scope;
        c.bufferSwitch(0,ASIOTrue);c.bufferSwitchTimeInfo(&time,1,ASIOTrue);
        c.sampleRateDidChange(0);c.asioMessage(kAsioResetRequest,0,nullptr,nullptr);
        allocated=scope.news();freed=scope.deletes();
    }
    EXPECT_EQ(allocated,0u);EXPECT_EQ(freed,0u);
}
#endif
TEST(AsioAssessment, ControlMappingRejectsUnsupportedPackingWithoutChangingTheResult) {
    for(auto type:{ASIOSTInt16LSB,ASIOSTInt24LSB,ASIOSTInt32LSB,ASIOSTFloat32LSB,ASIOSTFloat64LSB}){
        Layout value{};ASSERT_TRUE(layout(type,value));EXPECT_GT(mka::audio::convert::bytesPerSample(value),0u);
        if(type==ASIOSTInt24LSB)EXPECT_EQ(value,Layout::S24Packed);
    }
    for(auto type:std::array<ASIOSampleType,3>{ASIOSTInt16MSB,ASIOSTInt32LSB24,-1}){
        Layout value=Layout::F64;auto result=layout(type,value);
        ASSERT_FALSE(result);EXPECT_EQ(result.error(),ErrorType::FormatNotSupported);EXPECT_EQ(value,Layout::F64);
    }
}
}
