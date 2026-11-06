#include <expected>
#include <gtest/gtest.h>
import wasapi;
TEST(WasapiModule, OriginalModuleNameReexportsTheBackend) {
    mka::audio::core::Wasapi backend;
    EXPECT_TRUE(backend.setProcessFunction(nullptr));
}
