#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <atomic>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <chrono>
#include <expected>
#include <thread>
#include <gtest/gtest.h>
import mka.audio.backend.abstract;
import mka.audio.backend.pipewire;
namespace {
using namespace mka::audio::core;
enum class Fault { None, Format, Rate, Channels, Malformed, SmallQuantum, HugeQuantum, TerminalError, InspectSilence };
std::atomic<Fault> fault{Fault::None};
std::atomic<int> liveStreams{0};
std::string requestedLatency;
std::atomic<std::uint32_t> expectedFrames{0};
std::atomic<unsigned> checkedChunks{0};
std::atomic<bool> badChunk{false};
std::atomic<bool> injectTerminal{false};
template<class P> bool waitFor(P p) {
    // Large native buffers take longer to cycle under instrumentation.
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(6);
    while (!p() && std::chrono::steady_clock::now() < end) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return p();
}
const pw_stream_events* nativeEvents = nullptr;
pw_stream_events interceptedEvents{};
spa_audio_info_raw requested{};
pw_stream_flags requestedFlags{};
void interceptFormat(void* data, std::uint32_t id, const spa_pod* pod) {
    if (id != SPA_PARAM_Format || !pod || (fault != Fault::Format && fault != Fault::Rate && fault != Fault::Channels && fault != Fault::Malformed)) {
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
void interceptProcess(void* data) {
    if (injectTerminal.exchange(false)) nativeEvents->state_changed(data,PW_STREAM_STATE_STREAMING,PW_STREAM_STATE_ERROR,"injected disconnect");
    nativeEvents->process(data);
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
    const char* latency = pw_properties_get(props,PW_KEY_NODE_LATENCY);
    requestedLatency = latency ? latency : "";
    nativeEvents = events; interceptedEvents = *events; interceptedEvents.param_changed = interceptFormat; interceptedEvents.process = interceptProcess;
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

extern "C" {
pw_buffer* __real_pw_stream_dequeue_buffer(pw_stream*);
pw_buffer* __wrap_pw_stream_dequeue_buffer(pw_stream* stream) {
    auto* b = __real_pw_stream_dequeue_buffer(stream);
    if (b && fault == Fault::InspectSilence) {
        for (std::uint32_t i = 0; i < b->buffer->n_datas; ++i) {
            auto& d = b->buffer->datas[i];
            if (d.data) std::fill_n(static_cast<float*>(d.data),d.maxsize/sizeof(float),0.25f);
        }
    }
    if (!b || (fault != Fault::SmallQuantum && fault != Fault::HugeQuantum)) return b;
    std::uint32_t capacity = UINT32_MAX;
    for (std::uint32_t i = 0; i < b->buffer->n_datas; ++i) {
        auto& d = b->buffer->datas[i]; capacity = std::min<std::uint32_t>(capacity,d.maxsize/sizeof(float));
        if (d.chunk) d.chunk->flags = SPA_CHUNK_FLAG_CORRUPTED;
    }
    b->requested = fault == Fault::SmallQuantum ? 17 : UINT64_MAX;
    expectedFrames = static_cast<std::uint32_t>(std::min<std::uint64_t>(capacity,b->requested));
    return b;
}
int __real_pw_stream_queue_buffer(pw_stream*,pw_buffer*);
int __wrap_pw_stream_queue_buffer(pw_stream* stream,pw_buffer* b) {
    if ((fault == Fault::SmallQuantum || fault == Fault::HugeQuantum) && b->buffer->n_datas
        && b->buffer->datas[0].chunk && b->buffer->datas[0].chunk->size > 0) {
        for (std::uint32_t i = 0; i < b->buffer->n_datas; ++i) {
            const auto& d = b->buffer->datas[i];
            if (!d.chunk || d.chunk->offset != 0 || d.chunk->stride != sizeof(float)
                || d.chunk->size != expectedFrames.load()*sizeof(float)
                || d.chunk->size > d.maxsize || d.chunk->flags != 0) badChunk = true;
        }
        ++checkedChunks;
    }
    if (fault == Fault::InspectSilence && b->buffer->n_datas && b->buffer->datas[0].chunk && b->buffer->datas[0].chunk->size > 0) {
        for (std::uint32_t i = 0; i < b->buffer->n_datas; ++i) {
            const auto& d = b->buffer->datas[i];
            const auto* samples = static_cast<const float*>(d.data);
            for (std::uint32_t n = 0; n < d.chunk->size/sizeof(float); ++n) if (samples[n] != 0.0f) badChunk = true;
        }
        ++checkedChunks;
    }
    return __real_pw_stream_queue_buffer(stream,b);
}
}
TEST_F(PipeWireNativeContract, LatencyRequestAndEffectiveCallbackFramesRespectNativeCapacity) {
    for (const auto request : {Fault::SmallQuantum,Fault::HugeQuantum}) {
        PipeWire b; std::atomic<unsigned> calls{0}; std::atomic<bool> wrongFrames{false};
        struct Seen { std::atomic<unsigned>& calls; std::atomic<bool>& wrong; } seen{calls,wrongFrames};
        ASSERT_TRUE(b.setProcessFunction(+[](void* u,const AudioProcessContext& ctx) noexcept {
            auto& s = *static_cast<Seen*>(u);
            if (ctx.frames != expectedFrames.load() || ctx.frames == 0) s.wrong = true;
            ++s.calls;
        },&seen));
        ASSERT_TRUE(b.open(cfg));
        EXPECT_EQ(requestedLatency,std::to_string(cfg.bufferSize)+"/"+std::to_string(cfg.sampleRate));
        checkedChunks = 0; badChunk = false; fault = request;
        ASSERT_TRUE(b.start());
        EXPECT_TRUE(waitFor([&] { return calls.load() > 10; }));
        ASSERT_TRUE(b.stop()); ASSERT_TRUE(b.close());
        EXPECT_FALSE(wrongFrames.load()); EXPECT_FALSE(badChunk.load()); EXPECT_GT(checkedChunks.load(),0u);
        fault = Fault::None;
    }
}

TEST_F(PipeWireNativeContract, StopJoinsAnInFlightCallbackBeforeReplacingAndFreeingUserData) {
    struct Gate { std::atomic<bool> entered{false},release{false}; std::atomic<unsigned>& calls; };
    std::atomic<unsigned> oldCalls{0},newCalls{0};
    auto gate = std::unique_ptr<Gate>{new Gate{{false},{false},oldCalls}};
    PipeWire b;
    struct Release { Gate* gate; ~Release() { if (gate) gate->release = true; } } release{gate.get()};
    ASSERT_TRUE(b.setProcessFunction(+[](void* u,const AudioProcessContext&) noexcept {
        auto& g = *static_cast<Gate*>(u); g.entered = true;
        while (!g.release.load()) std::this_thread::yield();
        ++g.calls;
    },gate.get()));
    ASSERT_TRUE(b.open(cfg)); ASSERT_TRUE(b.start());
    EXPECT_TRUE(waitFor([&] { return gate->entered.load(); }));
    std::atomic<bool> stopped{false};
    std::thread control([&] { stopped = b.stop().has_value(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const bool waited = !stopped.load();
    gate->release = true; control.join();
    EXPECT_TRUE(waited); EXPECT_TRUE(stopped.load());
    ASSERT_TRUE(b.setProcessFunction(+[](void* u,const AudioProcessContext&) noexcept { ++*static_cast<std::atomic<unsigned>*>(u); },&newCalls));
    release.gate = nullptr; gate.reset();
    const auto before = oldCalls.load();
    ASSERT_TRUE(b.start());
    EXPECT_TRUE(waitFor([&] { return newCalls.load() > 10; }));
    ASSERT_TRUE(b.stop()); ASSERT_TRUE(b.close());
    EXPECT_EQ(oldCalls.load(),before);
}
TEST_F(PipeWireNativeContract, TerminalNativeStateDisablesPendingProcessCallbacks) {
    PipeWire b; std::atomic<unsigned> calls{0},failedEvents{0};
    ASSERT_TRUE(b.setProcessFunction(+[](void* u,const AudioProcessContext&) noexcept { ++*static_cast<std::atomic<unsigned>*>(u); },&calls));
    ASSERT_TRUE(b.setEventHandler(+[](void* u,const Event& e) noexcept { if(e.type == EventType::Failed) ++*static_cast<std::atomic<unsigned>*>(u); },&failedEvents));
    ASSERT_TRUE(b.open(cfg)); ASSERT_TRUE(b.start());
    EXPECT_TRUE(waitFor([&] { return calls.load() > 10; }));
    injectTerminal = true;
    EXPECT_TRUE(waitFor([&] { return b.status().failed && failedEvents.load() == 1; }));
    const auto count = calls.load(); std::this_thread::sleep_for(std::chrono::milliseconds(30));
    EXPECT_EQ(calls.load(),count);
    ASSERT_TRUE(b.stop()); ASSERT_TRUE(b.close());
    ASSERT_TRUE(b.open(cfg)); ASSERT_TRUE(b.start());
    EXPECT_FALSE(b.status().failed);
    ASSERT_TRUE(b.stop()); ASSERT_TRUE(b.close());
}

TEST_F(PipeWireNativeContract, NullCallbackQueuesNativeSilence) {
    PipeWire b; ASSERT_TRUE(b.open(cfg));
    checkedChunks = 0; badChunk = false; fault = Fault::InspectSilence;
    ASSERT_TRUE(b.start());
    EXPECT_TRUE(waitFor([&] { return checkedChunks.load() > 10; }));
    ASSERT_TRUE(b.stop()); ASSERT_TRUE(b.close());
    EXPECT_FALSE(badChunk.load()); fault = Fault::None;
}
