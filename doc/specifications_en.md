# MKA Core Audio

## 1. Purpose

MKA Core Audio is a real-time audio abstraction layer designed to provide a unified interface to audio systems across different platforms and backends.

It abstracts backend-specific APIs while exposing a small, predictable and backend-independent interface to higher-level components.

Core Audio is responsible for transporting audio between the application and the underlying audio system. It does not process or interpret the audio.

## 2. Objectives

Core Audio must:

* provide a common interface across different audio backends;
* enumerate available audio devices;
* expose device capabilities;
* allow the application to select a stream configuration;
* open and manage audio streams;
* provide real-time audio buffers;
* remain independent from any specific backend API;
* provide deterministic and real-time-safe behavior.

Core Audio must remain small, stable and predictable.

## 3. Conceptual Model

Core Audio uses three main concepts:

```text
Backend
   │
   ├── Endpoints
   │
   └── Stream
        ├── Input Channels
        └── Output Channels
```

### Endpoint

A device is a selectable audio resource or endpoint exposed by a backend.

A device does not necessarily represent physical hardware. The backend maps its native concepts to the generic `Endpoint` model.

### Stream

A stream is an active audio session opened for a specific device and configuration.

The stream configuration is immutable during the lifetime of the stream.

Changing the configuration requires closing the current stream and opening a new one.

### Channel

A channel represents one audio signal transported by a stream.

Backend-specific concepts such as ALSA PCM channels, PipeWire ports, WASAPI channels or CoreAudio elements must not leak into the Core Audio public interface.

## 4. Device Discovery and Capabilities

Device discovery and stream opening are two separate operations.

The public API lives in `mka::audio::core` and is re-exported by
`mka.audio.backend.abstract`. Discovery returns endpoint identity and capabilities together:

```cpp
std::vector<Endpoint> Backend::getEndPoints() const noexcept;
```

`Endpoint` contains `std::string id`, `std::string name`, and
`std::optional<StreamCapabilities> input` / `output`. No separate capability-query
method exists. Each `StreamCapabilities` has `minChannels`, `maxChannels`,
`sampleRates`, `formats` and `bufferSizes`.

Capabilities may include:

* supported sample rates;
* supported sample formats;
* supported input/output channel counts;
* supported buffer sizes or buffer-size ranges;
* other constraints required to open a stream.

Capabilities describe what the backend can support. They do not represent a stream configuration.

## 5. Stream Configuration

The application is responsible for selecting the desired stream configuration.

For example:

```cpp
using namespace mka::audio::core;

EndpointConfig config{
    .id = "...",
    .direction = Direction::Output,
    .inputChannels = 0,
    .outputChannels = 2,
    .sampleRate = 48000,
    .format = Format::Float32,
    .bufferSize = 256
};
```

The configuration represents an explicit request made to the backend.

### Strict Configuration Rule

`open(config)` must request the specified configuration exactly.

Core Audio must not silently modify, approximate or automatically negotiate the requested configuration.

If the backend cannot provide the requested configuration, `open()` must fail with an appropriate error.

For example:

```text
Requested:
    48000 Hz
    Float32
    256 frames
    2 input channels
    2 output channels

Backend:
    Supported

Result:
    OPEN
```

If the backend cannot provide that configuration:

```text
Requested:
    48000 Hz
    Float32
    256 frames

Backend:
    Configuration unsupported

Result:
    ERROR
```

The backend must not silently replace it with, for example, `44100 Hz` or `512 frames`.

This makes stream creation deterministic and prevents higher-level components from unknowingly operating with a different audio configuration.

The application can inspect the `Endpoint` returned by `getEndPoints()` before calling `open()` to determine whether the requested configuration is supported.

The backend must still validate the configuration during `open()`, since capabilities may change between discovery and stream creation.

## 6. Stream Lifecycle

The minimum stream lifecycle is:

```text
CLOSED
   │ open()
   ▼
OPEN
   │ start()
   ▼
RUNNING
   │ stop()
   ▼
OPEN
   │ close()
   ▼
CLOSED
```

The minimum public interface is:

```cpp
getEndPoints()

open(config)
start()
stop()
close()
```

Stream configuration cannot be modified while the stream is open.

A configuration change requires:

```text
stop()
   ↓
close()
   ↓
open(newConfig)
   ↓
start()
```

Any seamless hot-swap or advanced reconfiguration mechanism is the responsibility of the `AudioEngine`, not Core Audio.

## 7. Audio Buffer

Core Audio provides audio buffers to higher-level components.

A generic buffer may be represented as:

```cpp
struct InputBuffer {
    const float* const* channels;
    std::uint32_t count;
};
struct OutputBuffer {
    float* const* channels;
    std::uint32_t count;
};
struct AudioProcessContext {
    InputBuffer input;
    OutputBuffer output;
    std::uint32_t frames;
};
using ProcessFunction = void (*)(void* user, const AudioProcessContext&) noexcept;
```

The exact internal representation may vary between backends.

Core Audio is responsible for translating backend-native audio data into the generic buffer representation.

Backend-native sample formats may be converted when required by the abstraction.

Core Audio must not impose a fixed DSP block size. The number of frames provided by the backend may vary.

