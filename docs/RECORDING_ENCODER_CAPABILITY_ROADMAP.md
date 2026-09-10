# Recording and hardware-encoder capability roadmap

## Purpose

This document consolidates the incomplete or missing work identified while reviewing:

- `Investigate the existing encoder path first.md`
- `recording system to support H.265.md`

It also records the revised implementation order agreed on after reviewing the current recording and livestream architecture.

This is a planning document. It does not claim that the items below are implemented or device-validated.

The intended end state is:

- hardware video encoding only;
- crash-resilient fragmented MP4 recording by default;
- immediate ability to begin another recording after Stop;
- optional background remux from fragmented MP4 to ordinary MP4;
- capability-driven AVC and HEVC configuration;
- independent local-recording and livestream profiles;
- one encoder feeding both the network and optional local copy while live;
- no software H.264 or H.265 encoder fallback.

## Current implemented baseline

At the time of this audit, Saber Stage already has several useful foundations:

- The Direct FFmpeg backend uses FFmpeg 9.0.1 `h264_mediacodec` with Android NDK MediaCodec and a GPU/EGL input surface.
- The Direct path does not enable `libx264`, `libx265`, or another software video encoder.
- The same camera RenderTexture and hardware input-surface path can be reused for another hardware codec.
- Direct recording exposes 720p, 1080p, and 1440p; 30 or 60 FPS; target and peak bitrate; CBR or VBR; an encoder tuning choice; AVC profile and level; GOP interval; and AAC bitrate.
- The Direct encoder explicitly sets `ndk_codec=1`, disables B-frames, and passes bitrate mode, maximum bitrate, and complexity through Saber Stage's private FFmpeg patch.
- Encoded video packets already support bounded worker/fan-out handling rather than performing file I/O in the encoder callback.
- Livestream audio is already encoded to AAC in real time.
- Direct livestreaming already consumes the hardware AVC output rather than launching a second video encoder.
- Local recording and streaming use the same Direct FFmpeg hardware encoder path.

The present local-recording format is not yet the intended final architecture:

```text
hardware AVC -> .partial.h264
game audio   -> .partial.wav
Stop         -> AAC encode and MP4 mux -> .partial.mp4 -> final .mp4
```

This means normal Stop must wait for finalization, a new recording cannot safely begin immediately, and a crash can leave raw files that an ordinary user cannot play or repair.

## Non-negotiable architecture rules

### Hardware-only video encoding

Saber Stage must not add:

- `libx264`;
- `libx265`;
- another FFmpeg software video encoder;
- a CPU RGBA-to-YUV video-encoding fallback.

When a requested setting is unsupported, Saber Stage should try a safer configuration on the same hardware codec. It may use a hardware AVC fallback when HEVC itself cannot be configured and the selected fallback policy allows that. It must never silently fall back to software video encoding.

### Fragmented MP4 is the primary local recording format

The primary recording sink should write playable, progressively recoverable fragmented MP4 while capture is running:

```text
hardware video encoder ----+
                            +-> shared live fragmented-MP4 writer
real-time AAC encoder ------+
```

The writer should:

- write the MP4 initialization data as soon as the required stream formats are known;
- create independently recoverable, keyframe-aligned `moof`/`mdat` fragments;
- flush completed fragments without forcing a full-file finalization pass;
- retain only an incomplete final fragment as the likely loss after a crash;
- close the current fragment quickly on Stop so another recording can begin immediately;
- work with both hardware AVC and hardware HEVC packets;
- avoid H.264-only assumptions in keyframe, parameter-set, or recovery logic.

Direct FFmpeg should feed the same live recording-sink contract for every compatible local and remote consumer. Packet and timestamp limitations must be explicit rather than hidden.

### Optional normal-MP4 conversion

Provide a user option to remux a completed fragmented MP4 into a conventional MP4.

Although both files use the `.mp4` extension, the Files panel must distinguish fragmented and conventional MP4 files from recording metadata or container inspection rather than relying on the filename extension. Every fragmented MP4 that has not already been converted should have a file-row action labeled `Make Standard MP4` or `Convert for Compatibility`. A tooltip should explain that the operation reorganizes the file for broader editing compatibility without re-encoding or reducing quality.

This conversion must:

