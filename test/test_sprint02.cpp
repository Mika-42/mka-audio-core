#include <atomic>
#include <chrono>
#include <expected>
#include <memory>
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
    bool reserved = false;
    unsigned openCalls = 0;
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
    Result start_() override { return {}; }
    Result stop_() override { return {}; }
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
