# Claude Code Implementation Task — Add OBS-Style Quest Microphone Processing to Saber Stage

## Purpose

Implement a **production-quality microphone control and processing subsystem** for Saber Stage's recording and live-streaming audio path.

The feature must support:

- Quest headset microphone enable/disable;
- push-to-talk (PTT);
- optional voice activation / noise gate;
- microphone level metering;
- compressor;
- makeup gain;
- limiter;
- sensible presets/defaults;
- safe routing into recording/live-streaming audio;
- detailed diagnostics and error handling comparable to Big Screen.

This is not a cosmetic UI task. It must be a real realtime audio DSP pipeline designed for **Quest 2 performance, long-session reliability, and crash-safe lifecycle behavior**.

Do not destabilize Saber Stage's existing camera, recording, streaming, Twitch chat, avatar, or audio systems.

Use **Big Screen** read-only as the reference standard for diagnostic logging, failure isolation, worker/resource lifetime, shutdown discipline, and persistent error reporting. Do not modify Big Screen.

---

# 1. Mandatory checkpoint before work

Before any major edit:

1. Inspect repository status.
2. Confirm branch.
3. Confirm working-tree state.
4. Create the required pre-feature checkpoint.
5. Report the checkpoint SHA.
6. Do not begin implementation until the checkpoint exists.

Do not allow this feature to become another large uncheckpointed change set.

---

# 2. Core design principle

When microphone support is enabled, **keep the Quest microphone capture stream open**.

Do not repeatedly start/stop the Android microphone every time speech crosses a threshold.

PTT and voice activation must gate or route PCM **after capture**.

Target architecture:

```text
Quest microphone capture
        ↓
fixed-size PCM blocks
        ↓
optional DC/high-pass cleanup if justified
        ↓
PTT / voice-activation gate
        ↓
compressor
        ↓
makeup gain
        ↓
limiter
        ↓
existing Saber Stage audio mixer
       ↙              ↘
local recording      live stream
```

Optional headset monitoring should be separate and disabled by default unless existing behavior requires it.

---

# 3. Why capture remains open

Starting/stopping the mic for every utterance risks:

- first-syllable clipping;
- Android device startup latency;
- pops/clicks;
- permission/device churn;
- race conditions;
- inconsistent timestamps;
- resource leaks;
- stream desynchronization.

PTT/voice activation should control whether captured samples are passed through, attenuated, or replaced with silence.

---

# 4. Supported mic modes

Use one authoritative mode, not a collection of contradictory booleans.

Recommended modes:

```text
Off
Open / Always On
Push-To-Talk
Voice Activated
```

Optionally support:

```text
Voice Activated + PTT Override
```

where PTT forces the gate open.

Avoid impossible combinations such as both "always open" and "voice gated" simultaneously.

---

# 5. Push-to-talk behavior

Implement PTT as a low-latency gate override.

Requirements:

- configurable controller/button binding if practical;
- do not conflict with required Beat Saber controls;
- press opens quickly;
- release may use a short tail to avoid clicks;
- PTT state must be communicated to realtime audio safely;
- PTT must never start/stop the Android mic;
- lost controller/release events must fail closed.

Inspect current Saber Stage/Quest input APIs before adding new plumbing.

Do not hook unrelated input systems if a clean existing path exists.

---

# 6. Voice activation / noise gate

Implement OBS/Discord-style voice activation using signal level.

Do not attempt speech recognition.

Use a lightweight detector:

```text
RMS or stable envelope
→ dBFS
→ threshold comparison
```

Use separate open and close thresholds.

Example baseline:

```text
Open threshold:  -38 dBFS
Close threshold: -43 dBFS
```

This hysteresis prevents rapid gate chatter around the threshold.

Do not use one identical threshold for opening and closing.

---

# 7. Gate timing

Support:

```text
Attack
Hold
Release
```

Reasonable starting defaults:

```text
Attack:       5–15 ms
Hold:       150–300 ms
Release:    100–250 ms
```

The gate must not:

- cut initial consonants;
- chatter near threshold;
- close during normal pauses;
- produce clicks/pops.

Use a smooth gain envelope rather than abrupt sample discontinuities.

---

# 8. Optional pre-roll

Evaluate a small pre-roll circular buffer for voice activation.

Purpose: preserve the first phonemes that occurred just before the detector decided the gate should open.

Potential range:

```text
20–80 ms
```

If implemented:

