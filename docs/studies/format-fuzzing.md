# O09-05 — configuration and representation fuzzing

Decision: retain the existing configuration libFuzzer target and add an independent
format-conversion target. Neither opens native devices. The Core API is unchanged.

The existing target checks EndpointConfig enums, direction/channel combinations,
32-bit limits, overflow rejection and closed-state reuse. The new target exercises
all six converter layouts (S16, both Int24 representations, S32, F32, F64), invalid
layout values, empty/truncated inputs, 0–257 frames, unaligned addresses, padding,
strides and repeated sample addresses. Every allocation is bounded by the decoded
case; fuzzing is control/test work, not an audio callback.

The conversion oracle independently decodes signed integers and scales their
numeric values. Integer writes must remain in the documented symmetric range and
within quantization tolerance; float writes preserve finite/Inf/signed-zero values
and sanitize NaN to zero. F64 reads outside Float32 range must produce signed
infinity. Inputs remain immutable, and sentinels/masks check prefix, suffix and
strided padding. Repeated-address writes are checked against the last source sample.

Golden corpus GoogleTests cover every layout and empty/odd/maximum/repeated-address
cases, including signed zero, subnormals, infinities and NaN. Their invariant checks
use abort on violation and remain enabled in Release. Existing exact conversion
unit tests complement the numeric fuzz oracle.

The portability workflow executes 100,000 configuration cases and 100,000 format
cases with Clang libFuzzer, ASan and UBSan, saving reproducers/corpora on failure.
To replay or extend a corpus:

```sh
cmake --preset asan -B build/fuzz -DMKA_BUILD_TESTS=OFF -DMKA_BUILD_FUZZERS=ON
cmake --build build/fuzz --parallel 2
python ci/run_format_fuzz.py --binary build/fuzz/mka-audio-format-fuzz --work build/fuzz/format-corpus
build/fuzz/mka-audio-format-fuzz path/to/reproducer -runs=1
```

Coverage-guided executions are evidence for exercised invariants, not a proof of
all native device behavior or exhaustive input coverage. Native clock/SDK formats
remain separate backend validation concerns.
