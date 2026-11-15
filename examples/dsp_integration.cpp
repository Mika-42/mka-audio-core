#include <algorithm>
#include <array>
#include <chrono>
#include <expected>
#include <iostream>
#include <thread>
#include <vector>
#include "dsp/gain.hpp"
import mka.audio.backend.abstract;
using namespace mka::audio::core;

// Offline teaching transport, not an advertised/operational OS backend. A native
// application's binding below is identical; its backend supplies the buffers.
class OfflineTransport final:public Backend {
    std::jthread audio_;
    std::atomic<bool> bad_{false};
    std::vector<Endpoint> getEndPoints_() const override{return {};}
    Result open_(const EndpointConfig& config) override {
        if(config.id!="example:offline")return std::unexpected{ErrorType::EndpointUnavailable};
        if(config.format!=Format::Float32)return std::unexpected{ErrorType::FormatNotSupported};
        if(config.inputChannels!=1 || config.outputChannels!=1)return std::unexpected{ErrorType::ChannelsNotSupported};
        if(config.bufferSize!=4)return std::unexpected{ErrorType::BufferSizeNotSupported};
        return {};
    }
    Result start_() override {
        audio_=std::jthread([this](std::stop_token stop){
            const std::array<float,4> input{0.25f,-0.5f,0.75f,-1.0f};
            std::array<float,4> output{};
            const float* in[]{input.data()};float* out[]{output.data()};
            for(std::uint32_t frames:{1u,3u,2u,4u}){
                if(stop.stop_requested())break;
                std::fill(output.begin(),output.end(),0.0f);
                if(callback)callback(userData,{{in,1},{out,1},frames});
                for(std::uint32_t i=0;i<frames;++i)if(output[i]!=input[i]*mka_example::Gain::factor)bad_=true;
            }
        });
        return {};
    }
    Result stop_() override {if(audio_.joinable()){audio_.request_stop();audio_.join();}return {};}
    Result close_() override{return {};}
public:
    ~OfflineTransport() override {stopDispatcher();if(audio_.joinable()){audio_.request_stop();audio_.join();}}
    bool bad() const noexcept{return bad_.load();}
};
int main(){
    mka_example::Gain dsp; // Must outlive the backend and all callbacks.
    OfflineTransport backend;
    const auto bound=backend.setProcessFunction(+[](void* user,const AudioProcessContext& context) noexcept {
        static_cast<mka_example::Gain*>(user)->process(context);
    },&dsp);
    const EndpointConfig config{"example:offline",Direction::Duplex,1,1,48000,Format::Float32,4};
    if(!bound || !backend.open(config) || !backend.start())return 1;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(dsp.calls.load()<4 && std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if(!backend.stop() || !backend.close())return 1;
    if(backend.bad() || dsp.calls.load()!=4 || dsp.frames.load()!=10)return 1;
    std::cout<<"DSP example: 4 callbacks, 10 Float32 frames, gain 0.5\n";
}
