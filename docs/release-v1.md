# V1 candidate packaging and acceptance

The project version is prepared as **1.0.0**; package metadata marks it `candidate`.
The CI validates artifacts and uploads them as workflow artifacts, with read-only
repository permissions. It does not create a Git tag or GitHub Release.

## Prepare and verify

Use the documented compiler/toolchain for your OS, with CMake 4+, Ninja, Python 3.12+
and SDKs for the native backends. Work from a clean committed source tree:

```sh
python ci/build_release.py --work build/release
```

The helper builds Release production libraries without test hooks/GoogleTest, plus
the offline DSP example. It creates TGZ and ZIP development packages, extracts both
to new prefixes, compiles public clients against each package and executes the
installed example. It then creates a source tarball from Git's committed files and
builds/runs the SDK-independent example from that source archive. No VCS state or
untracked build output enters the source tarball.

`release-manifest.json` records version, candidate status, OS/architecture, compiler,
build type, compiled backends, source commit and artifact sizes/hashes. `SHA256SUMS`
contains the archive checksums. Candidate archives are platform/toolchain-specific:
module sources rebuild BMIs, while binary libraries still require compatible ABI,
standard library, native SDK and runtime (e.g. MSVC Release `/MD`).

Consumers use `find_package(mkaAudioCore 1.0 CONFIG REQUIRED COMPONENTS ...)` and
`mka::core` / `mka::<backend>` with `CMAKE_CXX_EXTENSIONS OFF`. Only requested native
components resolve their SDK dependencies. The [installation guide](sprint07-validation.md)
includes a separate test that also hides producer source and build directories.

## Acceptance evidence

- Core/DSP boundary: examples and DSP state remain outside `src/core`; variable
  callback sizes pass through immediately, with no Core graph/fixed-block adapter.
- Buffers: immutable Float32 input, mutable cleared output, no borrowed-pointer
  retention, canary bounds checks and allocator probes on the application DSP path.
- Reconfiguration: stop/close/open(new)/start, including unavailable new endpoints,
  new channel geometry and replaced/freed user data. OS tests use native null aliases
  or controlled SDK devices and do not claim physical port switching.
- Signal path: known Float32 samples traverse capture/layout conversion, application
  gain and native render memory on Linux/Windows/macOS. This is not an acoustic
  microphone-to-speaker test.
- Errors: symbolic Result/Status names, unknown values, failed-open reuse, terminal
  failure recovery and counter reset. GoogleTest assertions remain active in Release.
- Packaging: consumed binary/source archives on Linux/Windows/macOS, version/metadata
  and checksums, no producer paths, test hooks or GoogleTest in production artifacts.

The [backend support matrix](backend-support.md) states implemented directions,
formats, scheduling behavior and native proof limits. Microphone permission,
physical unplug, device-specific mixer/UCM routing and physical end-to-end latency
need their own hardware validation. Read [release diagnostics](release-diagnostics.md)
and [DSP integration](../examples/README.md) before interpreting a successful test.