- use stream copy, not video or audio re-encoding;
- preserve recorded quality exactly;
- run as a background storage task;
- never prevent a new recording from starting;
- yield, pause, or wait when active recording needs storage bandwidth;
- show current percentage, processed and total bytes, and a smoothed estimated time remaining;
- provide a Cancel action and allow the progress popup to be dismissed while work continues in the background;
- continue exposing progress and cancellation from the corresponding file row after the popup is dismissed;
- verify the conventional MP4 before deleting its fragmented source;
- preserve the source and report a useful error if conversion fails;
- check that enough temporary free space exists before starting.

Use a clearly owned temporary name such as `RecordingName.standard-remux.partial.mp4`. Cancellation must close the worker and file handles, delete only that conversion output, preserve the fragmented source, and restore the file-row conversion action. Failed conversions must follow the same source-preserving behavior.

Saber Stage should scan for its own abandoned conversion outputs once during startup, when the Files panel opens or is manually refreshed, and after cancellation or failure. It must not continuously poll storage while the menu is open. Cleanup must distinguish an abandoned conversion from an active conversion and from a crash-recoverable recording fragment through operation/session ownership plus a conservative age check.

Whether automatic conversion is enabled by default remains a product decision. Automatic conversion, the manual file-row action, and recovery are separate operations and must not be conflated.

### Crash recovery

On startup, Saber Stage should scan its own unfinished fragmented recordings and classify each one as:

- cleanly completed;
- recoverable through the last complete fragment;
- unrecoverable.

Recovery should validate the initialization section, identify the last complete fragment, ignore or truncate only an incomplete tail, and publish a playable recovered file without re-encoding. The UI and logs should clearly report what was recovered and what, if anything, was lost.

### Livestreaming is an explicit exclusive mode

Livestreaming should not be an automatic extension of ordinary local recording. When the user starts a livestream:

1. Any active local-only recording is cleanly stopped and its current fragment is closed.
2. Saber Stage switches to a separate livestream profile.
3. Stream-compatible AVC, rate-control, bitrate, resolution, frame-rate, GOP, and AAC settings are enforced.
4. One hardware AVC encoder feeds the network sink.
5. If `Save Local Copy` is enabled, the exact same encoded AVC and AAC packets are also written to a local fragmented MP4.

The local copy of a livestream must not be separately encoded or use different codec, bitrate, frame-rate, GOP, or audio settings from the public stream.

Local-recording and livestream profiles must persist independently. Starting a stream must not overwrite the user's preferred local AVC or HEVC configuration.

The menu should present both profiles in one `Recording` tab with a mode selector at the top:

```text
Recording Mode
    Local Recording
    Live Stream
```

Selecting a mode should replace the controls below it with that mode's panel. The local panel shows only the independently persisted local-recording profile. The livestream panel shows only the independently persisted livestream profile. Changing modes must never copy values between the profiles. An optional local copy made during a stream uses the livestream profile because it contains the exact packets being broadcast.

The mode selector must be locked while either a local recording or livestream is active. Its hover explanation must change with the active session, for example:

```text
Recording mode cannot be changed while a recording is in progress. Stop and save the recording first.
```

or:

```text
Recording mode cannot be changed while livestreaming. Stop the livestream first.
```

## Incomplete hardware-encoder capability work

### 1. Effective encoder configuration diagnostics

The current implementation knows what it requests but does not yet provide a complete report of what Android actually selected and accepted.

Add diagnostics for:

- requested FFmpeg encoder name;
- Android codec implementation and canonical names;
- encoder, vendor, hardware-accelerated, and software-only flags;
- codec MIME;
- requested and effective profile and level;
- requested and effective rate-control mode;
- target and peak bitrate;
- GOP/keyframe interval;
- B-frame configuration;
- requested and effective complexity;
- requested and effective quality;
- requested and effective QP bounds;
- relevant MediaFormat keys sent to MediaCodec;
- output format values reported after the codec starts.

The existing private FFmpeg patch forwards maximum bitrate and complexity, but device testing still needs to prove the effective Qualcomm MediaCodec configuration rather than assuming FFmpeg option names map exactly to Android behavior.

### 2. Hardware codec enumeration

Saber Stage does not yet enumerate and classify every encoder candidate for `video/avc` and `video/hevc`.

Add a small Android/JNI capability bridge using `MediaCodecList` and `MediaCodecInfo`. This is preferable to relying only on current NDK APIs because the comprehensive native codec-info APIs are not available on Saber Stage's Quest API baseline.

Reject software-only encoders and prefer the existing known-working vendor hardware implementation unless a different hardware codec is deliberately selected for a verified reason.

### 3. Per-codec capability model

Add a cached structure resembling:

