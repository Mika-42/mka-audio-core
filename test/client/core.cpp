// Deliberately a client TU: public modules only, no producer headers or SDK.
#include <cstdint>
#include <expected>
#include <type_traits>
#include <string_view>
#include <vector>
import mka.audio.backend.abstract;
import mka.audio.backend.plateform;
import mka.audio.convert;
import mka.audio.diagnostics;
using namespace mka::audio::core;
class ClientBackend final:public Backend {
    std::vector<Endpoint> getEndPoints_() const override{return {};}
    Result open_(const EndpointConfig&) override{return {};}
    Result start_() override{return {};}
    Result stop_() override{return {};}
    Result close_() override{return {};}
};
int main(){
    static_assert(std::is_same_v<Result,std::expected<void,ErrorType>>);
    static_assert(sizeof(SampleRate)==4 && sizeof(BufferSize)==4 && sizeof(Event{}.xruns)==8);
    static_assert(std::is_same_v<decltype(InputBuffer{}.channels),const float* const*>);
    static_assert(std::is_same_v<decltype(OutputBuffer{}.channels),float* const*>);
    static_assert(errorName(ErrorType::InvalidState)=="InvalidState");
    static_assert(realtimeName(RealtimeState::No)=="No");
    ClientBackend backend;EndpointConfig config{"",Direction::Output,0,2,48000,Format::Float32,256};
    if(!backend.open(config) || !backend.start() || !backend.stop() || !backend.close())return 1;
    float source=0.25f,result=0;
    mka::audio::convert::readChannel(mka::audio::convert::Layout::F32,
        reinterpret_cast<const std::byte*>(&source),sizeof(float),&result,1);
    return result==source && isBackendKnown(AudioBackend::Alsa)?0:2;
}
