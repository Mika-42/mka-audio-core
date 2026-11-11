#include <gtest/gtest.h>
#include "../../utils/manual_audio.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <thread>
#include "../../../src/backend/windows/detail/asio_driver.hpp"
import mka.audio.backend.asio;
#include "hardware_helpers.hpp"

namespace {
using namespace asio_hardware;
TEST(AsioManualTest, DISABLED_TestPlaySineWave) {
    ASSERT_TRUE(nativeWindows())<<"Essai sonore ASIO prevu sous Windows natif.";
    const auto selected=configuration(Direction::Output);ASSERT_TRUE(selected)<<selected.error();describe(*selected);
    manual_audio::Tone tone(selected->config.sampleRate);ASIO backend;
    ASSERT_TRUE(backend.setProcessFunction(+[](void* user,const AudioProcessContext& context) noexcept {
        static_cast<manual_audio::Tone*>(user)->process(context);
    },&tone));
    ASSERT_TRUE(open(backend,selected->config));
    std::puts("Sinus 440 Hz pendant 3 secondes, amplitude 0.2.");std::fflush(stdout);ASSERT_TRUE(backend.start());
    const bool complete=wait(backend,[&]{return tone.invalidContext.load() || tone.frames.load()>=tone.total;},std::chrono::seconds(8));
    const auto status=backend.status();ASSERT_TRUE(backend.stop());ASSERT_TRUE(backend.close());
    std::printf("Sinus : %zu frames, %llu xruns, echec=%s\n",tone.frames.load(),
        static_cast<unsigned long long>(status.xruns),status.failed?"true":"false");
    EXPECT_FALSE(tone.invalidContext.load());EXPECT_FALSE(status.failed);EXPECT_TRUE(complete);
}
TEST(AsioManualTest, DISABLED_TestRecordAndPlayback) {
    ASSERT_TRUE(nativeWindows())<<"Essai sonore ASIO prevu sous Windows natif.";
    const auto input=configuration(Direction::Input);ASSERT_TRUE(input)<<input.error();
    const auto output=configuration(Direction::Output);ASSERT_TRUE(output)<<output.error();describe(*input);describe(*output);
    const auto& capture=input->config;const auto& playback=output->config;
    manual_audio::Clip clip(capture.sampleRate,capture.inputChannels,std::size_t(capture.sampleRate)*5);
    {
        ASIO recorder;ASSERT_TRUE(recorder.setProcessFunction(+[](void* user,const AudioProcessContext& context) noexcept {
            static_cast<manual_audio::Clip*>(user)->record(context);
        },&clip));
        ASSERT_TRUE(open(recorder,capture));std::puts("Enregistrement 5 secondes : parlez dans le microphone maintenant.");std::fflush(stdout);
        ASSERT_TRUE(recorder.start());
        const bool complete=wait(recorder,[&]{return clip.invalidContext.load() || clip.recorded.load()>=clip.capacity;},std::chrono::seconds(12));
        const auto status=recorder.status();ASSERT_TRUE(recorder.stop());ASSERT_TRUE(recorder.close());
        std::printf("Capture : %zu frames, %llu xruns, echec=%s\n",clip.recorded.load(),
            static_cast<unsigned long long>(status.xruns),status.failed?"true":"false");
        ASSERT_FALSE(clip.invalidContext.load());ASSERT_FALSE(status.failed);ASSERT_TRUE(complete);
    }
    const auto levels=clip.levels();
    for (std::size_t channel=0;channel<levels.size();++channel) {
        ASSERT_TRUE(levels[channel].finite)<<"Echantillons non finis sur le canal "<<channel+1;
        std::printf("Micro canal %zu sans DC : crete=%.8f, RMS=%.8f (%.1f dBFS)\n",
            channel+1,levels[channel].peak,levels[channel].rms,20*std::log10(std::max(levels[channel].rms,1e-12)));
    }
    const auto* path=std::getenv("MKA_TEST_ASIO_CAPTURE_WAV");if (!path || !*path)path="mka-asio-capture.wav";
    ASSERT_TRUE(clip.saveWav(path));std::printf("Capture brute avant correction DC/gain : %s\n",path);
    const auto& best=levels[manual_audio::Clip::strongest(levels)];
    ASSERT_GT(best.peak,0.001f)<<"Capture silencieuse : verifier le pilote, le canal micro et son gain dans le panneau ASIO.";
    ASSERT_GT(best.rms,0.0001)<<"Energie inferieure a -80 dBFS : parler pendant la capture et verifier l'entree selectionnee.";
    manual_audio::Replay replay(clip,playback.sampleRate,levels);
    std::printf("Relecture immediate du canal %zu sur toutes les sorties, gain x%.2f (%u -> %u Hz).\n",
        replay.sourceChannel+1,replay.gain,capture.sampleRate,playback.sampleRate);
    ASIO player;ASSERT_TRUE(player.setProcessFunction(+[](void* user,const AudioProcessContext& context) noexcept {
        static_cast<manual_audio::Replay*>(user)->process(context);
    },&replay));
    ASSERT_TRUE(open(player,playback));ASSERT_TRUE(player.start());
    const bool complete=wait(player,[&]{return replay.invalidContext.load() || replay.played.load()>=replay.total();},std::chrono::seconds(12));
    const auto status=player.status();ASSERT_TRUE(player.stop());ASSERT_TRUE(player.close());
    std::printf("Relecture : %zu frames, %llu xruns, echec=%s\n",replay.played.load(),
        static_cast<unsigned long long>(status.xruns),status.failed?"true":"false");
    EXPECT_FALSE(replay.invalidContext.load());EXPECT_FALSE(status.failed);EXPECT_TRUE(complete);
}
}
