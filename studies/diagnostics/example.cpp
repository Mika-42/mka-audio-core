#include <chrono>
#include <expected>
#include <iostream>
#include <string>
#include <vector>
import mka.audio.diagnostics;
#include "error_details.hpp"
using namespace mka::audio::core;
using namespace mka_study::diagnostics;
class Unavailable final:public Backend {
    std::vector<Endpoint> getEndPoints_()const override{return {};}
    Result open_(const EndpointConfig&)override{return std::unexpected{ErrorType::EndpointUnavailable};}
    Result start_()override{return {};}
    Result stop_()override{return {};}
    Result close_()override{return {};}
};
int main(){
    Unavailable backend;
    const EndpointConfig config{std::string("probe:\xc3\xa9\0\xff",10),Direction::Output,0,2,48000,Format::Float32,256};
    const auto result=backend.open(config);if(result)return 1;
    ErrorDetails details;std::string json;
    if(!captureError(result.error(),Operation::Open,config,backend.status(),details) || !serialize(details,json))return 1;
    std::cout<<json<<'\n';
    const auto begin=std::chrono::steady_clock::now();
    for(unsigned i=0;i<1000;++i)if(!captureError(result.error(),Operation::Open,config,backend.status(),details) || !serialize(details,json))return 1;
    const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count();
    std::cout<<"{\"kind\":\"control_measurement\",\"samples\":1000,\"total_ns\":"<<elapsed<<",\"record_bytes\":"<<json.size()<<"}\n";
}
