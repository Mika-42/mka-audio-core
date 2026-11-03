// Real null PCM lifecycle; inject only native API failures/readbacks.
#include <alsa/asoundlib.h>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <expected>
#include <gtest/gtest.h>
import mka.audio.backend.alsa;

namespace {
using namespace mka::audio::core;
enum class Fault { None, SetRate, ReadRate, RateValue, RateDirection };
std::atomic<Fault> fault{Fault::None};
std::atomic<int> liveHandles{0};
unsigned requestedRate = 0;
int requestedDirection = 1;
EndpointConfig config() { return {"null", Direction::Output, 0, 2, 48000, Format::Float32, 256}; }
class AlsaNativeContract : public ::testing::Test {
protected:
    void SetUp() override { fault = Fault::None; EXPECT_EQ(liveHandles.load(), 0); }
    void TearDown() override { fault = Fault::None; EXPECT_EQ(liveHandles.load(), 0); }
};
}
extern "C" {
int __real_snd_pcm_open(snd_pcm_t**, const char*, snd_pcm_stream_t, int);
int __wrap_snd_pcm_open(snd_pcm_t** pcm, const char* name, snd_pcm_stream_t stream, int mode) {
    const int r = __real_snd_pcm_open(pcm, name, stream, mode);
    if (r >= 0) ++liveHandles;
    return r;
}
int __real_snd_pcm_close(snd_pcm_t*);
int __wrap_snd_pcm_close(snd_pcm_t* pcm) { --liveHandles; return __real_snd_pcm_close(pcm); }
int __real_snd_pcm_hw_params_set_rate(snd_pcm_t*, snd_pcm_hw_params_t*, unsigned, int);
int __wrap_snd_pcm_hw_params_set_rate(snd_pcm_t* pcm, snd_pcm_hw_params_t* hw, unsigned rate, int dir) {
    requestedRate = rate; requestedDirection = dir;
    if (fault == Fault::SetRate) return -EINVAL;
    return __real_snd_pcm_hw_params_set_rate(pcm, hw, rate, dir);
}
int __real_snd_pcm_hw_params_get_rate(const snd_pcm_hw_params_t*, unsigned*, int*);
int __wrap_snd_pcm_hw_params_get_rate(const snd_pcm_hw_params_t* hw, unsigned* rate, int* dir) {
    const int r = __real_snd_pcm_hw_params_get_rate(hw, rate, dir);
    if (fault == Fault::ReadRate) return -EIO;
    if (r >= 0 && fault == Fault::RateValue) ++*rate;
    if (r >= 0 && fault == Fault::RateDirection && dir) *dir = 1;
    return r;
}
}
TEST_F(AlsaNativeContract, ExactRatesArePassedWithoutNearestFallback) {
    for (const auto rate : {44100u, 48000u, 96000u}) {
        ALSA b; auto cfg = config(); cfg.sampleRate = rate;
        ASSERT_TRUE(b.open(cfg));
        EXPECT_EQ(requestedRate, rate);
        EXPECT_EQ(requestedDirection, 0);
        EXPECT_EQ(liveHandles.load(), 1);
        ASSERT_TRUE(b.close());
    }
}
TEST_F(AlsaNativeContract, RejectsRateSetAndReadbackFailuresWithoutLeakingAndCanRetry) {
    for (const auto [injected, error] : {
         std::pair{Fault::SetRate, ErrorType::SampleRateNotSupported},
         std::pair{Fault::RateValue, ErrorType::SampleRateNotSupported},
         std::pair{Fault::RateDirection, ErrorType::SampleRateNotSupported},
         std::pair{Fault::ReadRate, ErrorType::ConfigurationFailed}}) {
        ALSA b; fault = injected;
        const auto result = b.open(config());
        ASSERT_FALSE(result); EXPECT_EQ(result.error(), error);
        EXPECT_EQ(liveHandles.load(), 0);
        EXPECT_EQ(b.start().error(), ErrorType::InvalidState);
        fault = Fault::None;
        ASSERT_TRUE(b.open(config())); ASSERT_TRUE(b.close());
    }
}
