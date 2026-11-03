#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <atomic>
#include <chrono>
#include <expected>
#include <thread>
#include <gtest/gtest.h>
import mka.audio.backend.abstract;
import mka.audio.backend.pipewire;
namespace {
using namespace mka::audio::core;
enum class Fault { None, Format, Rate, Channels, Malformed };
std::atomic<Fault> fault{Fault::None};
std::atomic<int> liveStreams{0};
const pw_stream_events* nativeEvents = nullptr;
pw_stream_events interceptedEvents{};
spa_audio_info_raw requested{};
pw_stream_flags requestedFlags{};
void interceptFormat(void* data, std::uint32_t id, const spa_pod* pod) {
    if (id != SPA_PARAM_Format || !pod || fault == Fault::None) {
        nativeEvents->param_changed(data,id,pod); return;
    }
    if (fault == Fault::Malformed) {
        spa_pod invalid{0, SPA_TYPE_None}; nativeEvents->param_changed(data,id,&invalid); return;
    }
    spa_audio_info_raw info{};
    if (spa_format_audio_raw_parse(pod,&info) < 0) { nativeEvents->param_changed(data,id,pod); return; }
    if (fault == Fault::Format) info.format = SPA_AUDIO_FORMAT_F32;
    if (fault == Fault::Rate) ++info.rate;
    if (fault == Fault::Channels) ++info.channels;
    alignas(8) std::uint8_t storage[1024];
    spa_pod_builder builder = SPA_POD_BUILDER_INIT(storage,sizeof(storage));
    nativeEvents->param_changed(data,id,spa_format_audio_raw_build(&builder,SPA_PARAM_Format,&info));
}
class PipeWireNativeContract : public ::testing::Test {
protected:
    EndpointConfig cfg{"mka_test_sink",Direction::Output,0,2,48000,Format::Float32,256};
    void SetUp() override {
        fault = Fault::None;
        PipeWire probe;
        if (probe.getEndPoints().empty()) GTEST_SKIP() << "serveur PipeWire absent";
    }
    void TearDown() override { fault = Fault::None; EXPECT_EQ(liveStreams.load(),0); }
};
}
extern "C" {
pw_stream* __real_pw_stream_new_simple(pw_loop*,const char*,pw_properties*,const pw_stream_events*,void*);
pw_stream* __wrap_pw_stream_new_simple(pw_loop* loop,const char* name,pw_properties* props,const pw_stream_events* events,void* data) {
    nativeEvents = events; interceptedEvents = *events; interceptedEvents.param_changed = interceptFormat;
    auto* stream = __real_pw_stream_new_simple(loop,name,props,&interceptedEvents,data);
    if (stream) ++liveStreams;
    return stream;
}
void __real_pw_stream_destroy(pw_stream*);
void __wrap_pw_stream_destroy(pw_stream* stream) { --liveStreams; __real_pw_stream_destroy(stream); }
int __real_pw_stream_connect(pw_stream*,pw_direction,std::uint32_t,pw_stream_flags,const spa_pod**,std::uint32_t);
int __wrap_pw_stream_connect(pw_stream* stream,pw_direction dir,std::uint32_t id,pw_stream_flags flags,const spa_pod** params,std::uint32_t count) {
    if (count > 0) spa_format_audio_raw_parse(params[0],&requested);
    requestedFlags = flags;
    return __real_pw_stream_connect(stream,dir,id,flags,params,count);
}
}
TEST_F(PipeWireNativeContract, RequestsPlanarFloat32ExactRateChannelsAndInactiveHandshake) {
    PipeWire b; std::atomic<unsigned> calls{0};
    ASSERT_TRUE(b.setProcessFunction(+[](void* u,const AudioProcessContext&) noexcept { ++*static_cast<std::atomic<unsigned>*>(u); },&calls));
    ASSERT_TRUE(b.open(cfg));
    EXPECT_EQ(requested.format,SPA_AUDIO_FORMAT_F32P);
    EXPECT_EQ(requested.rate,cfg.sampleRate); EXPECT_EQ(requested.channels,cfg.outputChannels);
    EXPECT_NE(requestedFlags & PW_STREAM_FLAG_INACTIVE,0);
    std::this_thread::sleep_for(std::chrono::milliseconds(20)); EXPECT_EQ(calls.load(),0u);
    ASSERT_TRUE(b.start()); ASSERT_TRUE(b.stop()); ASSERT_TRUE(b.close());
}
TEST_F(PipeWireNativeContract, RejectsEveryBadNativeFormatAndCanReopenAfterCleanup) {
    PipeWire b;
    for (const auto [injected,error] : {
        std::pair{Fault::Format,ErrorType::FormatNotSupported},
        std::pair{Fault::Rate,ErrorType::SampleRateNotSupported},
        std::pair{Fault::Channels,ErrorType::ChannelsNotSupported},
        std::pair{Fault::Malformed,ErrorType::FormatNotSupported}}) {
        fault = injected;
        const auto result = b.open(cfg); ASSERT_FALSE(result); EXPECT_EQ(result.error(),error);
        EXPECT_EQ(b.start().error(),ErrorType::InvalidState);
        fault = Fault::None;
        ASSERT_TRUE(b.open(cfg)); ASSERT_TRUE(b.close());
    }
}
TEST_F(PipeWireNativeContract, RejectsNonFloat32RequestsBeforeNativeStreamCreation) {
    PipeWire b;
    for (const auto format : {Format::Int16,Format::Int24,Format::Int32,Format::Float64}) {
        auto invalid = cfg; invalid.format = format;
        EXPECT_EQ(b.open(invalid).error(),ErrorType::FormatNotSupported);
    }
    ASSERT_TRUE(b.open(cfg)); ASSERT_TRUE(b.close());
}
