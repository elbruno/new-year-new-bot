# StackChan firmware HTTP API

## Overview

Our custom firmware exposes a local HTTP API over Wi-Fi so applications can
inspect and interact with StackChan. The reusable C# RobotClient calls this
API. It is separate from the factory StackChan cloud and MCP experience.

The API is unauthenticated. Use it only on a trusted, isolated local network;
do not expose it to the Internet. The published 0.5.1 firmware source is a
draft snapshot, not evidence of the exact firmware installed on a robot.
Examples below are illustrative, not observations of physical hardware.
The microphone contract below applies to compatible Wi-Fi lab firmware 0.6.1
or later. The published 0.5.1 source snapshot does not contain its firmware
implementation.

## Endpoint reference

Each row is a registered method and route. **Base** means the 32 routes in
the common Wi-Fi server table; **Extended** means the 17 additional routes
compiled only under the condition in [Build profiles](#build-profiles-and-endpoint-availability).
Registration does not guarantee that a device feature is ready or that a
request is authorized by runtime state.

| Method | Endpoint | Description | Availability |
| --- | --- | --- | --- |
| GET | `/` | Read the HTML diagnostic page. | Base |
| GET | `/api/version` | Read firmware and protocol versions. | Base |
| GET | `/api/status` | Read status, capabilities, and command state. | Base |
| GET | `/api/network` | Read network state without credentials. | Base |
| GET | `/setup` | Open the setup page while setup is active. | Base |
| POST | `/api/config` | Submit Wi-Fi credentials during setup. | Base |
| POST | `/api/actions` | Request an action. | Base |
| POST | `/api/stop` | Request Stop. | Base |
| GET | `/api/motion` | Read the shared status snapshot. | Base |
| POST | `/api/motion` | Request a motion action. | Base |
| GET | `/api/routines` | Read the shared status snapshot. | Base |
| POST | `/api/routines` | Request a routine. | Base |
| GET | `/api/motion/config` | Read motion configuration and readiness. | Base |
| POST | `/api/motion/config` | Request a configuration operation. | Base |
| POST | `/api/companion/presence` | Update temporary companion presence. | Base |
| GET | `/api/display/brightness` | Read display brightness state. | Base |
| POST | `/api/display/brightness` | Request display brightness. | Base |
| GET | `/api/ui/screens` | Read the remote UI screen registry. | Base |
| GET | `/api/ui/screen` | Read the current UI screen. | Base |
| POST | `/api/ui/screen` | Request a remote UI screen change. | Base |
| POST | `/api/ui/touch` | Request a remote UI touch action. | Base |
| GET | `/api/debug/motion` | Read motion diagnostics. | Base |
| POST | `/api/debug/servo-sample` | Request a diagnostic servo observation. | Base |
| GET | `/api/debug/logs` | Read a bounded diagnostic log page. | Base |
| GET | `/api/events` | Read bounded operational events. | Base |
| GET | `/api/notification` | Read notification state. | Base |
| POST | `/api/notification` | Request a notification. | Base |
| POST | `/api/notification/ack` | Acknowledge a notification interaction. | Base |
| GET | `/api/camera` | Read camera state. | Base |
| GET | `/api/camera/image` | Read a captured camera image. | Base |
| POST | `/api/camera/capture` | Request a camera capture. | Base |
| POST | `/api/camera/save` | Request saving a capture. | Base |
| GET | `/api/debug/motion-test` | Read the BSP motion-test state. | Extended |
| POST | `/api/debug/motion-test` | Request a BSP motion test. | Extended |
| GET | `/api/debug/audio-test` | Read the audio-test state. | Extended |
| POST | `/api/debug/audio-test` | Request an audio test. | Extended |
| GET | `/api/debug/audio-files` | List audio files for the audio test. | Extended |
| GET | `/api/display/images` | List available display images. | Extended |
| GET | `/api/display/image` | Read display-image state. | Extended |
| POST | `/api/display/image` | Request an image on the display. | Extended |
| GET | `/api/display/text` | Read display-text state. | Extended |
| POST | `/api/display/text` | Request text on the display. | Extended |
| GET | `/api/display/rgb` | Read RGB effect state. | Extended |
| POST | `/api/display/rgb` | Request an RGB effect. | Extended |
| GET | `/api/sensors` | Read sensor and petting diagnostics. | Extended |
| GET | `/api/microphone/capture` | Read current or identity-bound capture state. | Extended |
| POST | `/api/microphone/capture` | Start a bounded microphone capture. | Extended |
| POST | `/api/microphone/capture/cancel` | Cancel a boot/request-scoped capture. | Extended |
| GET | `/api/microphone/capture/download` | Download completed transient PCM. | Extended |

GET routes are reads. POST routes can change device or network state.
Diagnostic, camera, setup, motion, and UI controls have additional
prerequisites and are not enabled merely because they appear in this table.

## API examples

These PowerShell commands prompt for an address rather than embedding private
configuration. Use a robot running the relevant Wi-Fi build.

```powershell
$robotIp = Read-Host "Robot private IPv4 address"
$base = "http://$robotIp"
```

### GET /api/version

Read firmware and protocol versions:

```powershell
Invoke-RestMethod "$base/api/version" | ConvertTo-Json
```

Illustrative response:

```json
{"firmwareVersion":"0.5.1","protocolVersion":1}
```

`firmwareVersion` is the version selected for the build; `protocolVersion`
is the numeric API protocol version.

### GET /api/status

Read capabilities, the current boot, and the most recent command:

```powershell
Invoke-RestMethod "$base/api/status" | ConvertTo-Json -Depth 6
```

Illustrative response:

```json
{
  "version": 1,
  "firmwareVersion": "0.5.1",
  "bootId": 123,
  "capabilities": ["neutral", "wink", "smile", "look_up", "sleep", "wake", "warning", "important", "important_notification"],
  "command": null,
  "motion": {
    "state": "disarmed", "position": null, "positionUnits": "scs_raw",
    "error": null, "mode": "normal", "diagnostic": null,
    "primaryError": null, "stopResult": "not_requested", "configRevision": 0
  },
  "quiet": false,
  "stopReceipt": null,
  "routines": [
    {"name": "important_notification", "available": true, "requiresMotion": false},
    {"name": "important_message", "available": false, "requiresMotion": true}
  ]
}
```

`bootId` scopes command identity. `capabilities` reflects current runtime
availability, not a promise that a later request will succeed. `command` is
null or an object containing `commandId`, `action`, `durationMs`, `state`,
`error`, and `configRevision`. Motion position and diagnostic details can be
null. `stopReceipt` is null until available.

### GET /api/network

Read connection progress without exposing the network name or credentials:

```powershell
Invoke-RestMethod "$base/api/network" | ConvertTo-Json
```

Illustrative response:

```json
{"state":"connected","setup":false,"error":null}
```

`state` is `unconfigured`, `connecting`, `connected`, `offline`, or `error`;
`setup` indicates whether the setup access point is active. `error` is null
or an error code.

### POST /api/actions

Request a supported expression. This changes device state; run it only when
that action is intended. Fetch fresh status and confirm that `smile` is
advertised. This example assumes no other caller or local interaction is
submitting commands and that the last ID is below the unsigned 32-bit maximum.

```powershell
$status = Invoke-RestMethod "$base/api/status"
if ("smile" -notin $status.capabilities) { throw "Smile is unavailable." }
$lastId = if ($null -eq $status.command) { 0 } else { $status.command.commandId }
if ($lastId -ge [uint32]::MaxValue) { throw "Command IDs exhausted for this boot." }
$body = @{
    bootId = $status.bootId
    commandId = [uint32]($lastId + 1)
    action = "smile"
    durationMs = 1000
} | ConvertTo-Json -Compress
Invoke-RestMethod "$base/api/actions" -Method Post -ContentType "application/json" -Body $body |
    ConvertTo-Json -Depth 6
```

The JSON body has exactly `bootId`, `commandId`, `action`, and `durationMs`,
plus optional `configRevision` when required for checked motion. IDs are
positive unsigned 32-bit integers; duration is 100 to 5000 milliseconds.
An accepted request returns HTTP 202 and the same status structure as
`GET /api/status`, with a `command` object such as:

```json
{"commandId":1,"action":"smile","durationMs":1000,"state":"accepted","error":null,"configRevision":null}
```

The object above is the `command` field of the response, not the entire
response. Compare its ID and action and the top-level `bootId` to the request.
The eventual command state can be checked with `GET /api/status`.

### POST /api/stop

Request Stop for the current boot. This is state-changing and is not a
read-only diagnostic. It may cancel active operations.

```powershell
$status = Invoke-RestMethod "$base/api/status"
$body = @{ bootId = $status.bootId } | ConvertTo-Json -Compress
Invoke-RestMethod "$base/api/stop" -Method Post -ContentType "application/json" -Body $body |
    ConvertTo-Json -Depth 6
```

The body contains only `bootId`. In the normal Wi-Fi implementation, HTTP
200 or 202 returns the status structure above with a `stopReceipt` such as:

```json
{"bootId":123,"stopId":1,"outcome":"confirmed"}
```

This object is the `stopReceipt` field, not a complete response. A pending
result is not a confirmed Stop; an error or interrupted response can leave the
outcome uncertain. The manual diagnostic profile returns a different Stop
response with rail and torque details and reports physical Stop as unverified.

### GET /api/sensors

When the extended routes are compiled, read a sensor snapshot:

```powershell
Invoke-RestMethod "$base/api/sensors" | ConvertTo-Json -Depth 6
```

Illustrative response, with no samples:

```json
{
  "top": {"initialized":false,"available":false,"error":null,"hasSample":false,"fresh":false,"ageMs":null,"front":null,"middle":null,"back":null},
  "light": {"initialized":false,"available":false,"error":null,"hasSample":false,"fresh":false,"ageMs":null,"proximity":null,"als0":null,"als1":null},
  "imu": {"available":false,"error":null,
    "accelerationG":{"hasSample":false,"fresh":false,"ageMs":null,"x":null,"y":null,"z":null},
    "gyroscopeDps":{"hasSample":false,"fresh":false,"ageMs":null,"x":null,"y":null,"z":null}},
  "rgb": {"active":false,"durationMs":null,"error":null},
  "petting": {"enabled":false,"soundEnabled":false,"quiet":false,"eligible":false,"ineligibleReason":null,
    "settled":false,"released":false,"touchCount":0,"releaseCount":0,"active":false,"stage":0,"soundState":"not_requested"}
}
```

`available` and `error` describe each device; `hasSample`, `fresh`, and
`ageMs` distinguish missing or stale data from a current sample. Values can
be null when no sample exists. A build may include the endpoint while sensors
are unavailable; `sensors_not_ready` can also be returned.

## Other endpoint contracts

The default POST limit and content type are described under
[Errors and validation](#errors-and-validation). Unless a route explicitly
defines a query below, do not depend on query parameters. Many handlers ignore
them; several reject them. A queued response is an admission receipt, not
proof that the requested effect completed.

### Setup and network

- **GET `/`** returns the diagnostic HTML page. **GET `/setup`** returns the
  setup HTML page only while the setup access point is active, otherwise
  `409 setup_not_active`; contention can return `503 device_busy`.
- **POST `/api/config`** accepts exactly `ssid` and `password`, both strings.
  The SSID must be nonempty and at most 32 bytes; the password is printable
  ASCII, at most 63 bytes, and either empty or at least eight bytes. During
  ready setup, `202 {"state":"pending_connection"}` queues a connection
  attempt, not a confirmed connection. Invalid input returns
  `400 invalid_config`; an inactive or busy setup returns
  `409 setup_not_ready`. Do not include real credentials in examples or
  logs. Implementation: `LabPlatform.h` and `LabNetwork.h`.

### Actions, motion, and routines

- **GET `/api/motion`** and **GET `/api/routines`** both return the same
  status object as `GET /api/status`, including the `motion` object and
  `routines` array. Neither is a dedicated standalone motion or routine
  document.
- **POST `/api/motion`** and **POST `/api/routines`** use the action request
  body and response described above. Motion actions are `arm`, `lock`,
  `release_torque`, `nod`, `move_left`, `move_right`, `move_up`, `move_down`,
  and `home`. Routine actions are `important_notification` and
  `important_message`. Sending an action to the wrong one of these routes
  returns `400 action_route_mismatch`. Availability, revision, readiness,
  safety checks, and minimum travel time can reject a request even when its
  JSON is valid. Response status and command identity must be correlated;
  `202` means accepted, not physically completed.
- **GET `/api/motion/config`** returns configuration state. Top-level fields
  include `schemaVersion`, `bootId`, `revision`, `source`, `units`,
  `hardwareValidated`, `busStarted`, `active`, `compiled`, `bounds`, `valid`,
  `error`, `applyAllowed`, `applyReason`, `candidate`, `calibrationMatches`,
  `observationAgeMs`, `calibration`, `hardwareObservation`, `torqueRelease`,
  and `emergencyStop`; `configured` and `manualSession` are conditional.
  Unsupported configurations return `422 config_unavailable`. The explicit
  `hardwareValidated:false` is not physical validation.
- **POST `/api/motion/config`** accepts exact flat key sets determined by
  `op`, all with positive `bootId` and nonnegative `baseRevision`:
  - `preview` and `apply`: add `expectedYawRaw`, `expectedPitchRaw`,
    `homeYawRaw`, `homePitchRaw`, `yawMinRaw`, `yawMaxRaw`, `pitchMinRaw`,
    `pitchMaxRaw`, `stepRaw`, `nodStepRaw`, and `travelMs`. Raw values are
    integers; `travelMs` is unsigned. The result is a configuration document
    with validity and apply fields.
  - `reset`: only the three common keys; returns configuration state.
  - `observe`: add positive `observationId`. Returns an observation document,
    possibly pending with HTTP 202; it reports
    `physicalValidation:"unverified"` and `powerEffect:"not_requested"`.
  - `prepare-single-axis`: add `axis` (`yaw` or `pitch`), `minimumRaw`,
    `maximumRaw`, `maximumStepRaw`, `travelMs`, `expectedYawRaw`,
    `expectedPitchRaw`, and `reviewed:true`. Raw minimum, maximum, and
    expected positions are unsigned integers in 0..1000; `maximumStepRaw` is
    in 1..1000 and `travelMs` is positive. Compiled bounds and timing impose
    further restrictions. The preparation response includes
    `configurationStored`, `motionAvailable`, `reason`, `revision`, and
    `physicalReview`.
  - `manual-power`, `manual-enable`, `manual-move`, `manual-return`,
    `manual-close`, and `manual-feedback-power`: add positive `commandId`.
    `manual-move` also requires a nonzero integer `deltaRaw` in -1000..1000;
    `manual-close` and `manual-feedback-power` require
    `supportConfirmed:true`; `manual-feedback-power` also requires
    `expectedYawRaw` and `expectedPitchRaw` in 0..1000. An admitted manual
    action returns 202 with `accepted`, `bootId`, `commandId`, `outcome`,
    and `motionAvailable`.

  The 512-byte limit, exact-key validation, range and geometry checks, and
  state-specific admission all apply. Examples of errors are
  `400 invalid_motion_config`, `409 boot_changed`,
  `409 config_revision_changed`, and `422 config_unavailable`. These
  operations can affect motion or its configuration. Consult `LabApi.h`,
  `LabPlatform.h`, and `LabMotion.h` for the individual operation gates.

### Companion and notifications

- **POST `/api/companion/presence`** accepts exactly
  `{"connected":true}` or `{"connected":false}`. A successful response
  contains `connected` and `expiresInMs` (15000 for connected, 0 otherwise).
  Invalid input returns `400 invalid_presence`. Presence expires; it is not
  a boot-scoped command identity.
- **GET `/api/notification`** returns a notification snapshot with
  `schemaVersion`, `demoVersion`, `arrivalCue`, `snapshotKind`, `bootId`,
  `notificationId`, `revision`, `templateId`, `title`, `detail`, `state`,
  `active`, `remainingMs`, `avatarRestored`, and an `interaction` object
  containing `interactionId`, `revision`, `action`, `status`, and `error`.
  A query is rejected with `400 invalid_notification_query`; an unavailable
  identity can return `503 notification_unavailable`.
- **POST `/api/notification`** requires exactly `schemaVersion:1`,
  positive `bootId` and `notificationId`, and a `templateId` from
  `synthetic_review`, `synthetic_form`, `demo_important_email`,
  `demo_important_message`, or `demo_github_action_failed`. The response is
  the notification snapshot: 202 on admission or 200 for an identical
  duplicate. Errors include `400 invalid_notification`,
  `409 boot_changed`, `notification_id_conflict`, `stale_notification`,
  and `notification_busy`.
- **POST `/api/notification/ack`** requires exactly `schemaVersion:1`,
  positive `bootId`, `notificationId`, `interactionId`, and `revision`,
  plus `outcome` equal to `completed` or `failed`. It returns the notification
  snapshot with HTTP 200 on success. Identity and revision must match the
  current interaction; errors include `400 invalid_notification_ack`,
  `409 stale_interaction`, and `interaction_conflict`.

Notification POSTs have a 512-byte limit and reject query strings.
Implementation: `LabPlatform.h`, `LabApi.h`, and `LabNotification.h`.

### Display and UI

- **GET `/api/display/brightness`** returns `brightness`, `minimum`,
  `maximum`, `default`, `pending`, `requestedBrightness` (number or null),
  `persistent:false`, and `error` (string or null). **POST** accepts exactly
  `{"brightness":N}`, where `N` is an integer from 32 to 192. HTTP 202
  returns the same state shape for a queued, nonpersistent change. Errors
  include `400 invalid_brightness`, `422 brightness_out_of_range`,
  `409 brightness_busy`, and `503 brightness_unavailable`.
- **GET `/api/display/images`** returns `files` (an array of filenames)
  and `truncated` (boolean). The list is bounded to 16 files and 64 inspected
  entries. **GET `/api/display/image`** returns `bootId`, `requestId`,
  `state`, and nullable `error`. **POST `/api/display/image`** accepts exactly
  `{"file":"name.jpg"}`: a 5-to-64-character `.jpg` filename with
  alphanumeric, underscore, hyphen, and dot characters, no leading dot or
  `..`. Admission returns 202 with `accepted:true`, `state:"queued"`,
  `bootId`, and `requestId`. Invalid names or requests return 400; busy,
  unavailable display, or exhausted identities can return 409 or 503.
- **GET `/api/display/text`** returns `bootId`, `requestId`, `state`, and
  nullable `error`. **POST** accepts exactly `{"text":"..."}`: nonempty text
  up to 128 bytes, without control characters other than tab. Admission
  returns the same queued receipt fields as image POST. Invalid text returns
  400; unavailable or busy state can return 409 or 503.
- **GET `/api/display/rgb`** returns `bootId`, `requestId`, `state`, and
  nullable `error`. **POST** accepts exactly `color` as `#RRGGBB` and numeric
  `durationMs`; the effect layer accepts 100 to 30000 ms. The handler
  converts the numeric duration to an integer; it does not explicitly require
  an integer input. Admission returns the queued receipt fields above.
  Invalid color or duration returns 400; busy or unavailable effects can
  return 409 or 503.

Image, text, and RGB routes reject query strings. They are Extended routes;
the brightness routes are Base. Image and text requests are separate from
avatar expressions. Implementation: `LabPlatform.h`, `LabImageDisplay.h`,
`LabTemporaryText.h`, `LabRgbControl.h`, and `DisplayBrightness.h`.

- **GET `/api/ui/screens`** returns `current` and a `screens` array of
  `{id,title}`. **GET `/api/ui/screen`** returns `id`, `title`, and
  `elements`; each element contains `id`, `label`, `type`, `enabled`,
  `motion`, and integer `bounds` (`x`, `y`, `w`, `h`). Some screens also
  include petting, avatar-comparison, or notification identity fields.
- **POST `/api/ui/screen`** accepts exactly `{"id":"..."}` for a known
  screen ID of at most 39 bytes. **POST `/api/ui/touch`** ordinarily accepts
  exactly `{"screen":"...","element":"..."}` with IDs at most 39 bytes.
  Notification-screen touches additionally require positive `bootId`,
  `notificationId`, and `revision`, for five keys total. Both return
  `202 {"accepted":true}` when queued. They do not bypass current-screen,
  notification identity, blocked-element, motion, Stop, or remote-UI checks.
  Errors include `400 invalid_ui_request`, `422 unknown_screen` or
  `unknown_element`, and 409 codes such as `remote_ui_disabled`,
  `screen_blocked`, `wrong_screen`, `stale_notification`, or `ui_busy`.
  Implementation: `LabPlatform.h` and `LabMenu.h`.

### Diagnostics, events, and sensors

- **GET `/api/debug/motion`** returns a retained snapshot, not a fresh
  hardware read: `snapshotKind:"retained"`, `status`, `busStarted`,
  `traceFrozen`, `traceOverwritten`, `trace`, `sample`, `calibration`, and
  `enable`. It requires the remote-yaw-bench or two-axis-test runtime gate;
  otherwise it returns `409 debug_disabled`.
- **POST `/api/debug/servo-sample`** accepts exactly positive unsigned
  `bootId`, `sampleId`, and `servoId`; `servoId` must be 1 or 2. It returns
  the motion-debug response with HTTP 202 while pending or 200 otherwise.
  Errors include `400 invalid_sample`, `409 boot_changed`, `debug_disabled`,
  `command_busy`, `observation_busy`, and `stopping`.
- **GET `/api/debug/logs`** reads a retained bounded log page. With a query,
  supply exactly `bootId`, `after`, and `limit`; `bootId` is positive and
  `limit` is 1..8. The response includes `schemaVersion`,
  `snapshotKind:"retained"`, `firmwareVersion`, `bootId`, `capacity`,
  `oldestSeq`, `latestSeq`, `nextAfter`, `hasMore`, `overwritten`, `dropped`,
  `queueLost`, `traceSkipped`, and `entries` of `{seq,line}`. Invalid queries
  return `400 invalid_log_query`; a disabled log returns `409 debug_disabled`.
  Cursor errors include the current `bootId`, `oldestSeq`, and `latestSeq`.
- **GET `/api/events`** reads a bounded in-memory operational event page.
  Optional query keys are `bootId`, `after`, `limit`, and `event`; `bootId`
  and `after` must appear together, `bootId` is nonzero, and `limit` is 1..8.
  `event` can be `boot`, `ui`, `action`, `brightness`, `stop`, `notification`,
  or `touch`. An optional `X-NetBot-Events-Schema` header selects `1` or `2`
  (default 1). The response contains `schemaVersion`,
  `snapshotKind:"operational"`, `bootId`, `capacity`, `oldestSeq`,
  `latestSeq`, `nextAfter`, `hasMore`, `overwritten`, `dropped`, and
  `entries`. Each entry contains `seq`, `atMs`, `event`, `status`, `action`,
  `error`, `correlationId`, `relatedId`, and `revision`. Schema 2 adds
  `interactionOrigin`, `brightnessRequested`, `brightnessBefore`, and
  `brightnessAfter`. Invalid query or schema returns 400; stale or
  mismatched cursors return 409 with cursor bounds. The ring holds 32 events,
  and sequence and correlation identities must be interpreted with `bootId`.
- **GET `/api/debug/motion-test`** returns `bootId`, `requestId`,
  `admissionReady`, `directionReferenceReady`, `action`, `state`, `error`,
  `gestureSubmitted`, and `physicalResult:"unverified"`.
  **POST** takes `action` and action-dependent integer `value`, `x`, `y`,
  and/or `speed`. Actions are `original_smoke`, `original_035`, `head_left`,
  `head_right`, `head_up`, `head_down`, `head_up_left`, `head_up_right`,
  `head_down_left`, `head_down_right`, `head_center`, `head_stop`,
  `home_capture`, `home_go`, `move_x`, `move_y`, `move_xy`, `nod_yes`,
  `shake_no`, `dance`, and `grumpy_shake`. `speed`, where supported, is
  0..1000; X offsets are -1280..1280 and Y offsets are 0..900. Direction
  step bounds and permitted fields depend on the action; see
  `BspMotionTest.h`. This is the experimental BSP test, not the normal checked
  motion controller. Admission returns 202 with `accepted`, `action`,
  `bootId`, `requestId`, `state:"queued"`, and `physicalResult:"unverified"`.
  Invalid actions or values return 400; unavailable/busy stations return
  409 or 503. A queued test is not evidence of physical movement.
- **GET `/api/debug/audio-test`** returns `bootId`, `requestId`, `state`,
  and nullable `error`. **POST** accepts exactly `{"file":"name.wav"}`:
  a 5-to-64-character `.wav` filename with alphanumeric, underscore, hyphen,
  and dot characters, no leading dot or `..`. It returns 202 with
  `accepted:true`, `state:"queued"`, `bootId`, and `requestId`. Invalid
  filenames return 400; quiet, busy, or unavailable audio can return 409
  or 503. **GET `/api/debug/audio-files`** returns bounded `files` and
  `truncated` values. Audio test and list routes reject query strings.
- **GET `/api/sensors`**, shown above, rejects queries and returns cached
  diagnostics. Freshness thresholds are 350 ms for top touch, 1500 ms for
  light, and 1000 ms for IMU samples. Errors include
  `400 invalid_sensor_query` and `503 sensors_not_ready`.

Motion-test, audio-test, audio-files, and sensor routes are Extended.
Diagnostics and operational events can contain device information: handle
their output appropriately. Implementation: `LabPlatform.h`,
`LabOperationalEvents.h`, `LabRemoteLog.h`, `BspMotionTest.h`, and
`LabAudioTest.h`.

### Camera

The camera routes are Base, but a registered handler does not establish that
the camera is initialized or available. Capture and save are state-changing;
image responses can contain private visual information.

- **POST `/api/camera/capture`** accepts exactly positive unsigned `bootId`
  and `requestId`. **POST `/api/camera/save`** accepts exactly positive
  unsigned `bootId`, `captureId`, and `saveRequestId`. Queries are rejected.
  An admitted request returns 202 and a receipt with `version`, `bootId`,
  `requestId`, `captureId`, `state`, `byteLength`, `saveRequestId`,
  `saveState`, nullable `error`, and nullable `saveError`. Save requires the
  current captured image and an unused save identity.
- **GET `/api/camera`** requires exactly four query parameters: `bootId`,
  `requestId`, `captureId`, and `saveRequestId`. All are unsigned decimal
  integers; boot and request IDs must be positive, while capture and save IDs
  may be zero. It returns the same receipt structure.
- **GET `/api/camera/image`** requires exactly positive `bootId` and
  `captureId` query parameters for a completed matching capture. Success is
  binary JPEG (`image/jpeg`) with `X-Camera-Boot-Id` and
  `X-Camera-Capture-Id` headers, not JSON.

Important errors include `400 invalid_camera_query` or
`invalid_camera_request`, `409 boot_changed`, `camera_identity_changed`,
`camera_busy`, or `camera_image_unavailable`, and `503 camera_unavailable`
or `camera_boot_init_failed`. Identity or cleanup conflicts must not be
treated as successful captures or replayed blindly. Implementation:
`LabPlatform.h` and `CameraProducer.h`.

### Microphone capture

These Extended routes are available in the Wi-Fi lab profile on compatible
firmware 0.6.1 or later. They support CoreS3 and StackChan microphone hardware,
not CoreS3 SE.
Capture is opt-in, finite, half-duplex with the speaker, and stored only in
temporary PSRAM. The public 0.5.1 firmware source snapshot does not include
this implementation.

Start with `GET /api/microphone/capture` to read `version`, `bootId`,
`requestId`, `durationMs`, `sampleRate`, `channels`, `bitsPerSample`, `state`,
`byteLength`, `error`, and `downloadAvailable`. The initial snapshot uses
`state:"idle"` and `requestId:0`. The next request must use the same nonzero
`bootId` and the next sequential nonzero `requestId`.

`POST /api/microphone/capture` accepts exactly positive unsigned `bootId`,
`requestId`, and integer `durationMs` from 1 through 3000. Admission returns
202 with `state:"queued"` and zero `byteLength`. Bind status reads to the
same identity:

```http
GET /api/microphone/capture?bootId=123&requestId=1
```

The format is mono, signed PCM16 little-endian at 16 kHz. A three-second
capture contains 48,000 samples and 96,000 bytes. The firmware allocates this
bounded buffer in PSRAM and does not store audio on the microSD card.
`downloadAvailable` indicates whether the completed buffer can be leased.

`POST /api/microphone/capture/cancel` accepts exactly `bootId` and
`requestId`, returning 202 when cancellation is accepted.
`GET /api/microphone/capture/download?bootId=123&requestId=1` returns raw
`application/octet-stream` only for the matching completed capture. Validate
the even `Content-Length` (2 through 96,000 bytes) and these exact response
headers: `X-Microphone-Sample-Rate: 16000`,
`X-Microphone-Channels: 1`, `X-Microphone-Bits-Per-Sample: 16`,
`X-Microphone-Boot-Id`, and `X-Microphone-Request-Id`.

Invalid input returns 400. Identity, busy, cancellation, or unavailable
download conflicts return 409; an unavailable microphone or server resource
returns 503. Check the returned error and current status. A timed-out POST may
have been admitted; do not automatically replay it. The raw transfer is
boot/request-scoped and does not include a WAV header.

## Build profiles and endpoint availability

- `cores3` uses the USB avatar entry point, not the Wi-Fi HTTP server.
- `cores3-wifi-lab` defines `NETBOT_WIFI_LAB` and starts the server from
  `wifi_main.cpp`; it registers Base and Extended routes.
- `cores3-lesson-examples` defines `NETBOT_LESSON_EXAMPLES` and selects the
  lesson entry point instead of `wifi_main.cpp`; it does not start this HTTP
  server.
- `cores3-wifi-manual-diagnostic` adds `NETBOT_MANUAL_AXIS_DIAGNOSTIC`. It
  uses the Wi-Fi entry point and the Base table, not Extended; some runtime
  handlers and the Stop response differ.

The Extended table requires `NETBOT_WIFI_LAB` and excludes
`NETBOT_LESSON_EXAMPLES`, `NETBOT_MANUAL_AXIS_DIAGNOSTIC`,
`NETBOT_TWO_AXIS_TEST`, and `NETBOT_YAW_BENCH`. Those latter macros also
remove Extended routes when selected by a build outside the listed profiles.
Feature availability can additionally depend on runtime state, hardware,
configuration, and the current boot.

## Errors and validation

The server normally returns JSON with `Content-Type: application/json`.
HTML pages and camera image responses are exceptions. POST requests require
`Content-Type: application/json` or `application/json; charset=utf-8`.
The default body limit is 256 bytes; motion configuration and notification
requests have a 512-byte limit. Common failures include:

| HTTP status | Meaning and examples |
| --- | --- |
| 200 | Read or completed/duplicate request, depending on endpoint. |
| 202 | Accepted or pending; not proof of completion. |
| 204 | No content for applicable responses. |
| 400 | Invalid or incomplete input, for example `invalid_command`. |
| 409 | Conflict with current state, for example `boot_changed`, `busy`, `stale_id`, or `id_conflict`. |
| 413 | Body exceeds the endpoint limit, `body_too_large`. |
| 415 | Unsupported content type, `json_required`. |
| 422 | Unsupported action, `unsupported_action`. |
| 503 | Feature unavailable, busy, encoding error, or an unconfirmed operation. |

Errors commonly have the form `{"error":"code"}`; some endpoints return
additional state or receipts. Exact keys, size, types, and state are validated
by the relevant parser. Action, motion, routine, and Stop requests use flat
JSON objects; unknown, duplicate, nested, or missing fields are rejected.

Commands share a boot-scoped ledger. A new command ID must be greater than the
last accepted ID for that boot. The same ID and matching request returns the
existing status without executing again; a different request with the same ID
or an older ID is rejected. Local actions also use this ledger. A changed
boot, stale ID, concurrent caller, unsupported capability, changed
configuration revision, Stop, or motion readiness can prevent admission.

If a POST times out, the connection fails, or a receipt cannot be correlated,
the operation may already have reached the robot. Do not automatically replay
an uncertain effect. Reconcile using the current boot, status, and applicable
receipt; a read alone does not prove a physical outcome. Stop confirmation is
not a blanket claim of physical torque-off.

## Using the API from C#

The [RobotClient library](https://github.com/elbruno/new-year-new-bot/tree/main/src/RobotClient/StackChan.RobotClient)
wraps HTTP and validates typed responses. `GetVersionAsync()` calls
`GET /api/version`; `GetStatusAsync()` calls `GET /api/status` and checks
the boot and capabilities. `PerformAsync()` checks current status and
capabilities, selects `/api/actions`, `/api/motion`, or `/api/routines`
according to the action, and correlates the command receipt. It treats
uncertain POST outcomes as uncertain rather than automatically replaying them.
The [microphone capture example](../../src/microphone-capture/README.md)
shows the firmware 0.6.1 microphone contract using `HttpClient`, including
boot/request-correlated polling and PCM download validation.

## Source references

- [HTTP server, route registration, and handlers](https://github.com/elbruno/new-year-new-bot/blob/main/src/firmware/usb-avatar/include/LabPlatform.h)
- [JSON parsers and response serialization](https://github.com/elbruno/new-year-new-bot/blob/main/src/firmware/usb-avatar/include/LabApi.h)
- [Action and command ledger contract](https://github.com/elbruno/new-year-new-bot/blob/main/src/firmware/usb-avatar/include/LabContract.h)
- [Runtime action admission](https://github.com/elbruno/new-year-new-bot/blob/main/src/firmware/usb-avatar/include/LabRuntime.h)
- [Motion configuration and admission](https://github.com/elbruno/new-year-new-bot/blob/main/src/firmware/usb-avatar/include/LabMotion.h)
- [Network setup](https://github.com/elbruno/new-year-new-bot/blob/main/src/firmware/usb-avatar/include/LabNetwork.h)
- [Notification state](https://github.com/elbruno/new-year-new-bot/blob/main/src/firmware/usb-avatar/include/LabNotification.h) and [companion presence](https://github.com/elbruno/new-year-new-bot/blob/main/src/firmware/usb-avatar/include/LabPresence.h)
- [UI screen and element registry](https://github.com/elbruno/new-year-new-bot/blob/main/src/firmware/usb-avatar/include/LabMenu.h)
- [Display brightness](https://github.com/elbruno/new-year-new-bot/blob/main/src/firmware/usb-avatar/include/DisplayBrightness.h), [image](https://github.com/elbruno/new-year-new-bot/blob/main/src/firmware/usb-avatar/include/LabImageDisplay.h), [text](https://github.com/elbruno/new-year-new-bot/blob/main/src/firmware/usb-avatar/include/LabTemporaryText.h), and [RGB](https://github.com/elbruno/new-year-new-bot/blob/main/src/firmware/usb-avatar/include/LabRgbControl.h) controls
- [Experimental BSP motion test](https://github.com/elbruno/new-year-new-bot/blob/main/src/firmware/usb-avatar/include/BspMotionTest.h), [audio test](https://github.com/elbruno/new-year-new-bot/blob/main/src/firmware/usb-avatar/include/LabAudioTest.h), and [sensor state](https://github.com/elbruno/new-year-new-bot/blob/main/src/firmware/usb-avatar/include/LabDevice.h)
- [Operational events](https://github.com/elbruno/new-year-new-bot/blob/main/src/firmware/usb-avatar/include/LabOperationalEvents.h) and [retained logs](https://github.com/elbruno/new-year-new-bot/blob/main/src/firmware/usb-avatar/include/LabRemoteLog.h)
- [Camera producer](https://github.com/elbruno/new-year-new-bot/blob/main/src/firmware/usb-avatar/include/CameraProducer.h)
- [Microphone capture C++ and .NET samples](../../src/microphone-capture/README.md)
- [Wi-Fi firmware entry point](https://github.com/elbruno/new-year-new-bot/blob/main/src/firmware/usb-avatar/src/wifi_main.cpp)
- [PlatformIO build profiles](https://github.com/elbruno/new-year-new-bot/blob/main/src/firmware/usb-avatar/platformio.ini)
