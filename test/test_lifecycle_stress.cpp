#include <atomic>
#include <expected>
#include <memory>
#include <gtest/gtest.h>
import mka.audio.backend.abstract;
#include "utils/backend_test_config.hpp"
#include "utils/lifecycle_backend.hpp"

namespace {
using namespace lifecycle_test;
struct UserContext {
    std::atomic<unsigned> calls{0}, events{0}, badBuffers{0};
};
void process(void* user, const AudioProcessContext& context) noexcept {
    auto& probe = *static_cast<UserContext*>(user);
    if (context.frames != 32 || context.input.channels != nullptr || context.input.count != 0
        || context.output.count != 1 || context.output.channels == nullptr
        || context.output.channels[0] == nullptr) {
        ++probe.badBuffers;
        return;
    }
    for (std::uint32_t frame = 0; frame < context.frames; ++frame)
        context.output.channels[0][frame] = 0.25f;
    ++probe.calls;
}
void event(void* user, const Event& value) noexcept {
    if (value.type == EventType::XRun && value.xruns == 1)
        ++static_cast<UserContext*>(user)->events;
}
void expectInvalid(Result result) {
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), ErrorType::InvalidState);
}
}

TEST(BackendLifecycleStress, ThousandCyclesReleaseResourcesAndJoinBothWorkers) {
    Counters counters;
    // Keep user data alive even if an assertion aborts before stop().
    std::unique_ptr<UserContext> user;
    LifecycleBackend backend(counters);
    auto config = testEndpointConfig();
    config.outputChannels = 1;
    config.bufferSize = 32;
    for (int cycle = 0; cycle < 1000; ++cycle) {
        SCOPED_TRACE(cycle);
        user = std::make_unique<UserContext>();
        ASSERT_TRUE(backend.setProcessFunction(process, user.get()));
        ASSERT_TRUE(backend.setEventHandler(event, user.get()));
        ASSERT_TRUE(backend.open(config));
        ASSERT_EQ(counters.resources.load(), 1);
        ASSERT_TRUE(backend.start());
        ASSERT_TRUE(waitUntil([&] { return user->calls.load() > 0 && user->events.load() > 0; }));
        expectInvalid(backend.start());
        expectInvalid(backend.close());
        expectInvalid(backend.setProcessFunction(nullptr));
        ASSERT_TRUE(backend.stop());
        ASSERT_EQ(counters.workers.load(), 0);
        const auto calls = user->calls.load();
        const auto events = user->events.load();
        EXPECT_EQ(events, 1u);
        EXPECT_EQ(user->badBuffers.load(), 0u);
        ASSERT_TRUE(backend.close());
        EXPECT_EQ(user->calls.load(), calls);
        EXPECT_EQ(user->events.load(), events);
        EXPECT_EQ(counters.resources.load(), 0);
        // Free the old callback and event contexts before the next cycle. ASan
        // detects any late access; TSan checks the worker/dispatcher handoff.
        user.reset();
        expectInvalid(backend.stop());
        expectInvalid(backend.close());
    }
    EXPECT_EQ(counters.opens, 1000);
    EXPECT_EQ(counters.starts, 1000);
    EXPECT_EQ(counters.stops, 1000);
    EXPECT_EQ(counters.closes, 1000);
}

TEST(BackendLifecycleStress, ThousandInvalidClosedCallsNeverReachNativeHooks) {
    Counters counters;
    LifecycleBackend backend(counters);
    for (int cycle = 0; cycle < 1000; ++cycle) {
        expectInvalid(backend.start());
        expectInvalid(backend.stop());
        expectInvalid(backend.close());
    }
    EXPECT_EQ(counters.starts, 0);
    EXPECT_EQ(counters.stops, 0);
    EXPECT_EQ(counters.closes, 0);
    EXPECT_EQ(counters.resources.load(), 0);
    EXPECT_EQ(counters.workers.load(), 0);
}
