//
// Created by mika on 9/26/26.
//

#include <algorithm>
#include <gtest/gtest.h>
#include <print>
#include <cmath>
#include "../../utils/alsa_manual_audio.hpp"

import mka.audio.backend.alsa;
import mka.audio.process;

static const char *fmtToStr(mka::audio::core::Format format) {
    switch (format) {
        case mka::audio::core::Format::Int16: return "int16";
        case mka::audio::core::Format::Int24: return "int24";
        case mka::audio::core::Format::Int32: return "int32";
        case mka::audio::core::Format::Float32: return "float32";
        case mka::audio::core::Format::Float64: return "float64";
    }
    std::unreachable();
}

static void printCaps(const mka::audio::core::StreamCapabilities &caps) {
    std::println("  channels: {} - {}", caps.minChannels, caps.maxChannels);

    std::print("  sample rates: ");
    for (const auto &sampleRate: caps.sampleRates) {
        std::print("{}, ", sampleRate);
    }
    std::print("\n  formats: ");
    for (const auto &format: caps.formats) {
        std::print("{}, ", fmtToStr(format));
    }
    std::print("\n  buffer sizes: ");
    for (const auto &bufferSize: caps.bufferSizes) {
        std::print("{}, ", bufferSize);
    }
    std::println("");
}

// Physical-device scenarios are explicit skips when the original hw:1,0
// fixture is absent. Null-PCM and argument-validation tests stay independent.
class ALSABackendHwTest : public ::testing::Test {
protected:
    void SetUp() override {
        mka::audio::core::ALSA backend;
        const auto endpoints = backend.getEndPoints();
        if (std::ranges::none_of(endpoints, [](const auto& e) { return e.id == "hw:1,0"; }))
            GTEST_SKIP() << "physical ALSA fixture hw:1,0 is absent";
    }
};

TEST(ALSABackendTest, TestGetEndPoints) {
    const mka::audio::core::ALSA alsa;
    const auto endpoints = alsa.getEndPoints();

    for (const auto &endpoint: endpoints) {
        EXPECT_TRUE(endpoint.input.has_value() || endpoint.output.has_value())
            << "endpoint " << endpoint.id << " has neither input nor output";

        EXPECT_FALSE(endpoint.id.empty());
        EXPECT_FALSE(endpoint.name.empty());

        if (endpoint.input) {
            EXPECT_LE(endpoint.input->minChannels, endpoint.input->maxChannels);
            EXPECT_FALSE(endpoint.input->sampleRates.empty()
                && endpoint.input->formats.empty()
                && endpoint.input->bufferSizes.empty())
                << "endpoint " << endpoint.id << " has empty input capabilities";
        }
        if (endpoint.output) {
            EXPECT_LE(endpoint.output->minChannels, endpoint.output->maxChannels);
            EXPECT_FALSE(endpoint.output->sampleRates.empty()
                && endpoint.output->formats.empty()
                && endpoint.output->bufferSizes.empty())
                << "endpoint " << endpoint.id << " has empty output capabilities";
        }

        std::println("id: {}", endpoint.id);
        std::println("name: {}", endpoint.name);

        if (endpoint.input) {
            std::println("input:");
            printCaps(*endpoint.input);
        }
        if (endpoint.output) {
            std::println("output:");
            printCaps(*endpoint.output);
        }

        std::println("-----------------");
    }
}

TEST_F(ALSABackendHwTest, TestGetEndPointsContainsKnownDevice) {
    const mka::audio::core::ALSA alsa;
    const auto endpoints = alsa.getEndPoints();

    const auto it = std::ranges::find_if(endpoints, [](const mka::audio::core::Endpoint &e) {
        return e.id == "hw:1,0";
    });

    ASSERT_NE(it, endpoints.end());
    EXPECT_TRUE(it->input.has_value());
    EXPECT_TRUE(it->output.has_value());
}

//--- Test Open

