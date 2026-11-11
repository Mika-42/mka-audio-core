#include <gtest/gtest.h>
#include <cstdint>
#include <type_traits>
#include <windows.h>
#include <mmreg.h>
#include <dsound.h>

TEST(DirectSoundSdk, LinksNativeSdkEntryPointsAndGuidLibraryWithoutCreatingADevice) {
    static_assert(sizeof(DWORD) == sizeof(std::uint32_t));
    static_assert(sizeof(HRESULT) == 4);
    static_assert(sizeof(GUID) == 16);
    static_assert(std::is_same_v<decltype(&DirectSoundEnumerateW), HRESULT(WINAPI*)(LPDSENUMCALLBACKW, LPVOID)>);
    // Volatile references keep the SDK link witness in optimized builds too.
    auto volatile enumerateOutput = &DirectSoundEnumerateW;
    auto volatile enumerateInput = &DirectSoundCaptureEnumerateW;
    auto volatile create = &DirectSoundCreate8;
    EXPECT_NE(enumerateOutput, nullptr);
    EXPECT_NE(enumerateInput, nullptr);
    EXPECT_NE(create, nullptr);
    const GUID zero{};
    EXPECT_FALSE(IsEqualGUID(IID_IDirectSound8, zero));
}
