# O09-02 — WMME / DirectSound fallback assessment

Decision: retain explicit backend selection and continue validating WASAPI. WMME
and DirectSound remain known, unimplemented identifiers. Windows operational
support is still pending, as stated in the READMEs. No automatic fallback changes
the requested device, representation or timing contract.

The opt-in Windows study calls the actual multimedia SDK and links `winmm`,
`dsound` and `dxguid`. Its WMME query uses `WAVE_FORMAT_QUERY | WAVE_FORMAT_DIRECT`,
with no handle, callback or ACM approximation. Exact mono/stereo 16/24/32-bit PCM
and 32/64-bit float candidates retain their native MMRESULT. Checked 64-bit
products reject DWORD byte-rate/buffer overflow before reaching the SDK.
DirectSound render/capture capability queries create their native interface,
query the SDK-sized caps structure and release it on every successful creation.
A capability result is not a successfully opened audio stream.

GoogleTest cases cover exact SDK arguments, both directions, zero/overflow,
unsupported format/duplex, native errors and failed-query reuse. Controlled caps
results and a separate native capability test distinguish ABI/mapping evidence
from device availability. The native test explicitly skips absent audio services.
Both normal and ASan Windows study builds run in CI.

## Adoption criteria

| Criterion | Evidence / remaining requirement |
|---|---|
| Exact configuration | Query proof; need real stream validation without mapper conversion |
| Callback contract | WMME completion/messages and DirectSound notifications are not a Core frame callback |
| Bounded memory | Byte-size arithmetic verified; preallocated prepared buffers/ring ownership still needed |
| Timing | Measure periods, latency and jitter on native hardware; no low-latency claim from caps |
| Shutdown | Add wave reset/unprepare/close and DirectSound stop/unlock quiescence tests before integration |
| Duplex | Independent input/output clocks need a documented synchronization policy |
| Value over WASAPI | Require an actual supported user/device case that WASAPI cannot serve |

The result is a **deferred fallback implementation**, with a compiled read-only
proof. WMME/DirectSound are legacy APIs; a future user decision can prioritize one
if the measurable device/latency criteria justify its maintenance cost.

Primary SDK references: [waveOutOpen](https://learn.microsoft.com/windows/win32/api/mmeapi/nf-mmeapi-waveoutopen),
[waveInOpen](https://learn.microsoft.com/windows/win32/api/mmeapi/nf-mmeapi-waveinopen),
[DirectSoundCreate8](https://learn.microsoft.com/previous-versions/windows/desktop/ee416756(v=vs.85)),
and the Windows SDK's `mmsystem.h`/`dsound.h` declarations used by the proof.