Fixed-size DSP processing belongs to the DSP Engine.

## 8. Real-Time Constraints

The audio callback must be real-time safe.

The real-time execution path must avoid:

* dynamic memory allocation;
* deallocation;
* blocking mutexes;
* filesystem access;
* network operations;
* potentially blocking system calls;
* unbounded operations;
* heavy logging;
* operations with unpredictable execution time.

Backend-specific real-time requirements must be respected by each backend implementation.

## 9. Core Audio Responsibilities

Core Audio is responsible for:

* device enumeration;
* capability discovery;
* stream configuration;
* stream creation;
* stream lifecycle management;
* audio input/output transport;
* audio buffer abstraction;
* sample-format abstraction and conversion where required;
* backend-independent error reporting;
* real-time callback handling;
* backend isolation.

## 10. Out of Scope

Core Audio must not handle:

* DSP processing;
* DSP block scheduling;
* plugins;
* VST/AU processing;
* mixers;
* audio graphs;
* nodes;
* tracks;
* buses;
* logical audio routing;
* transport;
* timeline;
* automation;
* MIDI;
* project management;
* UI;
* networking;
* DAW business logic.

Physical or virtual audio routing belongs to a higher-level audio component.

DSP processing and DSP scheduling belong to the DSP Engine.

## 11. Separation of Responsibilities

The architecture follows this principle:

```text
DAW Engine
    ↓
DSP Engine
    ↓
Audio Routing
    ↓
Audio Engine
    ↓
Core Audio
    ↓
Backend
    ↓
Operating System / Hardware
```

Responsibilities are separated as follows:

**Core Audio**

> Abstracts the backend and transports audio.

**Audio Engine**

> Orchestrates real-time audio execution and higher-level audio operations.

**Audio Routing**

> Determines how physical and virtual audio channels are connected.

**DSP Engine**

> Processes audio and manages DSP execution.

**DAW**

> Determines what the system should do.

## 12. Backend Independence

Core Audio must not expose backend-specific types or concepts.

The public interface should use generic concepts such as:

```cpp
Endpoint
StreamCapabilities
EndpointConfig
AudioProcessContext
Format
Error
```

Backend implementations may internally use:

* ALSA;
* PipeWire;
* JACK;
* WASAPI;
* CoreAudio;
* or other native APIs.

These implementation details must remain isolated from the rest of the application.

## 13. Error Handling


`Result` is exactly `std::expected<void, ErrorType>`. Success is `Result{}` and
failure is `std::unexpected{ErrorType::...}`; there is no `None` enumerator.
`open`, `start`, `stop`, `close`, `setProcessFunction` and `setEventHandler` return it.

| ErrorType | Meaning |
| --- | --- |
| InvalidState | Operation forbidden in the current lifecycle state or from the event dispatcher. |
| EndpointUnavailable | Endpoint/server absent, inaccessible or disconnected. |
| FormatNotSupported | Unknown or unsupported requested stream format. |
| ChannelsNotSupported | Inconsistent direction/counts or unsupported channel count. |
| SampleRateNotSupported | Zero rate or requested stream rate not provided exactly. |
| BufferSizeNotSupported | Zero size or unsupported backend transport parameter. |
| ConfigurationFailed | Other native setup failure, unknown direction, or exception from a control hook. |

Lifecycle guards run first. Common configuration validation runs before native
resources are acquired. Do not rely on a backend-independent ordering of other
errors when multiple fields are invalid. Hook errors are forwarded unchanged;
hook exceptions become `ConfigurationFailed`, without advancing the lifecycle
state. Native hooks must release any partially acquired resource on failure.

Asynchronous xruns and stream loss use `Status::xruns` / `Status::failed` and
`EventType::XRun` / `EventType::Failed`, not Result returns from a callback.
After stream loss, the control thread calls `stop()` then `close()` before
reopening. A realtime-priority refusal is a `RealtimeState::No` fallback, not an
opening error. Discovery errors retain the existing empty-vector convention.

## 14. Acceptance Criteria

A Core Audio implementation is considered compliant if it:

* enumerates available devices;
* exposes their capabilities;
* allows an application to construct an explicit stream configuration;
* validates that configuration when opening a stream;
* opens the stream only with the requested configuration;
* fails explicitly when the requested configuration is unsupported;
* supports `open/start/stop/close`;
* provides real-time audio buffers;
* does not impose a fixed DSP block size;
* respects real-time execution constraints;
* keeps backend-specific concepts out of the public API;
* allows multiple backends to implement the same interface;
* requires stream recreation when its configuration changes.

## 15. Architectural Principle

> **Core Audio must remain small, stable, deterministic and predictable.**

The backend transports audio.

Core Audio abstracts the backend.

The Audio Engine orchestrates real-time execution.

The DSP Engine processes audio.

The DAW decides what should be processed.

Any functionality that is not directly related to device discovery, capability discovery, stream configuration, stream lifecycle or real-time audio transport belongs to a higher-level component.

