#include <algorithm>
#include <array>
#include <gtest/gtest.h>
#include <mka/audio/build_config.hpp>
import mka.audio.backend.plateform;
using namespace mka::audio::core;
TEST(BackendAvailability, KnownIdentifiersAndActualLibraryBuildAreDistinct) {
    static_assert(knownBackends.size()==9);
    EXPECT_EQ(supportedBackends.size(),MKA_BUILD_ALSA+MKA_BUILD_PIPEWIRE+MKA_BUILD_JACK+MKA_BUILD_PULSEAUDIO+MKA_BUILD_COREAUDIO+MKA_BUILD_WASAPI);
    EXPECT_EQ(isBackendCompiled(AudioBackend::Alsa),bool(MKA_BUILD_ALSA));
    EXPECT_EQ(isBackendCompiled(AudioBackend::PipeWire),bool(MKA_BUILD_PIPEWIRE));
    EXPECT_EQ(isBackendCompiled(AudioBackend::Jack),bool(MKA_BUILD_JACK));
    EXPECT_EQ(isBackendCompiled(AudioBackend::PulseAudio),bool(MKA_BUILD_PULSEAUDIO));
    EXPECT_EQ(isBackendCompiled(AudioBackend::CoreAudio),bool(MKA_BUILD_COREAUDIO));
    EXPECT_EQ(isBackendCompiled(AudioBackend::Wasapi),bool(MKA_BUILD_WASAPI));
    for(auto backend:supportedBackends)EXPECT_TRUE(isBackendKnown(backend));
    EXPECT_FALSE(isBackendKnown(static_cast<AudioBackend>(-1)));
    EXPECT_FALSE(isBackendCompiled(static_cast<AudioBackend>(-1)));
}
