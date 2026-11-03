#include <pulse/pulseaudio.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <expected>
#include <gtest/gtest.h>
import mka.audio.backend.abstract;
import mka.audio.backend.pulseaudio;
namespace {
using namespace mka::audio::core;
thread_local bool manual = false;
struct Data {
    std::array<float,1202> memory{};
    std::size_t offered = 256*2*sizeof(float), captureBytes = 0, bytesWritten = 0, frames = 0;
    unsigned writes = 0, cancels = 0, drops = 0, peeks = 0, calls = 0;
    bool beginError = false, dropError = false, peekError = false, hole = false, consumed = false, bad = false;
};
thread_local Data data;
pa_threaded_mainloop* nativeLoop = nullptr;
pa_stream* playback = nullptr; pa_stream* capture = nullptr;
pa_stream_request_cb_t writeCallback = nullptr, readCallback = nullptr;
void* writeUser = nullptr; void* readUser = nullptr;
struct Injection {
    Injection() { pa_threaded_mainloop_lock(nativeLoop); manual = true; data = Data{}; data.memory.fill(999.0f); }
    ~Injection() { manual = false; pa_threaded_mainloop_unlock(nativeLoop); }
};
EndpointConfig config(bool input = false) {
    return {"", input ? Direction::Input : Direction::Output, input ? 2u : 0u, input ? 0u : 2u,48000,Format::Float32,256};
}
void process(void*,const AudioProcessContext& ctx) noexcept {
    if (!manual) return;
    if (ctx.frames == 0 || ctx.frames > 256) data.bad = true;
    if (ctx.input.count) {
        if (ctx.input.count != 2 || ctx.output.count != 0 || ctx.output.channels != nullptr) data.bad = true;
        for (unsigned i = 0; i < ctx.frames; ++i)
            for (unsigned ch = 0; ch < 2; ++ch)
                if (ctx.input.channels[ch][i] != float(ch*10000+data.frames+i+1)) data.bad = true;
    } else {
        if (ctx.output.count != 2 || ctx.input.channels != nullptr) data.bad = true;
        for (unsigned i = 0; i < ctx.frames; ++i)
            for (unsigned ch = 0; ch < 2; ++ch) ctx.output.channels[ch][i] = float(ch*1000+i+1);
    }
    data.frames += ctx.frames; ++data.calls;
}
class PulseBufferContract : public ::testing::Test {
protected:
    void SetUp() override {
        PulseAudio probe;
        if (probe.getEndPoints().empty()) GTEST_SKIP() << "serveur PulseAudio absent";
    }
};
}
extern "C" {
pa_threaded_mainloop* __real_pa_threaded_mainloop_new();
pa_threaded_mainloop* __wrap_pa_threaded_mainloop_new() { nativeLoop = __real_pa_threaded_mainloop_new(); return nativeLoop; }
void __real_pa_stream_set_write_callback(pa_stream*,pa_stream_request_cb_t,void*);
void __wrap_pa_stream_set_write_callback(pa_stream* s,pa_stream_request_cb_t cb,void* u) {
    if (cb) { playback = s; writeCallback = cb; writeUser = u; } __real_pa_stream_set_write_callback(s,cb,u);
}
void __real_pa_stream_set_read_callback(pa_stream*,pa_stream_request_cb_t,void*);
void __wrap_pa_stream_set_read_callback(pa_stream* s,pa_stream_request_cb_t cb,void* u) {
    if (cb) { capture = s; readCallback = cb; readUser = u; } __real_pa_stream_set_read_callback(s,cb,u);
}
int __real_pa_stream_begin_write(pa_stream*,void**,std::size_t*);
int __wrap_pa_stream_begin_write(pa_stream* s,void** p,std::size_t* n) {
    if (!manual) return __real_pa_stream_begin_write(s,p,n);
    if (data.beginError) return -1;
    *p = data.memory.data()+1; *n = data.offered; return 0;
}
int __real_pa_stream_cancel_write(pa_stream*);
int __wrap_pa_stream_cancel_write(pa_stream* s) { if (!manual) return __real_pa_stream_cancel_write(s); ++data.cancels; return 0; }
int __real_pa_stream_write(pa_stream*,const void*,std::size_t,pa_free_cb_t,std::int64_t,pa_seek_mode_t);
int __wrap_pa_stream_write(pa_stream* s,const void* p,std::size_t n,pa_free_cb_t freeFn,std::int64_t offset,pa_seek_mode_t mode) {
    if (!manual) return __real_pa_stream_write(s,p,n,freeFn,offset,mode);
    data.bytesWritten += n; ++data.writes; return 0;
}
int __real_pa_stream_peek(pa_stream*,const void**,std::size_t*);
int __wrap_pa_stream_peek(pa_stream* s,const void** p,std::size_t* n) {
    if (!manual) return __real_pa_stream_peek(s,p,n);
    ++data.peeks; if (data.peekError) return -1;
    *n = data.consumed ? 0 : data.captureBytes; *p = data.hole ? nullptr : data.memory.data()+1; return 0;
}
int __real_pa_stream_drop(pa_stream*);
int __wrap_pa_stream_drop(pa_stream* s) {
    if (!manual) return __real_pa_stream_drop(s);
    ++data.drops; if (data.dropError) return -1; data.consumed = true; return 0;
}
}
TEST_F(PulseBufferContract, OutputInterleavesWholeRequestedFramesAndKeepsCanaries) {
    PulseAudio b; ASSERT_TRUE(b.setProcessFunction(process)); ASSERT_TRUE(b.open(config())); ASSERT_TRUE(b.start());
    {
        Injection injected;
        writeCallback(playback,3*2*sizeof(float)+3,writeUser);
        EXPECT_EQ(data.calls,1u); EXPECT_EQ(data.frames,3u); EXPECT_EQ(data.bytesWritten,3*2*sizeof(float));
        for (unsigned i = 0; i < 3; ++i) for (unsigned ch = 0; ch < 2; ++ch)
            EXPECT_EQ(data.memory[1+i*2+ch],float(ch*1000+i+1));
        EXPECT_EQ(data.memory[0],999.0f); EXPECT_EQ(data.memory[7],999.0f); EXPECT_FALSE(data.bad);
    }
    ASSERT_TRUE(b.stop()); ASSERT_TRUE(b.close());
}
TEST_F(PulseBufferContract, CaptureDeinterleavesLargeFragmentsIntoBoundedBorrowedViews) {
    PulseAudio b; ASSERT_TRUE(b.setProcessFunction(process)); ASSERT_TRUE(b.open(config(true))); ASSERT_TRUE(b.start());
    {
        Injection injected; data.captureBytes = 259*2*sizeof(float)+3;
        for (unsigned i = 0; i < 259; ++i) for (unsigned ch = 0; ch < 2; ++ch) data.memory[1+i*2+ch] = float(ch*10000+i+1);
        readCallback(capture,data.captureBytes,readUser);
        EXPECT_EQ(data.calls,2u); EXPECT_EQ(data.frames,259u); EXPECT_EQ(data.drops,1u); EXPECT_FALSE(data.bad);
    }
    ASSERT_TRUE(b.stop()); ASSERT_TRUE(b.close());
}
TEST_F(PulseBufferContract, ZeroWriteCapacityAndReadHolesReturnWithoutSpinning) {
    PulseAudio output; ASSERT_TRUE(output.open(config())); ASSERT_TRUE(output.start());
    {
        Injection injected; data.offered = 1;
        writeCallback(playback,8,writeUser); EXPECT_EQ(data.cancels,1u); EXPECT_EQ(data.writes,0u);
        data.beginError = true; writeCallback(playback,8,writeUser); EXPECT_EQ(data.writes,0u);
    }
    ASSERT_TRUE(output.stop()); ASSERT_TRUE(output.close());
    PulseAudio input; ASSERT_TRUE(input.setProcessFunction(process)); ASSERT_TRUE(input.open(config(true))); ASSERT_TRUE(input.start());
    {
        Injection injected; data.hole = true; data.captureBytes = 16;
        readCallback(capture,16,readUser); EXPECT_EQ(data.calls,0u); EXPECT_EQ(data.drops,1u);
        data.consumed = false; data.hole = false; data.dropError = true; data.captureBytes = 8;
        readCallback(capture,8,readUser); EXPECT_EQ(data.drops,2u); EXPECT_TRUE(input.status().failed);
    }
    ASSERT_TRUE(input.stop()); ASSERT_TRUE(input.close());
}
