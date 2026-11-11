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

TEST(DirectSoundModule, PublicClassConstructsClosedWithoutInventingANativeStream) {
    using namespace mka::audio::core;
    static_assert(std::is_final_v<DirectSound> && std::is_base_of_v<Backend, DirectSound>);
    static_assert(!std::is_copy_constructible_v<DirectSound> && !std::is_move_constructible_v<DirectSound>);
    DirectSound backend;
    EXPECT_TRUE(backend.getEndPoints().empty());
    EXPECT_TRUE(backend.setProcessFunction(nullptr));
    EXPECT_EQ(backend.start().error(), ErrorType::InvalidState);
    EXPECT_EQ(backend.open({"missing", Direction::Output, 0, 2, 48000, Format::Float32, 256}).error(),
        ErrorType::EndpointUnavailable);
    EXPECT_EQ(backend.close().error(), ErrorType::InvalidState);
}
