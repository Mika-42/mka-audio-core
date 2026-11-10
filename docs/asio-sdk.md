# S06-01 — ASIO SDK integration and license record

ASIO is Steinberg's third-party driver API. It is optional, Windows-only and
kept out of the public Core API. The production driver host is implemented by
the following sprint cards; this prerequisite validates the SDK and its ABI.

## Current use and distribution

The project owner selected **internal use/prototyping, distribution undecided**.
This does not change the project's license or establish entitlement to publish
a proprietary SDK integration. Before distributing an ASIO-enabled binary, the
project must select and satisfy an applicable SDK license route:

- GPL version 3, including its source and redistribution obligations; or
- the proprietary Steinberg ASIO license, whose text requires a signed agreement
  before publishing and prior written permission to redistribute the SDK.

The SDK's root and common-directory LICENSE.txt state these alternatives.
Trademark/logo terms are separate. The repository bundles no SDK files and the
CI publishes test reports, not SDK archives or linked binaries. Neither the SDK
download nor a successful build constitutes a distribution-license decision.

## Audited SDK

The official archive is **ASIO SDK 2.3.4, 2025-10-15**:

- [Steinberg developer portal](https://www.steinberg.net/developers/)
- [Official archive](https://download.steinberg.net/sdk_downloads/ASIO-SDK_2.3.4_2025-10-15.zip)
- SHA-256: `d5ebf0c20dd2c5f43771fd0c1418f4b361bf52434ee670097cfa6b3a335e2eca`

`cmake/asio-sdk-2.3.4.json` records the archive digest and the hashes of the exact
headers and license files reviewed. Configuration rejects a missing, modified
or unaudited SDK. A different version needs an explicit manifest/license review.
The fetch helper is an explicit development/CI action; ordinary configuration
does not download the SDK.

## Optional SDK validation

Core and WASAPI builds remain independent of the SDK. On Windows, enable its
private validation target explicitly:

```powershell
python ci/fetch_asio_sdk.py --destination build/asio-sdk
cmake --preset windows-core -B build/asio-sdk-check -DMKA_BUILD_ASIO_SDK_TESTS=ON -DMKA_ASIO_SDK_DIR="$PWD/build/asio-sdk/ASIOSDK"
cmake --build build/asio-sdk-check --parallel 2
ctest --test-dir build/asio-sdk-check -L asio --output-on-failure
```

Validation covers exact SDK signatures (`ASIOInit`, `ASIOExit`, IASIO and native
callbacks), Windows integer widths, borrowed Core buffers and both buffer
indices. Configuration tests exercise valid, empty, absent, incomplete and
modified SDKs, including changed license files. MSVC and MinGW/GCC 15.2 compile
the real headers. This step loads no driver and claims no hardware result.

## Optional backend target (S06-02)

`MKA_BUILD_ASIO=ON` enables `mka-audio-asio` on Windows with the audited SDK.
Import `mka.audio.backend.asio`; the original `asio` module re-exports it for
compatibility. SDK include directories are private to the target. The Core and
module consumers need no SDK include path. `MKA_BUILD_TESTS=OFF` builds only the
libraries, without GoogleTest or the SDK validation executables.

```powershell
cmake --preset windows-core -B build/asio -DMKA_BUILD_ASIO=ON -DMKA_ASIO_SDK_DIR="$PWD/build/asio-sdk/ASIOSDK"
cmake --build build/asio --parallel 2
ctest --test-dir build/asio -L asio --output-on-failure
```

The target currently establishes module integration; the following cards add
the ASIO class, driver access and processing. Enabling the target does not yet
establish operational audio support. Linux/macOS requests fail explicitly.

## Backend hooks (S06-03)

`mka::audio::core::ASIO` implements the existing Backend hooks and is neither
copyable nor movable. SDK handles and native errors stay inside its private
session. Pending open resources are released on failure; the previous public
state remains available for retry. Destruction stops processing before releasing
the session. Tests inject private native operations to verify ownership,
rollback, error translation and lifecycle transitions. Test injection is omitted
from library-only builds. Driver discovery/loading is supplied by the following
cards; the current native default reports an unavailable endpoint.