- fixed-size;
- no allocation in realtime path;
- timestamp behavior documented;
- bounded latency;
- safe reset.

If testing shows it is not needed, do not add unnecessary complexity.

---

# 9. Mic level meter

Add a useful live meter in the audio UI.

It should show signal level in dBFS and, where practical:

- current level;
- gate threshold;
- gate/PTT open state.

Example concept:

```text
-60  -48  -36  -24  -12   0 dB
██████████████│
              ↑ threshold
```

Update at a modest UI rate rather than every audio block/sample.

No continuous diagnostic logging of meter values.

---

# 10. Compressor

Add a conventional dynamics compressor.

Minimum controls:

```text
Enabled
Threshold
Ratio
Attack
Release
Makeup Gain
```

Optional:

```text
Soft Knee
```

Only add knee control if it materially improves usability.

Reasonable baseline preset:

```text
Threshold:   -18 dBFS
Ratio:       3:1
Attack:        8 ms
Release:     120 ms
Makeup Gain:  +3 dB
```

Do not hard-code one tuning.

---

# 11. Compressor implementation requirements

Use stable gain-envelope processing.

Guard against:

- division by zero;
- invalid ratio;
- negative time constants;
- NaN/Inf;
- discontinuous gain;
- runaway makeup gain;
- bad dB conversions near zero.

Clamp and validate all user settings before they reach realtime state.

---

# 12. Limiter

Add a final peak limiter after compressor and makeup gain.

Minimum controls:

```text
Enabled
Ceiling
Release
```

Recommended default ceiling:

```text
-1 dBFS
```

A small look-ahead limiter may be considered.

If used:

- 1–5 ms is enough for this feature;
- fixed-size preallocated buffer;
- added latency documented;
- sync implications accounted for.

Do not implement a complicated mastering limiter.

---

# 13. DSP order

Default order:

```text
Mic capture
    ↓
optional DC/high-pass cleanup
    ↓
PTT / voice gate
    ↓
compressor
    ↓
makeup gain
    ↓
limiter
    ↓
mixer
```

Do not put the limiter before makeup gain.

If testing proves another order is better, document the reason.

---

# 14. Optional DC/high-pass cleanup

Investigate a very cheap DC blocker/high-pass filter for Quest mic rumble/handling noise.

Do not turn this into an EQ suite.

If added, keep it simple, cheap, and conservative.

---

# 15. Explicitly out of scope

Do not add as part of this task:

- neural noise suppression;
- AI voice isolation;
- dereverberation;
- full acoustic echo cancellation;
- voice changer;
- spectral denoiser;
- multiband compressor;
- parametric EQ suite;
- offline loudness mastering.

The requested gate/compressor/limiter chain should be very low overhead.

---

# 16. Realtime audio-thread rules

This is mandatory.

On realtime capture/mix paths, DO NOT:

- allocate;
- resize containers;
- acquire contended mutexes;
- block on condition variables;
- write files;
- perform network operations;
- call Unity APIs;
- perform Android service management;
- format complex log strings;
- flush logs;
- invoke arbitrary callbacks.

Use:

- fixed/preallocated buffers;
- simple state;
- atomics where suitable;
- bounded SPSC queues where needed;
- deterministic DSP.

---

# 17. DSP state ownership

Document:

```text
gate owner
compressor owner
limiter owner
capture owner
mixer source owner
config owner
PTT state owner
reset behavior
shutdown behavior
```

UI objects must not own realtime DSP state.

---

# 18. Configuration handoff

UI/control thread can edit settings, but realtime processing must receive a safe snapshot.

Potential approaches:

- atomics for simple scalar values;
- double-buffered configuration;
- lock-free config snapshot.

Do not lock the realtime callback around a mutable settings object.

Validate/clamp values before publishing them.

---

# 19. Microphone lifecycle

Trace end to end:

```text
mic enabled
→ permission/device availability
→ capture creation
→ format negotiation
→ capture start
→ PCM processing
→ routing
→ settings/mode changes
→ stream/record transitions
→ mic disable
→ capture stop
→ resource release
```

Audit every transition.

Do not assume the mic resource remains permanently valid.

---

# 20. Android/Quest mic failure handling

Handle at minimum:

- permission failure;
- mic unavailable;
- capture creation failure;
- capture start failure;
- invalid sample format;
- sample-rate mismatch;
- read/callback failure;
- buffer overrun/underrun;
- device/resource loss;
- shutdown race.

Mic failure must not crash Beat Saber.

Expected behavior:

```text
contextual error
→ disable mic contribution
→ stream/recording continues without mic where safe
→ user receives clear status
```

---

# 21. Stream/record routing

Use the existing Saber Stage audio mixer.

Support mic inclusion in:

```text
local recording
live stream
```

Do not create duplicate parallel mixers.

Do not accidentally add mic PCM twice.

Convert sample rate/channel format once in the correct place.

---

# 22. A/V synchronization

Do not break existing sync.

Document:

```text
mic capture timestamps
audio block duration
DSP latency
pre-roll latency
limiter look-ahead latency
mixer timing
encoder timing
```

Gate/compressor/limiter must preserve sample count.

Do not insert/remove samples dynamically.

---

# 23. PTT lifecycle safety

PTT must fail closed after:

- scene change;
- controller disconnect;
- lost input callback;
- mic disable;
- stream stop;
- recording stop;
- menu teardown;
- mod shutdown.

A missed release event must not leave the microphone effectively stuck live.

---

# 24. Gate lifecycle safety

Reset gate/envelope state appropriately when:

- mic is enabled;
- mic is disabled;
- capture restarts;
- audio format changes;
- scene changes if capture restarts;
- processing pipeline reinitializes.

Do not carry stale envelope state into a new session.

---

# 25. TTS interaction

If Saber Stage also has Twitch TTS, keep sources separate:

```text
TTS PCM ───────────────→ mixer
Mic PCM → DSP chain ───→ mixer
Game audio ────────────→ mixer
```

Do not route TTS through mic DSP.

Do not let digital TTS samples directly trigger voice activation.

If physical leakage becomes a problem, add only a measured/simple suppression policy later.

No full echo cancellation in this task.

---

# 26. Error handling — mandatory

Check every fallible operation:

- mic device access;
- permission/device state;
- capture creation;
- capture start;
- capture read/callback;
- format negotiation;
- buffer setup;
- conversion;
- mixer registration;
- PTT input registration;
- worker lifecycle;
- settings load/parse;
- stream/record routing.

Do not silently ignore error codes.

---

# 27. Exception boundaries

Where recoverable APIs can throw, use appropriate boundaries.

Do not let recoverable exceptions escape through:

- mod hooks;
- Android callback;
- audio worker;
- PTT input callback;
- stream/record startup.

Log useful details.

Do not pretend try/catch fixes:

```text
use-after-free
invalid IL2CPP object
destroyed Unity object
data race
memory corruption
SIGSEGV
```

Those require correct ownership/lifetime.

---

# 28. Big Screen reference requirement

Inspect Big Screen read-only before finalizing diagnostics.

Use it as the standard for:

- contextual logging;
- lifecycle breadcrumbs;
- error propagation;
- failure isolation;
- worker/resource ownership;
- shutdown logs;
- persistent diagnostics;
- avoiding reactive "only log after it crashed" behavior.

Do not modify Big Screen.

Do not copy unrelated architecture blindly.

---

# 29. Diagnostic logging

Design diagnostics proactively.

Log low-frequency events such as:

```text
mic enabled/disabled
capture device acquired
capture format
capture started/stopped
mic mode selected
PTT binding registered/unregistered
gate config applied
compressor config applied
limiter config applied
capture failure
buffer overrun/underrun
format conversion failure
mixer source attach/detach
stream routing change
record routing change
worker start/stop
shutdown
```

Do not log raw mic audio.

Do not log every block.

---

# 30. Gate/PTT diagnostic rules

Useful events:

```text
mic mode changed
PTT binding changed
PTT registration failed
gate disabled due to invalid config
capture format changed
```

Do not log every gate open/close in normal operation.

If transition tracing is added for debugging, make it explicit and rate-limited.

---

# 31. Error-log quality

Avoid:

```text
Mic failed
```

Prefer:

```text
[Mic] capture start failed: requested=48000Hz mono, backend=<...>, error=<...>; mic disabled, stream continues without microphone
```

or:

```text
[MicDSP] limiter config rejected: ceiling=+4.0 dBFS out of range; using safe default -1.0 dBFS
```

Every useful error should state:

- subsystem;
- operation;
- relevant lifecycle state/config;
- actual error/status;
- fallback action.

---

# 32. Crash-adjacent breadcrumbs

Persist important low-frequency lifecycle transitions:

```text
capture created
capture started
capture stopping
capture destroyed
DSP chain initialized
mixer source registered
mixer source removed
PTT input registered
PTT input removed
```

