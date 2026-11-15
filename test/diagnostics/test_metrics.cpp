#include <gtest/gtest.h>
#include "metrics.hpp"
TEST(AudioMetrics, ServicePercentilesAndBoundedLossAccounting) {
    mka_audit::Recorder samples(3);
    samples.record(1000,1100,128);samples.record(2000,2500,256);samples.record(3000,3200,64);
    samples.record(4000,4100,128);
    const auto summary=mka_audit::serviceTime(samples);
    EXPECT_EQ(summary.count,3u);EXPECT_DOUBLE_EQ(summary.p50,200);EXPECT_DOUBLE_EQ(summary.p99,500);
    EXPECT_EQ(samples.dropped,1u);EXPECT_EQ(samples.calls.load(),4u);EXPECT_EQ(samples.frames.load(),576u);
}
TEST(AudioMetrics, EmptyAndInvalidTimestampsDoNotInventMeasurements) {
    mka_audit::Recorder samples(2);EXPECT_EQ(mka_audit::serviceTime(samples).count,0u);
    samples.record(10,9,128);EXPECT_EQ(mka_audit::serviceTime(samples).count,0u);
}

TEST(AudioMetrics, JitterUsesPreviousFrameCountAndNeverUnderflowsUnsignedTimestamps) {
    mka_audit::Recorder samples(4);
    samples.record(0,100,48);samples.record(1000000,1000100,96);
    samples.record(4000000,4000100,48);samples.record(4500000,4500100,48);
    const auto jitter=mka_audit::jitter(samples,48000);
    EXPECT_EQ(jitter.count,3u);EXPECT_DOUBLE_EQ(jitter.mean,500000);
    EXPECT_DOUBLE_EQ(jitter.p99,1000000);EXPECT_DOUBLE_EQ(jitter.max,1000000);
    EXPECT_EQ(mka_audit::jitter(samples,0).count,0u);
}
