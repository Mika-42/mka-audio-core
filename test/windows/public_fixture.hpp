#pragma once
#include "fake_devices.hpp"
// Include after importing mka.audio.backend.wasapi.
namespace wasapi_test {
class PublicFixture : public DevicesFixture {
protected:
    mka_wasapi_detail::ComApi injected_;
    void SetUp() override {DevicesFixture::SetUp();injected_=api();mka_wasapi_detail::testApiOverride.store(&injected_);}
    void TearDown() override {mka_wasapi_detail::testApiOverride.store(nullptr);DevicesFixture::TearDown();}
    static mka::audio::core::EndpointConfig config(const std::string& id="output") {
        using namespace mka::audio::core;
        return {id,Direction::Output,0,2,48000,Format::Float32,256};
    }
};
}
