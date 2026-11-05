module;
#include <expected>
#include <vector>
#include <CoreAudio/CoreAudio.h>
export module mka.audio.backend.coreaudio;
export import mka.audio.backend.abstract;

export namespace mka::audio::core {
class CoreAudio final : public Backend {
public:
    ~CoreAudio() override { stopDispatcher(); (void)stop(); (void)close(); }
protected:
    std::vector<Endpoint> getEndPoints_() const override { return {}; }
    Result open_(const EndpointConfig&) override { return std::unexpected{ErrorType::ConfigurationFailed}; }
    Result start_() override { return std::unexpected{ErrorType::ConfigurationFailed}; }
    Result stop_() override { return {}; }
    Result close_() override { return {}; }
};
}
