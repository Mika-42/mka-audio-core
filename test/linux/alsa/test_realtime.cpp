#include <gtest/gtest.h>
#include <array>
#include <cerrno>
#include "../../../src/backend/linux/detail/realtime.hpp"
namespace {
int allow=0,attempts=0,applied=0,maximum=99;
bool verify=true;
}
extern "C" int __wrap_sched_get_priority_min(int){return 1;}
extern "C" int __wrap_sched_get_priority_max(int){return maximum;}
extern "C" int __wrap_pthread_setschedparam(pthread_t,int policy,const sched_param* parameters){
    ++attempts;if(policy!=SCHED_FIFO || parameters->sched_priority!=allow)return EPERM;
    applied=allow;return 0;
}
extern "C" int __wrap_pthread_getschedparam(pthread_t,int* policy,sched_param* parameters){
    *policy=verify?SCHED_FIFO:SCHED_OTHER;parameters->sched_priority=applied;return 0;
}
TEST(AlsaRealtime, PermissionDeniedIsBoundedAndDoesNotClaimRealtime) {
    allow=0;attempts=0;maximum=99;EXPECT_FALSE(mka_alsa_detail::requestRealtimePriority());EXPECT_EQ(attempts,5);
}
TEST(AlsaRealtime, LowerPermittedPriorityIsUsedAndVerified) {
    allow=10;attempts=0;maximum=99;verify=true;EXPECT_TRUE(mka_alsa_detail::requestRealtimePriority());EXPECT_EQ(attempts,4);
    verify=false;attempts=0;EXPECT_FALSE(mka_alsa_detail::requestRealtimePriority());verify=true;
}
TEST(AlsaRealtime, InvalidRangesAndDuplicateClampedPrioritiesAreBounded) {
    maximum=-1;attempts=0;EXPECT_FALSE(mka_alsa_detail::requestRealtimePriority());EXPECT_EQ(attempts,0);
    maximum=1;allow=0;EXPECT_FALSE(mka_alsa_detail::requestRealtimePriority());EXPECT_EQ(attempts,1);maximum=99;
}
