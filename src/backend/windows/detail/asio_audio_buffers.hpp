#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>
#include "asio_channel_layout.hpp"

namespace mka_asio_detail {
class AudioBuffers {
public:
    AudioBuffers() = default;
    AudioBuffers(const AudioBuffers&) = delete;
    AudioBuffers& operator=(const AudioBuffers&) = delete;
    AudioBuffers(AudioBuffers&&) = delete;
    AudioBuffers& operator=(AudioBuffers&&) = delete;
    Failure prepare(const Request& request, const std::vector<ChannelLayout>& inputs,
                    const std::vector<ChannelLayout>& outputs) {
        const auto channels = std::uint64_t(request.inputs) + request.outputs;
        if (!channels || channels > std::numeric_limits<long>::max()
            || inputs.size() != request.inputs || outputs.size() != request.outputs) return Failure::channels;
        if (!request.frames || request.frames > std::numeric_limits<long>::max()) return Failure::buffer;
        for (const auto count : {request.inputs, request.outputs})
            if (count && (request.frames > std::numeric_limits<std::uint32_t>::max() / sizeof(float) / count
                || request.frames > std::numeric_limits<std::size_t>::max() / sizeof(float) / count)) return Failure::buffer;
        auto valid = [&](const ChannelLayout& layout) {
            if (layout.sample != request.sample) return false;
            switch (layout.sample) {
                case Sample::i16: return layout.bytes == 2 && !layout.low24In32;
                case Sample::i24: return layout.low24In32 ? layout.bytes == 4 : layout.bytes == 3;
                case Sample::i32: case Sample::f32: return layout.bytes == 4 && !layout.low24In32;
                case Sample::f64: return layout.bytes == 8 && !layout.low24In32;
            }
            return false;
        };
        for (const auto& group : {&inputs, &outputs}) for (const auto& layout : *group)
            if (!valid(layout)) return Failure::format;
        frames_ = request.frames; inputs_ = request.inputs; outputs_ = request.outputs;
        layouts_ = inputs; layouts_.insert(layouts_.end(), outputs.begin(), outputs.end());
        infos_.resize(static_cast<std::size_t>(channels));
        inputScratch_.assign(static_cast<std::size_t>(inputs_) * frames_, 0.0f);
        outputScratch_.assign(static_cast<std::size_t>(outputs_) * frames_, 0.0f);
        inputChannels_.resize(inputs_); outputChannels_.resize(outputs_);
        for (std::uint32_t channel = 0; channel < inputs_; ++channel) {
            infos_[channel] = {ASIOTrue, static_cast<long>(channel), {nullptr, nullptr}};
            inputChannels_[channel] = inputScratch_.data() + static_cast<std::size_t>(channel) * frames_;
        }
        for (std::uint32_t channel = 0; channel < outputs_; ++channel) {
            infos_[static_cast<std::size_t>(inputs_) + channel] = {ASIOFalse, static_cast<long>(channel), {nullptr, nullptr}};
            outputChannels_[channel] = outputScratch_.data() + static_cast<std::size_t>(channel) * frames_;
        }
        return Failure::none;
    }
    Failure validateNative() const {
        struct Range { std::uintptr_t begin, end; };
        std::vector<Range> ranges;
        ranges.reserve(infos_.size() * 2);
        for (std::size_t index = 0; index < infos_.size(); ++index) {
            const auto& info = infos_[index];
            const bool input = index < inputs_;
            if (info.isInput != (input ? ASIOTrue : ASIOFalse)
                || info.channelNum != static_cast<long>(input ? index : index - inputs_)) return Failure::buffer;
            if (frames_ > std::numeric_limits<std::size_t>::max() / layouts_[index].bytes) return Failure::buffer;
            const auto bytes = static_cast<std::size_t>(frames_) * layouts_[index].bytes;
            for (const auto buffer : info.buffers) {
                const auto begin = reinterpret_cast<std::uintptr_t>(buffer);
                if (!buffer || bytes > std::numeric_limits<std::uintptr_t>::max() - begin) return Failure::buffer;
                ranges.push_back({begin, begin + bytes});
            }
        }
        std::sort(ranges.begin(), ranges.end(), [](auto a, auto b) { return a.begin < b.begin; });
        for (std::size_t index = 1; index < ranges.size(); ++index)
            if (ranges[index].begin < ranges[index-1].end) return Failure::buffer;
        return Failure::none;
    }
    void primeOutputs() noexcept {
        for (std::size_t index = inputs_; index < infos_.size(); ++index)
            for (auto buffer : infos_[index].buffers)
                std::memset(buffer, 0, static_cast<std::size_t>(frames_) * layouts_[index].bytes);
        std::fill(outputScratch_.begin(), outputScratch_.end(), 0.0f);
    }
    std::uint32_t frames() const noexcept { return frames_; }
    std::uint32_t inputs() const noexcept { return inputs_; }
    std::uint32_t outputs() const noexcept { return outputs_; }
    std::vector<ASIOBufferInfo>& native() noexcept { return infos_; }
    const std::vector<ASIOBufferInfo>& native() const noexcept { return infos_; }
    const std::vector<ChannelLayout>& layouts() const noexcept { return layouts_; }
    const std::vector<const float*>& inputChannels() const noexcept { return inputChannels_; }
    float* inputData(std::uint32_t channel) noexcept { return inputScratch_.data() + static_cast<std::size_t>(channel) * frames_; }
    const std::vector<float*>& outputChannels() const noexcept { return outputChannels_; }
private:
    std::uint32_t frames_ = 0, inputs_ = 0, outputs_ = 0;
    std::vector<ASIOBufferInfo> infos_;
    std::vector<ChannelLayout> layouts_;
    std::vector<float> inputScratch_, outputScratch_;
    std::vector<const float*> inputChannels_;
    std::vector<float*> outputChannels_;
};
inline void coldRateChanged(ASIOSampleRate) noexcept {}
}
