#pragma once
// Include before named-module imports. All callback storage is allocated by
// the control thread; callbacks only access borrowed planar Float32 buffers.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace manual_audio {
struct Tone {
    const std::uint32_t rate;
    const std::size_t signalFrames, total;
    std::atomic<std::size_t> frames{0};
    std::atomic<bool> invalidContext{false};
    double phase = 0;

    explicit Tone(std::uint32_t sampleRate)
        : rate(sampleRate), signalFrames(std::size_t(rate) * 3), total(signalFrames + rate / 2) {
        if (!rate) throw std::invalid_argument("Zero tone sample rate");
    }
    template<class Context>
    void process(const Context& ctx) noexcept {
        if (!ctx.frames || !ctx.output.count || !ctx.output.channels) {
            invalidContext = true;
            return;
        }
        for (std::uint32_t ch = 0; ch < ctx.output.count; ++ch)
            if (!ctx.output.channels[ch]) { invalidContext = true; return; }
        const auto offset = frames.load(std::memory_order_relaxed);
        const auto count = std::min<std::size_t>(ctx.frames, total - std::min(offset, total));
        const auto fade = std::max<std::size_t>(1, rate / 50);
        for (std::size_t i = 0; i < ctx.frames; ++i) {
            float value = 0;
            if (i < count && offset + i < signalFrames) {
                const auto position = offset + i;
                const auto edge = std::min({position, signalFrames - 1 - position, fade});
                value = static_cast<float>(0.2 * std::sin(phase) * double(edge) / double(fade));
                phase += 2 * std::numbers::pi * 440 / rate;
                if (phase >= 2 * std::numbers::pi) phase -= 2 * std::numbers::pi;
            }
            for (std::uint32_t ch = 0; ch < ctx.output.count; ++ch)
                ctx.output.channels[ch][i] = value;
        }
        frames.store(offset + count, std::memory_order_release);
    }
};

struct Level {
    double mean = 0, rms = 0;
    float peak = 0;
    bool finite = true;
};

struct Clip {
    const std::uint32_t rate;
    const std::size_t capacity;
    std::vector<std::vector<float>> samples;
    std::size_t warmup;
    std::atomic<std::size_t> recorded{0};
    std::atomic<bool> invalidContext{false};

    Clip(std::uint32_t sampleRate, std::uint32_t channels, std::size_t frameCount)
        : rate(sampleRate), capacity(frameCount), samples(channels, std::vector<float>(capacity)),
          warmup(rate / 5) {
        if (!rate || !channels || !capacity) throw std::invalid_argument("Empty audio capture");
    }
    template<class Context>
    void record(const Context& ctx) noexcept {
        if (!ctx.frames || ctx.input.count != samples.size() || !ctx.input.channels) {
            invalidContext = true;
            return;
        }
        for (std::size_t ch = 0; ch < samples.size(); ++ch)
            if (!ctx.input.channels[ch]) { invalidContext = true; return; }
        const auto skip = std::min<std::size_t>(warmup, ctx.frames);
        warmup -= skip;
        const auto offset = recorded.load(std::memory_order_relaxed);
        const auto count = std::min<std::size_t>(ctx.frames - skip, capacity - std::min(offset, capacity));
        for (std::size_t ch = 0; ch < samples.size(); ++ch)
            std::copy_n(ctx.input.channels[ch] + skip, count, samples[ch].data() + offset);
        recorded.store(offset + count, std::memory_order_release);
    }
    // Only call after stop() has joined the capture callback. Raw samples remain
    // intact for the WAV; DC is removed from statistics and the replay only.
    std::vector<Level> levels() const {
        std::vector<Level> result(samples.size());
        const auto count = std::min(recorded.load(std::memory_order_acquire), capacity);
        if (!count) return result;
        for (std::size_t ch = 0; ch < samples.size(); ++ch) {
            auto& level = result[ch];
            for (std::size_t i = 0; i < count; ++i) {
                level.finite = level.finite && std::isfinite(samples[ch][i]);
                level.mean += samples[ch][i];
            }
            if (!level.finite) continue;
            level.mean /= double(count);
            double energy = 0;
            for (std::size_t i = 0; i < count; ++i) {
                const auto value = double(samples[ch][i]) - level.mean;
                level.peak = std::max(level.peak, static_cast<float>(std::abs(value)));
                energy += value * value;
            }
            level.rms = std::sqrt(energy / double(count));
        }
        return result;
    }
    static std::size_t strongest(const std::vector<Level>& levels) {
        return static_cast<std::size_t>(std::max_element(levels.begin(), levels.end(),
            [](const auto& a, const auto& b) { return a.rms < b.rms; }) - levels.begin());
    }
    bool saveWav(const char* path) const {
        const auto count = std::min(recorded.load(std::memory_order_acquire), capacity);
        const auto channels = samples.size();
        const auto bytes = count * channels * 2;
        if (bytes > std::numeric_limits<std::uint32_t>::max() - 36 || channels > 32) return false;
        std::ofstream file(path, std::ios::binary);
        auto integer = [&](std::uint32_t value, unsigned width) {
            for (unsigned i = 0; i < width; ++i) file.put(static_cast<char>((value >> (8 * i)) & 255));
        };
        file.write("RIFF", 4); integer(static_cast<std::uint32_t>(36 + bytes), 4);
        file.write("WAVEfmt ", 8); integer(16, 4); integer(1, 2);
        integer(static_cast<std::uint32_t>(channels), 2); integer(rate, 4);
        integer(static_cast<std::uint32_t>(rate * channels * 2), 4);
        integer(static_cast<std::uint32_t>(channels * 2), 2); integer(16, 2);
        file.write("data", 4); integer(static_cast<std::uint32_t>(bytes), 4);
        for (std::size_t i = 0; i < count; ++i)
            for (const auto& channel : samples) {
                if (!std::isfinite(channel[i])) return false;
                const auto value = static_cast<std::int16_t>(std::clamp(channel[i], -1.0f, 1.0f) * 32767);
                integer(static_cast<std::uint16_t>(value), 2);
            }
        file.close();
        return !file.fail();
    }
};

