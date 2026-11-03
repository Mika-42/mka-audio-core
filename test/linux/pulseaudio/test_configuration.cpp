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
enum class Mismatch { None, Format, Rate, Channels };
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
