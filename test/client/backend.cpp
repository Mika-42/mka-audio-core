#include <expected>
#include <type_traits>
#if MKA_CLIENT_alsa
import mka.audio.backend.alsa;
using Native=mka::audio::core::ALSA;
#elif MKA_CLIENT_jack
import mka.audio.backend.jack;
using Native=mka::audio::core::JACK;
#elif MKA_CLIENT_pipewire
import mka.audio.backend.pipewire;
using Native=mka::audio::core::PipeWire;
#elif MKA_CLIENT_pulseaudio
import mka.audio.backend.pulseaudio;
using Native=mka::audio::core::PulseAudio;
#elif MKA_CLIENT_wasapi
import wasapi;
using Native=mka::audio::core::Wasapi;
#elif MKA_CLIENT_coreaudio
import coreaudio;
using Native=mka::audio::core::CoreAudio;
#endif
int main(){
    static_assert(std::is_base_of_v<mka::audio::core::Backend,Native>);
    Native backend;const auto started=backend.start();
    return !started && started.error()==mka::audio::core::ErrorType::InvalidState?0:1;
}
