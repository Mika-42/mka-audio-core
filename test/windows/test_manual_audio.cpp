#include <gtest/gtest.h>
#include "../utils/manual_audio.hpp"
#include "../../src/backend/windows/detail/wasapi_devices.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <format>
#include <print>
#include <string>
#include <thread>
import mka.audio.backend.wasapi;

namespace {
using namespace mka::audio::core;

std::expected<std::string, HRESULT> defaultId(bool input) {
    using namespace mka_wasapi_detail;
    ControlThread control(ComApi{});
    if (FAILED(control.initialization())) return std::unexpected(control.initialization());
    return control.invoke([&]() -> std::expected<std::string, HRESULT> {
        ComPtr<IMMDeviceEnumerator> enumerator;
        auto hr = createEnumerator(control.api(), enumerator.GetAddressOf());
        if (FAILED(hr)) return std::unexpected(hr);
        ComPtr<IMMDevice> device;
        hr = enumerator->GetDefaultAudioEndpoint(input ? eCapture : eRender, eConsole, device.GetAddressOf());
        if (FAILED(hr)) return std::unexpected(hr);
        wchar_t* raw = nullptr;
        hr = device->GetId(&raw);
        TaskPtr<wchar_t> id(raw);
        if (FAILED(hr)) return std::unexpected(hr);
        auto result = utf8(id.get());
        if (result.empty()) return std::unexpected(E_INVALIDARG);
        return result;
    });
}

const char* formatName(Format format) {
    switch (format) {
        case Format::Int16: return "S16";
        case Format::Int24: return "S24";
        case Format::Int32: return "S32";
        case Format::Float32: return "Float32";
        case Format::Float64: return "Float64";
    }
    return "unknown";
}

std::expected<EndpointConfig, std::string> configuration(bool input) {
    const auto* overrideId = std::getenv(input ? "MKA_TEST_WASAPI_INPUT_DEVICE" : "MKA_TEST_WASAPI_OUTPUT_DEVICE");
    std::string id;
    if (overrideId && *overrideId) id = overrideId;
    else {
        const auto selected = defaultId(input);
        if (!selected) return std::unexpected(std::format("Pas de peripherique Windows par defaut ({}) : HRESULT 0x{:08X}",
            input ? "microphone" : "sortie", static_cast<std::uint32_t>(selected.error())));
        id = *selected;
    }
    Wasapi discovery;
    for (const auto& endpoint : discovery.getEndPoints()) {
        if (endpoint.id != id) continue;
        const auto& caps = input ? endpoint.input : endpoint.output;
        if (!caps || caps->sampleRates.empty() || caps->formats.empty() || !caps->maxChannels || caps->maxChannels > 32)
            return std::unexpected("Capacites natives absentes ou incompatibles pour " + id);
        const auto rate = caps->sampleRates.front();
        if (rate < 8000 || rate > 384000) return std::unexpected("Frequence native hors limites du test");
        const auto buffer = caps->bufferSizes.empty() ? rate / 100 : caps->bufferSizes.front();
        EndpointConfig config{id, input ? Direction::Input : Direction::Output,
            input ? caps->maxChannels : 0, input ? 0 : caps->maxChannels, rate, caps->formats.front(), buffer};
        std::println("WASAPI {} : {}\n  ID={} | {} Hz | {} canaux | {} | buffer demande {} frames",
            input ? "capture" : "lecture", endpoint.name, id, rate, caps->maxChannels, formatName(config.format), buffer);
        return config;
    }
    return std::unexpected("Endpoint absent, indisponible ou mix natif inaccessible : " + id);
}

const char* errorName(ErrorType error) {
    switch (error) {
        case ErrorType::InvalidState: return "InvalidState";
        case ErrorType::EndpointUnavailable: return "EndpointUnavailable";
        case ErrorType::FormatNotSupported: return "FormatNotSupported";
        case ErrorType::ChannelsNotSupported: return "ChannelsNotSupported";
        case ErrorType::SampleRateNotSupported: return "SampleRateNotSupported";
        case ErrorType::BufferSizeNotSupported: return "BufferSizeNotSupported";
        case ErrorType::ConfigurationFailed: return "ConfigurationFailed";
    }
    return "Unknown";
}

bool open(Wasapi& backend, const EndpointConfig& config) {
    const auto result = backend.open(config);
    if (!result) std::println(stderr, "WASAPI open : {}. Verifier le peripherique selectionne, le service Audio Windows, "
        "le format partage et les autorisations Windows du microphone pour les applications de bureau.", errorName(result.error()));
    return result.has_value();
}

template<class Signal, class Predicate>
bool wait(Wasapi& backend, const Signal& signal, Predicate done, std::chrono::seconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!done()) {
        if (signal.invalidContext.load() || backend.status().failed || std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return true;
}
}

// Audible tests require an explicit opt-in. They are compiled and discovered
// in normal Windows builds, but carry hardware/manual labels instead of unit.
TEST(WasapiManualTest, DISABLED_TestPlaySineWave) {
    const auto config = configuration(false);
    ASSERT_TRUE(config) << config.error();
    manual_audio::Tone tone(config->sampleRate);
    Wasapi backend; // Destroy/join the backend before its callback state, even on ASSERT failure.
    ASSERT_TRUE(backend.setProcessFunction(+[](void* user, const AudioProcessContext& ctx) noexcept {
        static_cast<manual_audio::Tone*>(user)->process(ctx);
    }, &tone));
    ASSERT_TRUE(open(backend, *config));
    std::println("Lecture d'un sinus de 440 Hz pendant 3 secondes (amplitude 0.2).");
    std::fflush(stdout);
    ASSERT_TRUE(backend.start());
    const bool complete = wait(backend, tone, [&] { return tone.frames.load() >= tone.total; }, std::chrono::seconds(8));
    const auto status = backend.status();
    ASSERT_TRUE(backend.stop());
    ASSERT_TRUE(backend.close());
    std::println("Sinus : {} frames, {} xruns, echec={}", tone.frames.load(), status.xruns, status.failed);
    EXPECT_FALSE(tone.invalidContext.load());
    EXPECT_FALSE(status.failed);
    ASSERT_TRUE(complete) << "La lecture n'a pas progresse jusqu'au bout avant le delai limite.";
}

TEST(WasapiManualTest, DISABLED_TestRecordAndPlayback) {
    const auto input = configuration(true);
    ASSERT_TRUE(input) << input.error();
    const auto output = configuration(false);
    ASSERT_TRUE(output) << output.error();
    manual_audio::Clip clip(input->sampleRate, input->inputChannels, std::size_t(input->sampleRate) * 5);
    {
        Wasapi recorder;
        ASSERT_TRUE(recorder.setProcessFunction(+[](void* user, const AudioProcessContext& ctx) noexcept {
            static_cast<manual_audio::Clip*>(user)->record(ctx);
        }, &clip));
        ASSERT_TRUE(open(recorder, *input));
        std::println("Enregistrement pendant 5 secondes : parlez dans le microphone maintenant.");
        std::fflush(stdout);
        ASSERT_TRUE(recorder.start());
        const bool complete = wait(recorder, clip, [&] { return clip.recorded.load() >= clip.capacity; }, std::chrono::seconds(12));
        const auto status = recorder.status();
        ASSERT_TRUE(recorder.stop());
        ASSERT_TRUE(recorder.close());
        std::println("Capture : {} frames, {} xruns, echec={}", clip.recorded.load(), status.xruns, status.failed);
        ASSERT_FALSE(clip.invalidContext.load());
        ASSERT_FALSE(status.failed);
        ASSERT_TRUE(complete) << "La capture n'a pas progresse jusqu'au bout avant le delai limite.";
    }
    const auto levels = clip.levels();
    for (std::size_t ch = 0; ch < levels.size(); ++ch) {
        ASSERT_TRUE(levels[ch].finite) << "Echantillons non finis sur le canal " << ch + 1;
        std::println("Micro canal {} (sans DC) : crete={:.8f}, RMS={:.8f} ({:.1f} dBFS)",
            ch + 1, levels[ch].peak, levels[ch].rms, 20 * std::log10(std::max(levels[ch].rms, 1e-12)));
    }
    const char* path = std::getenv("MKA_TEST_WASAPI_CAPTURE_WAV");
    if (!path || !*path) path = "mka-wasapi-capture.wav";
    ASSERT_TRUE(clip.saveWav(path)) << "Impossible d'ecrire le WAV : " << path;
    std::println("Capture brute avant gain, correction DC et conversion de frequence : {}", path);
    const auto& best = levels[manual_audio::Clip::strongest(levels)];
    ASSERT_GT(best.peak, 0.001f) << "Capture silencieuse ou trop faible : verifier le micro, sa sourdine et son volume dans Windows.";
    ASSERT_GT(best.rms, 0.0001) << "Energie inferieure a -80 dBFS : parler pendant la capture et verifier le micro selectionne.";
    manual_audio::Replay replay(clip, output->sampleRate, levels);
    std::println("Relecture immediate du canal {} sur toutes les sorties, gain x{:.2f} ({} -> {} Hz).",
        replay.sourceChannel + 1, replay.gain, input->sampleRate, output->sampleRate);
    Wasapi player;
    ASSERT_TRUE(player.setProcessFunction(+[](void* user, const AudioProcessContext& ctx) noexcept {
        static_cast<manual_audio::Replay*>(user)->process(ctx);
    }, &replay));
    ASSERT_TRUE(open(player, *output));
    ASSERT_TRUE(player.start());
    const bool complete = wait(player, replay, [&] { return replay.played.load() >= replay.total(); }, std::chrono::seconds(12));
    const auto status = player.status();
    ASSERT_TRUE(player.stop());
    ASSERT_TRUE(player.close());
    std::println("Relecture : {} frames, {} xruns, echec={}", replay.played.load(), status.xruns, status.failed);
    EXPECT_FALSE(replay.invalidContext.load());
    EXPECT_FALSE(status.failed);
    ASSERT_TRUE(complete) << "La relecture n'a pas progresse jusqu'au bout avant le delai limite.";
}
