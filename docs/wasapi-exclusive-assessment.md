# WASAPI exclusive-mode assessment

The production backend keeps shared event-driven mode. The portable
EndpointConfig has no share-mode or exclusivity field, so enabling exclusive mode
implicitly would change its behavior and could reserve a user's device. This
sprint evaluates exclusive support without adding a public SDK type, an ID
prefix, a build-wide mode switch or a new user interface.

The Windows assessment calls IsFormatSupported(EXCLUSIVE, exact-format, nullptr)
for a bounded set of six representations, mono/stereo/native channels and
44.1/48/96 kHz/native rates. It records every HRESULT and GetDevicePeriod result.
S_OK means that exact candidate is supported; an unsupported mix Float32 format
does not establish that PCM16/PCM24 is also unsupported. Failed queries and unknown
periods remain explicit. The shared native mix is queried as a baseline.

The assessment never calls exclusive Initialize or Start and does not reserve the
device. The hardware GTest case records supported/unsupported/error counts in CI
JUnit, or explicitly skips when no active endpoint/audio service exists. Controlled
SDK tests verify the query ABI, null closest-match output, error reporting and
absence of initialization/start calls. A format query is not proof of a working
exclusive stream, latency, device alignment or physical hot-unplug behavior.

A future opt-in exclusive contract would need a portable share-mode decision,
access-denied/device-in-use behavior, exact-format rejection, native period/buffer
alignment handling (including BUFFER_SIZE_NOT_ALIGNED and client recreation),
MMCSS scheduling, quiescent cancellation and native-device render/capture tests.
Shared-mode buffer-duration rounding must not be reused as an exclusive timing
contract. No such runtime mode is enabled by this assessment.
