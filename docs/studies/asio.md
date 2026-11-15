# O09-01 — ASIO support and licensing assessment

Decision: keep ASIO as a known, unimplemented backend. The assessment builds an
isolated SDK callback probe; adopting a driver host needs a separate decision.
No ASIO SDK code is installed or included in the library's public module exports.

## Evidence and license route

Reviewed on 2026-11-06: Steinberg's [developer portal](https://www.steinberg.net/developers/)
links its [open ASIO SDK page](https://www.steinberg.net/developers/asiosdk-open/).
The official download resolves to [SDK 2.3.4, 2025-10-15](https://download.steinberg.net/sdk_downloads/ASIO-SDK_2.3.4_2025-10-15.zip).
Its SHA-256 is `d5ebf0c20dd2c5f43771fd0c1418f4b361bf52434ee670097cfa6b3a335e2eca`.
The archive's root and `common/LICENSE.txt` offer two alternatives:

- GPL version 3, with the applicable source/distribution obligations.
- The proprietary Steinberg ASIO license; its text requires a signed agreement
  before publishing and prior written agreement for SDK redistribution.

The portal also separates optional trademark/logo use from the GNU license.
This repository currently declares no project license. Consequently GPL
compatibility or a proprietary distribution entitlement is **not established**.
The prototype does not select a license for the Core project, bundle the SDK or
claim permission to ship a proprietary ASIO integration. A future distribution
decision must identify the project license and chosen SDK route first.

## Compiled proof and decision criteria

Enable `MKA_BUILD_STUDIES=ON`, `MKA_STUDY_ASIO=ON`, and supply
`MKA_ASIO_SDK_DIR` pointing at the unpacked `ASIOSDK` directory. The CI downloads
the pinned official archive separately and publishes test results only.

The Windows C++ draft-mode proof checks actual `ASIOCallbacks` signatures and
32-bit SDK `long`/boolean types, both native buffer indices, borrowed Core planar
views with 17 frames and boundary sentinels, unsupported packing and invalid
indices/null time information. Rate loss/reset only sets deferred atomic flags.
No callback allocates, logs, locks or calls Core lifecycle functions. The
single-instance synchronous routing pointer is deliberately a probe, not a safe
production callback registry or a driver teardown protocol.

Production acceptance would require: exact channel/sample-type/rate/buffer and
granularity validation; driver discovery/loading and native error mapping;
double-buffer priming/latencies; time-info validity and clock-loss handling;
in-flight callback quiescence before unloading; measured non-allocation and
scheduling evidence on actual drivers; and an agreed license route. MSB, DSD and
24-valid-bits-in-32 representations need explicit mappings before acceptance.

The probe loads no ASIO driver and proves neither device availability nor latency.
Hardware playback/capture remains unmeasured, rather than an invented successful
device test. The existing WASAPI backend remains the Windows implementation.
