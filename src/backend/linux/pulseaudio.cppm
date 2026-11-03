
module;
#include <pulse/pulseaudio.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <chrono>
#include <thread>
#include <expected>
#include <string>
#include <vector>

export module mka.audio.backend.pulseaudio;

import mka.audio.backend.abstract;
import mka.audio.error;
import mka.audio.endpoint;
import mka.audio.constants;
import mka.audio.process;

namespace mka::audio::core {

    // Backend PulseAudio : "best effort", NON temps réel.
    //  - Le callback audio s'exécute sur le thread du pa_threaded_mainloop, qui
    //    n'est pas en SCHED_FIFO (le statut realtime reste donc Unknown).
    //  - Il s'exécute sous le verrou du mainloop, partagé avec les opérations de
    //    contrôle (start/stop/close) : celles-ci peuvent bloquer le callback.
    //  - libpulse peut allouer en interne (pool de memblocks) dans
    //    pa_stream_begin_write / pa_stream_drop.
    // Pour du temps réel strict, utiliser JACK ou PipeWire.
    export class PulseAudio final : public Backend {
        public:
            PulseAudio() noexcept = default;

            ~PulseAudio() override {
                stopDispatcher();
                teardown();
            }

        protected:
            [[nodiscard]] std::vector<Endpoint> getEndPoints_() const override;
            [[nodiscard]] Result open_(EndpointConfig const &endpointCfg) override;
            [[nodiscard]] Result start_() override;
            [[nodiscard]] Result stop_() override;
            [[nodiscard]] Result close_() override;

        private:
            static void onContextState(pa_context *c, void *userdata);
            static void onStreamState(pa_stream *s, void *userdata);
            static void onStreamWrite(pa_stream *s, std::size_t requestedBytes, void *userdata);
            static void onStreamRead(pa_stream *s, std::size_t nbytesAvail, void *userdata);
            static void onStreamXRun(pa_stream *s, void *userdata);
            void disconnectLocked() noexcept;
            [[nodiscard]] Result connectStream();
            [[nodiscard]] Result setCorked(bool corked);

            void teardown() noexcept;

            pa_threaded_mainloop *loop_ = nullptr;
            bool threadStarted_ = false;

            pa_context *context_ = nullptr;
            pa_stream *stream_ = nullptr;

            Direction direction_ = Direction::Output;
            std::string targetName_;
            pa_sample_spec spec_{};
            std::uint32_t channels_ = 0;
            std::uint32_t bufferSize_ = 0;

            std::vector<std::vector<float>> scratch_;
            std::vector<float *> outPtrs_;
            std::vector<const float *> inPtrs_;
            std::atomic<bool> active_ = false;
    };

    // --- Thread audio -------------------------------------------------------

    void PulseAudio::onContextState(pa_context *c, void *userdata) {
        auto *self = static_cast<PulseAudio *>(userdata);
        const auto st = pa_context_get_state(c);
        if (self->active_.load(std::memory_order_acquire)
            && (st == PA_CONTEXT_FAILED || st == PA_CONTEXT_TERMINATED)) {
            self->notifyFailed();
            }
        pa_threaded_mainloop_signal(self->loop_, 0);
    }

    void PulseAudio::onStreamState(pa_stream *s, void *userdata) {
        auto *self = static_cast<PulseAudio *>(userdata);
        const auto st = pa_stream_get_state(s);
        if (self->active_.load(std::memory_order_acquire)
            && (st == PA_STREAM_FAILED || st == PA_STREAM_TERMINATED)) {
            self->notifyFailed();
            }
        pa_threaded_mainloop_signal(self->loop_, 0);
    }

    void PulseAudio::onStreamXRun(pa_stream *, void *userdata) {
        auto *self = static_cast<PulseAudio *>(userdata);
        if (self->active_.load(std::memory_order_acquire)) self->notifyXRun();
    }

