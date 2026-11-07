# O09-03 — two-stream PipeWire duplex study

Decision: retain the production backend's explicit duplex rejection. The opt-in
proof uses two actual `pw_stream` objects sharing one native context/core and
private virtual sink. It establishes a concurrent render/capture signal path,
not a new supported `Direction::Duplex` implementation.

Both streams request exact Float32 planar, stereo, 48 kHz. Native format callbacks
verify those values. Processing uses only borrowed native planes, bounded loops,
atomics and owned counters; it neither allocates nor retains sample pointers. The
render marker has distinct channels and a 251-frame period; a capture witness
requires 2,048 matching sequential frames after initial silence. Silent capture,
swapped channels, NaN and broken continuity are not successful signal evidence.

The two-stream handshake keeps application processing disabled until both formats
are known. A missing second target is bounded by a timeout, destroys both streams
and the shared native connection, and permits retry on the same object. Shutdown
disables processing, waits for entered callbacks, destroys streams under the loop
lock and joins the loop before releasing the owning state. Normal and ASan/UBSan
jobs use isolated servers and publish the native observation in JUnit.

The Ubuntu CI exposed an older session manager falling back to the default sink
for an unknown target despite `node.dont-fallback`. The proof now resolves both
requested names from a completed native registry roundtrip and connects by native
node ID. The second-ID check runs after the first leg connects, so the missing-leg
test also exercises rollback. Full post-link identity and device-removal/epoch
handling remain production adoption criteria, beyond this bounded prototype.

## Clock result and adoption criteria

`pw_stream_get_time_n` provides graph ticks, their rate and the monotonic time of
the observation. The study compares snapshots only after shutdown, extrapolating
them to one common time. Unit tests reject missing rates, incompatible rates,
discontinuities and counters beyond exact floating-point representation.

The observed frame offset is recorded; `pw_time` does **not** identify the physical
clock. Same rate or close timestamps do not establish shared hardware-clock
identity or callback ordering. The public process contract needs a single paired
input/output context even when the two callbacks arrive with different quanta.

| Requirement before adoption | Current study result |
|---|---|
| Concurrent native input/output and non-silent signal | Covered by two-stream virtual monitor proof |
| Exact format and bounded rollback | Covered; missing second target and reuse tested |
| Common hardware clock and graph changes | Not established by `pw_time`; explicit identity/epoch policy needed |
| One coherent Core callback | Not implemented; choose a bounded pairing policy or evaluate a single `pw_filter` |
| Different devices, drift, xruns | Require a declared clock strategy and measured hardware tests |
| Native sample/graph buffers | Borrowed; pointers never escape processing |

Run the `study.pipewire.*` tests through `test/linux/run_virtual_tests.sh` with
`MKA_BUILD_STUDIES=ON` and `MKA_BUILD_PIPEWIRE=ON`. Direct execution without the
explicit private fixture is a SKIP. Virtual monitor results do not measure a
microphone/speaker path or physical latency.

Primary references: [PipeWire streams](https://docs.pipewire.org/page_streams.html),
[stream API and pw_time](https://docs.pipewire.org/group__pw__stream.html), and the
SDK's `pipewire/stream.h` used to compile this proof.
