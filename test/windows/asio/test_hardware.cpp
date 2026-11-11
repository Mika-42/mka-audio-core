#include <gtest/gtest.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <expected>
#include <thread>
#include <tuple>
#include "../../../src/backend/windows/detail/asio_driver.hpp"
import mka.audio.backend.asio;
#include "hardware_helpers.hpp"

namespace {
using namespace asio_hardware;
struct HardwareProbe {
    std::uint32_t inputs,outputs,expectedFrames;
    std::atomic<std::uint64_t> frames{0};std::atomic<bool> invalid{false};
    std::array<std::chrono::steady_clock::time_point,256> arrivals{};std::size_t count=0;
    void process(const AudioProcessContext& context) noexcept {
        if (context.input.count!=inputs || context.output.count!=outputs || context.frames!=expectedFrames) {invalid=true;return;}
        if (count<arrivals.size()) arrivals[count++]=std::chrono::steady_clock::now();
        frames.fetch_add(context.frames);
    }
};
TEST(AsioHardwareDiscovery, RegisteredDriversOrAnExplicitUnavailableSkip) {
    if (!nativeWindows()) GTEST_SKIP()<<"Windows natif requis ; Wine ne valide pas le materiel ASIO.";
    ASIO backend;const auto endpoints=backend.getEndPoints();
    if (endpoints.empty()) GTEST_SKIP()<<"Aucun pilote ASIO enregistre sur cet hote Windows.";
    for (const auto& endpoint : endpoints) {
        EXPECT_EQ(endpoint.id.find("asio:{"),0u);EXPECT_FALSE(endpoint.name.empty());
        std::printf("Pilote ASIO : %s | %s\n",endpoint.id.c_str(),endpoint.name.c_str());
    }
}
class AsioHardware : public ::testing::TestWithParam<std::tuple<Direction,std::uint32_t>> {};
TEST_P(AsioHardware, ExactNativeStreamsProgressAndReportDriverLatencyAndCadence) {
    if (!nativeWindows()) GTEST_SKIP()<<"Windows natif requis ; Wine ne valide pas le materiel ASIO.";
    const auto [direction,frames]=GetParam();const auto selected=configuration(direction,frames);
    if (!selected) GTEST_SKIP()<<selected.error();
    describe(*selected);const auto& config=selected->config;
    HardwareProbe probe{config.inputChannels,config.outputChannels,config.bufferSize};ASIO backend;
    ASSERT_TRUE(backend.setProcessFunction(+[](void* user,const AudioProcessContext& context) noexcept {
        static_cast<HardwareProbe*>(user)->process(context);
    },&probe));
    ASSERT_TRUE(open(backend,config));ASSERT_TRUE(backend.start());
    const bool progressed=wait(backend,[&]{return probe.invalid.load() || probe.frames.load()>=config.sampleRate/4;},std::chrono::seconds(5));
    const auto status=backend.status();ASSERT_TRUE(backend.stop());
    ASSERT_NE(mka_asio_detail::activeDriver,nullptr);
    EXPECT_EQ(mka_asio_detail::activeDriver->audioBuffers().frames(),config.bufferSize);
    EXPECT_EQ(mka_asio_detail::activeDriver->inputLayouts().size(),config.inputChannels);
    EXPECT_EQ(mka_asio_detail::activeDriver->outputLayouts().size(),config.outputChannels);
    EXPECT_EQ(mka_asio_detail::activeDriver->sampleRates(),(std::vector<std::uint32_t>{config.sampleRate}));
    if (const auto latency=mka_asio_detail::activeDriver->latencies())
        std::printf("Latence annoncee par le pilote : entree=%u sortie=%u frames\n",latency->input,latency->output);
    else std::puts("Latence annoncee par le pilote : indisponible");
    ASSERT_TRUE(backend.close());
    if (probe.count>1) {
        double minimum=1e30,maximum=0,total=0;
        for (std::size_t index=1;index<probe.count;++index) {
            const auto ms=std::chrono::duration<double,std::milli>(probe.arrivals[index]-probe.arrivals[index-1]).count();
            minimum=std::min(minimum,ms);maximum=std::max(maximum,ms);total+=ms;
        }
        std::printf("Cadence callbacks mesuree : min=%.3f moyenne=%.3f max=%.3f ms ; %llu frames, %llu xruns\n",
            minimum,total/(probe.count-1),maximum,static_cast<unsigned long long>(probe.frames.load()),
            static_cast<unsigned long long>(status.xruns));
    }
    EXPECT_FALSE(probe.invalid.load());EXPECT_FALSE(status.failed);EXPECT_EQ(status.xruns,0u);EXPECT_TRUE(progressed);
}
INSTANTIATE_TEST_SUITE_P(ModesAndBuffers,AsioHardware,
    ::testing::Combine(::testing::Values(Direction::Input,Direction::Output,Direction::Duplex),::testing::Values(64u,128u,256u)));
}
