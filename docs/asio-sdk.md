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

## Driver discovery (S06-04)

Discovery reads the process-architecture ASIO registrations in HKCU and HKLM,
checks their CLSIDs and registered in-process DLL paths, and returns UTF-8 names
with canonical `asio:{guid}` identities. User registrations take precedence for
duplicate identities. Missing classes/DLLs and malformed registry values are
ignored. The scan neither instantiates drivers nor starts audio. Returned
snapshots own their strings and remain valid after registry changes. Capabilities
are added by the following cards; metadata alone does not promise an openable
configuration or available hardware.

## Driver ownership and initialization (S06-05)

Opening an enumerated CLSID creates its SDK IASIO interface on a dedicated STA
owner with a hidden window and message pump. IASIO::init and the SDK driver-info
queries perform the initialization normally wrapped by ASIOInit. The host owns
and releases the interface directly rather than adopting the SDK example's
global driver manager. This preserves stable CLSID selection and user-level
registrations without copying SDK implementation files into the project.

Only one native driver can be owned by this host at a time. COM creation/init
failures release partial interfaces and balance successful COM initialization;
the backend remains reusable. Close and destruction release the interface on its
original owner even when requested by another thread. The next cards validate
channels, buffers and formats before adding operational audio transport.

## Channel capabilities (S06-06)

Opening queries real input/output counts on the driver owner and rejects negative
counts or requests beyond them. Zero native channels means an absent direction;
the portable contract already rejects a request with zero active channels. The
Windows SDK's signed 32-bit counts are checked before conversion to `uint32_t`.

The metadata registry scan still loads no DLL. Public capability discovery now
temporarily initializes each available driver, without starting audio, to expose
channel ranges. Failed channel probes keep the identity but expose no usable direction.
While a session owns a driver, discovery uses its cached counts and leaves other
drivers unprobed rather than disturbing the one-driver ownership rule. Snapshot
capabilities own their data. Other capability lists are added by later cards.

## Exact buffer sizes (S06-07)

The driver owner's buffer-size query must provide positive, ordered bounds and
a valid preferred size. Zero granularity denotes one fixed size; -1 denotes
powers of two; a positive step is measured from the minimum. Invalid or
inconsistent bounds/granularity are rejected. `open()` requires exactly the
requested frame count and never replaces it with the driver's preferred size.

Capability snapshots include up to 256 sequential supported sizes plus the
preferred size and maximum, sorted and deduplicated. This bounds enumeration
for drivers advertising extremely dense ranges; the list is indicative and
`open()` still checks any requested size against the full native rule. Arithmetic
uses widened unsigned intermediates; no audio buffers are allocated by the query.

## Native channel formats (S06-08)

Every requested channel is queried on the owner before audio starts. Its index
and direction must match the query, and its sample format must match the request.
Layouts retain byte order and container width separately: Int16/24/32 and
Float32/64 support both ASIO byte orders; Int24 supports packed three-byte and
right-aligned 24-in-32 layouts. This follows the SDK 2.3 sample-type definitions
(pages 33–34). DSD and the other 16/18/20-in-32 variants are explicitly rejected.
Conversion and transport are added by S06-13 through S06-15.

Discovery reports the native format of channel zero as a candidate per direction,
without probing an unbounded number of advertised ports. These lists do not
promise every channel/format combination: opening validates all requested
channels starting at zero. Open-session snapshots use only cached verified
layouts; no channel query or port-table mutation is needed in the audio callback.

## Exact sample rate and clock policy (S06-09)

An explicit `open()` authorizes selecting its exact requested rate. The owner
checks ASIOCanSampleRate, reads a valid initial rate, sets the requested value
only if different, then re-reads it. An unavailable/external clock, invalid
measurement, or mismatch fails; 44.1 and 48 kHz are never interchanged silently.
Channel counts, buffer limits and layouts are queried after clock selection,
since they may depend on the chosen rate.

If this host attempted a clock change, rollback and close restore the original
rate before releasing the interface and verify that restoration. Cleanup still
releases the interface if restoration fails and reports the native failure.
Discovery only queries candidate and current supported rates; it never changes
the clock. Open-session rate snapshots contain the exact verified rate.

## Double buffers and scratch ownership (S06-10)

Opening preallocates the SDK descriptor array, immutable layouts, planar Float32
scratch and channel-pointer tables. Channel totals, signed SDK limits, sample
counts and byte/address arithmetic are checked before use. Descriptors list
inputs first, then outputs, with independent channel indices starting at zero.
Both native halves must be non-null and disjoint, including across channels;
the driver must not change descriptor identities. Both output halves are primed
to silence after validation, while input data remains untouched.

The SDK owns native sample memory; the host owns only scratch and tables. A
buffer-creation attempt is disposed exactly once during close or rollback,
including partial failure, before restoring the clock and releasing the driver.
Cold SDK callbacks remain allocation-free and never enter the user DSP before
start. S06-11 adds processing dispatch and S06-15 adds native start/stop.

## Callback dispatch (S06-11)

The C-compatible bufferSwitch bridge routes to one published processor. Readers
increment a lock-free in-flight counter before loading the owner. Close disables
DSP, removes the owner and waits outside RT for readers before disposing buffers.
Repeated or late callbacks with no owner do nothing. A non-waiting atomic flag
rejects overlapping/reentrant blocks and reports an XRun. Invalid indices fail
once without accessing native memory.

Each accepted block clears all Float32 outputs, supplies fixed planar views and
calls the bound user function once; a null function leaves silence. Both values
of the directProcess scheduling hint use synchronous processing with no host
allocation, lock or wait. Bindings preserve the backend instance and its user
pointer, with XRun/Failed notifications through Core atomics. The native start
gate stays disabled until S06-15; the dispatch tests enable the private processor
explicitly with mocks. S06-13/14 supply the Core conversion hooks.

## TimeInfo and diagnostic positions (S06-12)

The host advertises TimeInfo support and processes either callback form once per
block. It copies only flagged sample positions and nanosecond timestamps into
owned atomic diagnostics, returning the original SDK pointer unchanged. No SDK
timing pointer survives the callback. Missing/invalid flags leave the associated
measurement unavailable; no DAW timeline or timecode API is added to Core.

For the legacy callback or a null TimeInfo pointer, the host uses the SDK's
getSamplePosition inside the audio callback, as prescribed by ASIO. This getter
(and later optional outputReady) is an RT exception to the control-owner rule;
initialization and other control calls remain on the owner thread. A failed
optional timing query clears diagnostic validity without suppressing audio.
Non-RT readers obtain coherent atomic position/timestamp snapshots. Native
unsigned high/low words are widened before combining, including zero and UINT64_MAX.

## Capture conversion (S06-13)

The input hook reads the selected native half into preallocated planar Float32
scratch and passes const channel pointers to DSP. It reuses Core readChannel
for Int16, packed Int24, right-aligned Int24-in-32, Int32 and Float32/64.
Big-endian samples are reversed in a fixed eight-byte stack buffer. Int24-in-32
uses the 24-bit scale, ignoring container padding instead of attenuating as S32.

Null/short/overflowing regions and inconsistent layouts are rejected before
access. Input integer extrema and floating NaN/infinity behavior follow Core;
source data and neighboring sample guards remain untouched. No conversion
allocates, logs, waits or retains a callback view.
