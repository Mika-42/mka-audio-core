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
enum class Fault { None, SetRate, ReadRate, RateValue, RateDirection, SetPeriod, SetPeriods, ReadPeriod, PeriodValue, PeriodDirection, ReadBuffer, ShortBuffer, FractionalBuffer, ReadPeriods, PeriodsValue, PeriodsDirection, PlaybackChannels, ChannelsReadback };
std::atomic<Fault> fault{Fault::None};
std::atomic<int> liveHandles{0};
unsigned requestedRate = 0;
int requestedDirection = 1;
bool emulateLinkedDevice = false;
unsigned captureChannels = 0, playbackChannels = 0;
unsigned unlinkCalls = 0;
EndpointConfig config() { return {"null", Direction::Output, 0, 2, 48000, Format::Float32, 256}; }
class AlsaNativeContract : public ::testing::Test {
protected:
    void SetUp() override { fault = Fault::None; emulateLinkedDevice = false; captureChannels = playbackChannels = unlinkCalls = 0; EXPECT_EQ(liveHandles.load(), 0); }
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

extern "C" {
int __real_snd_pcm_hw_params_set_period_size(snd_pcm_t*, snd_pcm_hw_params_t*, snd_pcm_uframes_t, int);
int __wrap_snd_pcm_hw_params_set_period_size(snd_pcm_t* p, snd_pcm_hw_params_t* h, snd_pcm_uframes_t n, int d) {
    if (fault == Fault::SetPeriod) return -EINVAL;
    return __real_snd_pcm_hw_params_set_period_size(p,h,n,d);
}
int __real_snd_pcm_hw_params_set_periods(snd_pcm_t*, snd_pcm_hw_params_t*, unsigned, int);
int __wrap_snd_pcm_hw_params_set_periods(snd_pcm_t* p, snd_pcm_hw_params_t* h, unsigned n, int d) {
    if (fault == Fault::SetPeriods) return -EINVAL;
    return __real_snd_pcm_hw_params_set_periods(p,h,n,d);
}
int __real_snd_pcm_hw_params_get_period_size(const snd_pcm_hw_params_t*, snd_pcm_uframes_t*, int*);
int __wrap_snd_pcm_hw_params_get_period_size(const snd_pcm_hw_params_t* h, snd_pcm_uframes_t* n, int* d) {
    const int r = __real_snd_pcm_hw_params_get_period_size(h,n,d);
    if (fault == Fault::ReadPeriod) return -EIO;
    if (r >= 0 && fault == Fault::PeriodValue) ++*n;
    if (r >= 0 && fault == Fault::PeriodDirection && d) *d = 1;
    return r;
}
int __real_snd_pcm_hw_params_get_buffer_size(const snd_pcm_hw_params_t*, snd_pcm_uframes_t*);
int __wrap_snd_pcm_hw_params_get_buffer_size(const snd_pcm_hw_params_t* h, snd_pcm_uframes_t* n) {
    const int r = __real_snd_pcm_hw_params_get_buffer_size(h,n);
    if (fault == Fault::ReadBuffer) return -EIO;
    if (r >= 0 && fault == Fault::ShortBuffer) *n = 1;
    if (r >= 0 && fault == Fault::FractionalBuffer) --*n;
    return r;
}
int __real_snd_pcm_hw_params_get_periods(const snd_pcm_hw_params_t*, unsigned*, int*);
int __wrap_snd_pcm_hw_params_get_periods(const snd_pcm_hw_params_t* h, unsigned* n, int* d) {
    const int r = __real_snd_pcm_hw_params_get_periods(h,n,d);
    if (fault == Fault::ReadPeriods) return -EIO;
    if (r >= 0 && fault == Fault::PeriodsValue) ++*n;
    if (r >= 0 && fault == Fault::PeriodsDirection && d) *d = 1;
    return r;
}
}
TEST_F(AlsaNativeContract, ExactPeriodAndThreePeriodRingAreRequired) {
    for (const auto size : {64u, 256u, 4096u}) {
        ALSA b; auto cfg = config(); cfg.bufferSize = size;
        ASSERT_TRUE(b.open(cfg)); ASSERT_TRUE(b.close());
    }
    for (const auto injected : {Fault::SetPeriod, Fault::SetPeriods, Fault::ReadPeriod,
        Fault::PeriodValue, Fault::PeriodDirection, Fault::ReadBuffer, Fault::ShortBuffer,
        Fault::FractionalBuffer, Fault::ReadPeriods, Fault::PeriodsValue, Fault::PeriodsDirection}) {
        ALSA b; fault = injected;
        const auto result = b.open(config()); ASSERT_FALSE(result);
        const bool readFailure = injected == Fault::ReadPeriod || injected == Fault::ReadBuffer || injected == Fault::ReadPeriods;
        EXPECT_EQ(result.error(), readFailure ? ErrorType::ConfigurationFailed : ErrorType::BufferSizeNotSupported);
        EXPECT_EQ(liveHandles.load(), 0);
        fault = Fault::None; ASSERT_TRUE(b.open(config())); ASSERT_TRUE(b.close());
    }
}

extern "C" {
int __real_snd_pcm_hw_params_set_channels(snd_pcm_t*, snd_pcm_hw_params_t*, unsigned);
int __wrap_snd_pcm_hw_params_set_channels(snd_pcm_t* p, snd_pcm_hw_params_t* h, unsigned n) {
    if (snd_pcm_stream(p) == SND_PCM_STREAM_CAPTURE) captureChannels = n;
    else {
        playbackChannels = n;
        if (fault == Fault::PlaybackChannels) return -EINVAL;
    }
    return __real_snd_pcm_hw_params_set_channels(p,h,n);
}
int __real_snd_pcm_hw_params_get_channels(const snd_pcm_hw_params_t*, unsigned*);
int __wrap_snd_pcm_hw_params_get_channels(const snd_pcm_hw_params_t* h, unsigned* n) {
    const int r = __real_snd_pcm_hw_params_get_channels(h,n);
    if (r >= 0 && fault == Fault::ChannelsReadback) ++*n;
    return r;
}
int __real_snd_pcm_link(snd_pcm_t*, snd_pcm_t*);
int __wrap_snd_pcm_link(snd_pcm_t* capture, snd_pcm_t* playback) {
    // Only the link capability is emulated. Do not start these independent null
    // streams or claim physical clock synchronization in this test.
    if (emulateLinkedDevice) return 0;
    return __real_snd_pcm_link(capture,playback);
}
int __real_snd_pcm_unlink(snd_pcm_t*);
int __wrap_snd_pcm_unlink(snd_pcm_t* pcm) { ++unlinkCalls; return __real_snd_pcm_unlink(pcm); }
}
TEST_F(AlsaNativeContract, SupportsIndependentChannelCountsAndReleasesBothDuplexSides) {
    ALSA b; auto cfg = config(); cfg.direction = Direction::Duplex;
    cfg.inputChannels = 3; cfg.outputChannels = 1;
    emulateLinkedDevice = true;
    ASSERT_TRUE(b.open(cfg));
    EXPECT_EQ(captureChannels, 3u); EXPECT_EQ(playbackChannels, 1u);
    EXPECT_EQ(liveHandles.load(), 2);
    ASSERT_TRUE(b.close());
    EXPECT_EQ(unlinkCalls, 1u); EXPECT_EQ(liveHandles.load(), 0);
}
TEST_F(AlsaNativeContract, DuplexSecondSideAndChannelReadbackFailuresReleaseEverything) {
    ALSA b; auto cfg = config(); cfg.direction = Direction::Duplex;
    cfg.inputChannels = 3; cfg.outputChannels = 1;
    for (const auto injected : {Fault::PlaybackChannels, Fault::ChannelsReadback}) {
        fault = injected;
        const auto result = b.open(cfg); ASSERT_FALSE(result);
        EXPECT_EQ(result.error(), ErrorType::ChannelsNotSupported);
        EXPECT_EQ(liveHandles.load(), 0);
    }
    fault = Fault::None;
    const auto unsupported = b.open(cfg);
    ASSERT_FALSE(unsupported); // null PCM does not provide snd_pcm_link
    EXPECT_EQ(unsupported.error(), ErrorType::ConfigurationFailed);
    EXPECT_EQ(liveHandles.load(), 0);
    cfg = config(); ASSERT_TRUE(b.open(cfg)); ASSERT_TRUE(b.close());
}
TEST_F(AlsaNativeContract, InputOnlyAndOutputOnlyPreserveTheirChannelCounts) {
    for (const auto input : {false, true}) {
        ALSA b; auto cfg = config();
        cfg.direction = input ? Direction::Input : Direction::Output;
        cfg.inputChannels = input ? 3 : 0; cfg.outputChannels = input ? 0 : 1;
        ASSERT_TRUE(b.open(cfg));
        EXPECT_EQ(input ? captureChannels : playbackChannels, input ? 3u : 1u);
        ASSERT_TRUE(b.close());
    }
}
