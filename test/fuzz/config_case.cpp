#include "config_case.hpp"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <expected>
#include <limits>
#include <string>
#include <vector>
import mka.audio.backend.abstract;
namespace mka_fuzz {
using namespace mka::audio::core;
static void require(bool condition){if(!condition)std::abort();}
class Probe final:public Backend {
public:unsigned calls=0;
private:
    std::vector<Endpoint> getEndPoints_() const override{return {};}
    Result open_(const EndpointConfig& config) override {
        ++calls;
        const bool input=config.inputChannels>0,output=config.outputChannels>0;
        require((config.direction==Direction::Input && input && !output)
            || (config.direction==Direction::Output && output && !input)
            || (config.direction==Direction::Duplex && input && output));
        require(static_cast<std::uint32_t>(config.format)<=static_cast<std::uint32_t>(Format::Float64));
        require(config.sampleRate>0 && config.bufferSize>0);
        for(const auto channels:{config.inputChannels,config.outputChannels}){
            require(channels<=static_cast<std::uint32_t>(std::numeric_limits<int>::max()));
            // Independent wide-integer oracle: no overflowing 32-bit product.
            const auto samples=static_cast<std::uint64_t>(channels)*config.bufferSize;
            require(samples<=std::numeric_limits<std::uint32_t>::max()/sizeof(float));
            require(samples<=std::numeric_limits<std::size_t>::max()/sizeof(float));
        }
        return std::unexpected{ErrorType::ConfigurationFailed};
    }
    Result start_() override{return {};}
    Result stop_() override{return {};}
    Result close_() override{return {};}
};
Observation configuration(const std::uint8_t* data,std::size_t size){
    // Six little-endian uint32 fields, followed by up to 256 UID bytes.
    std::array<std::uint32_t,6> fields{};
    for(std::size_t i=0;i<std::min(size,fields.size()*4);++i)fields[i/4]|=static_cast<std::uint32_t>(data[i])<<(8*(i%4));
    const auto idBytes=size>24?std::min<std::size_t>(size-24,256):0;
    EndpointConfig config{idBytes?std::string(reinterpret_cast<const char*>(data+24),idBytes):std::string{},
        static_cast<Direction>(fields[0]),fields[2],fields[3],fields[4],static_cast<Format>(fields[1]),fields[5]};
    Probe backend;const auto result=backend.open(config);require(!result);
    const auto started=backend.start();require(!started && started.error()==ErrorType::InvalidState);
    require(!backend.stop() && !backend.close());
    require(bool(backend.setProcessFunction(nullptr)));
    const auto retry=backend.open({"",Direction::Output,0,2,48000,Format::Float32,256});
    require(!retry && retry.error()==ErrorType::ConfigurationFailed);
    return {static_cast<int>(result.error()),backend.calls-1};
}
}