    void PulseAudio::onStreamWrite(pa_stream *s, const std::size_t requestedBytes, void *userdata) {
        auto *self = static_cast<PulseAudio *>(userdata);
        const auto bytesPerFrame = static_cast<std::size_t>(self->channels_) * sizeof(float);
        if (bytesPerFrame == 0) return;

        std::size_t remaining = requestedBytes;
        while (remaining >= bytesPerFrame) {
            const std::size_t suggested = std::min<std::size_t>(
                remaining, static_cast<std::size_t>(self->bufferSize_) * bytesPerFrame);

            void *data = nullptr;
            std::size_t nbytes = suggested;
            if (pa_stream_begin_write(s, &data, &nbytes) < 0 || !data) return;

            // Clamp défensif : scratch_ contient exactement bufferSize_ échantillons par canal.
            const std::size_t frames = std::min<std::size_t>(nbytes / bytesPerFrame, self->bufferSize_);
            if (frames == 0) {
                // Évite une boucle infinie sous le verrou du mainloop.
                pa_stream_cancel_write(s);
                return;
            }
            for (auto &ch : self->scratch_) std::fill_n(ch.data(), frames, 0.0f);

            if (self->active_.load(std::memory_order_acquire) && self->callback) {
                const AudioProcessContext ctx{
                    .input = { nullptr, 0 },
                    .output = { self->outPtrs_.data(), self->channels_ },
                    .frames = static_cast<std::uint32_t>(frames)
                };
                self->callback(self->userData, ctx);
            }

            auto *out = static_cast<float *>(data);
            for (std::size_t i = 0; i < frames; ++i) {
                for (std::uint32_t ch = 0; ch < self->channels_; ++ch) {
                    out[i * self->channels_ + ch] = self->scratch_[ch][i];
                }
            }

            // N'envoie que des trames complètes (pas d'octets de queue non initialisés).
            nbytes = frames * bytesPerFrame;
            if (pa_stream_write(s, data, nbytes, nullptr, 0, PA_SEEK_RELATIVE) < 0) {
                if (self->active_.load(std::memory_order_acquire)) self->notifyXRun();
                return;
            }
            remaining -= std::min(remaining, nbytes);
        }
    }

    void PulseAudio::onStreamRead(pa_stream *s, std::size_t /*nbytesAvail*/, void *userdata) {
        auto *self = static_cast<PulseAudio *>(userdata);
        const auto bytesPerFrame = static_cast<std::size_t>(self->channels_) * sizeof(float);
        if (bytesPerFrame == 0) return;

        for (;;) {
            const void *data = nullptr;
            std::size_t nbytes = 0;
            if (pa_stream_peek(s, &data, &nbytes) < 0) return;
            if (nbytes == 0) return;

            if (!data) {
                // Trou dans le flux : données perdues côté serveur.
                if (self->active_.load(std::memory_order_acquire)) self->notifyXRun();
                pa_stream_drop(s);
                continue;
            }

            const auto *in = static_cast<const float *>(data);
            std::size_t framesLeft = nbytes / bytesPerFrame;
            std::size_t offset = 0;

            while (framesLeft > 0) {
                const std::size_t frames = std::min<std::size_t>(framesLeft, self->bufferSize_);

                for (std::size_t i = 0; i < frames; ++i) {
                    for (std::uint32_t ch = 0; ch < self->channels_; ++ch) {
                        self->scratch_[ch][i] = in[(offset + i) * self->channels_ + ch];
                    }
                }

                if (self->active_.load(std::memory_order_acquire) && self->callback) {
                    const AudioProcessContext ctx{
                        .input = { self->inPtrs_.data(), self->channels_ },
                        .output = { nullptr, 0 },
                        .frames = static_cast<std::uint32_t>(frames)
                    };
                    self->callback(self->userData, ctx);
                }

                offset += frames;
                framesLeft -= frames;
            }

            pa_stream_drop(s);
        }
    }

