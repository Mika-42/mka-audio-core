# Release diagnostics

Import `mka.audio.diagnostics` for `errorName(ErrorType)`,
`realtimeName(RealtimeState)` and `eventName(EventType)`. They return static
string_views, are noexcept and do not log or allocate. Formatting/I/O belongs on
the control thread. Unknown enum values map to `Unknown`; the original enums and
Result/ErrorType contract are unchanged.

| Error | Meaning / next control action |
|---|---|
| InvalidState | respect open/start/stop/close order; stop/close before reconfiguration |
| EndpointUnavailable | check endpoint identity, service, ownership and permissions |
| FormatNotSupported | request an exactly supported transport representation |
| ChannelsNotSupported | fix direction/channel counts and native channel constraints |
| SampleRateNotSupported | request an exactly supported nonzero rate |
| BufferSizeNotSupported | fix nonzero transport frames and native constraints |
| ConfigurationFailed | another native/configuration failure, unsupported duplex or hook exception |

`Status::xruns` counts since start; `failed` means that run needs stop/close/reopen.
The scheduling state is tri-state: Unknown means the backend cannot establish it,
No means no grant was established, Yes means a backend-specific grant was verified
(SCHED_FIFO for ALSA, server realtime for JACK, MMCSS for WASAPI). Inspect the
[backend support matrix](backend-support.md) before interpreting a snapshot.
Core's dispatcher coalesces xruns and sends Failed once outside the audio thread.
Handlers must post control work elsewhere, not call backend lifecycle methods.

`mka-audio-profile` prints symbolic open/start/stop errors and includes the existing
numeric `realtime` plus `realtime_name` in JSON. Mapping a name is safe; printing
from a process callback is not. A successful transport test does not prove physical
microphone signal energy or speaker output.

ALSA's `Unknown PCM InvalidID` during TestOpenInvalidID is an expected native stderr
message from a negative test. It is not a failing suite when the rejection assertion
passes and the executable exits zero. Hardware tests use explicit SKIP when the OS
service/device/permission is absent, not an invented successful audio measurement.

Release validation runs GoogleTest checks under NDEBUG; its assertions and the
configuration fuzzer's abort-on-invariant-violation checks remain active.
