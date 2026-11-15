#include <gtest/gtest.h>
#include <array>
#include <cstdint>
#include <expected>
#include <limits>
#include <tuple>
#include "fake_native.hpp"
#include "../../../src/backend/windows/detail/directsound_capabilities.hpp"
import mka.audio.backend.directsound;

namespace {
using namespace mka_ds_detail;
using directsound_test::Native;
Request request(Flow flow = Flow::output) { return {{}, flow, flow == Flow::input ? 2u : 0u, flow == Flow::output ? 2u : 0u, 48000, 256, Sample::i16}; }
void clean(const Native& native) {
    EXPECT_EQ(native.live, 0u); EXPECT_EQ(native.creations, native.releases); EXPECT_FALSE(native.wrongThread);
    EXPECT_EQ(native.initialized, native.uninitialized); EXPECT_EQ(native.plays, 0u); EXPECT_EQ(native.starts, 0u);
}
class DirectSoundCapabilities : public ::testing::TestWithParam<Flow> {};

TEST(DirectSoundWave, BuildsIndependentPcmAndFloatOraclesWithoutChangingRequestedFrames) {
    const std::tuple<Sample, WORD, WORD, WORD> cases[]{
        {Sample::i16, WAVE_FORMAT_PCM, 16, 4}, {Sample::i24, WAVE_FORMAT_PCM, 24, 6},
        {Sample::i32, WAVE_FORMAT_PCM, 32, 8}, {Sample::f32, WAVE_FORMAT_IEEE_FLOAT, 32, 8},
        {Sample::f64, WAVE_FORMAT_IEEE_FLOAT, 64, 16}};
    for (const auto& [sample, tag, bits, align] : cases) {
        auto cfg = request(); cfg.sample = sample; const auto spec = waveSpec(cfg); ASSERT_TRUE(spec);
        EXPECT_EQ(spec->wave.wFormatTag, tag); EXPECT_EQ(spec->wave.wBitsPerSample, bits); EXPECT_EQ(spec->wave.nChannels, 2);
        EXPECT_EQ(spec->wave.nBlockAlign, align); EXPECT_EQ(spec->wave.nAvgBytesPerSec, DWORD(48000 * align));
        EXPECT_EQ(spec->periodBytes, DWORD(256 * align)); EXPECT_EQ(spec->totalBytes, DWORD(2048 * align));
        EXPECT_EQ(spec->frames, 256u); EXPECT_EQ(spec->wave.nSamplesPerSec, 48000u); EXPECT_EQ(spec->wave.cbSize, 0);
    }
}

TEST(DirectSoundWave, RejectsInvalidShapesAndOverflowsBeforeNativeCalls) {
    auto cfg = request(); cfg.flow = Flow::duplex; EXPECT_EQ(waveSpec(cfg).error(), Failure::configuration);
    for (const auto channels : {0u, 3u, std::numeric_limits<std::uint32_t>::max()}) {
        cfg = request(); cfg.outputs = channels; EXPECT_EQ(waveSpec(cfg).error(), Failure::channels);
    }
    cfg = request(); cfg.inputs = 1; EXPECT_EQ(waveSpec(cfg).error(), Failure::channels);
    cfg = request(Flow::input); cfg.outputs = 1; EXPECT_EQ(waveSpec(cfg).error(), Failure::channels);
    cfg = request(); cfg.sample = static_cast<Sample>(99); EXPECT_EQ(waveSpec(cfg).error(), Failure::format);
    cfg = request(); cfg.rate = 0; EXPECT_EQ(waveSpec(cfg).error(), Failure::rate);
    cfg = request(); cfg.rate = std::numeric_limits<std::uint32_t>::max(); EXPECT_EQ(waveSpec(cfg).error(), Failure::rate);
    cfg = request(); cfg.frames = 0; EXPECT_EQ(waveSpec(cfg).error(), Failure::buffer);
    cfg = request(); cfg.frames = std::numeric_limits<std::uint32_t>::max(); EXPECT_EQ(waveSpec(cfg).error(), Failure::buffer);
}

TEST_P(DirectSoundCapabilities, ListsOnlyNativelyVerifiedTuplesAndNeverStartsAudio) {
    Native mock;
    const auto caps = probeCapabilities({GetParam(), std::nullopt}); ASSERT_TRUE(caps);
    EXPECT_EQ(caps->minimumChannels, 2u); EXPECT_EQ(caps->maximumChannels, 2u);
    EXPECT_EQ(caps->rates, (std::vector<std::uint32_t>{48000})); EXPECT_EQ(caps->formats, (std::vector<Sample>{Sample::i16}));
    EXPECT_EQ(caps->buffers, (std::vector<std::uint32_t>{128, 256, 512}));
    EXPECT_GT(mock.attempts, mock.buffers); EXPECT_GT(mock.buffers, 0u);
    EXPECT_EQ(mock.initialized, 1u);
    EXPECT_EQ(mock.apartmentFlags, DWORD(COINIT_MULTITHREADED));
    if (GetParam() == Flow::output) { EXPECT_TRUE(mock.validWindow); EXPECT_FALSE(IsWindow(mock.observedWindow)); }
    clean(mock);
}

TEST_P(DirectSoundCapabilities, BroadGetCapsCannotProveUnsupportedFormatsOrRates) {
    Native mock; mock.rejectAll = true;
    EXPECT_FALSE(probeCapabilities({GetParam(), std::nullopt}));
    EXPECT_EQ(mock.capsCalls, 1u); EXPECT_GT(mock.attempts, 0u); EXPECT_EQ(mock.buffers, 0u); clean(mock);
}

TEST_P(DirectSoundCapabilities, NativeCapsFailureOrMalformedStructureNeverAdvertisesCandidates) {
    Native mock; mock.capsResult = DSERR_NODRIVER;
    EXPECT_FALSE(probeCapabilities({GetParam(), std::nullopt})); EXPECT_EQ(mock.attempts, 0u); clean(mock);
    mock.capsResult = DS_OK; mock.malformedCaps = true;
    EXPECT_FALSE(probeCapabilities({GetParam(), std::nullopt})); EXPECT_EQ(mock.attempts, 0u); clean(mock);
}

TEST_P(DirectSoundCapabilities, AllSupportedCoreFormatsAndChannelsAreDiscoveredFromActualBufferCreation) {
    Native mock; mock.allowAll = true;
    const auto caps = probeCapabilities({GetParam(), std::nullopt}); ASSERT_TRUE(caps);
    EXPECT_EQ(caps->minimumChannels, 1u); EXPECT_EQ(caps->maximumChannels, 2u);
    EXPECT_EQ(caps->formats, (std::vector<Sample>{Sample::i16, Sample::i24, Sample::i32, Sample::f32, Sample::f64}));
    EXPECT_EQ(caps->rates, (std::vector<std::uint32_t>{44100, 48000, 96000})); clean(mock);
}

TEST_P(DirectSoundCapabilities, FailedCreationWithAPartialInterfaceStillReleasesIt) {
    Native mock; mock.factoryResult = DSERR_NODRIVER; mock.partialFactory = true;
    EXPECT_FALSE(probeCapabilities({GetParam(), std::nullopt})); EXPECT_EQ(mock.creations, 1u); clean(mock);
}

TEST_P(DirectSoundCapabilities, SuccessWithANullNativeInterfaceIsRejected) {
    Native mock; mock.nullFactorySuccess = true;
    EXPECT_FALSE(probeCapabilities({GetParam(), std::nullopt})); EXPECT_EQ(mock.creations, 0u); clean(mock);
}

TEST_P(DirectSoundCapabilities, FailedBufferCreationReleasesPartialNativeBuffers) {
    Native mock; mock.rejectAll = true; mock.partialBuffer = true;
    EXPECT_FALSE(probeCapabilities({GetParam(), std::nullopt})); EXPECT_GT(mock.buffers, 0u); clean(mock);
}

TEST_P(DirectSoundCapabilities, MissingFactoryCannotExposeAnyCapabilities) {
    Native mock; mock.api.createOutput = nullptr; mock.api.createInput = nullptr;
    EXPECT_FALSE(probeCapabilities({GetParam(), std::nullopt})); EXPECT_EQ(mock.creations, 0u); clean(mock);
}

TEST_P(DirectSoundCapabilities, ExactDeviceIdentityAndOwnerThreadArePreserved) {
    Native mock;
    const GUID guid{0x12345678, 0xabcd, 0xef01, {1, 2, 3, 4, 5, 6, 7, 8}};
    {
        NativeDevice device; EXPECT_NE(device.thread(), GetCurrentThreadId());
        EXPECT_EQ(device.initialize({GetParam(), guid}), Failure::none);
        ASSERT_TRUE(mock.requestedGuid); EXPECT_TRUE(IsEqualGUID(*mock.requestedGuid, guid));
        EXPECT_TRUE(device.supports(request(GetParam())));
        EXPECT_EQ(device.initialize({GetParam(), std::nullopt}), Failure::configuration);
    }
    clean(mock);
}

TEST_P(DirectSoundCapabilities, InvalidRequestsAndWrongDirectionNeverCreateABuffer) {
    Native mock;
    {
        NativeDevice device; EXPECT_FALSE(device.capabilitiesValid()); EXPECT_FALSE(device.supports(request(GetParam())));
        ASSERT_EQ(device.initialize({GetParam(), std::nullopt}), Failure::none);
        auto cfg = request(GetParam()); if (cfg.flow == Flow::output) cfg.outputs = 3; else cfg.inputs = 3;
        EXPECT_FALSE(device.supports(cfg)); EXPECT_EQ(mock.attempts, 0u);
        EXPECT_FALSE(device.supports(request(GetParam() == Flow::input ? Flow::output : Flow::input)));
        EXPECT_EQ(mock.attempts, 0u);
    }
    clean(mock);
}

TEST_P(DirectSoundCapabilities, PcmFormatSizeWithoutCbSizeIsAcceptedAndBuffersAreReleased) {
    Native mock; mock.formatBytes = sizeof(PCMWAVEFORMAT);
    EXPECT_TRUE(probeCapabilities({GetParam(), std::nullopt})); clean(mock);
}

TEST(DirectSoundCapabilitiesControl, CooperativeFailureReleasesTheDeviceBeforeDestroyingTheWindow) {
    Native mock; mock.cooperativeResult = DSERR_INVALIDCALL;
    EXPECT_FALSE(probeCapabilities({Flow::output, std::nullopt}));
    EXPECT_TRUE(mock.validWindow); EXPECT_FALSE(IsWindow(mock.observedWindow)); EXPECT_EQ(mock.creations, 1u); clean(mock);
}

TEST(DirectSoundCapabilitiesControl, ZeroCaptureChannelsAreNotReplacedWithStereo) {
    Native mock; mock.captureChannels = 0;
    EXPECT_FALSE(probeCapabilities({Flow::input, std::nullopt})); EXPECT_EQ(mock.attempts, 0u); clean(mock);
}

HRESULT WINAPI failApartment(LPVOID, DWORD) { return E_FAIL; }
HWND WINAPI failWindow(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY); return nullptr;
}
TEST(DirectSoundCapabilitiesControl, ApartmentFailureDoesNotCreateANativeDeviceOrUnbalanceCom) {
    Native mock; mock.api.apartment.initialize = &failApartment;
    EXPECT_FALSE(probeCapabilities({Flow::output, std::nullopt})); EXPECT_EQ(mock.outputCreates, 0u); clean(mock);
}

