# O09-06 — non-realtime structured diagnostics assessment

Decision: a control-thread adapter can already structure ErrorType, the requested
configuration and Status. Keep the prototype outside the public API. Native error
details require an explicit future backend/API decision; the current Result does
not expose an HRESULT, errno or server error string.

`studies/diagnostics/error_details.hpp` is not installed/exported. Its fallible
capture/serialization operations return the existing Result/ErrorType. It owns a
bounded copy of the endpoint identity (4,096 bytes), records original length and
truncation, and copies the uint32 configuration/uint64 status fields. Unknown enum
values retain safe names. Serialization rejects manually constructed oversized
records without replacing the previous output. Allocation exceptions become
ConfigurationFailed; allocation and formatting belong to the control thread.

The versioned JSON prototype encodes opaque ID bytes as hex, preserving NUL,
invalid UTF-8 and characters that would otherwise require JSON escaping. uint64
counters and optional signed native codes use decimal strings to preserve their
full precision in consumers with limited numeric precision. Unknown native codes
are JSON null; an explicitly supplied zero remains "0". No native code is inferred
from the portable ErrorType. A future human-readable UTF-8 presentation can be
evaluated separately from lossless identity transport.

The executable obtains a real EndpointUnavailable Result from an explicitly
offline probe, captures its control-side status and emits a JSON record. A Python
parser checks the schema and byte roundtrip, then records the measured time/size
for 1,000 control-side capture/serialize operations. These measurements carry no
RT or universal performance claim. GoogleTests exercise ownership, all 64-bit
limits, absent/zero native codes, unknown enums, truncation and rejection/reuse.
The studies compile on Linux/Windows/macOS, with sanitizer profiles in CI.

| Criterion | Decision |
|---|---|
| No RT formatting/allocation | Keep the adapter on the serialized control owner; never register it as an audio callback |
| Stable portable error contract | Keep existing ErrorType/Result and Status fields |
| Native provenance | Optional caller-supplied private study evidence, otherwise unknown |
| Snapshot consistency | Copy after the control operation; Status atomics are observations, not a transactional native error record |
| Async failures | Existing coalesced events need a future epoch/sequence policy before richer native evidence is exposed |
| Public adoption | Defer ErrorDetails API/ABI and retention-policy changes until an explicit decision |

Run `study.details.*` with `MKA_BUILD_STUDIES=ON`; use
`python ci/check_details_study.py --binary build/studies/mka-study-details-example`
to parse the executable's output (adjust `.exe`/build path on Windows).
