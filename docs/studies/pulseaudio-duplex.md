# O09-04 — PulseAudio record/playback pair study

Decision: defer production duplex. Two native `pa_stream` objects in one context
can transfer known audio through a private sink and its monitor. This does not
provide the coherent shared-clock callback required by Core's duplex contract.

The isolated prototype requests exact Float32 stereo at 48 kHz, verifies both
native sample specs while corked, then uncorks both only after readiness. It uses
the same 251-frame stereo marker/witness as the PipeWire study. Silent buffers do
not establish a successful capture. An absent capture endpoint rolls back the
already-created playback stream and permits a subsequent successful run.

Control waits are bounded, operations are cancelled/unreferenced before their
owning scope ends, and callbacks are removed before streams/context are released.
Application callback work has bounded sample loops and retains no native sample
pointers. PulseAudio's threaded mainloop already holds its own lock and libpulse
can allocate internally, so this remains best-effort rather than strict realtime.
Normal and ASan/UBSan study jobs cover native transfer and cleanup on private servers.

Timing updates occur on the control thread. JUnit records native per-stream time;
the proof verifies usable timing/index information, not equality of two independent
timelines. `pa_stream_connect_playback`'s `sync_stream` is a playback-stream
synchronization mechanism, not a way to attach an arbitrary recording stream.
The prototype passes no capture stream in that parameter.

| Criterion | Current result / remaining decision |
|---|---|
| Concurrent exact-format streams | Native signal/format proof covered |
| Failed second leg and cleanup | Native rollback and retry covered |
| Same callback input/output frames | A bounded pairing policy remains to be specified |
| Device clock identity and drift | Per-stream time does not establish a common hardware clock |
| Latency and changing fragment sizes | Observe negotiated buffers; requests are not exact engine periods |
| Realtime guarantees | Mainloop locking and native allocation prevent a strict RT claim |
| Different devices, suspend/move/xruns | Require dedicated paired-lifecycle/clock tests before adoption |

Enable `MKA_BUILD_STUDIES=ON` and `MKA_BUILD_PULSEAUDIO=ON`, then run
`study.pulse.*` through the existing private virtual-server fixture. The production
backend retains its documented unidirectional behavior and Linux operational
support for those documented capabilities. Virtual monitor transfer does not
measure physical capture/render latency.

Primary references: [stream API](https://www.freedesktop.org/software/pulseaudio/doxygen/stream_8h.html),
[timing information](https://www.freedesktop.org/software/pulseaudio/doxygen/structpa__timing__info.html),
and the libpulse headers compiled by the study.