TEST(ALSABackendTest, TestOpenInvalidID) {
    mka::audio::core::ALSA alsa;
    const mka::audio::core::EndpointConfig config{
        .id = "InvalidID",
        .direction = mka::audio::core::Direction::Output,
        .inputChannels = 0, .outputChannels = 2, .sampleRate = 48000,
        .format = mka::audio::core::Format::Float32, .bufferSize = 256,
    };

    auto ret = alsa.open(config);
    ASSERT_FALSE(ret);
    ASSERT_EQ(ret.error(), mka::audio::core::ErrorType::EndpointUnavailable);

    ASSERT_FALSE(alsa.close());
}

TEST(ALSABackendTest, TestOpenInvalidFmt) {
    mka::audio::core::ALSA alsa;
    const mka::audio::core::EndpointConfig config{
        .id = "hw:1,0",
        .direction = mka::audio::core::Direction::Duplex,
        .inputChannels = 1,
        .outputChannels = 1,
        .sampleRate = 44100,
        .format = static_cast<mka::audio::core::Format>(255),
        .bufferSize = 512,
    };

    auto ret = alsa.open(config);
    ASSERT_FALSE(ret);
    ASSERT_EQ(ret.error(), mka::audio::core::ErrorType::FormatNotSupported);
    ASSERT_FALSE(alsa.close());
}

TEST(ALSABackendTest, TestOpenInvalidChannelCount) {
    mka::audio::core::ALSA alsa;
    const mka::audio::core::EndpointConfig config{
        .id = "hw:1,0",
        .direction = mka::audio::core::Direction::Duplex,
        .inputChannels = 89,
        .outputChannels = 0,
        .sampleRate = 44100,
        .format = mka::audio::core::Format::Int32,
        .bufferSize = 512,
    };

    auto ret = alsa.open(config);
    ASSERT_FALSE(ret);
    ASSERT_EQ(ret.error(), mka::audio::core::ErrorType::ChannelsNotSupported);
    ASSERT_FALSE(alsa.close());
}

TEST(ALSABackendTest, TestOpenInvalidSamplerate) {
    mka::audio::core::ALSA alsa;
    const mka::audio::core::EndpointConfig config{
        .id = "hw:1,0",
        .direction = mka::audio::core::Direction::Duplex,
        .inputChannels = 2,
        .outputChannels = 2,
        .sampleRate = 0,
        .format = mka::audio::core::Format::Int32,
        .bufferSize = 512,
    };

    auto ret = alsa.open(config);
    ASSERT_FALSE(ret);
    ASSERT_EQ(ret.error(), mka::audio::core::ErrorType::SampleRateNotSupported);
    ASSERT_FALSE(alsa.close());
}

TEST(ALSABackendTest, TestOpenInvalidBuffSize) {
    mka::audio::core::ALSA alsa;
    const mka::audio::core::EndpointConfig config{
        .id = "hw:1,0",
        .direction = mka::audio::core::Direction::Duplex,
        .inputChannels = 2,
        .outputChannels = 2,
        .sampleRate = 44100,
        .format = mka::audio::core::Format::Int32,
        .bufferSize = 0,
    };

    auto ret = alsa.open(config);
    ASSERT_FALSE(ret);
    ASSERT_EQ(ret.error(), mka::audio::core::ErrorType::BufferSizeNotSupported);
    ASSERT_FALSE(alsa.close());
}

TEST_F(ALSABackendHwTest, TestOpenPartialFailureCleansUpCapture) {
    mka::audio::core::ALSA alsa;
    const mka::audio::core::EndpointConfig config{
        .id = "hw:1,0",
        .direction = mka::audio::core::Direction::Duplex,
        .inputChannels = 2,
        .outputChannels = 89,
        .sampleRate = 44100,
        .format = mka::audio::core::Format::Int32,
        .bufferSize = 512,
    };

    auto ret = alsa.open(config);
    ASSERT_FALSE(ret);
    ASSERT_EQ(ret.error(), mka::audio::core::ErrorType::ChannelsNotSupported);
    ASSERT_FALSE(alsa.close());
}

