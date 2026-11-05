# Changelog

## 1.0.0 candidate

- Portable Backend Result/ErrorType lifecycle, borrowed planar Float32 process views,
  callback user data, quiescent stop, atomic status and non-RT event dispatch.
- Linux ALSA/JACK/PipeWire/PulseAudio, Windows shared-mode WASAPI and macOS HAL
  CoreAudio implementations, with documented format/duplex/device constraints.
- Accurate compiled-backend availability, static diagnostic names and standalone
  public module clients. ASIO/WMME/DirectSound remain known, unimplemented identifiers.
- Application-owned DSP example, bounds/RT tests, endpoint switching and controlled
  Float32 capture/DSP/render signal validation. No DSP graph or block accumulation
  added to Core.
- Relocatable component-based CMake development packages and candidate TGZ/ZIP/source
  archives with platform/compiler metadata, SHA-256 manifests and external clients.
- Cross-platform unit/fault tests, sanitizers, fuzzing, virtual endurance/server-loss
  tests and explicit hardware skips. Physical latency/unplug and hours-long stability
  remain distinct from these automated proofs.

Candidate packaging is prepared for review; CI does not create tags or publish a release.