```text
HardwareEncoderCapabilities
    codec type and MIME
    codec and canonical names
    encoder / hardware / software / vendor flags
    bitrate range
    width and height ranges and alignment
    frame-rate and size-plus-rate support
    supported bitrate modes
    complexity range and reported default
    quality range and reported default
    QP-bound feature support
    profiles and levels
    maximum instances, when useful
    achievable/performance frame-rate data, when available
```

AVC and HEVC capability records must remain independent. Cache keys should include enough device, OS, codec, MIME, resolution, frame-rate, and profile information to invalidate stale results safely after a headset or OS change.

### 4. Actual device defaults

Inspect `CodecCapabilities.getDefaultFormat()` for complexity, quality, bitrate mode, profile, and level defaults.

`Device Default` must mean leaving the value unset or using a value actually reported by the codec. Do not treat the midpoint of a supported numeric range as the default.

### 5. Encoder effort

The current Performance/Balanced/Quality mapping uses fixed numeric complexity values rather than the active codec's advertised range and default.

Replace that assumption with capability-driven choices:

- Reduced;
- Device Default;
- Higher;
- Custom;
- optionally Minimum, if device testing proves it useful.

`Reduced` means one valid supported step below the codec's own reported default. If the default is unknown, keep Device Default and Custom available but do not invent a relative Reduced value.

The intended large-file strategy is lower or default encoder effort plus more bitrate, not automatically increasing encoder computational effort.

Present these choices in a dropdown. Every choice needs an average-user tooltip and must avoid promising an unverified performance or quality result:

- `Reduced`: uses a lower supported hardware-encoder effort. This may reduce encoder load or backpressure and improve recording stability, but can require more bitrate and storage to preserve comparable quality.
- `Device Default`: lets the headset use its normal hardware-encoder behavior and is the safest compatibility choice.
- `Higher`: allows the hardware encoder to spend more effort compressing each frame. This may improve compression efficiency but may increase encoder load.
- `Custom`: selects a specific supported hardware-encoder effort value and is intended for advanced testing.

Quest measurements must establish whether Reduced has a meaningful effect on gameplay, power, backpressure, file size, or image quality. The UI must use terms such as `may` rather than claiming that Reduced always improves gameplay or never affects quality.

### 6. Rate-control modes

The current UI exposes CBR and VBR. Add capability-driven support for:

- Device Default/Auto;
- Constant Quality (CQ);
- VBR;
- CBR;
- CBR with frame dropping (CBR-FD).

Use a dropdown containing only modes supported and validated for the active hardware codec. Do not list unsupported modes in the user-facing dropdown. Diagnostics may still report the complete capability result. CBR-FD should not become the quality-oriented local-recording default.

CQ must pass real configuration and output validation before being marked usable. An advertised numeric range alone is insufficient.

### 7. Bitrate and peak-bitrate validation

Replace fixed UI assumptions with the active codec's `VideoCapabilities.getBitrateRange()` plus validation of the complete candidate format.

Validation must include:

- codec and MIME;
- profile and level;
- width, height, and alignment;
- frame rate;
- rate-control mode;
- target and peak bitrate;
- complexity, quality, and QP bounds when present.

Use `areSizeAndRateSupported()` and `isFormatSupported()` where meaningful, then treat successful real codec configure/start as the final authority.

Do not impose an artificial low bitrate ceiling for storage conservation. Large files are an intentional supported use case.

Peak bitrate must never be lower than target bitrate. Apply mode-specific behavior:

- CBR: lock peak to target or hide peak because a separate user value is not useful.
- VBR manual: clamp or reject any peak below target with a clear explanation.
- VBR automatic: calculate peak from the active tested quality policy and target, display the effective result, and clamp it to the codec's validated range.
- CQ: hide or disable peak unless the active hardware codec demonstrates meaningful peak-bitrate support in CQ mode.

Add `Auto Peak Bitrate` for users who do not want to choose a numeric peak. There is no universal correct multiplier. A tentative ratio such as 1.25 to 1.5 times target must not become a claimed Quest default until device testing establishes an appropriate codec-, resolution-, FPS-, and preset-specific policy.

### 8. Constant-quality controls

If the active hardware codec genuinely supports CQ, query its implementation-specific quality range and default. Normalize the supported range into understandable labels while retaining and logging the actual value.

Do not assume equal numeric quality values mean equal output across headsets or codecs.

### 9. QP-bound quality guard

Detect `FEATURE_QpBounds` before exposing or applying QP controls.