TEST_F(ALSABackendHwTest, TestOpenTwiceFailsWithInvalidState) {
    mka::audio::core::ALSA alsa;
    const mka::audio::core::EndpointConfig config{
        .id = "hw:1,0",
        .direction = mka::audio::core::Direction::Duplex,
        .inputChannels = 2,
        .outputChannels = 2,
        .sampleRate = 44100,
        .format = mka::audio::core::Format::Int32,
        .bufferSize = 512,
    };

    ASSERT_TRUE(alsa.open(config));

    auto ret = alsa.open(config);
    ASSERT_FALSE(ret);
    ASSERT_EQ(ret.error(), mka::audio::core::ErrorType::InvalidState);
    ASSERT_TRUE(alsa.close());
}

TEST_F(ALSABackendHwTest, TestOpenSucceed) {
    mka::audio::core::ALSA alsa;
    const mka::audio::core::EndpointConfig config{
        .id = "hw:1,0",
        .direction = mka::audio::core::Direction::Duplex,
        .inputChannels = 2,
        .outputChannels = 2,
        .sampleRate = 44100,
        .format = mka::audio::core::Format::Int32,
        .bufferSize = 512,
    };

    auto ret = alsa.open(config);
    ASSERT_TRUE(alsa.close());
    ASSERT_TRUE(ret);
}

namespace {
    std::atomic<bool> g_callbackCalled{false};
    std::atomic<std::thread::id> g_callbackThreadId{};

    void testCallback(void *, const mka::audio::core::AudioProcessContext &) noexcept {
        g_callbackThreadId.store(std::this_thread::get_id());
        g_callbackCalled.store(true);
    }
}

//--- Test Start

TEST(ALSABackendTest, TestStartWithoutOpenFailsInvalidState) {
    mka::audio::core::ALSA alsa;

    auto ret = alsa.start();
    ASSERT_FALSE(ret);
    ASSERT_EQ(ret.error(), mka::audio::core::ErrorType::InvalidState);
}

TEST_F(ALSABackendHwTest, TestStartSucceedsAfterOpen) {
    mka::audio::core::ALSA alsa;
    const mka::audio::core::EndpointConfig config{
        .id = "hw:1,0",
        .direction = mka::audio::core::Direction::Duplex,
        .inputChannels = 2,
        .outputChannels = 2,
        .sampleRate = 44100,
        .format = mka::audio::core::Format::Int32,
        .bufferSize = 512,
    };

    ASSERT_TRUE(alsa.open(config));

    auto ret = alsa.start();
    ASSERT_TRUE(ret);
}

TEST_F(ALSABackendHwTest, TestStartTwiceFailsInvalidState) {
    mka::audio::core::ALSA alsa;
    const mka::audio::core::EndpointConfig config{
        .id = "hw:1,0",
        .direction = mka::audio::core::Direction::Duplex,
        .inputChannels = 2,
        .outputChannels = 2,
        .sampleRate = 44100,
        .format = mka::audio::core::Format::Int32,
        .bufferSize = 512,
    };

    ASSERT_TRUE(alsa.open(config));
    ASSERT_TRUE(alsa.start());

    auto ret = alsa.start();
    ASSERT_FALSE(ret);
    ASSERT_EQ(ret.error(), mka::audio::core::ErrorType::InvalidState);
}

