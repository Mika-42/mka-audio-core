#include <gtest/gtest.h>
#include "../../src/backend/macos/detail/devices.hpp"
#include <chrono>
#include <cstdlib>
#include <thread>
import mka.audio.backend.coreaudio;
using namespace mka::audio::core;

static void nativeSmoke(bool input) {
    const char* uid=std::getenv(input?"MKA_TEST_COREAUDIO_INPUT_UID":"MKA_TEST_COREAUDIO_OUTPUT_UID");
    if(input && (!uid || !*uid))GTEST_SKIP()<<"Capture requires explicit MKA_TEST_COREAUDIO_INPUT_UID and macOS microphone permission; no permission prompt is requested by CI.";
    CoreAudio backend;const auto endpoints=backend.getEndPoints();
    const Endpoint* selected=nullptr;
    for(const auto& endpoint:endpoints)if((input?endpoint.input:endpoint.output) && (!uid || !*uid || endpoint.id==uid)){selected=&endpoint;break;}
    if(!selected)GTEST_SKIP()<<"No usable native CoreAudio "<<(input?"capture":"render")<<" endpoint in this macOS session.";
    const auto& api=mka_coreaudio_detail::nativeApi();
    AudioObjectID object=0;
    for(const auto& device:mka_coreaudio_detail::devices(api))if(device.uid==selected->id)object=device.object;
    Float64 rate=0;UInt32 frames=0;
    ASSERT_TRUE(mka_coreaudio_detail::read(api,object,kAudioDevicePropertyNominalSampleRate,rate));
    ASSERT_TRUE(mka_coreaudio_detail::read(api,object,kAudioDevicePropertyBufferFrameSize,frames));
    const auto channels=(input?selected->input:selected->output)->maxChannels;
    EndpointConfig cfg{selected->id,input?Direction::Input:Direction::Output,input?channels:0,input?0:channels,
        static_cast<SampleRate>(rate),Format::Float32,frames};
    std::atomic<unsigned> calls=0;
    ASSERT_TRUE(backend.setProcessFunction(+[](void* user,const AudioProcessContext&) noexcept {++*static_cast<std::atomic<unsigned>*>(user);},&calls));
    const auto opened=backend.open(cfg);
    if(!opened && opened.error()==ErrorType::EndpointUnavailable)
        GTEST_SKIP()<<"Native endpoint is unavailable or permission was denied in this session.";
    ASSERT_TRUE(opened)<<"Discovered native configuration must open; ErrorType="<<static_cast<int>(opened.error());
    const auto started=backend.start();
    if(!started){
        ASSERT_TRUE(backend.close());
        if(started.error()==ErrorType::EndpointUnavailable)GTEST_SKIP()<<"Native device/permission unavailable at start.";
        FAIL()<<"Native start failed, ErrorType="<<static_cast<int>(started.error());
    }
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(calls.load()<4 && !backend.status().failed && std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ASSERT_TRUE(backend.stop());ASSERT_TRUE(backend.close());
    EXPECT_GE(calls.load(),4u);EXPECT_FALSE(backend.status().failed);
    const auto stoppedCalls=calls.load();std::this_thread::sleep_for(std::chrono::milliseconds(20));EXPECT_EQ(calls.load(),stoppedCalls);
}
TEST(CoreAudioHardware, NativeRenderCallbacksAndStop){nativeSmoke(false);}
TEST(CoreAudioHardware, NativeCaptureCallbacksAndStop){nativeSmoke(true);}
