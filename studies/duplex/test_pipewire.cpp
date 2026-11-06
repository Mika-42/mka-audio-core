#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <expected>
#include <string>
#include <cstring>
#include <thread>
#include <gtest/gtest.h>
#include "timeline.hpp"
import mka.audio.backend.abstract;
#include "../../src/backend/linux/detail/pipewire_buffer.hpp"
namespace {
using namespace mka::audio::core;
using namespace mka_study;
class Pair {
    struct Lane {
        pw_stream* stream=nullptr;spa_hook listener{};bool listening=false,output=false;
        std::atomic<bool> enabled{false},inProcess{false},format{false},failed{false};
        std::atomic<std::uint64_t> calls{0},frames{0},matched{0};
        SignalWitness witness;Stamp time;
    } lanes_[2];
    pw_thread_loop* loop_=nullptr;pw_context* context_=nullptr;pw_core* core_=nullptr;
    pw_registry* registry_=nullptr;spa_hook registryListener_{},coreListener_{};
    bool registryListening_=false,coreListening_=false;
    std::array<std::string,2> targets_;
    std::array<std::atomic<std::uint32_t>,2> targetIds_{PW_ID_ANY,PW_ID_ANY};
    std::atomic<bool> enumerated_{false};int sync_=0;
    bool running_=false;
    static void state(void* data,pw_stream_state,pw_stream_state value,const char*) {
        if(value==PW_STREAM_STATE_ERROR)static_cast<Lane*>(data)->failed=true;
    }
    static void format(void* data,std::uint32_t id,const spa_pod* pod) {
        if(id!=SPA_PARAM_Format || !pod)return;
        auto& lane=*static_cast<Lane*>(data);spa_audio_info_raw actual{};
        const bool exact=spa_format_audio_raw_parse(pod,&actual)>=0 && actual.format==SPA_AUDIO_FORMAT_F32P
            && actual.rate==48000 && actual.channels==2;
        lane.format=exact;if(!exact)lane.failed=true;
    }
    static void process(void* data) noexcept {
        auto& lane=*static_cast<Lane*>(data);lane.inProcess.store(true,std::memory_order_seq_cst);
        struct Exit {Lane& lane;~Exit(){lane.inProcess.store(false,std::memory_order_release);}} exit{lane};
        auto* queued=pw_stream_dequeue_buffer(lane.stream);if(!queued)return;
        auto* b=queued->buffer;const bool enabled=lane.enabled.load(std::memory_order_seq_cst);
        std::uint32_t count=UINT32_MAX;bool valid=b && b->n_datas==2;
        if(valid)for(unsigned ch=0;ch<2;++ch){
            auto& d=b->datas[ch];
            if(lane.output){if(!detail::writableFloatPlane(d)){valid=false;break;}count=std::min(count,d.maxsize/4);}
            else {auto available=detail::readableFloatFrames(d);if(!available){valid=false;break;}count=std::min(count,*available);}
        }
        if(valid && lane.output && queued->requested)count=static_cast<std::uint32_t>(std::min<std::uint64_t>(count,queued->requested));
        if(!valid){lane.failed=true;count=0;}
        if(lane.output && b){
            const auto offset=lane.frames.load();
            for(unsigned ch=0;ch<b->n_datas;++ch){auto& d=b->datas[ch];
                if(valid)for(std::uint32_t i=0;i<count;++i)static_cast<float*>(d.data)[i]=enabled?marker(ch,offset+i):0;
                if(d.chunk){d.chunk->offset=0;d.chunk->stride=4;d.chunk->size=valid?count*4:0;d.chunk->flags=0;}
            }
        }else if(valid && enabled){
            const float* channels[2]{};
            for(unsigned ch=0;ch<2;++ch)channels[ch]=reinterpret_cast<const float*>(static_cast<const std::byte*>(b->datas[ch].data)+b->datas[ch].chunk->offset);
            for(std::uint32_t i=0;i<count;++i)lane.witness.sample(channels[0][i],channels[1][i]);
            lane.matched=lane.witness.matches;
        }
        if(enabled){pw_time native{};if(pw_stream_get_time_n(lane.stream,&native,sizeof(native))>=0)
            lane.time={native.now,native.ticks,native.rate.num,native.rate.denom};
            lane.frames.fetch_add(count);lane.calls.fetch_add(1);}
        pw_stream_queue_buffer(lane.stream,queued);
    }
public:
    struct Report {std::uint64_t render=0,capture=0,matches=0,errors=0;Stamp input,output;};
    Pair(){pw_init(nullptr,nullptr);lanes_[0].output=true;}
    ~Pair(){close();pw_deinit();}
    bool resources()const{return loop_ || context_ || core_ || registry_ || lanes_[0].stream || lanes_[1].stream;}
    void close() noexcept {
        for(auto& lane:lanes_)lane.enabled.store(false,std::memory_order_seq_cst);
        if(loop_ && running_)pw_thread_loop_lock(loop_);
        for(auto& lane:lanes_)if(lane.stream)pw_stream_set_active(lane.stream,false);
        if(loop_ && running_)pw_thread_loop_unlock(loop_);
        for(auto& lane:lanes_)while(lane.inProcess.load(std::memory_order_seq_cst))std::this_thread::yield();
        if(loop_ && running_)pw_thread_loop_lock(loop_);
        for(auto& lane:lanes_)if(lane.stream){if(lane.listening)spa_hook_remove(&lane.listener);lane.listening=false;pw_stream_destroy(lane.stream);lane.stream=nullptr;}
        if(registryListening_){spa_hook_remove(&registryListener_);registryListening_=false;}
        if(coreListening_){spa_hook_remove(&coreListener_);coreListening_=false;}
        if(registry_){pw_proxy_destroy(reinterpret_cast<pw_proxy*>(registry_));registry_=nullptr;}
        if(core_){pw_core_disconnect(core_);core_=nullptr;}
        if(context_){pw_context_destroy(context_);context_=nullptr;}
        if(loop_ && running_)pw_thread_loop_unlock(loop_);
        if(loop_){if(running_)pw_thread_loop_stop(loop_);pw_thread_loop_destroy(loop_);loop_=nullptr;}running_=false;
    }
    Result run(const std::string& output,const std::string& capture,Report& report) {
        if(resources())return std::unexpected{ErrorType::InvalidState};
        if(output.empty() || capture.empty())return std::unexpected{ErrorType::EndpointUnavailable};
        struct Cleanup {Pair& pair;~Cleanup(){pair.close();}} cleanup{*this};
        targets_={output,capture};for(auto& id:targetIds_)id=PW_ID_ANY;enumerated_=false;
        loop_=pw_thread_loop_new("mka-duplex-study",nullptr);if(!loop_)return std::unexpected{ErrorType::ConfigurationFailed};
        context_=pw_context_new(pw_thread_loop_get_loop(loop_),nullptr,0);if(!context_)return std::unexpected{ErrorType::ConfigurationFailed};
        core_=pw_context_connect(context_,nullptr,0);if(!core_)return std::unexpected{ErrorType::EndpointUnavailable};
        registry_=pw_core_get_registry(core_,PW_VERSION_REGISTRY,0);if(!registry_)return std::unexpected{ErrorType::EndpointUnavailable};
        static const pw_registry_events registryEvents=[] {
            pw_registry_events e{};e.version=PW_VERSION_REGISTRY_EVENTS;
            e.global=+[](void* data,std::uint32_t id,std::uint32_t,const char* type,std::uint32_t,const spa_dict* props){
                if(std::strcmp(type,PW_TYPE_INTERFACE_Node)!=0 || !props)return;
                const char* name=spa_dict_lookup(props,PW_KEY_NODE_NAME);const char* media=spa_dict_lookup(props,PW_KEY_MEDIA_CLASS);
                if(!name || !media || std::strcmp(media,"Audio/Sink")!=0)return;
                auto& pair=*static_cast<Pair*>(data);for(unsigned i=0;i<2;++i)if(pair.targets_[i]==name)pair.targetIds_[i]=id;
            };
            return e;
        }();
        static const pw_core_events coreEvents=[] {
            pw_core_events e{};e.version=PW_VERSION_CORE_EVENTS;
            e.done=+[](void* data,std::uint32_t,int sequence){auto& pair=*static_cast<Pair*>(data);if(sequence==pair.sync_)pair.enumerated_=true;};return e;
        }();
        pw_registry_add_listener(registry_,&registryListener_,&registryEvents,this);registryListening_=true;
        pw_core_add_listener(core_,&coreListener_,&coreEvents,this);coreListening_=true;
        sync_=pw_core_sync(core_,PW_ID_CORE,0);if(sync_<0)return std::unexpected{ErrorType::EndpointUnavailable};
        static const pw_stream_events events=[] {pw_stream_events e{};e.version=PW_VERSION_STREAM_EVENTS;e.state_changed=state;e.param_changed=format;e.process=process;return e;}();
        for(unsigned i=0;i<2;++i){auto& lane=lanes_[i];
            lane.failed=false;lane.format=false;lane.frames=0;lane.calls=0;lane.matched=0;lane.witness={};lane.time={};
            auto* props=pw_properties_new(PW_KEY_MEDIA_TYPE,"Audio",PW_KEY_MEDIA_CATEGORY,i?"Capture":"Playback",
                PW_KEY_MEDIA_ROLE,"Production",PW_KEY_TARGET_OBJECT,(i?capture:output).c_str(),
                PW_KEY_NODE_LATENCY,"256/48000","node.dont-fallback","true",nullptr);
            if(!props)return std::unexpected{ErrorType::ConfigurationFailed};
            if(i)pw_properties_set(props,"stream.capture.sink","true");
            lane.stream=pw_stream_new(core_,i?"study-capture":"study-render",props);if(!lane.stream)return std::unexpected{ErrorType::ConfigurationFailed};
            pw_stream_add_listener(lane.stream,&lane.listener,&events,&lane);lane.listening=true;
        }
        if(pw_thread_loop_start(loop_)<0)return std::unexpected{ErrorType::ConfigurationFailed};running_=true;
        const auto registryDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
        while(!enumerated_ && std::chrono::steady_clock::now()<registryDeadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if(!enumerated_)return std::unexpected{ErrorType::EndpointUnavailable};
        for(unsigned i=0;i<2;++i){auto& lane=lanes_[i];
            // Some older session managers ignore dont-fallback for unknown names.
            // Resolve the requested object before connecting either native leg.
            if(targetIds_[i]==PW_ID_ANY)return std::unexpected{ErrorType::EndpointUnavailable};
            std::uint8_t storage[1024];spa_pod_builder builder=SPA_POD_BUILDER_INIT(storage,sizeof(storage));
            spa_audio_info_raw raw{};raw.format=SPA_AUDIO_FORMAT_F32P;raw.rate=48000;raw.channels=2;
            const spa_pod* params[]{spa_format_audio_raw_build(&builder,SPA_PARAM_EnumFormat,&raw)};
            const auto flags=static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT|PW_STREAM_FLAG_MAP_BUFFERS|PW_STREAM_FLAG_RT_PROCESS);
            pw_thread_loop_lock(loop_);
            const auto connected=pw_stream_connect(lane.stream,i?PW_DIRECTION_INPUT:PW_DIRECTION_OUTPUT,targetIds_[i].load(),flags,params,1);
            pw_thread_loop_unlock(loop_);
            if(connected<0)return std::unexpected{ErrorType::EndpointUnavailable};
        }
        auto wait=[&](auto done){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);
            while(!done() && !lanes_[0].failed && !lanes_[1].failed && std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(1));
            return done() && !lanes_[0].failed && !lanes_[1].failed;};
        if(!wait([&]{return lanes_[0].format && lanes_[1].format;}))return std::unexpected{ErrorType::EndpointUnavailable};
        for(auto& lane:lanes_)lane.enabled.store(true,std::memory_order_seq_cst);
        if(!wait([&]{return lanes_[1].matched.load()>=2048;}))return std::unexpected{ErrorType::ConfigurationFailed};
        close();report={lanes_[0].frames.load(),lanes_[1].frames.load(),lanes_[1].matched.load(),lanes_[1].witness.errors,lanes_[1].time,lanes_[0].time};
        return {};
    }
};
TEST(PipeWireDuplexStudy, NativeStreamsTransferKnownSignalAndExposeGraphTiming) {
    const char* sink=std::getenv("MKA_TEST_PIPEWIRE_DEVICE_ID");if(!sink)GTEST_SKIP()<<"Requires private PipeWire sink fixture";
    Pair pair;Pair::Report report;ASSERT_TRUE(pair.run(sink,sink,report));EXPECT_FALSE(pair.resources());
    EXPECT_GE(report.matches,2048u);EXPECT_EQ(report.errors,0u);EXPECT_GT(report.render,0u);EXPECT_GT(report.capture,0u);
    auto offset=frameOffset(report.input,report.output);ASSERT_TRUE(offset);
    RecordProperty("observed_frame_offset",std::to_string(static_cast<double>(*offset)));
    RecordProperty("clock_identity","not exposed by pw_time; not established by this observation");
}
TEST(PipeWireDuplexStudy, MissingSecondEndpointRollsBackBothStreamsAndAllowsRetry) {
    const char* sink=std::getenv("MKA_TEST_PIPEWIRE_DEVICE_ID");if(!sink)GTEST_SKIP()<<"Requires private PipeWire sink fixture";
    Pair pair;Pair::Report report;EXPECT_FALSE(pair.run(sink,"mka-study-missing-capture",report));EXPECT_FALSE(pair.resources());
    ASSERT_TRUE(pair.run(sink,sink,report));EXPECT_FALSE(pair.resources());EXPECT_GE(report.matches,2048u);
}
}