Add a high-level `Compression Quality Guard` that primarily constrains maximum QP so difficult scenes can consume more bitrate rather than degrading excessively. Named values must be derived from real Quest recordings, not universal hard-coded assumptions.

Advanced settings may expose per-frame-type minimum and maximum QP values. Hide B-frame controls when the active encoder produces no B-frames. Avoid forcing an unnecessarily low minimum QP.

### 10. Quality-strategy presets

Add capability-aware presets such as:

- Storage Saver;
- Default;
- High Quality;
- Very High Quality;
- Maximum Quality / Large File;
- Custom.

These are the basic bitrate controls. Ordinary users should not need to understand Mbps. A tooltip should explain that the setting changes how much video data is used per second, that higher settings generally reduce compression artifacts while producing larger files, and that resolution and frame rate are configured separately. The effective numeric bitrate remains visible in Advanced Controls and diagnostics.

`Maximum Quality / Large File` should retain the selected resolution, FPS, and hardware encoder while allowing more bitrate, using Reduced effort when safely supported, and applying a tested maximum-QP guard where available.

Preserve explicit target and peak bitrate controls even when presets are available.

### 11. Storage estimates and low-space behavior

Show informational estimates such as MB/minute and GB/hour from configured video target bitrate plus audio bitrate. CQ should be labeled variable unless an observed-history estimate is clearly identified as such.

Keep real low-space protections, but do not prevent a high-bitrate selection merely because it intentionally produces a large file.

### 12. Safe per-setting fallback

Build an ordered fallback ladder that removes only unsupported optional settings before abandoning the selected hardware codec. Examples:

```text
CQ -> VBR
Reduced effort -> Device Default
QP bounds -> no QP bounds
requested profile/level -> nearest verified profile/level
```

Every fallback must be logged and shown to the user without crashing recording. Silent software fallback is prohibited.

### 13. Post-recording statistics

For each completed recording, report and log:

- codec;
- duration;
- file size;
- average video and total bitrate;
- requested target and peak bitrate;
- effective rate-control mode;
- effective complexity and quality;
- effective QP guard;
- scheduled, submitted, encoded, and dropped frames;
- encoder queue or backpressure statistics;
- fragment and recovery status;
- whether a normal-MP4 remux was performed.

Add a setting named `Show Post Recording Summary`. It defaults to OFF and controls only whether a popup appears after a recording stops. Statistics must always still be collected, written to diagnostics, stored with the recording catalog entry, and accessible through a `Details` action in the Files panel.

The optional summary popup should prioritize duration, file size, codec, resolution/FPS, average bitrate, encoded/dropped frames, recovery state, container state, and conversion result. More specialized values such as complexity, quality, QP bounds, backpressure, and fallbacks can appear under expandable technical details.

### 14. Basic and advanced recording controls

Place an `Advanced Recording Controls` switch near the top of both the local-recording and livestream panels. Local and livestream advanced settings remain separate.

When Advanced Recording Controls is OFF:

- apply recommended automatic/default values rather than hidden manual overrides;
- retain the user's saved advanced values without applying, resetting, or overwriting them;
- show a concise indication that saved advanced settings are not currently active;
- expose only approachable controls such as codec where applicable, resolution, FPS, and the named recording-quality strategy.

When Advanced Recording Controls is ON:

- apply the user's saved advanced values;
- restore the previously saved values exactly;
- expose numeric bitrate, Auto Peak Bitrate, rate control, encoder effort, profile, level, GOP, CQ quality, QP guard/bounds, AAC bitrate, and other validated advanced controls.

Every adjustable recording value needs a reset button using Saber Stage's established reset glyph on the right side of its row. Reset is context-aware: local and livestream values reset independently, AVC and HEVC values reset independently, and capability-dependent values reset to a valid recommended value for the active profile and codec. Each reset button needs a tooltip describing exactly what it restores.

### 15. Recording-profile import and export

Allow advanced users to export and import versioned recording profiles so tested settings can be shared.

The profile format should be human-readable, schema-versioned JSON and include:

- profile name and description;
- local-recording or livestream profile type;
- codec, resolution, and FPS;
- quality strategy;
- rate control, target bitrate, peak bitrate, and Auto Peak state;
- encoder effort, profile, level, GOP, CQ quality, QP settings, and AAC bitrate where relevant;
- exporting Saber Stage version and profile schema version.

Profiles must never contain stream keys, account credentials, or private service tokens. On import, validate every requested value against the current headset and codec before applying it. Show unsupported values and the proposed safe alternatives. Preserve the imported requested values separately from effective device fallbacks where practical so a profile is not destructively rewritten merely because the current headset supports less.

