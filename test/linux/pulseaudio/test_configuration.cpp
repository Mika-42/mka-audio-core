#include <atomic>
#include <chrono>
#include <cstdlib>
#include <optional>
#include <string>
#include <thread>
#include <pulse/pulseaudio.h>
#include <gtest/gtest.h>

import mka.audio.backend.abstract;
import mka.audio.backend.pulseaudio;

namespace {
using namespace mka::audio::core;
enum class Mismatch { None, Format, Rate, Channels, MissingAttributes, ZeroLength, UnalignedLength, ShortMaximum, ExcessPrebuf, RoundedLatency };
Mismatch mismatch = Mismatch::None;

EndpointConfig config() {
    return {"", Direction::Output, 0, 2, 48000, Format::Float32, 256};
}

class PulseConfigurationTest : public ::testing::Test {
protected:
    void SetUp() override {
        mismatch = Mismatch::None;
        PulseAudio backend;
        if (backend.getEndPoints().empty()) GTEST_SKIP() << "serveur PulseAudio absent";
    }
    void TearDown() override { mismatch = Mismatch::None; }
};
}

// Keep the real server/connection lifecycle, only inject a mismatched native
// readback to exercise rejection and cleanup without requiring unusual hardware.
extern "C" const pa_sample_spec* __real_pa_stream_get_sample_spec(pa_stream*);
extern "C" const pa_sample_spec* __wrap_pa_stream_get_sample_spec(pa_stream* stream) {
    const auto* actual = __real_pa_stream_get_sample_spec(stream);
    if (!actual || mismatch == Mismatch::None) return actual;
    static pa_sample_spec modified;
    modified = *actual;
    if (mismatch == Mismatch::Format) modified.format = PA_SAMPLE_S16NE;
    if (mismatch == Mismatch::Rate) modified.rate = 44100;
    if (mismatch == Mismatch::Channels) modified.channels = 1;
    return &modified;
}

TEST_F(PulseConfigurationTest, OpenVerifiesNativeConfigurationWithoutCallingTheUser) {
    std::atomic<unsigned> calls{0};
    PulseAudio backend;
    ASSERT_TRUE(backend.setProcessFunction([](void* user, const AudioProcessContext&) noexcept {
        static_cast<std::atomic<unsigned>*>(user)->fetch_add(1);
    }, &calls));
    ASSERT_TRUE(backend.open(config()));
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    EXPECT_EQ(calls.load(), 0u);
    ASSERT_TRUE(backend.start());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (calls.load() == 0 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    ASSERT_TRUE(backend.stop());
    EXPECT_GT(calls.load(), 0u);
    ASSERT_TRUE(backend.close());
}

TEST_F(PulseConfigurationTest, RejectsEveryMismatchedReadbackAndCanReopen) {
    PulseAudio backend;
    for (const auto [injected, expected] : {
            std::pair{Mismatch::Format, ErrorType::FormatNotSupported},
            std::pair{Mismatch::Rate, ErrorType::SampleRateNotSupported},
            std::pair{Mismatch::Channels, ErrorType::ChannelsNotSupported}}) {
        mismatch = injected;
        const auto result = backend.open(config());
        ASSERT_FALSE(result);
        EXPECT_EQ(result.error(), expected);
        EXPECT_FALSE(backend.start());
        mismatch = Mismatch::None;
        ASSERT_TRUE(backend.open(config()));
        ASSERT_TRUE(backend.close());
    }
}

TEST(PulseConfigurationOfflineTest, UnavailableServerFailsAtOpenAndLeavesTheBackendClosed) {
    const char* current = std::getenv("PULSE_SERVER");
    const std::optional<std::string> saved = current ? std::optional<std::string>{current} : std::nullopt;
    struct Restore {
        const std::optional<std::string>& saved;
        ~Restore() { if (saved) setenv("PULSE_SERVER", saved->c_str(), 1); else unsetenv("PULSE_SERVER"); }
    } restore{saved};
    ASSERT_EQ(setenv("PULSE_SERVER", "unix:/tmp/mka-nonexistent-pulse-sprint01", 1), 0);
    PulseAudio backend;
    const auto result = backend.open(config());
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), ErrorType::EndpointUnavailable);
    EXPECT_FALSE(backend.start());
    EXPECT_FALSE(backend.close());
}