`EndpointConfig::direction` requires Input: `inputChannels > 0`, `outputChannels == 0`; Output: `inputChannels == 0`, `outputChannels > 0`; Duplex: both counts greater than zero. Inconsistent counts return `ChannelsNotSupported` before any native resource is opened. Unknown direction values return `ConfigurationFailed`. Backend-specific channel limits still apply.

## Backend buffer size and callback frames

`bufferSize` is measured in frames per channel, not bytes or total samples.
ALSA interprets it as a period (the ring contains several periods); JACK validates
the initial server quantum. PipeWire/PulseAudio request transport latency;
PulseAudio also uses it to bound preallocated scratch chunks. These latency
requests do not guarantee server latency or a fixed callback length.
`AudioProcessContext::frames` is authoritative for every call: process exactly
that many samples per channel. Core Audio never accumulates fixed DSP blocks.

## Callback buffer lifetime

`InputBuffer::channels` is `const float* const*`; `OutputBuffer::channels` is
`float* const*`. Both are borrowed planar Float32 views, aligned for `float`.
Their pointer tables and samples remain valid only until the callback returns.
Do not retain/free them, modify the pointer tables, or hand them to other threads.
Every active channel is non-null and provides `frames` samples; an absent side
is `{nullptr, 0}`. Input is read-only and does not alias writable output. Output
starts as silence on every callback; write only `[0, frames)`. Without a callback,
output stays silent. Keep `user` alive until `stop()` returns, which quiesces the
callback. Control methods cannot be called from the audio callback.

## Strict stream configuration

`open(config)` validates and reserves the native application stream before
returning success, without invoking `ProcessFunction`. Its sample rate, format,
direction and channel counts must match the request; no nearest-value fallback is
allowed. A failure leaves the backend Closed and releases partial native resources.
The configuration is immutable until `close()`; change it with
`stop()` / `close()` / `open(newConfig)` / `start()`.

Strictness concerns the application stream boundary. A sound server may convert
between that stream and its hardware. `bufferSize` retains the backend-specific
meaning above: period for ALSA, initial server quantum for JACK, transport latency
request for PipeWire/PulseAudio. A latency request is not an exact hardware ring
size, graph quantum, callback length or end-to-end latency guarantee. Native
callback `frames` remains authoritative. JACK also rechecks the server rate and
quantum before start, because the server can change while the client is Open.

## Discovery snapshots and effective capabilities

`getEndPoints() const noexcept` runs on a control thread. It may allocate and
communicate with the audio server; `noexcept` does not make it real-time safe.
It returns a snapshot by value and reserves no resource. A missing `input` or
`output` means that direction was not exposed in this snapshot; it may be busy,
unavailable or unsupported. An empty endpoint list can also mean discovery failed;
the current interface has no separate discovery error channel.

| Backend | What discovery actually reports |
| --- | --- |
| ALSA | Native channel bounds and independent probes of the framework’s candidate rates, formats and period sizes. Busy/unqueryable streams can be omitted. |
| JACK | Current server rate/quantum and counts of audio ports grouped by client, with server output ports exposed as framework inputs. |
| PipeWire | Registry node identities and generic candidates: 1–2 channels, Float32, the framework’s rate and buffer-size lists. These are not per-node EnumFormat probes. |
| PulseAudio | Sink/source identities and reported server channel counts; Float32 plus the framework’s rate and buffer-size candidates. Server conversion may apply. |

The lists are neither exhaustive hardware capabilities nor a promise that every
combination works. `supportedSampleRates`, `supportedFormats` and
`supportedBufferSizes` are framework candidates, not proof of device support.
Never validate `open()` only against a cached discovery result: the native stream
must still be checked and can explicitly reject an advertised candidate.

## Duplex model by backend

`Direction::Duplex` addresses **one endpoint ID** with positive, independently
chosen input/output counts. Both directions share one configuration and one
`AudioProcessContext` per cycle. Optional capabilities in both directions do not
alone prove that a simultaneous opening is possible.

| Backend | Endpoint ID and current duplex contract |
| --- | --- |
| ALSA | One PCM ID (normally `hw:X,Y`) opened for capture and playback. Both exact configurations and `snd_pcm_link` must succeed. A link failure returns `ConfigurationFailed` and closes both handles; no unsynchronized fallback. |
| JACK | One client name whose output ports feed framework input and whose input ports receive framework output. Both port groups must have enough channels. One server cycle supplies one duplex callback. Empty ID creates unconnected client ports. |
| PipeWire | One source or sink node name; a valid Duplex request returns `ConfigurationFailed` before native stream creation. Input and Output are supported separately. |
| PulseAudio | One source or sink name; a valid Duplex request returns `ConfigurationFailed` before native stream creation. Empty ID selects the default device for the requested single direction. |
| Windows | WASAPI shared-mode input/output; duplex requests are rejected. |
| macOS | CoreAudio HAL Float32 input/output or duplex on one device/clock; no implicit aggregate device. |

ALSA does not combine unrelated capture/playback devices; JACK can expose both
port groups under the same client ID. Two unidirectional backend instances do not
constitute a synchronized duplex stream and provide no shared clock/latency
guarantee. Coordinating unrelated endpoints or adding duplex server backends is
outside this contract and does not add a DSP graph to Core Audio.
