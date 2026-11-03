#include <atomic>
#include <chrono>
#include <expected>
#include <memory>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>
#include <gtest/gtest.h>
import mka.audio.backend.abstract;
#include "utils/backend_test_config.hpp"

namespace {
using namespace mka::audio::core;
using namespace std::chrono_literals;
// A resource-owning native hook: rollback belongs to the hook, not the base.
class TransactionBackend : public Backend {
public:
    int failOpen = 0;
    int failStart = 0;
    int failStop = 0;
    std::atomic<int> liveWorkers{0};
    std::jthread worker;
    bool reserved = false;
    unsigned openCalls = 0;
    void failStream() noexcept { notifyXRun(); notifyFailed(); }
    void reportRealtime(bool yes) noexcept { notifyRealtime(yes); }
protected:
    std::vector<Endpoint> getEndPoints_() const override { return {}; }
    Result open_(const EndpointConfig&) override {
        ++openCalls;
        struct Reservation {
            bool& value;
            bool committed = false;
            explicit Reservation(bool& v) : value(v) { value = true; }
            ~Reservation() { if (!committed) value = false; }
        } reservation(reserved);
        if (failOpen == 1) return std::unexpected{ErrorType::EndpointUnavailable};
        if (failOpen == 2) throw std::runtime_error("native acquisition");
        reservation.committed = true;
        return {};
    }
    Result start_() override {
        std::jthread pending([this](std::stop_token token) {
            ++liveWorkers;
            while (!token.stop_requested()) std::this_thread::yield();
            --liveWorkers;
        });
        if (failStart == 1) return std::unexpected{ErrorType::EndpointUnavailable};
        if (failStart == 2) throw std::runtime_error("native activation");
        worker = std::move(pending);
        return {};
    }
    Result stop_() override {
        if (failStop == 1) return std::unexpected{ErrorType::EndpointUnavailable};
        if (failStop == 2) throw std::runtime_error("native stop");
        worker.request_stop(); if (worker.joinable()) worker.join(); return {}; }
    Result close_() override { reserved = false; return {}; }
};

TEST(Sprint02Open, NativeErrorAndExceptionReleasePartialResourceAndAllowRetry) {
    for (int failure : {1, 2}) {
        TransactionBackend b;
        for (int attempt = 0; attempt < 20; ++attempt) {
            b.failOpen = failure;
            const auto result = b.open(testEndpointConfig());
            ASSERT_FALSE(result);
            EXPECT_EQ(result.error(), failure == 1 ? ErrorType::EndpointUnavailable : ErrorType::ConfigurationFailed);
            EXPECT_FALSE(b.reserved);
            EXPECT_EQ(b.start().error(), ErrorType::InvalidState);
            EXPECT_EQ(b.close().error(), ErrorType::InvalidState);
        }
        b.failOpen = 0;
        ASSERT_TRUE(b.open(testEndpointConfig()));
        EXPECT_TRUE(b.reserved);
        EXPECT_EQ(b.open(testEndpointConfig()).error(), ErrorType::InvalidState);
        ASSERT_TRUE(b.start());
        ASSERT_TRUE(b.stop());
        ASSERT_TRUE(b.close());
        EXPECT_FALSE(b.reserved);
    }
}
}

TEST(Sprint02Start, FailureJoinsPartialWorkerAndCanRetryOrClose) {
    for (int failure : {1, 2}) {
        TransactionBackend b;
        ASSERT_TRUE(b.open(testEndpointConfig()));
        b.failStart = failure;
        for (int attempt = 0; attempt < 20; ++attempt) {
            const auto result = b.start();
            ASSERT_FALSE(result);
            EXPECT_EQ(result.error(), failure == 1 ? ErrorType::EndpointUnavailable : ErrorType::ConfigurationFailed);
            EXPECT_EQ(b.liveWorkers.load(), 0);
            EXPECT_TRUE(b.reserved);
            EXPECT_EQ(b.stop().error(), ErrorType::InvalidState);
        }
        ASSERT_TRUE(b.close());
        ASSERT_TRUE(b.open(testEndpointConfig()));
        b.failStart = 0;
        ASSERT_TRUE(b.start());
        ASSERT_TRUE(b.stop());
        EXPECT_EQ(b.liveWorkers.load(), 0);
        ASSERT_TRUE(b.close());
    }
}

TEST(Sprint02Stop, FailureRetainsRunningAndResourcesUntilSuccessfulRetry) {
    for (int failure : {1, 2}) {
        TransactionBackend b;
        ASSERT_TRUE(b.open(testEndpointConfig()));
        ASSERT_TRUE(b.start());
        b.failStop = failure;
        const auto result = b.stop();
        ASSERT_FALSE(result);
        EXPECT_EQ(result.error(), failure == 1 ? ErrorType::EndpointUnavailable : ErrorType::ConfigurationFailed);
        EXPECT_TRUE(b.reserved);
        EXPECT_EQ(b.close().error(), ErrorType::InvalidState);
        EXPECT_EQ(b.start().error(), ErrorType::InvalidState);
        EXPECT_EQ(b.setProcessFunction(nullptr).error(), ErrorType::InvalidState);
        b.failStop = 0;
        ASSERT_TRUE(b.stop());
        EXPECT_EQ(b.liveWorkers.load(), 0);
        ASSERT_TRUE(b.close());
        ASSERT_TRUE(b.open(testEndpointConfig()));
        ASSERT_TRUE(b.start());
        ASSERT_TRUE(b.stop());
        ASSERT_TRUE(b.close());
    }
}

TEST(Sprint02Failure, StopThenCloseAfterFailurePreservesDiagnosticsAndReopens) {
    TransactionBackend b;
    for (int cycle = 0; cycle < 20; ++cycle) {
        ASSERT_TRUE(b.open(testEndpointConfig()));
        ASSERT_TRUE(b.start());
        EXPECT_FALSE(b.status().failed);
        EXPECT_EQ(b.status().xruns, 0u);
        std::thread native([&] { b.failStream(); });
        native.join();
        EXPECT_TRUE(b.status().failed);
        EXPECT_EQ(b.close().error(), ErrorType::InvalidState);
        ASSERT_TRUE(b.stop());
        EXPECT_EQ(b.liveWorkers.load(), 0);
        ASSERT_TRUE(b.close());
        EXPECT_FALSE(b.reserved);
        EXPECT_TRUE(b.status().failed);
        EXPECT_EQ(b.status().xruns, 1u);
    }
}

TEST(Sprint02Bounds, RejectsExtremeChannelsAndByteLengthsBeforeNativeAcquisition) {
    TransactionBackend b;
    auto cfg = testEndpointConfig();
    cfg.outputChannels = std::numeric_limits<std::uint32_t>::max();
    EXPECT_EQ(b.open(cfg).error(), ErrorType::ChannelsNotSupported);
    cfg = testEndpointConfig();
    cfg.bufferSize = std::numeric_limits<std::uint32_t>::max();
    EXPECT_EQ(b.open(cfg).error(), ErrorType::BufferSizeNotSupported);
    cfg.outputChannels = 1;
    cfg.bufferSize = std::numeric_limits<std::uint32_t>::max() / sizeof(float) + 1;
    EXPECT_EQ(b.open(cfg).error(), ErrorType::BufferSizeNotSupported);
    EXPECT_EQ(b.openCalls, 0u);
    EXPECT_FALSE(b.reserved);
    cfg.bufferSize = 1;
    ASSERT_TRUE(b.open(cfg));
    ASSERT_TRUE(b.close());
}
