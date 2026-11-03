#include <expected>
#include <optional>
#include <type_traits>
#include <vector>
#include <gtest/gtest.h>

import mka.audio.backend.abstract;
#include "utils/backend_test_config.hpp"

namespace {
using namespace mka::audio::core;
enum class Operation { Open, Start, Stop, Close };

class ErrorBackend : public Backend {
public:
    Operation target = Operation::Open;
    std::optional<ErrorType> error;
    unsigned hookCalls = 0;
protected:
    Result hook(Operation op) {
        ++hookCalls;
        if (op == target && error) return std::unexpected{*error};
        return {};
    }
    std::vector<Endpoint> getEndPoints_() const override { return {}; }
    Result open_(const EndpointConfig&) override { return hook(Operation::Open); }
    Result start_() override { return hook(Operation::Start); }
    Result stop_() override { return hook(Operation::Stop); }
    Result close_() override { return hook(Operation::Close); }
};

Result invoke(ErrorBackend& backend, Operation op) {
    switch (op) {
        case Operation::Open: return backend.open(testEndpointConfig());
        case Operation::Start: return backend.start();
        case Operation::Stop: return backend.stop();
        case Operation::Close: return backend.close();
    }
    return std::unexpected{ErrorType::InvalidState};
}

class ResultErrorContractTest : public ::testing::TestWithParam<ErrorType> {};
TEST_P(ResultErrorContractTest, PreservesTheNativeErrorAndLifecycleStateForEveryHook) {
    constexpr Operation operations[]{Operation::Open, Operation::Start, Operation::Stop, Operation::Close};
    for (unsigned index = 0; index < 4; ++index) {
        ErrorBackend backend;
        for (unsigned setup = 0; setup < index; ++setup)
            ASSERT_TRUE(invoke(backend, operations[setup]));
        backend.target = operations[index];
        backend.error = GetParam();
        const auto result = invoke(backend, operations[index]);
        ASSERT_FALSE(result);
        EXPECT_EQ(result.error(), GetParam());
        backend.error.reset();
        ASSERT_TRUE(invoke(backend, operations[index])); // same state, operation can be retried
        for (unsigned cleanup = index + 1; cleanup < 4; ++cleanup)
            ASSERT_TRUE(invoke(backend, operations[cleanup]));
    }
}
INSTANTIATE_TEST_SUITE_P(AllErrorTypes, ResultErrorContractTest, ::testing::Values(
    ErrorType::InvalidState, ErrorType::EndpointUnavailable, ErrorType::FormatNotSupported,
    ErrorType::ChannelsNotSupported, ErrorType::SampleRateNotSupported,
    ErrorType::BufferSizeNotSupported, ErrorType::ConfigurationFailed));

TEST(ResultErrorContractTest, InvalidStateTakesPrecedenceWithoutReenteringTheNativeHook) {
    ErrorBackend backend;
    ASSERT_TRUE(backend.open(testEndpointConfig()));
    backend.error = ErrorType::EndpointUnavailable;
    const auto result = backend.open(testEndpointConfig());
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), ErrorType::InvalidState);
    EXPECT_EQ(backend.hookCalls, 1u);
    ASSERT_TRUE(backend.close());
}

static_assert(std::is_same_v<Result, std::expected<void, ErrorType>>);
static_assert(std::is_same_v<decltype(std::declval<Backend&>().open(std::declval<const EndpointConfig&>())), Result>);
static_assert(std::is_same_v<decltype(std::declval<Backend&>().start()), Result>);
static_assert(std::is_same_v<decltype(std::declval<Backend&>().stop()), Result>);
static_assert(std::is_same_v<decltype(std::declval<Backend&>().close()), Result>);
static_assert(std::is_same_v<decltype(std::declval<Backend&>().setProcessFunction(nullptr)), Result>);
static_assert(std::is_same_v<decltype(std::declval<Backend&>().setEventHandler(nullptr)), Result>);
}