TEST(DirectSoundCapabilitiesControl, WindowFailureBalancesTheApartmentAndNeverCreatesAnAudioDevice) {
    Native mock; mock.api.windows.create = &failWindow;
    EXPECT_FALSE(probeCapabilities({Flow::output, std::nullopt})); EXPECT_EQ(mock.outputCreates, 0u); clean(mock);
}

HRESULT WINAPI oneOutput(LPDSENUMCALLBACKW callback, LPVOID context) {
    GUID guid{0x12345678, 0xabcd, 0xef01, {1, 2, 3, 4, 5, 6, 7, 8}};
    callback(&guid, L"Verified output", L"fake.dll", context); return DS_OK;
}
HRESULT WINAPI noInputs(LPDSENUMCALLBACKW, LPVOID) { return DS_OK; }
struct MetadataScope {
    DiscoveryApi api{&oneOutput, &noInputs};
    MetadataScope() { testDiscoveryOverride = &api; }
    ~MetadataScope() { testDiscoveryOverride = nullptr; }
};
TEST(DirectSoundCapabilitiesControl, PublicDiscoveryCombinesNativeMetadataAndVerifiedCapabilities) {
    Native mock; MetadataScope metadata; mka::audio::core::DirectSound backend;
    const auto endpoints = backend.getEndPoints(); ASSERT_EQ(endpoints.size(), 1u); ASSERT_TRUE(endpoints[0].output);
    EXPECT_EQ(endpoints[0].id, "directsound:output:{12345678-abcd-ef01-0102-030405060708}");
    EXPECT_EQ(endpoints[0].output->minChannels, 2u); EXPECT_EQ(endpoints[0].output->maxChannels, 2u);
    EXPECT_EQ(endpoints[0].output->sampleRates, (std::vector<std::uint32_t>{48000}));
    EXPECT_EQ(endpoints[0].output->formats, (std::vector<mka::audio::core::Format>{mka::audio::core::Format::Int16}));
    clean(mock);
}
TEST(DirectSoundCapabilitiesControl, FailedProbeKeepsIdentityWithNoInventedCapabilities) {
    Native mock; MetadataScope metadata; mock.factoryResult = DSERR_NODRIVER;
    mka::audio::core::DirectSound backend; const auto endpoints = backend.getEndPoints(); ASSERT_EQ(endpoints.size(), 1u);
    EXPECT_EQ(endpoints[0].name, "Verified output"); ASSERT_TRUE(endpoints[0].output);
    EXPECT_EQ(endpoints[0].output->maxChannels, 0u); EXPECT_TRUE(endpoints[0].output->formats.empty());
    EXPECT_TRUE(endpoints[0].output->sampleRates.empty()); clean(mock);
}

