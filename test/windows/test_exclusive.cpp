#include <gtest/gtest.h>
#include "fake_devices.hpp"
#include "../../src/backend/windows/detail/wasapi_exclusive.hpp"
using namespace wasapi_test;
TEST_F(DevicesFixture, ExclusiveAssessmentQueriesExactFormatsWithoutReservingOrStartingTheDevice) {
    auto d=add(L"output",eRender);d->client->exclusiveFormatsMask=(1u<<static_cast<unsigned>(SampleKind::S16))|(1u<<static_cast<unsigned>(SampleKind::S24Msb32));
    ControlThread control(api());const auto reports=assessExclusive(control);ASSERT_EQ(reports.size(),1u);
    const auto& report=reports[0];EXPECT_EQ(report.sharedMixSupport,S_OK);EXPECT_EQ(report.acquisition,S_OK);
    EXPECT_EQ(report.minimumPeriod,30000);ASSERT_FALSE(report.candidates.empty());
    bool foundPcm=false,foundFloatRejection=false;
    for(const auto& c:report.candidates){
        if(c.kind==SampleKind::S16 && c.rate==48000 && c.channels==2){foundPcm=true;EXPECT_EQ(c.support,S_OK);EXPECT_EQ(c.minimumPeriodFrames,144u);}
        if(c.kind==SampleKind::F32){foundFloatRejection=true;EXPECT_EQ(c.support,AUDCLNT_E_UNSUPPORTED_FORMAT);}
    }
    EXPECT_TRUE(foundPcm);EXPECT_TRUE(foundFloatRejection);EXPECT_TRUE(d->client->exclusiveClosestWasNull);
    EXPECT_EQ(d->client->initializeCalls,0u);EXPECT_EQ(d->client->starts,0u);EXPECT_EQ(d->client->eventCalls,0u);d.Reset();
}
TEST_F(DevicesFixture, FailedExclusiveQueriesAndUnknownPeriodsRemainExplicitInTheReport) {
    auto d=add(L"output",eRender);d->client->exclusiveResult=E_ACCESSDENIED;d->client->periodResult=E_FAIL;
    ControlThread control(api());const auto reports=assessExclusive(control);ASSERT_EQ(reports.size(),1u);
    EXPECT_EQ(reports[0].periodQuery,E_FAIL);
    for(const auto& c:reports[0].candidates){EXPECT_EQ(c.support,E_ACCESSDENIED);EXPECT_FALSE(c.minimumPeriodFrames);}
    EXPECT_EQ(d->client->initializeCalls,0u);d.Reset();
}
TEST(WasapiHardware, ReadOnlyExclusiveAssessmentOrExplicitUnavailableHardwareSkip) {
    ControlThread control;const auto reports=assessExclusive(control);
    if(reports.empty())GTEST_SKIP()<<"No active WASAPI endpoint/audio service for a native exclusive capability assessment";
    unsigned supported=0,unsupported=0,errors=0,checked=0;
    for(const auto& report:reports)for(const auto& c:report.candidates){++checked;if(c.support==S_OK)++supported;else if(c.support==AUDCLNT_E_UNSUPPORTED_FORMAT)++unsupported;else ++errors;}
    if(!checked)GTEST_SKIP()<<"Endpoints became unavailable before the read-only exclusive queries";
    RecordProperty("exclusive_candidates",checked);RecordProperty("exclusive_supported",supported);
    RecordProperty("exclusive_unsupported",unsupported);RecordProperty("exclusive_query_errors",errors);
    // Support is a measurement, not a requirement and not an exclusive stream test.
    EXPECT_GT(checked,0u);
}