    // --- open_ / start_ / stop_ / close_ -----------------------------------

    Result PulseAudio::open_(EndpointConfig const &endpointCfg) {
        if (endpointCfg.direction == Direction::Duplex) {
            return std::unexpected{ ErrorType::ConfigurationFailed };
        }

        const auto contains = [](auto const &arr, auto value) {
            return std::find(arr.begin(), arr.end(), value) != arr.end();
        };
        if (!contains(supportedSampleRates, endpointCfg.sampleRate))
            return std::unexpected{ ErrorType::SampleRateNotSupported };
        if (!contains(supportedBufferSizes, endpointCfg.bufferSize))
            return std::unexpected{ ErrorType::BufferSizeNotSupported };

        if (endpointCfg.format != Format::Float32)
            return std::unexpected{ ErrorType::FormatNotSupported };

        const std::uint32_t channels = endpointCfg.direction == Direction::Input
            ? endpointCfg.inputChannels
            : endpointCfg.outputChannels;
        if (channels == 0 || channels > PA_CHANNELS_MAX)
            return std::unexpected{ ErrorType::ChannelsNotSupported };

        if (!endpointCfg.id.empty()) {
            const auto endpoints = getEndPoints_();
            const auto it = std::ranges::find_if(endpoints, [&](Endpoint const &e) {
                return e.id == endpointCfg.id;
            });
            const bool hasCapability = it != endpoints.end() && (
                endpointCfg.direction == Direction::Input ? it->input.has_value() : it->output.has_value());
            if (!hasCapability)
                return std::unexpected{ ErrorType::EndpointUnavailable };
        }

        direction_ = endpointCfg.direction;
        targetName_ = endpointCfg.id;
        channels_ = channels;
        bufferSize_ = endpointCfg.bufferSize;

        spec_ = pa_sample_spec{};
        spec_.format = PA_SAMPLE_FLOAT32NE;
        spec_.rate = endpointCfg.sampleRate;
        spec_.channels = static_cast<std::uint8_t>(channels_);
        if (!pa_sample_spec_valid(&spec_))
            return std::unexpected{ ErrorType::ConfigurationFailed };

        scratch_.assign(channels_, std::vector<float>(bufferSize_, 0.0f));
        outPtrs_.assign(channels_, nullptr);
        inPtrs_.assign(channels_, nullptr);
        for (std::uint32_t i = 0; i < channels_; ++i) {
            outPtrs_[i] = scratch_[i].data();
            inPtrs_[i] = scratch_[i].data();
        }

        loop_ = pa_threaded_mainloop_new();
        if (!loop_) return std::unexpected{ ErrorType::ConfigurationFailed };

        const auto result = connectStream();
        if (!result) teardown();
        return result;
    }

    void PulseAudio::disconnectLocked() noexcept {
        active_.store(false, std::memory_order_release);
        
        if (stream_) {
            pa_stream_set_write_callback(stream_, nullptr, nullptr);
            pa_stream_set_read_callback(stream_, nullptr, nullptr);
            pa_stream_disconnect(stream_);
            pa_stream_unref(stream_);
            stream_ = nullptr;
        }
        if (context_) {
            pa_context_disconnect(context_);
            pa_context_unref(context_);
            context_ = nullptr;
        }
    }