enum class Mismatch { rate, channels, bits, tag, align, average, extension, shortFormat, oversizedFormat, bufferSize, formatError, bufferCapsError };
class DirectSoundTupleValidation : public ::testing::TestWithParam<std::tuple<Flow, Mismatch>> {};
TEST_P(DirectSoundTupleValidation, RejectsAnyNativeSubstitutionOrReadbackFailureAndReleasesTheBuffer) {
    Native mock; const auto [flow, mismatch] = GetParam();
    switch (mismatch) {
        case Mismatch::rate: mock.substituteRate = true; break;
        case Mismatch::channels: mock.substituteChannels = true; break;
        case Mismatch::bits: mock.substituteBits = true; break;
        case Mismatch::tag: mock.substituteTag = true; break;
        case Mismatch::align: mock.substituteAlign = true; break;
        case Mismatch::average: mock.substituteAverage = true; break;
        case Mismatch::extension: mock.extension = true; break;
        case Mismatch::shortFormat: mock.formatBytes = 15; break;
        case Mismatch::oversizedFormat: mock.formatBytes = 40; break;
        case Mismatch::bufferSize: mock.reportedBufferDelta = 1; break;
        case Mismatch::formatError: mock.formatResult = DSERR_BADFORMAT; break;
        case Mismatch::bufferCapsError: mock.bufferCapsResult = E_FAIL; break;
    }
    { NativeDevice device; ASSERT_EQ(device.initialize({flow, std::nullopt}), Failure::none);
      EXPECT_FALSE(device.supports(request(flow))); EXPECT_EQ(mock.buffers, 1u); }
    clean(mock);
}
INSTANTIATE_TEST_SUITE_P(Directions, DirectSoundCapabilities, ::testing::Values(Flow::input, Flow::output));
INSTANTIATE_TEST_SUITE_P(DirectionsAndFailures, DirectSoundTupleValidation,
    ::testing::Combine(::testing::Values(Flow::input, Flow::output), ::testing::Values(Mismatch::rate, Mismatch::channels,
        Mismatch::bits, Mismatch::tag, Mismatch::align, Mismatch::average, Mismatch::extension,
        Mismatch::shortFormat, Mismatch::oversizedFormat, Mismatch::bufferSize,
        Mismatch::formatError, Mismatch::bufferCapsError)));
}
