#include <array>
#include <expected>
#include <vector>
#include <gtest/gtest.h>
import mka.audio.backend.abstract;
#include "legacy.hpp"
namespace {
using namespace mka::audio::core;
using namespace mka_study::legacy;
MMRESULT returned=MMSYSERR_NOERROR;unsigned calls=0;WAVEFORMATEX observed{};DWORD observedFlags=0;
MMRESULT WINAPI fakeOutput(LPHWAVEOUT handle,UINT device,LPCWAVEFORMATEX format,DWORD_PTR callback,DWORD_PTR user,DWORD flags){
    ++calls;observed=*format;observedFlags=flags;
    EXPECT_EQ(handle,nullptr);EXPECT_EQ(device,WAVE_MAPPER);EXPECT_EQ(callback,0u);EXPECT_EQ(user,0u);
    return returned;
}
MMRESULT WINAPI fakeInput(LPHWAVEIN handle,UINT device,LPCWAVEFORMATEX format,DWORD_PTR callback,DWORD_PTR user,DWORD flags){
    EXPECT_EQ(handle,nullptr);return fakeOutput(nullptr,device,format,callback,user,flags);
}
HRESULT fakeRender(DSCAPS* caps) noexcept {if(caps->dwSize!=sizeof(*caps))return E_INVALIDARG;caps->dwFlags=0x42;return S_OK;}
HRESULT fakeCapture(DSCCAPS* caps) noexcept {if(caps->dwSize!=sizeof(*caps))return E_INVALIDARG;caps->dwFlags=0x24;caps->dwChannels=2;return S_OK;}
HRESULT absentRender(DSCAPS*) noexcept{return DSERR_NODRIVER;}
Api fakeApi(){return {fakeOutput,fakeInput,fakeRender,fakeCapture};}
EndpointConfig nominal(){return {"",Direction::Output,0,2,48000,Format::Int16,256};}
TEST(LegacyAssessment, QueriesExactFormatsWithoutOpeningHandlesOrEnablingAcmConversion) {
    auto api=fakeApi();Report report;returned=MMSYSERR_NOERROR;
    for(auto format:{Format::Int16,Format::Int24,Format::Int32,Format::Float32,Format::Float64}){
        auto cfg=nominal();cfg.format=format;ASSERT_TRUE(queryWave(cfg,report,api));
        EXPECT_EQ(observed.nSamplesPerSec,48000u);EXPECT_EQ(observed.nChannels,2);
        EXPECT_EQ(observed.nAvgBytesPerSec,observed.nBlockAlign*48000u);
        EXPECT_EQ(observedFlags,DWORD(WAVE_FORMAT_QUERY|WAVE_FORMAT_DIRECT));
        cfg.direction=Direction::Input;cfg.inputChannels=2;cfg.outputChannels=0;ASSERT_TRUE(queryWave(cfg,report,api));
    }
}
TEST(LegacyAssessment, RejectsZeroOverflowUnsupportedDirectionsAndFormatsBeforeSdkCalls) {
    auto api=fakeApi();calls=0;Report report;
    auto cfg=nominal();cfg.sampleRate=0;EXPECT_EQ(queryWave(cfg,report,api).error(),ErrorType::SampleRateNotSupported);
    cfg=nominal();cfg.bufferSize=0;EXPECT_EQ(queryWave(cfg,report,api).error(),ErrorType::BufferSizeNotSupported);
    cfg=nominal();cfg.bufferSize=UINT32_MAX;EXPECT_EQ(queryWave(cfg,report,api).error(),ErrorType::BufferSizeNotSupported);
    cfg=nominal();cfg.sampleRate=UINT32_MAX;EXPECT_EQ(queryWave(cfg,report,api).error(),ErrorType::SampleRateNotSupported);
    cfg=nominal();cfg.outputChannels=0;EXPECT_EQ(queryWave(cfg,report,api).error(),ErrorType::ChannelsNotSupported);
    cfg=nominal();cfg.direction=Direction::Duplex;EXPECT_EQ(queryWave(cfg,report,api).error(),ErrorType::ConfigurationFailed);
    cfg=nominal();cfg.format=static_cast<Format>(99);EXPECT_EQ(queryWave(cfg,report,api).error(),ErrorType::FormatNotSupported);
    EXPECT_EQ(calls,0u);EXPECT_FALSE(report.wave.has_value());
}
TEST(LegacyAssessment, NativeErrorsRemainExplicitAndDoNotSelectAnotherBackend) {
    Report report;auto api=fakeApi();
    for(auto [code,error]:std::array<std::pair<MMRESULT,ErrorType>,3>{{{WAVERR_BADFORMAT,ErrorType::FormatNotSupported},{MMSYSERR_NODRIVER,ErrorType::EndpointUnavailable},{MMSYSERR_ERROR,ErrorType::ConfigurationFailed}}}){
        returned=code;auto result=queryWave(nominal(),report,api);ASSERT_FALSE(result);EXPECT_EQ(result.error(),error);EXPECT_EQ(report.wave,code);
    }
    returned=MMSYSERR_NOERROR;ASSERT_TRUE(queryWave(nominal(),report,api));
}
TEST(LegacyAssessment, DirectSoundCapabilitiesAndUnavailableDriverPreserveNativeEvidence) {
    Report report;auto api=fakeApi();ASSERT_TRUE(queryDirectSound(Direction::Output,report,api));EXPECT_EQ(report.flags,0x42u);
    ASSERT_TRUE(queryDirectSound(Direction::Input,report,api));EXPECT_EQ(report.captureChannels,2u);EXPECT_EQ(report.flags,0x24u);
    api.render=absentRender;EXPECT_EQ(queryDirectSound(Direction::Output,report,api).error(),ErrorType::EndpointUnavailable);EXPECT_EQ(report.directSound,DSERR_NODRIVER);
    EXPECT_EQ(queryDirectSound(Direction::Duplex,report,api).error(),ErrorType::ConfigurationFailed);EXPECT_FALSE(report.directSound.has_value());
}
TEST(LegacyHardware, ReadOnlyCapabilitiesOrExplicitUnavailableSkip) {
    Report report;const auto wave=queryWave(nominal(),report);const auto ds=queryDirectSound(Direction::Output,report);
    if(waveOutGetNumDevs()==0 && !wave && !ds)GTEST_SKIP()<<"No WMME/DirectSound render endpoint or audio service";
    ASSERT_TRUE(report.wave.has_value());ASSERT_TRUE(report.directSound.has_value());
    RecordProperty("wave_result",std::to_string(*report.wave));RecordProperty("directsound_result",std::to_string(*report.directSound));
    RecordProperty("wave_render_devices",std::to_string(waveOutGetNumDevs()));
}
TEST(LegacyHardware, ReadOnlyCaptureCapabilitiesOrExplicitUnavailableSkip) {
    Report report;auto cfg=nominal();cfg.direction=Direction::Input;cfg.inputChannels=2;cfg.outputChannels=0;
    const auto wave=queryWave(cfg,report);const auto ds=queryDirectSound(Direction::Input,report);
    if(waveInGetNumDevs()==0 && !wave && !ds)GTEST_SKIP()<<"No WMME/DirectSound capture endpoint or audio service";
    ASSERT_TRUE(report.wave.has_value());ASSERT_TRUE(report.directSound.has_value());
    RecordProperty("wave_result",std::to_string(*report.wave));RecordProperty("directsound_result",std::to_string(*report.directSound));
    RecordProperty("wave_capture_devices",std::to_string(waveInGetNumDevs()));
}
}