## Incomplete AVC/HEVC codec work

### HEVC availability

Saber Stage's pinned FFmpeg source supports `hevc_mediacodec`, but the private runtime currently builds only `h264_mediacodec`. HEVC is therefore not presently available in the mod.

Add HEVC by extending the shared hardware-encoder path rather than creating a second camera or CPU conversion path.

### Codec selector and codec-specific state

Add a local-recording codec selector:

```text
H.264 / AVC - best compatibility
H.265 / HEVC - better compression efficiency; requires HEVC-compatible playback/editing software
```

Enable HEVC only after a valid hardware encoder has been discovered and configured. Changing codec must refresh every capability-dependent control and must not leave stale AVC ranges or values visible for HEVC.

Persist codec-specific profiles independently. If a saved codec becomes unavailable, fall back safely to hardware AVC and notify/log once.

### Shared zero-copy input path

AVC and HEVC must use the same Saber Stage camera RenderTexture, EGL bridge, and MediaCodec Surface-input path. Neither codec may introduce `ReadPixels`, GPU-to-CPU frame copies, or CPU RGBA-to-YUV conversion.

### HEVC muxing and fragmentation

The shared fragmented-MP4 writer must support correct HEVC sample entries and extradata (`hvc1` or `hev1` as appropriate), and must handle VPS/SPS/PPS plus HEVC IDR/CRA behavior.

Fragment boundaries, recovery, and conventional-MP4 remuxing must be codec-agnostic. No recovery logic may assume only AVC SPS/PPS or AVC NAL/keyframe rules.

### Codec fallback policy

When HEVC is selected, first relax unsupported HEVC options while keeping the HEVC hardware codec:

```text
HEVC CQ -> HEVC VBR
HEVC Reduced effort -> HEVC Device Default
HEVC with QP guard -> HEVC without QP guard
```

Only failure of the HEVC encoder itself should allow a visible hardware-AVC fallback, subject to the user's selected policy. Never fall back to software.

### HEVC profile, level, and bit depth

Enumerate supported HEVC profiles and levels and log requested versus effective output values.

Initial HEVC support should remain 8-bit SDR 4:2:0. Ten-bit, HDR, AV1, and VP9 are outside this milestone unless separately designed and gated later.

### Livestream codec scope

Initial livestreaming should remain AVC/H.264 because the present RTMP/FLV packet path, sequence-header logic, and service compatibility are AVC-specific.

HEVC should initially be a local-recording option. It should not be exposed for livestreaming until the chosen services, transport/container, and packetization support it explicitly.

## Revised implementation order

### Phase 1 - Replace post-stop raw-track finalization

1. Define a shared codec-neutral encoded-video and encoded-audio packet contract.
2. Add a shared live fragmented-MP4 writer for hardware AVC plus real-time AAC.
3. Route Direct FFmpeg local recording through that writer.
4. Adapt the current local recording output to the same sink contract used by remote consumers.
5. Remove the requirement to retain a raw WAV for normal local recording by encoding AAC during capture.
6. Make Stop close the active fragment and release the session quickly enough to start another recording immediately.

### Phase 2 - Recovery and optional conventional MP4

7. Add startup scanning and recovery of incomplete fragmented recordings.
8. Add clean/recovered/unrecoverable classification and user-visible diagnostics.
9. Add background stream-copy remux from fragmented MP4 to conventional MP4.
10. Catalog fragmented versus conventional MP4 files and add `Make Standard MP4` to eligible file rows.
11. Add the automatic-remux toggle and manual file-management action.
12. Add progress percentage, byte counts, smoothed ETA, cancellation, background continuation, and file-row progress.
13. Add distinct conversion temporary-file ownership and safe stale-output cleanup at startup and Files-panel refresh.
14. Add free-space checks, recording-priority I/O scheduling, result validation, and safe source deletion.

### Phase 3 - Correct livestream ownership

15. Separate persisted local-recording and livestream profiles.
16. Replace separate local/live tabs with one Recording tab, a Local Recording/Live Stream mode selector, and conditional mode panels.
17. Lock the mode selector during an active session and provide a state-specific tooltip explaining why it cannot be changed.
18. Make Go Live stop and close any active local-only recording before starting the stream session.
19. Enforce stream-compatible AVC, bitrate, rate-control, FPS, resolution, GOP, and AAC settings.
20. Feed the network and optional local fragmented-MP4 copy from the same hardware AVC and AAC packets.
21. Ensure stopping the local stream copy does not incorrectly reconfigure or duplicate the live encoder.

