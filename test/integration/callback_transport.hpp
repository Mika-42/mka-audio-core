// Include after importing the portable Backend, with standard headers already included.
namespace dsp_test {
using namespace mka::audio::core;
class Transport final:public Backend {
    bool running_=false;
    EndpointConfig config_;
    std::array<std::array<float,64>,4> inputs_{},outputs_{};
    std::array<const float*,4> inputPointers_{};
    std::array<float*,4> outputPointers_{};
    std::vector<Endpoint> getEndPoints_() const override{return {};}
    Result open_(const EndpointConfig& config) override {
        if(config.id!="A" && config.id!="B")return std::unexpected{ErrorType::EndpointUnavailable};
        if(config.format!=Format::Float32)return std::unexpected{ErrorType::FormatNotSupported};
        if(config.bufferSize>64)return std::unexpected{ErrorType::BufferSizeNotSupported};
        if(config.inputChannels>4 || config.outputChannels>4)return std::unexpected{ErrorType::ChannelsNotSupported};
        config_=config;
        for(std::size_t ch=0;ch<4;++ch){inputs_[ch].fill(ch==0?0.25f:-0.5f);inputPointers_[ch]=inputs_[ch].data();outputPointers_[ch]=outputs_[ch].data();}
        return {};
    }
    Result start_() override {running_=true;return {};}
    Result stop_() override {running_=false;return {};}
    Result close_() override {config_={};return {};}
public:
    ~Transport() override {stopDispatcher();(void)stop();(void)close();}
    void emit(std::uint32_t frames){
        if(!running_ || frames>64)return;
        for(auto& channel:outputs_)channel.fill(0);
        if(callback)callback(userData,{{config_.inputChannels?inputPointers_.data():nullptr,config_.inputChannels},
            {config_.outputChannels?outputPointers_.data():nullptr,config_.outputChannels},frames});
    }
};
}
