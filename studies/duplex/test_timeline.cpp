#include <limits>
#include <gtest/gtest.h>
#include "timeline.hpp"
using namespace mka_study;
TEST(DuplexTimeline, ExtrapolatesDifferentCallbackTimesBeforeComparingFrames) {
    auto delta=frameOffset({1'000'000'000,48000,1,48000},{1'010'000'000,48480,1,48000});
    ASSERT_TRUE(delta);EXPECT_EQ(*delta,0);
    delta=frameOffset({1'000'000'000,48000,1,48000},{1'010'000'000,48497,1,48000});
    ASSERT_TRUE(delta);EXPECT_EQ(*delta,17);
}
TEST(DuplexTimeline, RejectsUnknownRatesDiscontinuityAndUnrepresentableCounters) {
    const Stamp valid{1'000'000'000,48000,1,48000};
    for(auto bad:{Stamp{},Stamp{1'000'000'000,48000,0,48000},Stamp{1'000'000'000,48000,1,44100},
        Stamp{5'000'000'000,48000,1,48000},Stamp{1'000'000'000,UINT64_MAX,1,48000}})
        EXPECT_FALSE(frameOffset(valid,bad));
}
TEST(DuplexSignal, SilenceDoesNotPassAndChannelOrderContinuityAndNanAreChecked) {
    SignalWitness witness;for(unsigned i=0;i<100;++i)witness.sample(0,0);EXPECT_EQ(witness.matches,0u);
    for(unsigned i=0;i<513;++i)witness.sample(marker(0,i),marker(1,i));
    EXPECT_EQ(witness.matches,513u);EXPECT_EQ(witness.errors,0u);
    witness.sample(marker(1,513),marker(0,513));witness.sample(std::numeric_limits<float>::quiet_NaN(),0);
    EXPECT_EQ(witness.errors,2u);
}
