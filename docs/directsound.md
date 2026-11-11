# DirectSound (Windows)

DirectSound is an optional Windows SDK target. Enable it with
`MKA_BUILD_DIRECTSOUND=ON`, link `mka-audio-directsound` and import
`mka.audio.backend.directsound` (the `ds` import is also retained).
It links `dsound` and `dxguid`, independently of WASAPI and ASIO. The option
defaults to OFF; an explicit ON on another platform is rejected.

From a Visual Studio 2022 17.14+ developer shell with CMake 4 and Ninja:

```powershell
cmake --preset windows-directsound
cmake --build --preset windows-directsound
ctest --preset windows-directsound
```

S07-01 establishes the build and module boundary. S07-02 adds the final
`mka::audio::core::DirectSound` class and its private native session ownership.
Audio streaming is added by the following sprint cards; the production adapter
currently rejects opening an unavailable stream and is not yet operational.
The SDK link and ABI checks need no audio hardware. Core tests and public module
consumers do not include native headers. CI also builds with the option OFF and
builds the production target with tests, WASAPI and ASIO disabled.

The private test adapter exercises lifecycle transitions, exact request fields,
partial-open rollback, cleanup errors and callbacks/notifications for separate
instances. It is compiled only with `MKA_BUILD_TESTS=ON` and adds no SDK types or
constructor parameters to the portable public interface. Native HRESULT failures
map to Core errors; failed cleanup consumes the native ownership once, even if
the public `close()` reports an error. A failed `stop()` leaves the Core state
Running until a retry succeeds or forced destruction quiesces the session.

S07-03 discovers playback devices with `DirectSoundEnumerateW` only. Physical
IDs are `directsound:output:{lowercase-guid}`. A null GUID supplied by Windows is
the separate `directsound:output:default` alias, which follows the system default
and is not a fixed physical device. Duplicate IDs are removed; devices sharing
a name remain distinct. Names are copied from UTF-16 to UTF-8 with strict Unicode
validation; absent labels use the ID or a default-device label. A native or
callback error discards that enumeration rather than publishing a partial list.
Discovery creates no audio stream. The output capability object identifies the
direction, with zero channel bounds and empty lists until native capability
probing is added in S07-05; these values do not promise any format support.

DirectSound uses Windows I/O workers and does not offer a hard real-time
guarantee. The application audio callback will still obey the Core contract:
preallocated planar Float32 buffers, no allocation, blocking or logging.
