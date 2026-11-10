#include <gtest/gtest.h>
#include <atomic>
#include <expected>
#include <limits>
#include <type_traits>
#include "fake_driver.hpp"
import mka.audio.backend.asio;

namespace {
using namespace asio_test;
using namespace mka::audio::core;
EndpointConfig audioConfig(Direction direction=Direction::Output,std::uint32_t input=0,std::uint32_t output=2) {
    return {driverId,direction,input,output,48000,Format::Float32,256};
}
TEST(AsioAudioBuffers, DuplexHasIndependentNativeHalvesAndPreallocatedPlanarViews) {
    static_assert(!std::is_copy_constructible_v<AudioBuffers> && !std::is_move_constructible_v<AudioBuffers>);
    DriverFixture fixture; ASIO backend;
    ASSERT_TRUE(backend.open(audioConfig(Direction::Duplex,1,2)));
    const auto& buffers=activeDriver->audioBuffers();
    EXPECT_EQ(buffers.frames(),256u); EXPECT_EQ(buffers.inputs(),1u); EXPECT_EQ(buffers.outputs(),2u);
    ASSERT_EQ(buffers.native().size(),3u); ASSERT_EQ(fixture.storage.size(),6u);
    for (std::size_t index=0;index<3;++index) {
        const auto& descriptor=buffers.native()[index];
        EXPECT_EQ(descriptor.isInput,index==0?ASIOTrue:ASIOFalse);
        EXPECT_EQ(descriptor.channelNum,index==0?0:static_cast<long>(index-1));
        EXPECT_NE(descriptor.buffers[0],descriptor.buffers[1]);
        for (std::size_t half=0;half<2;++half) {
            const auto* bytes=static_cast<const std::byte*>(descriptor.buffers[half]);
            for (std::size_t i=0;i<256*4;++i) EXPECT_EQ(bytes[i],index==0?std::byte{0x5a}:std::byte{0});
        }
    }
    ASSERT_EQ(buffers.inputChannels().size(),1u); ASSERT_EQ(buffers.outputChannels().size(),2u);
    EXPECT_EQ(buffers.outputChannels()[1]-buffers.outputChannels()[0],256);
    EXPECT_NE(buffers.inputChannels()[0],buffers.outputChannels()[0]);
    for (std::uint32_t frame=0;frame<256;++frame) {
        EXPECT_FLOAT_EQ(buffers.inputChannels()[0][frame],0); EXPECT_FLOAT_EQ(buffers.outputChannels()[1][frame],0);
    }
    EXPECT_EQ(fixture.bufferCreations,1u); EXPECT_EQ(fixture.createBuffersThread,fixture.initThread);
    ASSERT_TRUE(backend.close()); EXPECT_EQ(fixture.bufferDisposals,1u);
    EXPECT_EQ(fixture.disposeThread,fixture.initThread); EXPECT_TRUE(fixture.storage.empty());
}
TEST(AsioAudioBuffers, UnidirectionalStreamsHaveOnlyTheRequestedTables) {
    DriverFixture fixture; ASIO backend;
    for (const auto& cfg : {audioConfig(Direction::Input,2,0),audioConfig(Direction::Output,0,2)}) {
        ASSERT_TRUE(backend.open(cfg)); const auto& audio=activeDriver->audioBuffers();
        EXPECT_EQ(audio.inputChannels().size(),cfg.inputChannels); EXPECT_EQ(audio.outputChannels().size(),cfg.outputChannels);
        EXPECT_EQ(audio.native().size(),2u); ASSERT_TRUE(backend.close());
    }
}
TEST(AsioAudioBuffers, EveryNativeContainerIsSilencedWithinItsOwnGuardedHalves) {
    DriverFixture fixture; ASIO backend;
    const std::pair<ASIOSampleType,Format> formats[]{{ASIOSTInt16MSB,Format::Int16},{ASIOSTInt24LSB,Format::Int24},
        {ASIOSTInt32MSB24,Format::Int24},{ASIOSTInt32LSB,Format::Int32},{ASIOSTFloat32MSB,Format::Float32},
        {ASIOSTFloat64LSB,Format::Float64}};
    for (const auto& [native,format] : formats) {
        fixture.nativeFormat=native; auto cfg=audioConfig(); cfg.format=format;
        ASSERT_TRUE(backend.open(cfg)); const auto bytes=256*channelLayout(native)->bytes;
        for (const auto& storage : fixture.storage) {
            ASSERT_EQ(storage.size(),bytes+16);
            for (std::size_t i=0;i<8;++i) {
                EXPECT_EQ(storage[i],std::byte{0x5a}); EXPECT_EQ(storage[storage.size()-1-i],std::byte{0x5a});
            }
            EXPECT_TRUE(std::all_of(storage.begin()+8,storage.end()-8,[](auto byte){return byte==std::byte{0};}));
        }
        ASSERT_TRUE(backend.close());
    }
}
TEST(AsioAudioBuffers, PartialSdkCreationErrorsDisposeExactlyOnceAndPermitReopen) {
    DriverFixture fixture; ASIO backend; fixture.currentRate=44100;
    const std::pair<ASIOError,ErrorType> errors[]{{ASE_NoMemory,ErrorType::ConfigurationFailed},
        {ASE_NotPresent,ErrorType::EndpointUnavailable},{ASE_InvalidMode,ErrorType::BufferSizeNotSupported},
        {ASE_SUCCESS,ErrorType::ConfigurationFailed}};
    unsigned attempts=0;
    for (const auto& [error,expected] : errors) {
        fixture.createBuffersResult=error;
        EXPECT_EQ(backend.open(audioConfig()).error(),expected); ++attempts;
        EXPECT_EQ(fixture.bufferDisposals,attempts); EXPECT_TRUE(fixture.storage.empty());
        EXPECT_EQ(fixture.live,0u); EXPECT_DOUBLE_EQ(fixture.currentRate,44100);
    }
    fixture.createBuffersResult=ASE_OK;
    ASSERT_TRUE(backend.open(audioConfig())); ASSERT_TRUE(backend.close()); EXPECT_EQ(fixture.bufferDisposals,attempts+1);
}
TEST(AsioAudioBuffers, NullAliasedMutatedAndWrappingNativePointersAreRejectedBeforeWriting) {
    DriverFixture fixture; ASIO backend;
    bool DriverFixture::* flags[]{&DriverFixture::nullHalf,&DriverFixture::aliasedHalves,&DriverFixture::aliasedChannels,
        &DriverFixture::changedDescriptor,&DriverFixture::changedBufferDirection,&DriverFixture::wrappingPointer};
    for (const auto flag : flags) {
        fixture.*flag=true;
        EXPECT_EQ(backend.open(audioConfig()).error(),ErrorType::BufferSizeNotSupported);
        EXPECT_EQ(fixture.live,0u); EXPECT_TRUE(fixture.storage.empty()); fixture.*flag=false;
    }
    ASSERT_TRUE(backend.open(audioConfig())); ASSERT_TRUE(backend.close());
}
TEST(AsioAudioBuffers, NumericOverflowAndMismatchedTablesFailBeforeAllocation) {
    AudioBuffers audio;
    Request request{driverId,Flow::output,0,1,48000,std::numeric_limits<std::uint32_t>::max(),Sample::f32};
    const std::vector<ChannelLayout> one{*channelLayout(ASIOSTFloat32LSB)};
    EXPECT_EQ(audio.prepare(request,{},one),Failure::buffer);
    request.frames=256; request.outputs=std::numeric_limits<std::uint32_t>::max();
    EXPECT_EQ(audio.prepare(request,{},{}),Failure::channels);
    request.outputs=1; EXPECT_EQ(audio.prepare(request,{},{}),Failure::channels);
    EXPECT_TRUE(audio.native().empty()); EXPECT_TRUE(audio.inputChannels().empty()); EXPECT_TRUE(audio.outputChannels().empty());
}
TEST(AsioAudioBuffers, InvalidLayoutsCannotProduceZeroOrIncorrectByteStrides) {
    AudioBuffers audio; const Request request{driverId,Flow::output,0,1,48000,256,Sample::f32};
    for (const auto& layout : {ChannelLayout{Sample::f32,0,false},ChannelLayout{Sample::f32,8,false},
        ChannelLayout{Sample::f32,4,false,true},ChannelLayout{Sample::i16,2,false}})
        EXPECT_EQ(audio.prepare(request,{},std::vector<ChannelLayout>{layout}),Failure::format);
    EXPECT_TRUE(audio.native().empty());
}
TEST(AsioAudioBuffers, SdkCallbacksDuringCreationNeverEnterTheUserProcessFunction) {
    DriverFixture fixture; ASIO backend; fixture.fireColdCallbacks=true; std::atomic<unsigned> calls{0};
    ASSERT_TRUE(backend.setProcessFunction(+[](void* user,const AudioProcessContext&) noexcept {
        ++*static_cast<std::atomic<unsigned>*>(user);
    },&calls));
    ASSERT_TRUE(backend.open(audioConfig())); EXPECT_EQ(calls.load(),0u);
    fixture.callbacks.bufferSwitch(0,ASIOTrue); fixture.callbacks.bufferSwitch(1,ASIOFalse);
    EXPECT_EQ(calls.load(),0u); ASSERT_TRUE(backend.close());
}
TEST(AsioAudioBuffers, CapabilityDiscoveryNeverCreatesAudioBuffers) {
    DriverFixture fixture; ASIO backend;
    ASSERT_EQ(backend.getEndPoints().size(),1u);
    EXPECT_EQ(fixture.bufferCreations,0u); EXPECT_EQ(fixture.bufferDisposals,0u); EXPECT_EQ(fixture.live,0u);
}
TEST(AsioAudioBuffers, DisposeFailureStillReleasesTheInterfaceAndRestoresTheClock) {
    DriverFixture fixture; ASIO backend; fixture.currentRate=44100;
    ASSERT_TRUE(backend.open(audioConfig())); fixture.disposeResult=ASE_HWMalfunction;
    EXPECT_EQ(backend.close().error(),ErrorType::EndpointUnavailable);
    EXPECT_EQ(fixture.live,0u); EXPECT_TRUE(fixture.storage.empty()); EXPECT_DOUBLE_EQ(fixture.currentRate,44100);
    EXPECT_EQ(fixture.bufferDisposals,1u); ASSERT_TRUE(backend.close()); EXPECT_EQ(fixture.bufferDisposals,1u);
}
TEST(AsioAudioBuffers, DescriptorAndScratchFrameCountsRemainExactlyTheRequestedSize) {
    DriverFixture fixture; ASIO backend;
    for (const auto frames : {64u,128u,256u}) {
        auto cfg=audioConfig(); cfg.bufferSize=frames; ASSERT_TRUE(backend.open(cfg));
        EXPECT_EQ(fixture.createdFrames,static_cast<long>(frames)); EXPECT_EQ(activeDriver->audioBuffers().frames(),frames);
        EXPECT_EQ(activeDriver->audioBuffers().outputChannels()[1]-activeDriver->audioBuffers().outputChannels()[0],frames);
        ASSERT_TRUE(backend.close());
    }
}
}