No per-sample/per-block logs.

---

# 33. Realtime error logging handoff

Never format or write logs from realtime audio callbacks.

If realtime code sees:

- overrun;
- invalid sample;
- impossible state;
- backend read failure;
- repeated clipping;

record a compact status/event and report it later on a normal thread.

Rate-limit repeated errors.

---

# 34. UI design

Add an OBS-style mic section without rewiring unrelated UI.

Suggested Basic controls:

```text
Microphone                            [On/Off]
Mic Mode                              [Open / PTT / Voice Activated]
Mic Volume                            [slider]
Normalize Voice                       [On/Off preset/helper]
Mic Level                             [meter]
```

Suggested Advanced controls:

```text
Push-To-Talk
  Binding                             [selector]
  Release Tail                        [ms]

Voice Activation
  Open Threshold                      [dBFS]
  Close Threshold                     [dBFS]
  Attack                              [ms]
  Hold                                [ms]
  Release                             [ms]

Compressor
  Enabled                             [On/Off]
  Threshold                           [dBFS]
  Ratio                               [slider]
  Attack                              [ms]
  Release                             [ms]
  Makeup Gain                         [dB]

Limiter
  Enabled                             [On/Off]
  Ceiling                             [dBFS]
  Release                             [ms]
```

Do not expose unnecessary expert complexity in the basic view.

---

# 35. UI wiring audit

Before editing, map:

```text
control
→ callback
→ setting
→ persistence key
→ runtime consumer
```

After editing, verify every new and adjacent control.

Do not:

- bind controls to the wrong settings;
- reuse another control's callback;
- create unnecessary nested groups;
- change unrelated menu layout;
- initialize before settings load;
- overwrite saved values with defaults;
- rename persistence keys casually.

---

# 36. Safe parameter ranges

Clamp all DSP parameters.

Evaluate ranges such as:

```text
Gate open threshold:  -60 to -5 dBFS
Gate close threshold: -70 to -5 dBFS
Attack:                1 to 100 ms
Hold:                  0 to 1000 ms
Release:               10 to 2000 ms

Compressor threshold:  -60 to 0 dBFS
Ratio:                 1:1 to 20:1
Attack:                1 to 200 ms
Release:               10 to 2000 ms
Makeup:                conservative bounded dB range

Limiter ceiling:       -12 to 0 dBFS
```

Adjust if testing or existing architecture suggests better bounds.

Reject/fix invalid combinations such as:

```text
close threshold above open threshold
ratio < 1
negative time
NaN
Inf
```

---

# 37. Preset defaults

Start with a sane voice preset, then validate on actual Quest mic recordings.

Example:

```text
Voice Activation
Open -38 dBFS
Close -43 dBFS
Attack 10 ms
Hold 200 ms
Release 150 ms

Compressor
Threshold -18 dBFS
Ratio 3:1
Attack 8 ms
Release 120 ms
Makeup +3 dB

Limiter
Ceiling -1 dBFS
Release 60 ms
```

Do not claim OBS-equivalent sound without on-device evidence.

---

# 38. Normalize Voice helper

If adding a simple `Normalize Voice` option, define it as enabling/loading a known-good compressor + limiter preset.

Do not implement offline normalization or full-track analysis.

This is realtime dynamics control, not destructive loudness normalization.

---

# 39. Loudness target

Do not add full LUFS normalization unless an existing Saber Stage mixer architecture already uses it and integration is trivial.

Compressor + limiter + makeup gain is sufficient for this task.

---

# 40. CPU overhead target

This DSP chain should be effectively negligible.

At 48 kHz mono, gate/compressor/limiter processing is a very small workload.

Do not introduce:

- FFTs without need;
- neural inference;
- large convolution;
- unnecessary resampling.

Benchmark rather than assume.

---

# 41. Host/DSP tests

Add deterministic tests where practical for:

- RMS/dBFS calculation;
- gate hysteresis;
- attack;
- hold;
- release;
- pre-roll if used;
- compressor transfer curve;
- compressor envelope;
- limiter ceiling;
- parameter clamping;
- NaN/Inf rejection;
- silence;
- near-zero values;
- full-scale input;
- clipping/transients;
- mode transitions;
- settings persistence.

Use generated PCM test signals.

---

# 42. Python/reference validation

Use Python simulations if useful to validate the DSP math.

Useful inputs:

- known dBFS sine wave;
- stepped amplitude;
- speech-like envelope;
- transient impulse/peak;
- silence-to-speech transition.

