#pragma once
#include "wasapi_devices.hpp"
#include <algorithm>
namespace mka_wasapi_detail {
struct ExclusiveCandidate {
    SampleKind kind;
    std::uint32_t channels,rate;
    HRESULT support=E_FAIL;
    std::optional<std::uint32_t> minimumPeriodFrames;
};
struct ExclusiveAssessment {
    std::string id,name;
    HRESULT acquisition=E_FAIL,sharedMixSupport=E_FAIL,periodQuery=E_FAIL;
    REFERENCE_TIME defaultPeriod=0,minimumPeriod=0;
    std::vector<ExclusiveCandidate> candidates;
};
// Read-only capability assessment: no exclusive Initialize, Start or device
// reservation. A supported format is not a proven working exclusive stream.
inline std::vector<ExclusiveAssessment> assessExclusive(ControlThread& control) {
    auto devices=enumerateDevices(control);
    if(devices.empty())return {};
    return control.invoke([&]{
        std::vector<ExclusiveAssessment> results;
        ComPtr<IMMDeviceEnumerator> enumerator;
        const auto created=createEnumerator(control.api(),enumerator.GetAddressOf());
        for(const auto& info:devices) {
            ExclusiveAssessment assessment;assessment.id=info.id;assessment.name=info.name;
            assessment.acquisition=SUCCEEDED(created) && !enumerator?E_POINTER:created;
            if(SUCCEEDED(created) && enumerator) {
                ComPtr<IMMDevice> device;
                const auto id=utf16(info.id);
                assessment.acquisition=enumerator->GetDevice(id.c_str(),device.GetAddressOf());
                ComPtr<IAudioClient> client;
                if(SUCCEEDED(assessment.acquisition) && device)
                    assessment.acquisition=device->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(client.GetAddressOf()));
                if(SUCCEEDED(assessment.acquisition) && client) {
                    WAVEFORMATEX* rawMix=nullptr;
                    assessment.acquisition=client->GetMixFormat(&rawMix);TaskPtr<WAVEFORMATEX> mix(rawMix);
                    if(SUCCEEDED(assessment.acquisition) && mix && validWaveFormat(*mix)) {
                        WAVEFORMATEX* rawClosest=nullptr;
                        assessment.sharedMixSupport=client->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED,mix.get(),&rawClosest);
                        TaskPtr<WAVEFORMATEX> closest(rawClosest);
                        assessment.periodQuery=client->GetDevicePeriod(&assessment.defaultPeriod,&assessment.minimumPeriod);
                        std::vector<std::uint32_t> rates{info.rate,44100,48000,96000};
                        std::vector<std::uint32_t> channels{info.channels,1,2};
                        std::sort(rates.begin(),rates.end());rates.erase(std::unique(rates.begin(),rates.end()),rates.end());
                        std::sort(channels.begin(),channels.end());channels.erase(std::unique(channels.begin(),channels.end()),channels.end());
                        for(auto rate:rates)for(auto count:channels)
                            for(auto kind:{SampleKind::S16,SampleKind::S24Packed,SampleKind::S24Msb32,SampleKind::S32,SampleKind::F32,SampleKind::F64}) {
                                DWORD mask=0;
                                if(mix->wFormatTag==WAVE_FORMAT_EXTENSIBLE && mix->nChannels==count)
                                    mask=reinterpret_cast<const WAVEFORMATEXTENSIBLE&>(*mix).dwChannelMask;
                                const auto format=makeWaveFormat(kind,count,rate,mask);
                                if(!format)continue;
                                ExclusiveCandidate candidate{kind,count,rate};
                                // Exclusive queries require a NULL closest-match output.
                                candidate.support=client->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE,&format->Format,nullptr);
                                if(SUCCEEDED(assessment.periodQuery))candidate.minimumPeriodFrames=periodFrames(assessment.minimumPeriod,rate);
                                assessment.candidates.push_back(std::move(candidate));
                            }
                    } else if(SUCCEEDED(assessment.acquisition))assessment.acquisition=AUDCLNT_E_UNSUPPORTED_FORMAT;
                } else if(SUCCEEDED(assessment.acquisition))assessment.acquisition=E_POINTER;
            }
            results.push_back(std::move(assessment));
        }
        return results;
    });
}
}
