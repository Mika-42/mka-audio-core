#include <gtest/gtest.h>
#include <expected>
#include "fake_driver.hpp"
#include "../../../src/backend/windows/detail/asio_host.hpp"
import mka.audio.backend.asio;

namespace {
using namespace asio_test;
using namespace mka::audio::core;
EndpointConfig driverConfig() { return {driverId, Direction::Output, 0, 2, 48000, Format::Float32, 256}; }

TEST(AsioDriver, CreationInitializationAndReleaseStayOnTheSameStaOwner) {
    DriverFixture fixture;
    { ASIO backend; ASSERT_TRUE(backend.open(driverConfig())); EXPECT_EQ(fixture.live, 1u); ASSERT_TRUE(backend.close()); }
    EXPECT_EQ(fixture.creates, 1u); EXPECT_EQ(fixture.inits, 1u); EXPECT_EQ(fixture.destroyed, 1u);
    EXPECT_NE(fixture.creationThread, GetCurrentThreadId());
    EXPECT_EQ(fixture.creationThread, fixture.initThread);
    EXPECT_EQ(fixture.initThread, fixture.releaseThread);
    EXPECT_EQ(fixture.releaseThread, fixture.uninitializeThread);
    EXPECT_TRUE(fixture.sta); EXPECT_TRUE(fixture.validWindow); EXPECT_FALSE(IsWindow(fixture.lastWindow));
}

TEST(AsioDriver, DriverInfoUsesSdkVersionAndAnOwnedWindow) {
    DriverFixture fixture; void* state = nullptr;
    ASSERT_EQ(openDriver(driverId, &state), ASE_OK);
    auto& driver = *static_cast<Driver*>(state);
    EXPECT_EQ(driver.information().asioVersion, 2);
    EXPECT_EQ(driver.information().driverVersion, 123);
    EXPECT_STREQ(driver.information().name, "Test ASIO");
    EXPECT_EQ(driver.information().sysRef, driver.window());
    ASSERT_EQ(closeDriver(&state), ASE_OK); EXPECT_EQ(state, nullptr);
}

TEST(AsioDriver, InvalidUnregisteredAndEmbeddedNullIdsNeverInstantiateADriver) {
    DriverFixture fixture; ASIO backend;
    for (const auto& id : {std::string("missing"), std::string("asio:{00000000-0000-0000-0000-000000000000}"),
         std::string(driverId) + std::string(1, '\0') + "tail"}) {
        auto cfg = driverConfig(); cfg.id = id;
        EXPECT_EQ(backend.open(cfg).error(), ErrorType::EndpointUnavailable);
    }
    fixture.registrations.clear();
    EXPECT_EQ(backend.open(driverConfig()).error(), ErrorType::EndpointUnavailable);
    EXPECT_EQ(fixture.creates, 0u); EXPECT_EQ(fixture.comAttempts, 0u);
}

TEST(AsioDriver, CreationErrorsAndNullInterfacesRollBackAndAllowRetry) {
    DriverFixture fixture; ASIO backend;
    fixture.creation = REGDB_E_CLASSNOTREG;
    EXPECT_EQ(backend.open(driverConfig()).error(), ErrorType::EndpointUnavailable);
    fixture.creation = E_OUTOFMEMORY;
    EXPECT_EQ(backend.open(driverConfig()).error(), ErrorType::ConfigurationFailed);
    fixture.creation = S_OK; fixture.nullInterface = true;
    EXPECT_EQ(backend.open(driverConfig()).error(), ErrorType::EndpointUnavailable);
    fixture.nullInterface = false;
    ASSERT_TRUE(backend.open(driverConfig())); ASSERT_TRUE(backend.close());
}

TEST(AsioDriver, APartialComFailureAndInitRefusalReleaseEveryInterface) {
    DriverFixture fixture; ASIO backend;
    fixture.creation = E_FAIL; fixture.partialFailure = true;
    EXPECT_EQ(backend.open(driverConfig()).error(), ErrorType::EndpointUnavailable);
    EXPECT_EQ(fixture.live, 0u); EXPECT_EQ(fixture.destroyed, 1u);
    fixture.creation = S_OK; fixture.partialFailure = false; fixture.acceptInit = false;
    EXPECT_EQ(backend.open(driverConfig()).error(), ErrorType::EndpointUnavailable);
    EXPECT_EQ(fixture.live, 0u); EXPECT_EQ(fixture.destroyed, 2u);
    fixture.acceptInit = true;
    ASSERT_TRUE(backend.open(driverConfig())); ASSERT_TRUE(backend.close());
    EXPECT_EQ(fixture.destroyed, 3u);
}

TEST(AsioDriver, TwoBackendInstancesCannotOwnDriversAtTheSameTime) {
    DriverFixture fixture; ASIO first, second;
    ASSERT_TRUE(first.open(driverConfig()));
    EXPECT_EQ(second.open(driverConfig()).error(), ErrorType::EndpointUnavailable);
    EXPECT_EQ(fixture.creates, 1u);
    ASSERT_TRUE(first.close());
    ASSERT_TRUE(second.open(driverConfig())); ASSERT_TRUE(second.close());
    EXPECT_EQ(fixture.creates, 2u);
}

TEST(AsioDriver, ComFailureDoesNotCreateInterfacesOrUninitializeAnUnacquiredApartment) {
    DriverFixture fixture; ASIO backend; fixture.apartment = RPC_E_CHANGED_MODE;
    EXPECT_EQ(backend.open(driverConfig()).error(), ErrorType::EndpointUnavailable);
    EXPECT_EQ(fixture.creates, 0u); EXPECT_EQ(fixture.uninitializes, 0u);
    fixture.apartment = S_FALSE;
    ASSERT_TRUE(backend.open(driverConfig())); ASSERT_TRUE(backend.close());
    EXPECT_EQ(fixture.comReferences, 1u); EXPECT_EQ(fixture.uninitializes, 1u);
}

TEST(AsioDriver, CrossThreadCloseAndDestructionReleaseOnTheOriginalOwner) {
    DriverFixture fixture;
    {
        ASIO backend; ASSERT_TRUE(backend.open(driverConfig()));
        std::thread closer([&] { EXPECT_TRUE(backend.close()); }); closer.join();
        EXPECT_EQ(fixture.releaseThread, fixture.initThread);
        ASSERT_TRUE(backend.open(driverConfig()));
    }
    EXPECT_EQ(fixture.destroyed, 2u); EXPECT_EQ(fixture.releaseThread, fixture.initThread);
}
}
