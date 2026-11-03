#include <jack/jack.h>
#include <atomic>
#include <chrono>
#include <expected>
#include <thread>
#include <gtest/gtest.h>
import mka.audio.backend.abstract;
import mka.audio.backend.jack;

namespace {
using namespace mka::audio::core;
using namespace std::chrono_literals;
enum class Fault { None, RateOnActivate, QuantumOnActivate };
std::atomic<Fault> fault{Fault::None};
std::atomic<bool> activated{false};
template<class P> bool waitFor(P p) {
    const auto end = std::chrono::steady_clock::now() + 3s;
    while (!p() && std::chrono::steady_clock::now() < end) std::this_thread::sleep_for(1ms);
    return p();
}
class JackNativeContract : public ::testing::Test {
protected:
    jack_client_t* probe = nullptr;
    EndpointConfig cfg{};
    void SetUp() override {
        fault = Fault::None; activated = false;
        jack_status_t status{};
        probe = jack_client_open("s03-probe", JackNoStartServer, &status);
        if (!probe) GTEST_SKIP() << "serveur JACK absent";
        cfg = {"", Direction::Output, 0, 2, jack_get_sample_rate(probe), Format::Float32, jack_get_buffer_size(probe)};
    }
    void TearDown() override { fault = Fault::None; if (probe) jack_client_close(probe); }
};
}
extern "C" {
int __real_jack_activate(jack_client_t*);
int __wrap_jack_activate(jack_client_t* c) {
    const int r = __real_jack_activate(c);
    if (r == 0) activated = true;
    return r;
}
jack_nframes_t __real_jack_get_sample_rate(jack_client_t*);
jack_nframes_t __wrap_jack_get_sample_rate(jack_client_t* c) {
    const auto r = __real_jack_get_sample_rate(c);
    return r + (activated && fault == Fault::RateOnActivate ? 1 : 0);
}
jack_nframes_t __real_jack_get_buffer_size(jack_client_t*);
jack_nframes_t __wrap_jack_get_buffer_size(jack_client_t* c) {
    const auto r = __real_jack_get_buffer_size(c);
    return r + (activated && fault == Fault::QuantumOnActivate ? 1 : 0);
}
}
TEST_F(JackNativeContract, OpenRejectsDifferentNativeRateAndQuantumAndCanRetry) {
    JACK b; auto invalid = cfg;
    invalid.sampleRate = cfg.sampleRate == 44100 ? 48000 : 44100;
    EXPECT_EQ(b.open(invalid).error(), ErrorType::SampleRateNotSupported);
    invalid = cfg; invalid.bufferSize = cfg.bufferSize == 256 ? 512 : 256;
    EXPECT_EQ(b.open(invalid).error(), ErrorType::BufferSizeNotSupported);
    ASSERT_TRUE(b.open(cfg)); ASSERT_TRUE(b.close());
}
TEST_F(JackNativeContract, ReconfigurationDuringActivationRollsBackAndQuiescesCallbacks) {
    for (const auto injected : {Fault::RateOnActivate, Fault::QuantumOnActivate}) {
        JACK b; std::atomic<unsigned> calls{0};
        ASSERT_TRUE(b.setProcessFunction(+[](void* u, const AudioProcessContext&) noexcept { ++*static_cast<std::atomic<unsigned>*>(u); }, &calls));
        ASSERT_TRUE(b.open(cfg));
        activated = false; fault = injected;
        const auto result = b.start(); ASSERT_FALSE(result);
        EXPECT_EQ(result.error(), injected == Fault::RateOnActivate ? ErrorType::SampleRateNotSupported : ErrorType::BufferSizeNotSupported);
        const auto count = calls.load(); std::this_thread::sleep_for(30ms);
        EXPECT_EQ(calls.load(), count);
        fault = Fault::None;
        ASSERT_TRUE(b.start()); ASSERT_TRUE(b.stop()); ASSERT_TRUE(b.close());
    }
}
TEST_F(JackNativeContract, CallbackUsesTheServersRealFrameCount) {
    struct Observed { std::atomic<unsigned> calls{0}; std::atomic<bool> wrong{false}; unsigned expected; } seen{{0},{false},cfg.bufferSize};
    JACK b;
    ASSERT_TRUE(b.setProcessFunction(+[](void* u, const AudioProcessContext& ctx) noexcept {
        auto& s = *static_cast<Observed*>(u);
        if (ctx.frames != s.expected || ctx.input.channels != nullptr || ctx.input.count != 0 || ctx.output.count != 2) s.wrong = true;
        ++s.calls;
    }, &seen));
    ASSERT_TRUE(b.open(cfg)); ASSERT_TRUE(b.start());
    EXPECT_TRUE(waitFor([&] { return seen.calls.load() > 10; }));
    ASSERT_TRUE(b.stop()); ASSERT_TRUE(b.close());
    EXPECT_FALSE(seen.wrong.load());
}