Compare expected gain reduction and limiter output.

Python is for testing only, not runtime.

---

# 43. Quest functional test matrix

Test:

```text
Mic Off
Mic Open
PTT press/release
PTT controller disconnect
Voice gate quiet room
Voice gate background fan/noise
quiet speech
normal speech
loud speech/yelling
long speech
rapid pauses
stream start/stop
record start/stop
pause/resume
scene transition
mic disable while streaming
mic enable while streaming
TTS active if present
```

Verify no crash, stuck mic state, or stale callback.

---

# 44. Performance benchmark

Use the existing Graphics Tweaks methodology.

Record:

```text
average FPS
1% low FPS
0.1% low FPS
```

Scenarios:

```text
baseline mic off
mic on/gated silent
mic open
voice gate active
compressor + limiter active
PTT active
Qavatars active
Big Screen 2K video
recording
live streaming
heavy lighting map
full combined workload
```

The feature is not complete until measured.

---

# 45. Audio-quality benchmark

Capture test recordings for:

```text
quiet voice
normal voice
loud voice
shouting
background fan/noise
silence
```

Evaluate:

- gate clipping;
- first-consonant loss;
- pumping;
- breathing;
- compressor distortion;
- limiter distortion;
- volume consistency;
- background noise.

Tune defaults from evidence.

---

# 46. Resource/lifecycle testing

Repeatedly:

- enable/disable mic;
- start/stop recording;
- start/stop stream;
- change modes;
- change scenes;
- reconnect controllers;
- adjust settings.

Look for:

- capture leaks;
- buffer leaks;
- worker leaks;
- duplicate PTT callbacks;
- mixer source duplication;
- stale configuration;
- stale audio sources.

---

# 47. Privacy/security

Microphone data is sensitive.

Do not:

- save raw mic audio outside requested recordings;
- log PCM;
- transmit mic anywhere except configured recording/stream path;
- enable the mic automatically without user setting;
- silently change mic routing.

Make current mic state obvious in UI.

---

# 48. Things to avoid

DO NOT:

- start/stop Android mic for each utterance;
- perform DSP on Unity thread;
- block realtime audio callbacks;
- allocate in realtime callbacks;
- log from realtime callbacks;
- add neural noise suppression in this task;
- use hard gate discontinuities;
- use one threshold with no hysteresis;
- leave PTT stuck open;
- permit invalid compressor/limiter values;
- duplicate mic in the mix;
- break A/V sync;
- modify unrelated UI;
- claim completion after only compiling/linking.

---

# 49. Completion gate

Before saying ready:

```text
[ ] pre-feature checkpoint created and SHA reported
[ ] continuous mic capture verified
[ ] mic modes implemented
[ ] PTT implemented and fails closed
[ ] voice activation with hysteresis
[ ] smooth attack/hold/release
[ ] mic meter
[ ] compressor
[ ] makeup gain
[ ] limiter
[ ] optional pre-roll evaluated
[ ] parameters clamped/validated
[ ] realtime path has no allocation/blocking/log I/O
[ ] config handoff realtime-safe
[ ] audio routing verified
[ ] A/V sync preserved
[ ] TTS interaction reviewed
[ ] Big Screen-quality error handling/logging
[ ] host/DSP tests
[ ] Quest functional tests
[ ] audio-quality recordings reviewed
[ ] average/1%/0.1% FPS benchmarks
[ ] resource/leak tests
[ ] no unrelated UI rewiring
```

If anything required remains unfinished, state:

```text
NOT READY FOR RUNTIME TESTING
```

Do not deploy a preparation build as if the feature is complete.

---

# 50. Final completion report

Provide:

```text
Checkpoint SHA:
Implementation commit SHA:
Mic capture API/backend:
Capture format:
Mixer format:
DSP order:
Mic modes:
PTT binding:
Gate defaults:
Compressor defaults:
Limiter defaults:
Fixed DSP latency:
Tests run:
Quest benchmark results:
Audio-quality observations:
Known limitations:
Diagnostic/logging additions:
Files changed:
```

---

# Final objective

> Add a low-overhead, realtime-safe, OBS-style microphone subsystem to Saber Stage that keeps the Quest mic capture stream stable while providing push-to-talk, optional voice activation, useful dynamics control, safe stream/recording routing, predictable lifecycle behavior, and Big Screen-level error handling and diagnostics without materially affecting Quest 2 performance.
