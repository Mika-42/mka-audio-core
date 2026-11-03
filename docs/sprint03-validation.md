# Sprint 03 validation

The 16 cards were handled sequentially, with one branch and pull request per
card. The public Backend signatures and types are unchanged. No PR was merged
or configured for automatic merging.

| Check | Observed result |
| --- | --- |
| C++26, Clang 19, four Linux backends | Build succeeded |
| Core and allocation-probe positive controls | 105 passed |
| Core ASan/UBSan, including LeakSanitizer | 103 passed |
| Core ThreadSanitizer | 103 passed |
| Applicable native regression | 215 cases: 205 passed, 3 skipped, 7 disabled, no failures |
| Native contract/buffer ASan/UBSan, LeakSanitizer active | 40 passed |
| Final isolated PipeWire contract tests | 7 passed in normal, ASan/UBSan and ThreadSanitizer builds |

The three native skips require one-sided JACK endpoints missing from the dummy
server. Seven disabled cases are the existing manual playback/record/loopback
scenarios. Legacy ALSA cases hard-coded to `hw:1,0` are excluded from this virtual
suite. No skipped or disabled case is counted as a pass.

The full native ThreadSanitizer pass was **not clean**: 28 cases passed and
12 failed. It reported libpulse mutex publication and JACK shared-audio-buffer
accesses through uninstrumented SDK synchronization. The libpulse diagnostic
was reproduced in a minimal SDK-only program, without Backend code. PipeWire
openings also failed in that grouped run; all seven PipeWire contract cases
subsequently passed in isolation. A test-only cycle deadline was increased from
three to six seconds for large instrumented buffers. These observations do not
establish a clean native TSan pass or prove the third-party SDKs race-free.

## Reproduction

Install the Linux audio SDKs and runtime tools, build normally, then run:

```sh
bash test/linux/run_virtual_tests.sh ctest --test-dir build --output-on-failure \
  -R '^(pipewire|jack|pulseaudio|probe)\.|^alsa\.(contract\.|ALSARtContractTest\.|ALSABackendTest\.(OpenValidates|Unlinkable))'
```

The harness uses private runtime directories, a named JACK dummy server,
PulseAudio null devices, a PipeWire null sink and a continuous silent source
provided by a client stream. The original audiotestsrc fixture could exhaust
its buffers and was replaced; the final capture test ran and passed.

Allocation interposition and sanitizer runtimes are checked in separate
executables. Native sanitizer checks build the contract/buffer targets, which
do not link the allocation probe. C++ scalar, array, nothrow and aligned
allocations have positive detection controls; glibc malloc/calloc/realloc/free
are checked on a marked audio thread.

The virtual PipeWire clients are explicitly non-RT and do not load RTKit. The
installed SDK's optional RT module retained allocations in a minimal
context-create/destroy program; the non-RT client configuration avoids that
external dependency with LeakSanitizer still enabled. Production backend
configuration is unchanged. RTKit scheduling, physical devices, Windows and
macOS require separate platform testing. The successful ALSA duplex-link
opening path uses a simulated link capability; no physical synchronized ALSA
duplex result is claimed. PulseAudio remains a non-RT server backend.
