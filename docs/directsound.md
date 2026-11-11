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

S07-01 establishes the build and module boundary. Audio streaming is added by
the following sprint cards; this first target is not yet an operational backend.
The SDK link and ABI checks need no audio hardware. Core tests and public module
consumers do not include native headers. CI also builds with the option OFF and
builds the production target with tests, WASAPI and ASIO disabled.

DirectSound uses Windows I/O workers and does not offer a hard real-time
guarantee. The application audio callback will still obey the Core contract:
preallocated planar Float32 buffers, no allocation, blocking or logging.
