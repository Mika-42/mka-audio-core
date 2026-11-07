#include <atomic>
#include <chrono>
#include <expected>
#include <thread>
#include <gtest/gtest.h>
import mka.audio.backend.abstract;
#include "utils/backend_test_config.hpp"
#include "utils/lifecycle_backend.hpp"

namespace {
using namespace lifecycle_test;
void process(void* user, const AudioProcessContext&) noexcept {
    ++*static_cast<std::atomic<unsigned>*>(user);
}
void expectError(Result result, ErrorType error) {
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), error);
}
class HookRollback : public testing::TestWithParam<Exception> {
protected:
    Counters counters;
    std::atomic<unsigned> calls{0};
    LifecycleBackend backend{counters};
    EndpointConfig config = [] { auto c = testEndpointConfig(); c.outputChannels = 1; c.bufferSize = 32; return c; }();
    void SetUp() override { ASSERT_TRUE(backend.setProcessFunction(process, &calls)); }
    void TearDown() override {
        backend.failAt(Hook::None);
        (void)backend.stop();
        (void)backend.close();
        EXPECT_EQ(counters.resources.load(), 0);
        EXPECT_EQ(counters.workers.load(), 0);
    }
};

TEST_P(HookRollback, OpenExceptionFreesPendingResourceAndAllowsRetry) {
    backend.failAt(Hook::Open, GetParam());
    expectError(backend.open(config), ErrorType::ConfigurationFailed);
    EXPECT_EQ(counters.resources.load(), 0);
    EXPECT_EQ(counters.workers.load(), 0);
    EXPECT_EQ(calls.load(), 0u);
    EXPECT_EQ(backend.storedUser(), &calls);
    expectError(backend.start(), ErrorType::InvalidState);
    backend.failAt(Hook::None);
    ASSERT_TRUE(backend.open(config));
    EXPECT_EQ(counters.resources.load(), 1);
    ASSERT_TRUE(backend.start());
    ASSERT_TRUE(waitUntil([&] { return calls.load() != 0; }));
}

TEST_P(HookRollback, StartExceptionJoinsPendingWorkerWithoutLosingOpenResource) {
    ASSERT_TRUE(backend.open(config));
    backend.failAt(Hook::Start, GetParam());
    expectError(backend.start(), ErrorType::ConfigurationFailed);
    EXPECT_EQ(counters.resources.load(), 1);
    EXPECT_EQ(counters.workers.load(), 0);
    EXPECT_EQ(calls.load(), 0u);
    EXPECT_EQ(backend.storedUser(), &calls);
    expectError(backend.stop(), ErrorType::InvalidState);
    expectError(backend.open(config), ErrorType::InvalidState);
    backend.failAt(Hook::None);
    ASSERT_TRUE(backend.start());
    ASSERT_TRUE(waitUntil([&] { return calls.load() != 0; }));
}

TEST_P(HookRollback, StopExceptionKeepsRunningAndRetryJoinsBeforeUserReplacement) {
    ASSERT_TRUE(backend.open(config));
    ASSERT_TRUE(backend.start());
    ASSERT_TRUE(waitUntil([&] { return calls.load() != 0; }));
    backend.failAt(Hook::Stop, GetParam());
    expectError(backend.stop(), ErrorType::ConfigurationFailed);
    EXPECT_EQ(counters.resources.load(), 1);
    EXPECT_EQ(counters.workers.load(), 1);
    expectError(backend.close(), ErrorType::InvalidState);
    expectError(backend.setProcessFunction(nullptr), ErrorType::InvalidState);
    const auto before = calls.load();
    ASSERT_TRUE(waitUntil([&] { return calls.load() > before; }));
    EXPECT_EQ(backend.storedUser(), &calls);
    backend.failAt(Hook::None);
    ASSERT_TRUE(backend.stop());
    const auto stopped = calls.load();
    ASSERT_TRUE(backend.setProcessFunction(nullptr));
    ASSERT_TRUE(backend.close());
    EXPECT_EQ(calls.load(), stopped);
}

TEST_P(HookRollback, CloseExceptionRetainsResourceInOpenStateForRestartAndRetry) {
    ASSERT_TRUE(backend.open(config));
    backend.failAt(Hook::Close, GetParam());
    expectError(backend.close(), ErrorType::ConfigurationFailed);
    EXPECT_EQ(counters.resources.load(), 1);
    EXPECT_EQ(counters.workers.load(), 0);
    expectError(backend.open(config), ErrorType::InvalidState);
    ASSERT_TRUE(backend.start());
    ASSERT_TRUE(waitUntil([&] { return calls.load() != 0; }));
    ASSERT_TRUE(backend.stop());
    backend.failAt(Hook::None);
    ASSERT_TRUE(backend.close());
    ASSERT_TRUE(backend.open(config));
}

TEST_P(HookRollback, DiscoveryExceptionReturnsEmptyWithoutChangingOpenState) {
    ASSERT_TRUE(backend.open(config));
    backend.failAt(Hook::Discover, GetParam());
    EXPECT_TRUE(backend.getEndPoints().empty());
    EXPECT_EQ(counters.resources.load(), 1);
    backend.failAt(Hook::None);
    const auto endpoints = backend.getEndPoints();
    ASSERT_EQ(endpoints.size(), 1);
    EXPECT_EQ(endpoints[0].id, "test:output");
    ASSERT_TRUE(backend.close());
}
INSTANTIATE_TEST_SUITE_P(AllExceptions, HookRollback,
    testing::Values(Exception::Allocation, Exception::Standard, Exception::NonStandard),
    [](const testing::TestParamInfo<Exception>& info) {
        switch (info.param) {
            case Exception::Allocation: return "BadAlloc";
            case Exception::Standard: return "Standard";
            case Exception::NonStandard: return "NonStandard";
        }
        return "Unknown";
    });
}
