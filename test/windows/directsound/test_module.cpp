#include <gtest/gtest.h>
#include <cstdint>
#include <expected>
#include <type_traits>
import mka.audio.backend.directsound;
import ds;

#if defined(_WINDOWS_) || defined(_INC_DSOUND)
#error The public module consumer must not include Windows or DirectSound headers.
#endif

TEST(DirectSoundModule, ExportsThePortableCoreContractWithoutNativeHeaders) {
    using namespace mka::audio::core;
    static_assert(std::is_abstract_v<Backend>);
    static_assert(std::is_same_v<Result, std::expected<void, ErrorType>>);
    static_assert(std::is_same_v<ProcessFunction, void(*)(void*, const AudioProcessContext&) noexcept>);
    EXPECT_EQ(sizeof(SampleRate), 4u);
    EXPECT_EQ(sizeof(BufferSize), 4u);
    EXPECT_EQ(sizeof(decltype(Status{}.xruns)), 8u);
}
