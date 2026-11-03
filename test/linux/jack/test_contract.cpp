#include <jack/jack.h>
#include <atomic>
#include <chrono>
#include <cerrno>
#include <expected>
#include <thread>
#include <gtest/gtest.h>
import mka.audio.backend.abstract;
import mka.audio.backend.jack;

namespace {
using namespace mka::audio::core;
using namespace std::chrono_literals;
enum class Fault { None, RateOnActivate, QuantumOnActivate, RegisterPort, ConnectPort };
std::atomic<Fault> fault{Fault::None};
std::atomic<bool> activated{false};
std::atomic<unsigned> registrations{0}, connections{0};
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
        fault = Fault::None; activated = false; registrations = connections = 0;
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

extern "C" {
jack_port_t* __real_jack_port_register(jack_client_t*,const char*,const char*,unsigned long,unsigned long);
jack_port_t* __wrap_jack_port_register(jack_client_t* c,const char* name,const char* type,unsigned long flags,unsigned long size) {
    const auto index = ++registrations;
    if (fault == Fault::RegisterPort && index == 2) return nullptr;
    return __real_jack_port_register(c,name,type,flags,size);
}
int __real_jack_connect(jack_client_t*,const char*,const char*);
int __wrap_jack_connect(jack_client_t* c,const char* from,const char* to) {
    const auto index = ++connections;
    if (fault == Fault::ConnectPort && index == 2) return EIO;
    return __real_jack_connect(c,from,to);
}
}
TEST_F(JackNativeContract, PortsRouteEachChannelOneToOneAndReconnectAfterStop) {
    struct Peer {
        jack_port_t* sources[2]{}; jack_port_t* sinks[2]{};
        std::atomic<unsigned> matchingCycles{0};
    } peer;
    for (unsigned ch = 0; ch < 2; ++ch) {
        peer.sources[ch] = jack_port_register(probe, ch ? "from_2" : "from_1", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
        peer.sinks[ch] = jack_port_register(probe, ch ? "to_2" : "to_1", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
        ASSERT_NE(peer.sources[ch], nullptr); ASSERT_NE(peer.sinks[ch], nullptr);
    }
    struct Quiesce { jack_client_t* client; ~Quiesce() { jack_deactivate(client); } } quiesce{probe};
    ASSERT_EQ(jack_set_process_callback(probe, +[](jack_nframes_t n,void* u) {
        auto& p = *static_cast<Peer*>(u); bool matching = true;
        for (unsigned ch = 0; ch < 2; ++ch) {
            auto* out = static_cast<float*>(jack_port_get_buffer(p.sources[ch], n));
            const auto* in = static_cast<const float*>(jack_port_get_buffer(p.sinks[ch], n));
            for (jack_nframes_t i = 0; i < n; ++i) {
                out[i] = float(ch + 1);
                if (in[i] != float(ch + 1) + 0.5f) matching = false;
            }
        }
        if (matching) ++p.matchingCycles;
        return 0;
    }, &peer),0);
    ASSERT_EQ(jack_activate(probe), 0);
    JACK b; cfg.id = jack_get_client_name(probe); cfg.direction = Direction::Duplex; cfg.inputChannels = 2;
    ASSERT_TRUE(b.setProcessFunction(+[](void*, const AudioProcessContext& ctx) noexcept {
        for (unsigned ch = 0; ch < 2; ++ch)
            for (unsigned i = 0; i < ctx.frames; ++i) ctx.output.channels[ch][i] = ctx.input.channels[ch][i] + 0.5f;
    }));
    ASSERT_TRUE(b.open(cfg));
    for (int cycle = 0; cycle < 2; ++cycle) {
        peer.matchingCycles = 0;
        ASSERT_TRUE(b.start());
        EXPECT_TRUE(waitFor([&] { return peer.matchingCycles.load() > 10; }));
        EXPECT_EQ(jack_port_connected_to(peer.sources[0], "mka-audio:in_1"),1);
        EXPECT_EQ(jack_port_connected_to(peer.sources[0], "mka-audio:in_2"),0);
        EXPECT_EQ(jack_port_connected_to(peer.sinks[1], "mka-audio:out_2"),1);
        ASSERT_TRUE(b.stop());
        EXPECT_EQ(jack_port_connected(peer.sources[0]),0);
        EXPECT_EQ(jack_port_connected(peer.sinks[1]),0);
    }
    ASSERT_TRUE(b.close());
    EXPECT_EQ(jack_port_by_name(probe,"mka-audio:in_1"),nullptr);
}
TEST_F(JackNativeContract, PortRegistrationAndRoutingFailuresRollbackAndAllowRetry) {
    JACK b;
    fault = Fault::RegisterPort;
    const auto open = b.open(cfg); ASSERT_FALSE(open); EXPECT_EQ(open.error(),ErrorType::ConfigurationFailed);
    EXPECT_EQ(jack_port_by_name(probe,"mka-audio:out_1"),nullptr);
    fault = Fault::None; cfg.id = "system";
    ASSERT_TRUE(b.open(cfg));
    fault = Fault::ConnectPort; connections = 0;
    const auto started = b.start(); ASSERT_FALSE(started); EXPECT_EQ(started.error(),ErrorType::EndpointUnavailable);
    fault = Fault::None;
    ASSERT_TRUE(b.start()); ASSERT_TRUE(b.stop()); ASSERT_TRUE(b.close());
    EXPECT_EQ(jack_port_by_name(probe,"mka-audio:out_1"),nullptr);
}
