#include <pulse/pulseaudio.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <expected>
#include <string>
#include <thread>
#include <gtest/gtest.h>
#include "timeline.hpp"
import mka.audio.backend.abstract;
namespace {
using namespace mka::audio::core;
using namespace mka_study;
class Pair {
    struct Lane {
        pa_stream* stream=nullptr;
        std::atomic<bool> enabled{false},failed{false};
        std::atomic<std::uint64_t> frames{0},matched{0};
        SignalWitness witness;
    } lanes_[2];
    pa_threaded_mainloop* loop_=nullptr;pa_context* context_=nullptr;bool running_=false;
    static void write(pa_stream* stream,std::size_t requested,void* data) noexcept {
        auto& lane=*static_cast<Lane*>(data);void* memory=nullptr;
        std::size_t bytes=std::min<std::size_t>(requested,1024*8);
        if(bytes<8)return;
        if(pa_stream_begin_write(stream,&memory,&bytes)<0){lane.failed=true;return;}
        const auto count=std::min<std::size_t>({bytes/8,requested/8,1024});
        if(!memory || reinterpret_cast<std::uintptr_t>(memory)%alignof(float) || !count){pa_stream_cancel_write(stream);lane.failed=true;return;}
        auto* samples=static_cast<float*>(memory);const auto offset=lane.frames.load();const bool enabled=lane.enabled.load();
        for(std::size_t i=0;i<count;++i)for(unsigned ch=0;ch<2;++ch)samples[i*2+ch]=enabled?marker(ch,offset+i):0;
        if(pa_stream_write(stream,memory,count*8,nullptr,0,PA_SEEK_RELATIVE)<0)lane.failed=true;
        else if(enabled)lane.frames.fetch_add(count);
    }
    static void read(pa_stream* stream,std::size_t,void* data) noexcept {
        auto& lane=*static_cast<Lane*>(data);const void* memory=nullptr;std::size_t bytes=0;
        if(pa_stream_peek(stream,&memory,&bytes)<0){lane.failed=true;return;}
        if(!bytes)return;
        if(lane.enabled && memory){
            if(bytes%8 || bytes/8>16384 || reinterpret_cast<std::uintptr_t>(memory)%alignof(float))lane.failed=true;
            else {auto* samples=static_cast<const float*>(memory);for(std::size_t i=0;i<bytes/8;++i)lane.witness.sample(samples[i*2],samples[i*2+1]);
                lane.frames.fetch_add(bytes/8);lane.matched=lane.witness.matches;}
        }else if(lane.enabled && !memory && lane.witness.started)lane.failed=true;
        if(pa_stream_drop(stream)<0)lane.failed=true;
    }
    template<class Predicate>bool waitLocked(Predicate ready) {
        const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);
        while(!ready() && std::chrono::steady_clock::now()<end){
            pa_threaded_mainloop_unlock(loop_);std::this_thread::sleep_for(std::chrono::milliseconds(1));pa_threaded_mainloop_lock(loop_);
        }
        return ready();
    }
    bool operations(pa_operation* first,pa_operation* second) {
        const bool completed=first && second && waitLocked([&]{return pa_operation_get_state(first)!=PA_OPERATION_RUNNING && pa_operation_get_state(second)!=PA_OPERATION_RUNNING;})
            && pa_operation_get_state(first)==PA_OPERATION_DONE && pa_operation_get_state(second)==PA_OPERATION_DONE;
        for(auto* op:{first,second})if(op){if(pa_operation_get_state(op)==PA_OPERATION_RUNNING)pa_operation_cancel(op);pa_operation_unref(op);}
        return completed;
    }
public:
    struct Report {std::uint64_t render=0,capture=0,matches=0,errors=0;pa_usec_t outputTime=0,inputTime=0;std::int64_t readIndex=0,writeIndex=0;bool timingKnown=false;};
    ~Pair(){close();}
    bool resources()const{return loop_ || context_ || lanes_[0].stream || lanes_[1].stream;}
    void close() noexcept {
        for(auto& lane:lanes_)lane.enabled=false;
        if(loop_ && running_)pa_threaded_mainloop_lock(loop_);
        for(auto& lane:lanes_)if(lane.stream){pa_stream_set_read_callback(lane.stream,nullptr,nullptr);pa_stream_set_write_callback(lane.stream,nullptr,nullptr);
            pa_stream_disconnect(lane.stream);pa_stream_unref(lane.stream);lane.stream=nullptr;}
        if(context_){pa_context_disconnect(context_);pa_context_unref(context_);context_=nullptr;}
        if(loop_ && running_)pa_threaded_mainloop_unlock(loop_);
        if(loop_){if(running_)pa_threaded_mainloop_stop(loop_);pa_threaded_mainloop_free(loop_);loop_=nullptr;}running_=false;
    }
    Result run(const std::string& sink,const std::string& monitor,Report& report) {
        if(resources())return std::unexpected{ErrorType::InvalidState};
        if(sink.empty() || monitor.empty())return std::unexpected{ErrorType::EndpointUnavailable};
        struct Cleanup{Pair& pair;~Cleanup(){pair.close();}} cleanup{*this};
        for(auto& lane:lanes_){lane.failed=false;lane.enabled=false;lane.frames=0;lane.matched=0;lane.witness={};}
        loop_=pa_threaded_mainloop_new();if(!loop_)return std::unexpected{ErrorType::ConfigurationFailed};
        if(pa_threaded_mainloop_start(loop_)<0)return std::unexpected{ErrorType::ConfigurationFailed};running_=true;
        pa_threaded_mainloop_lock(loop_);
        struct Unlock{pa_threaded_mainloop* loop;~Unlock(){pa_threaded_mainloop_unlock(loop);}} unlock{loop_};
        context_=pa_context_new(pa_threaded_mainloop_get_api(loop_),"mka-pulse-duplex-study");
        if(!context_ || pa_context_connect(context_,nullptr,PA_CONTEXT_NOFLAGS,nullptr)<0)return std::unexpected{ErrorType::EndpointUnavailable};
        if(!waitLocked([&]{return pa_context_get_state(context_)==PA_CONTEXT_READY || !PA_CONTEXT_IS_GOOD(pa_context_get_state(context_));})
            || pa_context_get_state(context_)!=PA_CONTEXT_READY)return std::unexpected{ErrorType::EndpointUnavailable};
        const pa_sample_spec spec{PA_SAMPLE_FLOAT32NE,48000,2};
        for(unsigned i=0;i<2;++i){auto& lane=lanes_[i];lane.stream=pa_stream_new(context_,i?"study-capture":"study-render",&spec,nullptr);
            if(!lane.stream)return std::unexpected{ErrorType::ConfigurationFailed};
            pa_buffer_attr attr{UINT32_MAX,2048,0,UINT32_MAX,2048};
            const auto flags=static_cast<pa_stream_flags_t>(PA_STREAM_START_CORKED|PA_STREAM_ADJUST_LATENCY|PA_STREAM_AUTO_TIMING_UPDATE|PA_STREAM_INTERPOLATE_TIMING|PA_STREAM_DONT_MOVE);
            int result;
            if(i){pa_stream_set_read_callback(lane.stream,read,&lane);result=pa_stream_connect_record(lane.stream,monitor.c_str(),&attr,flags);}
            else {pa_stream_set_write_callback(lane.stream,write,&lane);result=pa_stream_connect_playback(lane.stream,sink.c_str(),&attr,flags,nullptr,nullptr);}
            if(result<0)return std::unexpected{ErrorType::EndpointUnavailable};
        }
        if(!waitLocked([&]{return (pa_stream_get_state(lanes_[0].stream)==PA_STREAM_READY && pa_stream_get_state(lanes_[1].stream)==PA_STREAM_READY)
            || !PA_STREAM_IS_GOOD(pa_stream_get_state(lanes_[0].stream)) || !PA_STREAM_IS_GOOD(pa_stream_get_state(lanes_[1].stream));}))return std::unexpected{ErrorType::EndpointUnavailable};
        for(auto& lane:lanes_){if(pa_stream_get_state(lane.stream)!=PA_STREAM_READY)return std::unexpected{ErrorType::EndpointUnavailable};
            const auto* actual=pa_stream_get_sample_spec(lane.stream);if(!actual || !pa_sample_spec_equal(actual,&spec))return std::unexpected{ErrorType::ConfigurationFailed};}
        for(auto& lane:lanes_)lane.enabled=true;
        if(!operations(pa_stream_cork(lanes_[0].stream,0,nullptr,nullptr),pa_stream_cork(lanes_[1].stream,0,nullptr,nullptr))
            || pa_stream_is_corked(lanes_[0].stream)!=0 || pa_stream_is_corked(lanes_[1].stream)!=0)return std::unexpected{ErrorType::ConfigurationFailed};
        if(!waitLocked([&]{return lanes_[1].matched>=2048 || lanes_[0].failed || lanes_[1].failed;}) || lanes_[0].failed || lanes_[1].failed)return std::unexpected{ErrorType::ConfigurationFailed};
        for(auto& lane:lanes_)lane.enabled=false;
        report={};report.render=lanes_[0].frames.load();report.capture=lanes_[1].frames.load();report.matches=lanes_[1].matched.load();report.errors=lanes_[1].witness.errors;
        if(operations(pa_stream_update_timing_info(lanes_[0].stream,nullptr,nullptr),pa_stream_update_timing_info(lanes_[1].stream,nullptr,nullptr))){
            const auto* out=pa_stream_get_timing_info(lanes_[0].stream);const auto* in=pa_stream_get_timing_info(lanes_[1].stream);
            report.timingKnown=out && in && !out->write_index_corrupt && !in->read_index_corrupt
                && pa_stream_get_time(lanes_[0].stream,&report.outputTime)>=0 && pa_stream_get_time(lanes_[1].stream,&report.inputTime)>=0;
            if(out)report.writeIndex=out->write_index;if(in)report.readIndex=in->read_index;
        }
        return {};
    }
};
TEST(PulseDuplexStudy, NativeStreamsTransferMarkerAndReportIndependentTiming) {
    const char* target=std::getenv("MKA_TEST_PULSEAUDIO_DEVICE_ID");if(!target)GTEST_SKIP()<<"Requires private PulseAudio sink/monitor fixture";
    Pair pair;Pair::Report report;ASSERT_TRUE(pair.run(target,std::string(target)+".monitor",report));EXPECT_FALSE(pair.resources());
    EXPECT_GE(report.matches,2048u);EXPECT_EQ(report.errors,0u);EXPECT_GT(report.render,0u);EXPECT_GT(report.capture,0u);EXPECT_TRUE(report.timingKnown);
    RecordProperty("capture_usec",std::to_string(report.inputTime));RecordProperty("render_usec",std::to_string(report.outputTime));
    RecordProperty("clock_identity","not established; record/playback timing is stream-specific");
}
TEST(PulseDuplexStudy, MissingCaptureRollsBackTheCorkedPairAndAllowsRetry) {
    const char* target=std::getenv("MKA_TEST_PULSEAUDIO_DEVICE_ID");if(!target)GTEST_SKIP()<<"Requires private PulseAudio sink/monitor fixture";
    Pair pair;Pair::Report report;EXPECT_FALSE(pair.run(target,"mka-study-missing-monitor",report));EXPECT_FALSE(pair.resources());
    ASSERT_TRUE(pair.run(target,std::string(target)+".monitor",report));EXPECT_FALSE(pair.resources());EXPECT_EQ(report.errors,0u);EXPECT_GE(report.matches,2048u);
}
}
