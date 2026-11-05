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