namespace {
pa_buffer_attr requestedAttributes{};
}
extern "C" {
int __real_pa_stream_connect_playback(pa_stream*,const char*,const pa_buffer_attr*,pa_stream_flags_t,const pa_cvolume*,pa_stream*);
int __wrap_pa_stream_connect_playback(pa_stream* s,const char* d,const pa_buffer_attr* a,pa_stream_flags_t f,const pa_cvolume* v,pa_stream* sync) {
    requestedAttributes = *a; return __real_pa_stream_connect_playback(s,d,a,f,v,sync);
}
int __real_pa_stream_connect_record(pa_stream*,const char*,const pa_buffer_attr*,pa_stream_flags_t);
int __wrap_pa_stream_connect_record(pa_stream* s,const char* d,const pa_buffer_attr* a,pa_stream_flags_t f) {
    requestedAttributes = *a; return __real_pa_stream_connect_record(s,d,a,f);
}
const pa_buffer_attr* __real_pa_stream_get_buffer_attr(pa_stream*);
const pa_buffer_attr* __wrap_pa_stream_get_buffer_attr(pa_stream* s) {
    const auto* actual = __real_pa_stream_get_buffer_attr(s);
    if (!actual || mismatch == Mismatch::MissingAttributes) return nullptr;
    static pa_buffer_attr modified;
    modified = *actual;
    const bool recording = requestedAttributes.fragsize != UINT32_MAX;
    auto& length = recording ? modified.fragsize : modified.tlength;
    if (mismatch == Mismatch::ZeroLength) length = 0;
    if (mismatch == Mismatch::UnalignedLength) ++length;
    if (mismatch == Mismatch::ShortMaximum) modified.maxlength = 1;
    if (mismatch == Mismatch::ExcessPrebuf) modified.prebuf = modified.tlength + 8;
    if (mismatch == Mismatch::RoundedLatency) { length += 8*37; modified.maxlength = UINT32_MAX; }
    return &modified;
}
}
TEST_F(PulseConfigurationTest, ChecksNegotiatedLengthsAndAcceptsServerLatencyRounding) {
    for (const bool input : {false,true}) {
        auto cfg = config();
        if (input) { cfg.direction = Direction::Input; cfg.inputChannels = 2; cfg.outputChannels = 0; }
        for (const auto injected : {Mismatch::None,Mismatch::RoundedLatency}) {
            mismatch = injected;
            PulseAudio b; ASSERT_TRUE(b.open(cfg));
            EXPECT_EQ(input ? requestedAttributes.fragsize : requestedAttributes.tlength,cfg.bufferSize*2*sizeof(float));
            ASSERT_TRUE(b.start()); ASSERT_TRUE(b.stop()); ASSERT_TRUE(b.close());
        }
        for (const auto injected : {Mismatch::MissingAttributes,Mismatch::ZeroLength,Mismatch::UnalignedLength,Mismatch::ShortMaximum}) {
            mismatch = injected; PulseAudio b;
            const auto result = b.open(cfg); ASSERT_FALSE(result);
            EXPECT_EQ(result.error(),injected == Mismatch::MissingAttributes ? ErrorType::ConfigurationFailed : ErrorType::BufferSizeNotSupported);
            mismatch = Mismatch::None; ASSERT_TRUE(b.open(cfg)); ASSERT_TRUE(b.close());
        }
    }
    mismatch = Mismatch::ExcessPrebuf; PulseAudio b;
    EXPECT_EQ(b.open(config()).error(),ErrorType::BufferSizeNotSupported);
}
