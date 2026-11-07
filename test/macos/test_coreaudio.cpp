#include <gtest/gtest.h>
#include "../../src/backend/macos/detail/native.hpp"
#include <type_traits>
import mka.audio.backend.coreaudio;

TEST(CoreAudioModule, PortableFacadeAndNativeFrameworkLink) {
    static_assert(std::is_base_of_v<mka::audio::core::Backend,mka::audio::core::CoreAudio>);
    static_assert(sizeof(AudioObjectID)==sizeof(std::uint32_t));
    mka::audio::core::CoreAudio backend;
    EXPECT_FALSE(backend.status().failed);
    const AudioObjectPropertyAddress address{kAudioObjectPropertyName,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
    EXPECT_FALSE(AudioObjectHasProperty(kAudioObjectUnknown,&address));
}