struct Replay {
    const std::uint32_t rate;
    const std::size_t sourceChannel, lead, tail;
    const float gain;
    std::vector<float> mono;
    std::atomic<std::size_t> played{0};
    std::atomic<bool> invalidContext{false};

    Replay(const Clip& clip, std::uint32_t outputRate, const std::vector<Level>& levels)
        : rate(outputRate), sourceChannel(Clip::strongest(levels)), lead(rate / 10), tail(rate / 2),
          gain(levels[sourceChannel].peak > 0 ? std::min(32.0f, 0.5f / levels[sourceChannel].peak) : 1.0f) {
        if (!rate) throw std::invalid_argument("Zero replay sample rate");
        const auto count = std::min(clip.recorded.load(std::memory_order_acquire), clip.capacity);
        if (!count) throw std::invalid_argument("Empty replay");
        mono.resize(static_cast<std::size_t>(std::llround(double(count) * rate / clip.rate)));
        // Rate conversion belongs to this manual test's offline preparation,
        // never to the backend. Input and output retain their exact native mixes.
        for (std::size_t i = 0; i < mono.size(); ++i) {
            const double position = double(i) * clip.rate / rate;
            const auto first = std::min(static_cast<std::size_t>(position), count - 1);
            const auto next = std::min(first + 1, count - 1);
            const auto fraction = position - double(first);
            const auto value = (1 - fraction) * clip.samples[sourceChannel][first]
                + fraction * clip.samples[sourceChannel][next] - levels[sourceChannel].mean;
            mono[i] = static_cast<float>(value * gain);
        }
        const auto fade = std::min<std::size_t>(rate / 50, mono.size() / 2);
        for (std::size_t i = 0; i < fade; ++i) {
            const auto edge = static_cast<float>(0.5 * (1 - std::cos(std::numbers::pi * double(i) / double(fade))));
            mono[i] *= edge;
            mono[mono.size() - 1 - i] *= edge;
        }
    }
    std::size_t total() const { return lead + mono.size() + tail; }
    template<class Context>
    void process(const Context& ctx) noexcept {
        if (!ctx.frames || !ctx.output.count || !ctx.output.channels) {
            invalidContext = true;
            return;
        }
        for (std::uint32_t ch = 0; ch < ctx.output.count; ++ch)
            if (!ctx.output.channels[ch]) { invalidContext = true; return; }
        const auto offset = played.load(std::memory_order_relaxed);
        for (std::size_t i = 0; i < ctx.frames; ++i) {
            const auto position = offset + i;
            const auto value = position >= lead && position - lead < mono.size() ? mono[position - lead] : 0.0f;
            for (std::uint32_t ch = 0; ch < ctx.output.count; ++ch) ctx.output.channels[ch][i] = value;
        }
        played.store(offset + ctx.frames, std::memory_order_release);
    }
};
}