TEST_F(ALSABackendHwTest, TestStartInvokesCallbackOnDifferentThread) {
    mka::audio::core::ALSA alsa;
    const mka::audio::core::EndpointConfig config{
        .id = "hw:1,0",
        .direction = mka::audio::core::Direction::Duplex,
        .inputChannels = 2,
        .outputChannels = 2,
        .sampleRate = 44100,
        .format = mka::audio::core::Format::Int32,
        .bufferSize = 512,
    };

    g_callbackCalled.store(false);
    g_callbackThreadId.store({});

    ASSERT_TRUE(alsa.setProcessFunction(testCallback));
    ASSERT_TRUE(alsa.open(config));

    const auto callingThreadId = std::this_thread::get_id();
    ASSERT_TRUE(alsa.start());

    for (int i = 0; i < 100 && !g_callbackCalled.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    ASSERT_TRUE(g_callbackCalled.load());
    ASSERT_NE(g_callbackThreadId.load(), callingThreadId);
}

TEST_F(ALSABackendHwTest, TestSetProcessFunctionFailsWhileRunning) {
    mka::audio::core::ALSA alsa;
    const mka::audio::core::EndpointConfig config{
        .id = "hw:1,0",
        .direction = mka::audio::core::Direction::Duplex,
        .inputChannels = 2,
        .outputChannels = 2,
        .sampleRate = 44100,
        .format = mka::audio::core::Format::Int32,
        .bufferSize = 512,
    };

    ASSERT_TRUE(alsa.open(config));
    ASSERT_TRUE(alsa.start());

    auto ret = alsa.setProcessFunction(testCallback);
    ASSERT_FALSE(ret);
    ASSERT_EQ(ret.error(), mka::audio::core::ErrorType::InvalidState);
}

//--- Test Stop

TEST_F(ALSABackendHwTest, TestStopWithoutStartFailsInvalidState) {
    mka::audio::core::ALSA alsa;
    const mka::audio::core::EndpointConfig config{
        .id = "hw:1,0",
        .direction = mka::audio::core::Direction::Duplex,
        .inputChannels = 2,
        .outputChannels = 2,
        .sampleRate = 44100,
        .format = mka::audio::core::Format::Int32,
        .bufferSize = 512,
    };

    ASSERT_TRUE(alsa.open(config));

    auto ret = alsa.stop();
    ASSERT_FALSE(ret);
    ASSERT_EQ(ret.error(), mka::audio::core::ErrorType::InvalidState);
}

TEST_F(ALSABackendHwTest, TestStopSucceedsAfterStart) {
    mka::audio::core::ALSA alsa;
    const mka::audio::core::EndpointConfig config{
        .id = "hw:1,0",
        .direction = mka::audio::core::Direction::Duplex,
        .inputChannels = 2,
        .outputChannels = 2,
        .sampleRate = 44100,
        .format = mka::audio::core::Format::Int32,
        .bufferSize = 512,
    };

    ASSERT_TRUE(alsa.open(config));
    ASSERT_TRUE(alsa.start());

    auto ret = alsa.stop();
    ASSERT_TRUE(ret);
}

TEST_F(ALSABackendHwTest, TestStopTwiceFailsInvalidState) {
    mka::audio::core::ALSA alsa;
    const mka::audio::core::EndpointConfig config{
        .id = "hw:1,0",
        .direction = mka::audio::core::Direction::Duplex,
        .inputChannels = 2,
        .outputChannels = 2,
        .sampleRate = 44100,
        .format = mka::audio::core::Format::Int32,
        .bufferSize = 512,
    };

    ASSERT_TRUE(alsa.open(config));
    ASSERT_TRUE(alsa.start());
    ASSERT_TRUE(alsa.stop());

    auto ret = alsa.stop();
    ASSERT_FALSE(ret);
    ASSERT_EQ(ret.error(), mka::audio::core::ErrorType::InvalidState);
}

TEST_F(ALSABackendHwTest, TestStopActuallyHaltsCallbackInvocations) {
    mka::audio::core::ALSA alsa;
    const mka::audio::core::EndpointConfig config{
        .id = "hw:1,0",
        .direction = mka::audio::core::Direction::Duplex,
        .inputChannels = 2,
        .outputChannels = 2,
        .sampleRate = 44100,
        .format = mka::audio::core::Format::Int32,
        .bufferSize = 512,
    };

    g_callbackCalled.store(false);

    ASSERT_TRUE(alsa.setProcessFunction(testCallback));
    ASSERT_TRUE(alsa.open(config));
    ASSERT_TRUE(alsa.start());

    for (int i = 0; i < 100 && !g_callbackCalled.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    ASSERT_TRUE(g_callbackCalled.load());

    ASSERT_TRUE(alsa.stop());

    g_callbackCalled.store(false);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    ASSERT_FALSE(g_callbackCalled.load());
}

TEST_F(ALSABackendHwTest, TestStopAllowsReopenAndRestart) {
    mka::audio::core::ALSA alsa;
    const mka::audio::core::EndpointConfig config{
        .id = "hw:1,0",
        .direction = mka::audio::core::Direction::Duplex,
        .inputChannels = 2,
        .outputChannels = 2,
        .sampleRate = 44100,
        .format = mka::audio::core::Format::Int32,
        .bufferSize = 512,
    };

    ASSERT_TRUE(alsa.open(config));
    ASSERT_TRUE(alsa.start());
    ASSERT_TRUE(alsa.stop());

    auto ret = alsa.start();
    ASSERT_TRUE(ret);
    ASSERT_TRUE(alsa.stop());
}

//--- Test Close

TEST(ALSABackendTest, TestCloseWithoutOpenFailsInvalidState) {
    mka::audio::core::ALSA alsa;

    auto ret = alsa.close();
    ASSERT_FALSE(ret);
    ASSERT_EQ(ret.error(), mka::audio::core::ErrorType::InvalidState);
}

TEST_F(ALSABackendHwTest, TestCloseSucceedsAfterOpen) {
    mka::audio::core::ALSA alsa;
    const mka::audio::core::EndpointConfig config{
        .id = "hw:1,0",
        .direction = mka::audio::core::Direction::Duplex,
        .inputChannels = 2,
        .outputChannels = 2,
        .sampleRate = 44100,
        .format = mka::audio::core::Format::Int32,
        .bufferSize = 512,
    };

    ASSERT_TRUE(alsa.open(config));

    auto ret = alsa.close();
    ASSERT_TRUE(ret);
}

TEST_F(ALSABackendHwTest, TestCloseFailsWhileRunning) {
    mka::audio::core::ALSA alsa;
    const mka::audio::core::EndpointConfig config{
        .id = "hw:1,0",
        .direction = mka::audio::core::Direction::Duplex,
        .inputChannels = 2,
        .outputChannels = 2,
        .sampleRate = 44100,
        .format = mka::audio::core::Format::Int32,
        .bufferSize = 512,
    };

    ASSERT_TRUE(alsa.open(config));
    ASSERT_TRUE(alsa.start());

    // Backend impose l'ordre open -> start -> stop -> close : close_ doit être
    // refusé tant que le flux tourne encore (state == Running, pas Open).
    auto ret = alsa.close();
    ASSERT_FALSE(ret);
    ASSERT_EQ(ret.error(), mka::audio::core::ErrorType::InvalidState);

    ASSERT_TRUE(alsa.stop());
    ASSERT_TRUE(alsa.close());
}

TEST_F(ALSABackendHwTest, TestCloseTwiceFailsInvalidState) {
    mka::audio::core::ALSA alsa;
    const mka::audio::core::EndpointConfig config{
        .id = "hw:1,0",
        .direction = mka::audio::core::Direction::Duplex,
        .inputChannels = 2,
        .outputChannels = 2,
        .sampleRate = 44100,
        .format = mka::audio::core::Format::Int32,
        .bufferSize = 512,
    };

    ASSERT_TRUE(alsa.open(config));
    ASSERT_TRUE(alsa.close());

    auto ret = alsa.close();
    ASSERT_FALSE(ret);
    ASSERT_EQ(ret.error(), mka::audio::core::ErrorType::InvalidState);
}

TEST_F(ALSABackendHwTest, TestCloseReleasesDeviceForReopen) {
    mka::audio::core::ALSA alsa;
    const mka::audio::core::EndpointConfig config{
        .id = "hw:1,0",
        .direction = mka::audio::core::Direction::Duplex,
        .inputChannels = 2,
        .outputChannels = 2,
        .sampleRate = 44100,
        .format = mka::audio::core::Format::Int32,
        .bufferSize = 512,
    };

    ASSERT_TRUE(alsa.open(config));
    ASSERT_TRUE(alsa.close());

    // Si close_ ne libère pas vraiment les handles ALSA (snd_pcm_close), le
    // device resterait marqué occupé et ce second open échouerait en
    // EndpointUnavailable plutôt que de réussir.
    auto ret = alsa.open(config);
    ASSERT_TRUE(ret);
    ASSERT_TRUE(alsa.close());
}

TEST_F(ALSABackendHwTest, TestFullLifecycleOpenStartStopClose) {
    mka::audio::core::ALSA alsa;
    const mka::audio::core::EndpointConfig config{
        .id = "hw:1,0",
        .direction = mka::audio::core::Direction::Duplex,
        .inputChannels = 2,
        .outputChannels = 2,
        .sampleRate = 44100,
        .format = mka::audio::core::Format::Int32,
        .bufferSize = 512,
    };

    ASSERT_TRUE(alsa.open(config));
    ASSERT_TRUE(alsa.start());
    ASSERT_TRUE(alsa.stop());
    ASSERT_TRUE(alsa.close());
}

// Remain disabled in CI; run these explicitly with --gtest_also_run_disabled_tests.
// This suite does not inherit the old hard-coded hw:1,0 hardware fixture.
TEST(ALSAManualTest, DISABLED_TestPlaySineWave) {
    alsa_manual::Tone tone;
    mka::audio::core::ALSA backend;
    ASSERT_TRUE(backend.setProcessFunction(+[](void* user,const mka::audio::core::AudioProcessContext& ctx) noexcept {
        static_cast<alsa_manual::Tone*>(user)->process(ctx);
    },&tone));
    ASSERT_TRUE(alsa_manual::open(backend,alsa_manual::config<mka::audio::core::EndpointConfig>(false)));
    ASSERT_TRUE(backend.start());
    const bool complete=alsa_manual::wait(backend,[&]{return tone.frames.load()>=tone.total;},std::chrono::seconds(8));
    const auto status=backend.status();
    ASSERT_TRUE(backend.stop());ASSERT_TRUE(backend.close());
    std::println("Sinus: {} frames generees, {} xruns, echec={}",tone.frames.load(),status.xruns,status.failed);
    ASSERT_TRUE(complete)<<"Le callback de sortie n'a pas progresse jusqu'au bout (voir status/PCM).";
    EXPECT_FALSE(status.failed);
}

TEST(ALSAManualSignalTest, ConstantInputDoesNotCountAsCapturedAudio) {
    alsa_manual::Clip clip(4800);
    for(auto& channel:clip.samples)std::fill(channel.begin(),channel.end(),0.25f);
    clip.recorded=clip.capacity;
    clip.removeDC();
    EXPECT_FLOAT_EQ(clip.level().peak,0.0f);
    EXPECT_DOUBLE_EQ(clip.level().rms,0.0);
}

TEST(ALSAManualSignalTest, QuietSingleChannelIsAmplifiedAndReplayedOnBothSpeakers) {
    alsa_manual::Clip clip(4800);
    for(std::size_t i=0;i<clip.capacity;++i)
        clip.samples[1][i]=static_cast<float>(0.01*std::sin(2*std::numbers::pi*440*i/alsa_manual::rate));
    clip.recorded=clip.capacity;
    clip.removeDC();
    EXPECT_GT(clip.level().rms,0.004);
    EXPECT_FLOAT_EQ(clip.prepareReplay(),32.0f);
    EXPECT_EQ(clip.samples[0],clip.samples[1]);
    EXPECT_GT(clip.level().peak,0.31f);
    EXPECT_FLOAT_EQ(clip.samples[0].front(),0.0f);
    EXPECT_FLOAT_EQ(clip.samples[0].back(),0.0f);
}

TEST(ALSAManualTest, DISABLED_TestRecordAndPlayback) {
    alsa_manual::Clip clip;
    std::println("Enregistrement pendant 5 secondes : parlez dans le microphone maintenant.");
    std::fflush(stdout);
    {
        mka::audio::core::ALSA recorder;
        ASSERT_TRUE(recorder.setProcessFunction(+[](void* user,const mka::audio::core::AudioProcessContext& ctx) noexcept {
            static_cast<alsa_manual::Clip*>(user)->record(ctx);
        },&clip));
        ASSERT_TRUE(alsa_manual::open(recorder,alsa_manual::config<mka::audio::core::EndpointConfig>(true)));
        ASSERT_TRUE(recorder.start());
        const bool complete=alsa_manual::wait(recorder,[&]{return clip.recorded.load()>=clip.capacity;},std::chrono::seconds(12));
        const auto status=recorder.status();
        ASSERT_TRUE(recorder.stop());ASSERT_TRUE(recorder.close());
        std::println("Capture: {} frames, {} xruns, echec={}",clip.recorded.load(),status.xruns,status.failed);
        ASSERT_TRUE(complete)<<"Pas de progression de capture : delai depasse, fin du test plutot qu'une attente infinie.";
        ASSERT_FALSE(status.failed);
    }
    clip.removeDC();
    const auto level=clip.level();
    std::println("Signal capture (sans composante continue): crete={:.8f}, RMS={:.8f} ({:.1f} dBFS)",
        level.peak,level.rms,20*std::log10(std::max(level.rms,1e-12)));
    const char* capturePath=std::getenv("MKA_TEST_ALSA_CAPTURE_WAV");
    if(!capturePath || !*capturePath)capturePath="mka-alsa-capture.wav";
    ASSERT_TRUE(clip.saveWav(capturePath))<<"Impossible d'ecrire le WAV : "<<capturePath;
    std::println("Capture avant amplification sauvegardee : {}",capturePath);
    ASSERT_GT(level.peak,0.001f)
        <<"Capture silencieuse ou trop faible. Verifier le PCM d'entree, Capture/Mic mute et le gain avec alsamixer/amixer.";
    ASSERT_GT(level.rms,0.0001)
        <<"Energie capturee insuffisante (moins de -80 dBFS) : parler pendant la capture et verifier le micro selectionne.";
    const auto gain=clip.prepareReplay();
    std::println("Relecture du canal micro le plus fort sur les deux sorties, gain x{:.2f}",gain);
    std::println("Lecture de la capture dans 1 seconde...");
    std::this_thread::sleep_for(std::chrono::seconds(1));
    {
        mka::audio::core::ALSA player;
        ASSERT_TRUE(player.setProcessFunction(+[](void* user,const mka::audio::core::AudioProcessContext& ctx) noexcept {
            static_cast<alsa_manual::Clip*>(user)->play(ctx);
        },&clip));
        ASSERT_TRUE(alsa_manual::open(player,alsa_manual::config<mka::audio::core::EndpointConfig>(false)));
        ASSERT_TRUE(player.start());
        const bool complete=alsa_manual::wait(player,[&]{return clip.played.load()>=clip.lead+clip.capacity+clip.tail;},std::chrono::seconds(12));
        const auto status=player.status();
        ASSERT_TRUE(player.stop());ASSERT_TRUE(player.close());
        std::println("Lecture: {} frames, {} xruns, echec={}",clip.played.load(),status.xruns,status.failed);
        ASSERT_TRUE(complete)<<"La relecture n'a pas progresse jusqu'au bout.";
        EXPECT_FALSE(status.failed);
    }
}

TEST(ALSABackendTest, OpenValidatesTheNullPcmConfigurationAndClosesWithoutHardware) {
    mka::audio::core::ALSA backend;
    const mka::audio::core::EndpointConfig config{
        .id = "null", .direction = mka::audio::core::Direction::Output,
        .inputChannels = 0, .outputChannels = 2, .sampleRate = 48000,
        .format = mka::audio::core::Format::Float32, .bufferSize = 256
    };
    ASSERT_TRUE(backend.open(config));
    ASSERT_TRUE(backend.close());
    ASSERT_TRUE(backend.open(config));
    ASSERT_TRUE(backend.close());
}

TEST(ALSABackendTest, UnlinkableDuplexIsRejectedAndBothHandlesAreReleased) {
    mka::audio::core::ALSA backend;
    mka::audio::core::EndpointConfig config{
        .id = "null", .direction = mka::audio::core::Direction::Duplex,
        .inputChannels = 1, .outputChannels = 2, .sampleRate = 48000,
        .format = mka::audio::core::Format::Float32, .bufferSize = 256
    };
    // ALSA's null PCM cannot link capture/playback. Do not silently start two
    // independent streams when the requested contract is synchronized duplex.
    const auto result = backend.open(config);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), mka::audio::core::ErrorType::ConfigurationFailed);
    EXPECT_FALSE(backend.start());
    config.direction = mka::audio::core::Direction::Output;
    config.inputChannels = 0;
    ASSERT_TRUE(backend.open(config));
    ASSERT_TRUE(backend.close());
    config.direction = mka::audio::core::Direction::Input;
    config.inputChannels = 1;
    config.outputChannels = 0;
    ASSERT_TRUE(backend.open(config));
    ASSERT_TRUE(backend.close());
}