### Phase 4 - Hardware capability foundation

22. Add the Java/JNI MediaCodec capability bridge.
23. Enumerate and classify AVC hardware encoders.
24. Add the per-codec capability model and cache.
25. Log current requested and effective encoder behavior before changing defaults.
26. Add complete format validation and ordered same-codec hardware fallbacks.
27. Replace fixed complexity assumptions with Device Default, Reduced, Higher, and Custom semantics plus clear user-facing tooltips.
28. Add capability-driven rate-control dropdowns that omit unsupported modes.
29. Add target/peak validation, mode-specific peak behavior, and Auto Peak Bitrate.
30. Add CQ, quality, and QP-bound controls.
31. Add named basic quality strategies, storage estimates, and persistent post-recording statistics.
32. Add `Show Post Recording Summary`, defaulted OFF, plus Files-panel recording details.
33. Add independent local/live Advanced Recording Controls modes that preserve but do not apply hidden manual values.
34. Add context-aware reset buttons and tooltips for every adjustable recording value.
35. Add validated, credential-free, versioned JSON recording-profile import and export.

### Phase 5 - HEVC local recording

36. Enable `hevc_mediacodec` in the private FFmpeg runtime without enabling a software HEVC encoder.
37. Enumerate and validate Quest hardware HEVC independently from AVC.
38. Add the local AVC/HEVC selector and codec-specific persisted settings.
39. Extend the fragmented-MP4 writer, recovery, and remux paths for HEVC metadata and keyframe rules.
40. Add HEVC-specific profile, level, rate-control, effort, quality, and QP validation.
41. Keep livestreaming on AVC until a separate service/transport compatibility milestone supports HEVC.

### Phase 6 - Device comparison and preset selection

42. Establish a controlled Quest 2 AVC baseline using the same map, camera, renderer settings, mod stack, resolution, and FPS.
43. Compare AVC baseline, AVC high bitrate, HEVC efficiency bitrate, HEVC equal bitrate, and Reduced-effort variants where supported.
44. Record game CPU/GPU timing, capture drops, encoder submit/drain behavior, backpressure, storage throughput, thermals, battery observations, file size, bitrate, and visual quality.
45. Repeat core capability and AVC/HEVC comparisons on Quest 3 when hardware is available; do not infer Quest 3 or Quest 3S results from Quest 2.
46. Select recommended Default, High Quality, Very High Quality, and Maximum Quality / Large File presets only from measured device evidence.

## Required validation gates

Each phase should preserve ordinary camera, preview, audio, streaming, chat, and menu behavior unless that phase explicitly changes recording behavior.

Before a new recording format or codec becomes a normal user option, verify:

- host tests and ARM64 build;
- actual hardware-codec selection on Quest;
- no software video encoder present or selected;
- correct color and orientation;
- audible game audio and stable long-session A/V sync;
- playable output after clean Stop;
- playable recovery after forced interruption;
- immediate start of a second recording after Stop;
- no destruction of the recoverable source after a failed normal-MP4 remux;
- cancellation removes only the owned conversion output and preserves the source;
- cleanup never deletes an active conversion or recoverable recording fragment;
- sustained gameplay frame rate and bounded memory/queue growth;
- correct local-copy behavior during livestreaming;
- local and livestream settings remain independent through mode changes and restarts;
- Advanced Controls OFF applies defaults without overwriting saved manual values;
- imported recording profiles cannot carry credentials and are validated before use;
- desktop `ffprobe` and decode validation;
- side-by-side motion-quality inspection, not static frames alone.

## Deferred or explicitly excluded work

This roadmap does not include:

- software AVC or HEVC encoding;
- AV1 or VP9 encoding;
- automatic resolution or FPS increases as a quality feature;
- HDR or 10-bit HEVC in the initial codec milestone;
- HEVC livestreaming over the current AVC-specific RTMP/FLV path;
- claiming Quest 3 or Quest 3S support without device evidence;
- claiming that equal CQ, quality, complexity, or QP values are comparable across different codecs or devices.

## Completion definition

This roadmap is complete only when Saber Stage can safely record crash-resilient fragmented MP4 with real-time audio, immediately begin another recording, optionally produce a verified conventional MP4 without re-encoding, and select validated hardware AVC or HEVC settings from the actual headset's capabilities.

Livestreaming is complete under this model only when it is an explicit exclusive mode using a stream-safe AVC profile and the optional local copy is produced from the exact same encoded packets sent to the network.