    namespace {
        // Called with the mainloop lock held. Polling is confined to control code;
        // callbacks continue on the native loop while the lock is released.
        template<class Predicate>
        bool waitPulse(pa_threaded_mainloop* loop, Predicate ready) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!ready()) {
                if (std::chrono::steady_clock::now() >= deadline) return false;
                pa_threaded_mainloop_unlock(loop);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                pa_threaded_mainloop_lock(loop);
            }
            return true;
        }
    }

    Result PulseAudio::connectStream() {
        if (!loop_) return std::unexpected{ ErrorType::ConfigurationFailed };

        pa_threaded_mainloop_lock(loop_);

        disconnectLocked();

        if (!threadStarted_) {
            if (pa_threaded_mainloop_start(loop_) < 0) {
                pa_threaded_mainloop_unlock(loop_);
                return std::unexpected{ ErrorType::ConfigurationFailed };
            }
            threadStarted_ = true;
        }

        const auto fail = [this](const ErrorType error) -> Result {
            disconnectLocked();
            pa_threaded_mainloop_unlock(loop_);
            return std::unexpected{ error };
        };

        context_ = pa_context_new(pa_threaded_mainloop_get_api(loop_), "mka-audio");
        if (!context_) return fail(ErrorType::ConfigurationFailed);
        pa_context_set_state_callback(context_, &PulseAudio::onContextState, this);

        if (pa_context_connect(context_, nullptr, PA_CONTEXT_NOFLAGS, nullptr) < 0)
            return fail(ErrorType::EndpointUnavailable);

        if (!waitPulse(loop_, [&] { return pa_context_get_state(context_) == PA_CONTEXT_READY
                                        || !PA_CONTEXT_IS_GOOD(pa_context_get_state(context_)); })
            || pa_context_get_state(context_) != PA_CONTEXT_READY)
            return fail(ErrorType::EndpointUnavailable);

        stream_ = pa_stream_new(context_, "mka-audio-stream", &spec_, nullptr);
        if (!stream_) return fail(ErrorType::ConfigurationFailed);
        pa_stream_set_state_callback(stream_, &PulseAudio::onStreamState, this);
        pa_stream_set_underflow_callback(stream_, &PulseAudio::onStreamXRun, this);
        pa_stream_set_overflow_callback(stream_, &PulseAudio::onStreamXRun, this);


        const auto bytesPerFrame = static_cast<std::uint32_t>(channels_ * sizeof(float));
        pa_buffer_attr attr{};
        attr.maxlength = static_cast<std::uint32_t>(-1);
        attr.prebuf = static_cast<std::uint32_t>(-1);
        attr.minreq = static_cast<std::uint32_t>(-1);
        attr.tlength = static_cast<std::uint32_t>(-1);
        attr.fragsize = static_cast<std::uint32_t>(-1);

        const char *target = targetName_.empty() ? nullptr : targetName_.c_str();
        if (direction_ == Direction::Input) {
            attr.fragsize = bufferSize_ * bytesPerFrame;
            pa_stream_set_read_callback(stream_, &PulseAudio::onStreamRead, this);
            if (pa_stream_connect_record(stream_, target, &attr, static_cast<pa_stream_flags_t>(PA_STREAM_ADJUST_LATENCY | PA_STREAM_START_CORKED)) < 0)
                return fail(ErrorType::EndpointUnavailable);
        } else {
            attr.tlength = bufferSize_ * bytesPerFrame;
            pa_stream_set_write_callback(stream_, &PulseAudio::onStreamWrite, this);
            if (pa_stream_connect_playback(stream_, target, &attr, static_cast<pa_stream_flags_t>(PA_STREAM_ADJUST_LATENCY | PA_STREAM_START_CORKED), nullptr, nullptr) < 0)
                return fail(ErrorType::EndpointUnavailable);
        }

        if (!waitPulse(loop_, [&] { return pa_stream_get_state(stream_) == PA_STREAM_READY
                                        || !PA_STREAM_IS_GOOD(pa_stream_get_state(stream_)); })
            || pa_stream_get_state(stream_) != PA_STREAM_READY)
            return fail(ErrorType::EndpointUnavailable);

        const auto* actual = pa_stream_get_sample_spec(stream_);
        if (!actual) return fail(ErrorType::ConfigurationFailed);
        if (actual->format != spec_.format) return fail(ErrorType::FormatNotSupported);
        if (actual->rate != spec_.rate) return fail(ErrorType::SampleRateNotSupported);
        if (actual->channels != spec_.channels) return fail(ErrorType::ChannelsNotSupported);
        if (!PA_CONTEXT_IS_GOOD(pa_context_get_state(context_)))
            return fail(ErrorType::EndpointUnavailable);

        // Keep the verified native stream corked: open() never invokes user code.
        pa_threaded_mainloop_unlock(loop_);
        return {};
    }

    Result PulseAudio::setCorked(const bool corked) {
        if (!loop_ || !threadStarted_) return std::unexpected{ErrorType::InvalidState};
        pa_threaded_mainloop_lock(loop_);
        if (!stream_ || pa_stream_get_state(stream_) != PA_STREAM_READY
            || !context_ || pa_context_get_state(context_) != PA_CONTEXT_READY) {
            active_.store(false, std::memory_order_release);
            pa_threaded_mainloop_unlock(loop_);
            return std::unexpected{ErrorType::EndpointUnavailable};
        }
        struct Completion { bool done = false; bool success = false; } completion;
        active_.store(!corked, std::memory_order_release);
        pa_operation* op = pa_stream_cork(stream_, corked, [](pa_stream*, int success, void* user) {
            auto& state = *static_cast<Completion*>(user);
            state.success = success != 0;
            state.done = true;
        }, &completion);
        const bool success = op && waitPulse(loop_, [&] {
            return completion.done || !PA_STREAM_IS_GOOD(pa_stream_get_state(stream_));
        }) && completion.done && completion.success;
        if (op) {
            if (!completion.done) pa_operation_cancel(op);
            pa_operation_unref(op);
        }
        if (!success) active_.store(false, std::memory_order_release);
        pa_threaded_mainloop_unlock(loop_);
        return success ? Result{} : std::unexpected{ErrorType::ConfigurationFailed};
    }

    Result PulseAudio::start_() {
        return setCorked(false);
    }

    Result PulseAudio::stop_() {
        if (status().failed) {
            pa_threaded_mainloop_lock(loop_);
            disconnectLocked();
            pa_threaded_mainloop_unlock(loop_);
            return {};
        }
        // Taking the native lock quiesces any in-flight callback; active_=false
        // prevents callbacks during the server's asynchronous cork acknowledgement.
        return setCorked(true);
    }

    Result PulseAudio::close_() {
        teardown();
        return {};
    }

    void PulseAudio::teardown() noexcept {
        if (!loop_) return;

        pa_threaded_mainloop_lock(loop_);
        disconnectLocked();
        pa_threaded_mainloop_unlock(loop_);

        if (threadStarted_) {
            pa_threaded_mainloop_stop(loop_);
            threadStarted_ = false;
        }
        pa_threaded_mainloop_free(loop_);
        loop_ = nullptr;
    }

    // --- getEndPoints_ ------------------------------------------------------

    namespace {
        struct SinkSourceInfo {
            std::string name;
            std::uint32_t channels = 0;
        };

        struct ScanState {
            pa_threaded_mainloop *loop = nullptr;
            std::vector<SinkSourceInfo> sinks;
            std::vector<SinkSourceInfo> sources;
        };

        void onScanContextState(pa_context * /*c*/, void *userdata) {
            auto *state = static_cast<ScanState *>(userdata);
            pa_threaded_mainloop_signal(state->loop, 0);
        }

        void onSinkInfo(pa_context * /*c*/, const pa_sink_info *info, const int eol, void *userdata) {
            auto *state = static_cast<ScanState *>(userdata);
            if (!eol && info && info->name) {
                state->sinks.push_back({ info->name, info->sample_spec.channels });
            }
            pa_threaded_mainloop_signal(state->loop, 0);
        }

        void onSourceInfo(pa_context * /*c*/, const pa_source_info *info, const int eol, void *userdata) {
            auto *state = static_cast<ScanState *>(userdata);
            if (!eol && info && info->name) {
                // Ignore les sources "monitor" (capture de la sortie d'un sink) :
                // ce ne sont pas des périphériques d'entrée physiques.
                if (info->monitor_of_sink == PA_INVALID_INDEX) {
                    state->sources.push_back({ info->name, info->sample_spec.channels });
                }
            }
            pa_threaded_mainloop_signal(state->loop, 0);
        }

        void waitOperation(pa_threaded_mainloop *loop, pa_operation *op) {
            if (!op) return;
            while (pa_operation_get_state(op) == PA_OPERATION_RUNNING) {
                pa_threaded_mainloop_wait(loop);
            }
            pa_operation_unref(op);
        }
    }

    std::vector<Endpoint> PulseAudio::getEndPoints_() const {
        try {
            pa_threaded_mainloop *scanLoop = pa_threaded_mainloop_new();
            if (!scanLoop) return {};

            if (pa_threaded_mainloop_start(scanLoop) < 0) {
                pa_threaded_mainloop_free(scanLoop);
                return {};
            }

            pa_threaded_mainloop_lock(scanLoop);

            pa_context *context = pa_context_new(pa_threaded_mainloop_get_api(scanLoop), "mka-audio-scan");
            if (!context) {
                pa_threaded_mainloop_unlock(scanLoop);
                pa_threaded_mainloop_stop(scanLoop);
                pa_threaded_mainloop_free(scanLoop);
                return {};
            }

            ScanState state;
            state.loop = scanLoop;
            pa_context_set_state_callback(context, &onScanContextState, &state);

            if (pa_context_connect(context, nullptr, PA_CONTEXT_NOFLAGS, nullptr) < 0) {
                pa_context_unref(context);
                pa_threaded_mainloop_unlock(scanLoop);
                pa_threaded_mainloop_stop(scanLoop);
                pa_threaded_mainloop_free(scanLoop);
                return {};
            }

            bool ready = false;
            for (;;) {
                const auto st = pa_context_get_state(context);
                if (st == PA_CONTEXT_READY) { ready = true; break; }
                if (!PA_CONTEXT_IS_GOOD(st)) break;
                pa_threaded_mainloop_wait(scanLoop);
            }

            if (ready) {
                waitOperation(scanLoop, pa_context_get_sink_info_list(context, &onSinkInfo, &state));
                waitOperation(scanLoop, pa_context_get_source_info_list(context, &onSourceInfo, &state));
            }

            pa_context_disconnect(context);
            pa_context_unref(context);
            pa_threaded_mainloop_unlock(scanLoop);
            pa_threaded_mainloop_stop(scanLoop);
            pa_threaded_mainloop_free(scanLoop);

            if (!ready) return {};

            std::vector<Endpoint> endpoints;
            const auto upsert = [&](const std::string &name) -> Endpoint & {
                const auto it = std::ranges::find_if(endpoints, [&](Endpoint const &e) { return e.id == name; });
                if (it != endpoints.end()) return *it;
                endpoints.push_back(Endpoint{ .id = name, .name = name });
                return endpoints.back();
            };

            for (const auto &src : state.sources) {
                upsert(src.name).input = StreamCapabilities{
                    .minChannels = 1,
                    .maxChannels = std::max<std::uint32_t>(1, src.channels),
                    .sampleRates = { supportedSampleRates.begin(), supportedSampleRates.end() },
                    .formats = { Format::Float32 },
                    .bufferSizes = { supportedBufferSizes.begin(), supportedBufferSizes.end() },
                };
            }
            for (const auto &snk : state.sinks) {
                upsert(snk.name).output = StreamCapabilities{
                    .minChannels = 1,
                    .maxChannels = std::max<std::uint32_t>(1, snk.channels),
                    .sampleRates = { supportedSampleRates.begin(), supportedSampleRates.end() },
                    .formats = { Format::Float32 },
                    .bufferSizes = { supportedBufferSizes.begin(), supportedBufferSizes.end() },
                };
            }
            return endpoints;
        } catch (...) {
            return {};
        }
    }

}
