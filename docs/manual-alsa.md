# Audition and recording with direct ALSA

The audible tests are in **mka-audio-alsa-tests**. The core-only executable does
not contain them. Keep DISABLED_ in source; run one test explicitly so the IDE
cannot accidentally execute a stale binary or all audible backends at once:

```sh
MKA_TEST_ALSA_OUTPUT_DEVICE='hw:1,0' \
  bash test/linux/run_manual_alsa.sh sine cmake-build-debug

MKA_TEST_ALSA_INPUT_DEVICE='hw:2,0' \
MKA_TEST_ALSA_OUTPUT_DEVICE='hw:1,0' \
  bash test/linux/run_manual_alsa.sh record cmake-build-debug
```

These IDs match the supplied ALC257 Analog output and acp63 input listing. Check
`aplay -l` and `arecord -l` on each machine: card numbers can change. Use
`hw:CARD=<stable-card-id>,DEV=0` if needed. `hw:1,0` capture refers to the analog
codec and may use a headset microphone; it is distinct from acp63 `hw:2,0`.

Both tests request S32, stereo, 48 kHz and a 512-frame period. They print the
selected PCM, processed frames, xruns and failure status. Capture prints peak/RMS
and explicitly fails on a silent recording; it no longer replays zeros as a
successful recording. All waits are bounded. Fades and silence around replay
avoid abrupt signal edges. No logging, allocation or mixer change happens in the
process callbacks.

## Diagnose the actual failing layer

`ALSA ... Unknown PCM InvalidID` from **TestOpenInvalidID** is an expected negative
test. A `MKA_TEST_PIPEWIRE_DEVICE_ID` skip is unrelated to the direct ALSA path.
Neither message diagnoses the audible test. Run the isolated test above first.

If open fails, the test prints its ErrorType and checks the native open error.
`Device or resource busy (-16)` means another client owns the exclusive hw PCM.
Identify it without changing audio configuration:

```sh
fuser -v /dev/snd/pcmC1D0p /dev/snd/pcmC2D0c
```

If callbacks progress but the speakers remain silent, inspect the hardware mixer:

```sh
amixer -c 1 scontents
amixer -c 2 scontents
alsamixer -c 1
```

Inspect Master/Speaker/Headphone playback, Capture/Mic switches, gains and jack
routing for the selected card. The backend opens PCM streams; it does not change
the hardware mixer, unmute a speaker/microphone, select a UCM route or stop
PipeWire/PulseAudio automatically. Those settings are separate from writing PCM.

Compare with the native ALSA tools using the same device and parameters:

```sh
speaker-test -D hw:1,0 -c 2 -r 48000 -F S32_LE -t sine -f 440 -l 1
arecord -D hw:2,0 -c 2 -r 48000 -f S32_LE -d 5 /tmp/mka-capture.wav
aplay -D hw:1,0 /tmp/mka-capture.wav
```

A silent native reference points to the selected PCM/mixer/device ownership. A
working native reference with a failing mka test needs the isolated test's output
(frames, levels, xruns, error) to diagnose the backend. The automated null-PCM
signal test checks S32 mmap conversion and replay, not a physical amplifier or
microphone.
