#pragma once
#include <algorithm>
#include <pthread.h>
#include <sched.h>
namespace mka_alsa_detail {
inline bool requestRealtimePriority() noexcept {
    const int minimum=sched_get_priority_min(SCHED_FIFO),maximum=sched_get_priority_max(SCHED_FIFO);
    if(minimum<=0 || maximum<minimum)return false;
    int previous=-1;
    for(int candidate:{80,50,20,10,1}){
        const int wanted=std::clamp(candidate,minimum,maximum);
        if(wanted==previous)continue;
        previous=wanted;sched_param request{};request.sched_priority=wanted;
        if(pthread_setschedparam(pthread_self(),SCHED_FIFO,&request)!=0)continue;
        sched_param actual{};int policy=0;
        if(pthread_getschedparam(pthread_self(),&policy,&actual)==0
            && policy==SCHED_FIFO && actual.sched_priority==wanted)return true;
    }
    return false;
}
}
