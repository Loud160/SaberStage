# Claude Code Implementation Task — Add Local Twitch Chat Text-to-Speech to Saber Stage

## Purpose

Implement a **production-quality, optional local text-to-speech (TTS) subsystem** for Saber Stage so new Twitch chat messages can be read aloud while playing Beat Saber on Quest.

This is not a prototype. Do not bolt a synchronous TTS call directly onto the Twitch callback.

The implementation must be designed for **Quest 2 performance, long-session stability, predictable failure handling, bounded resource usage, and detailed diagnostics**.

Saber Stage already has camera, camera preview, local recording, live streaming, Twitch chat, and audio mixing infrastructure. TTS must integrate into those systems without destabilizing them.

Use **Big Screen** read-only as the reference standard for logging, failure isolation, lifecycle handling, worker shutdown, diagnostics, and durable error reporting. Do not modify Big Screen.

---

# 1. Mandatory checkpoint before work

Before making any major edit:

1. Confirm repository state and branch.
2. Confirm whether the working tree is clean.
3. Create the required pre-feature checkpoint.
4. Record and report its commit SHA.
5. Do not begin implementation until the checkpoint exists.

Do not accumulate unrelated changes into this feature.

---

# 2. Required user functionality

Add an optional Twitch-chat TTS feature that supports:

- reading new chat messages aloud;
- optional username reading;
- optional bot-message filtering;
- optional command filtering;
- URL suppression;
- emote suppression or simple emote-name reading;
- maximum message length;
- bounded queue depth;
- stale-message expiry;
- TTS volume;
- speech-rate control when the backend supports it;
- output routing to:
  - headset/player only;
  - stream/recording only;
  - both.

TTS must default to **off** after update/install.

When disabled, it should add effectively zero meaningful per-frame overhead.

---

# 3. Performance requirements

Quest 2 is the conservative baseline.

Never:

- synthesize on the Unity/game thread;
- synthesize inside the Twitch callback;
- perform blocking Android calls on the Unity thread;
- perform filesystem I/O on the Unity thread;
- perform network calls for the default implementation;
- allocate large temporary buffers per frame;
- poll continuously in Update when no work exists;
- use a heavy local neural TTS model;
- log per audio block or per frame.

The existing combined Saber Stage workload can include Qavatars, map lighting, Big Screen 2K video, local recording, live streaming, and Twitch chat. TTS must coexist with that workload.

---

# 4. Backend strategy

Create a backend abstraction.

## Preferred backend A — Android/system TTS

Investigate whether the Quest OS exposes a usable Android TextToSpeech engine on the target Quest OS versions.

Do not assume it exists.

Verify on-device:

- initialization;
- language/voice availability;
- speech-rate control;
- synthesis callbacks;
- ability to obtain synthesized PCM or equivalent audio without forcing a disk-file workflow;
- utterance completion/error callbacks;
- safe shutdown.

If the Quest Android TTS service is missing or unsuitable for routing into Saber Stage's mixer, it must not block the feature.

## Preferred backend B — lightweight embedded fallback

Evaluate a lightweight local engine such as eSpeak NG or equivalent.

Requirements:

- Android ARM64;
- low CPU/memory;
- in-memory PCM synthesis;
- no cloud dependency;
- no large neural model;
- redistributable under an acceptable license.

Verify the license before adding a dependency.

---

# 5. Backend interface

Keep the interface narrow, conceptually:

```text
ITtsBackend
    Initialize(...)
    IsAvailable()
    SetVoice(...)
    SetRate(...)
    Synthesize(text, utterance/session token)
    Cancel(...)
    Shutdown()
```

The rest of Saber Stage should not contain Android-TTS-specific or eSpeak-specific logic.

---

# 6. Required architecture

```text
Twitch chat receive
        ↓
existing chat parser
        ↓
TTS eligibility/filter stage
        ↓
sanitization/normalization
        ↓
bounded request queue
        ↓
dedicated TTS worker/backend context
        ↓
PCM
        ↓
single resample/format conversion if needed
        ↓
bounded PCM/audio queue
        ↓
existing Saber Stage audio mixer
       ↙                 ↘
headset/player       stream/recording
```

The Twitch callback should enqueue a small request and return immediately.

---

# 7. Threading/lifecycle rules

Document all execution contexts:

```text
Twitch callback context
TTS queue owner
TTS worker owner
backend callback context
PCM queue owner
audio mixer context
Unity/game thread
shutdown path
```

Requirements:

- no Unity object access from arbitrary backend/background callbacks;
- no detached worker may outlive referenced state;
- workers must stop/join before owned state disappears;
- callbacks must be unregisterable/cancellable;
- do not join while holding a lock the worker needs;
- scene exit, stream stop, recording stop, menu teardown, and mod shutdown must all be safe.

---

# 8. Bounded queue design

Do not allow Twitch traffic to create unbounded speech backlog.

Use a small configurable queue, for example:

```text
Maximum queued messages: 3–5
Maximum message length: configurable
Maximum message age: configurable
One active utterance by default
```

When full, use and document a deterministic drop policy.

Prefer stream usefulness over preserving every stale message.

---

# 9. Message sanitation

Handle at minimum:

- empty messages;
- whitespace-only messages;
- URLs;
- emotes;
- repeated punctuation;
- control characters;
- Unicode;
- excessive repeated characters;
- commands;
- bot messages if configured;
- malformed text conversion;
- very long usernames/messages.

Malformed chat must never crash TTS.

Never speak hidden metadata, tokens, or credentials.

---

# 10. Username behavior

Provide:

```text
Read Usernames Aloud [On/Off]
```

If enabled, use a simple format such as:

```text
<username> says: <message>
```

Do not add expensive phonetic processing.

---

# 11. Emote behavior

Provide a clear policy, e.g.:

```text
Ignore recognized emotes
Read emote names
```

Avoid reading long repeated emote spam.

Collapse repeated identical emotes if emote reading is enabled.

---

# 12. Audio format

Prefer efficient mono speech.

A reasonable source format is:

```text
mono
16-bit PCM or float PCM
16–24 kHz source rate
```

Resample once into the existing Saber Stage mixer format.

Do not synthesize stereo unnecessarily.

Document:

```text
backend PCM format
mixer format
resampling location
buffer ownership
```

---

# 13. Audio routing

Support:

```text
Headset only
Stream/recording only
Headset + stream/recording
```

Use the existing Saber Stage mixer architecture.

Do not create a parallel audio system if the current mixer can accept another source.

TTS must be a distinct mixer source and must never be routed through microphone capture.

---

# 14. TTS/microphone interaction

Keep TTS and mic separate:

```text
TTS PCM ───────────────→ mixer
Mic PCM → mic DSP ─────→ mixer
Game audio ────────────→ mixer
```

If physical speaker/headset leakage later causes mic retriggering, consider a simple optional suppression policy only after measurement.

Do not implement acoustic echo cancellation in this task.

---

# 15. Buffering

Use bounded/preallocated buffers.

Avoid:

- unbounded PCM accumulation;
- unnecessary full-utterance copies;
- per-frame allocation;
- repeated format conversion;
- disk-backed temporary WAV files unless there is no practical alternative.

Prefer ring-buffer/bounded-queue patterns already proven in Saber Stage/Big Screen where appropriate.

---

# 16. Cancellation

Define behavior for:

- TTS disabled while speaking;
- Twitch disconnect;
- scene exit;
- stream stop;
- recording stop;
- settings changes;
- backend failure;
- queue clear;
- mod shutdown.

Current speech must be stoppable promptly, queued messages must clear safely, and no stale callback may target destroyed state.

---

# 17. State model

Avoid many unrelated booleans.

Use a coherent lifecycle such as:

```text
Disabled
Initializing
Ready
Speaking
Stopping
Failed
```

Only formalize this if it improves safety; do not overengineer trivial state.

---

# 18. Error handling — mandatory

Audit every fallible external operation:

- backend initialization;
- Android service access;
- language/voice selection;
- synthesis submission;
- callbacks;
- PCM generation;
- resampling;
- allocation;
- queue creation;
- audio-source registration;
- mixer interaction;
- shutdown.

TTS failure must never hard-crash Beat Saber.

Expected failure behavior:

```text
contextual diagnostic
→ fallback backend if available
→ otherwise disable TTS
→ gameplay/recording/streaming continues
```

---

# 19. Exception boundaries

Where APIs can throw normal recoverable exceptions, use appropriate boundaries.

Do not let them cross:

- mod entrypoints/hooks;
- Twitch callback;
- TTS worker;
- backend callback;
- audio worker.

Log useful exception information.

Do not use try/catch as a fake fix for SIGSEGV, use-after-free, invalid IL2CPP/Unity objects, races, or memory corruption.

---

# 20. Big Screen diagnostic standard

Before finalizing diagnostics, inspect Big Screen read-only and identify its established patterns for:

- subsystem-specific logging;
- lifecycle breadcrumbs;
- contextual errors;
- session/diagnostic logging;
- worker startup/shutdown;
- failure isolation;
- error propagation;
- persistent log durability;
- avoidance of hot-path spam.

Follow the same engineering philosophy in Saber Stage.

Do not copy unrelated code blindly.

---

# 21. Diagnostic logging requirements

Design logs before failures occur.

Log low-frequency events such as:

```text
TTS enabled/disabled
backend selected
backend initialization success/failure
language/voice selection
worker start/stop
queue overflow/drop
message rejected + reason
synthesis start
synthesis failure
synthesis completion
PCM conversion failure
audio source attach/detach
backend fallback
backend unavailable
shutdown
```

Do not log full chat text by default.

Never log Twitch credentials/tokens.

---

# 22. Error log quality

Avoid:

```text
TTS failed
```

Prefer:

```text
[TTS] Android backend initialization failed: status=<...>; falling back to embedded backend
```

or:

```text
[TTS] synthesis failed: backend=<...>, utteranceId=42, textLength=83, error=<...>; utterance dropped
```

Include subsystem, operation, relevant state, actual error/status, and fallback action.

---

# 23. Crash-adjacent breadcrumbs

Persist useful low-frequency lifecycle events:

```text
worker created/stopping
backend created/destroyed
mixer source attached/detached
queue cleared
utterance cancelled
```

Do not add per-block logs.

---

# 24. Log durability

Important errors and lifecycle transitions should be reasonably likely to survive a later hard crash.

Use Big Screen's durability philosophy.

Do not synchronously flush every PCM block or message.

---

# 25. Realtime logging rules

Never format/write logs from realtime audio callbacks.

If realtime code detects an error:

```text
record compact status/event
→ hand to normal worker/control thread
→ log there
```

No blocking I/O or log flushing on realtime paths.

---

# 26. UI requirements

Add controls to the existing Twitch/chat/audio section without rewiring unrelated UI.

Do not introduce unnecessary nested layout groups.

Suggested controls:

```text
Read Twitch Chat Aloud                 [On/Off]
Read Usernames                         [On/Off]
Ignore Bots                            [On/Off]
Ignore Commands                        [On/Off]
Read Emotes                            [On/Off]
TTS Output                             [Headset / Stream / Both]
TTS Volume                             [slider]
Speech Rate                            [slider]
Max Message Length                     [slider/dropdown]
Max Queued Messages                    [small dropdown]
```

Only expose settings supported by the active backend.

---

# 27. UI wiring audit

Before editing, map:

```text
control
→ callback
→ setting
→ persistence key
→ runtime consumer
```

After editing, re-verify every new and neighboring control.

Do not:

- reuse the wrong callback;
- bind multiple sliders to the wrong field;
- change unrelated menu hierarchy;
- overwrite saved settings with defaults.

---

# 28. Settings persistence and defaults

Persist through the existing Saber Stage settings system.

Recommended defaults:

```text
TTS Off
Ignore Bots On
Ignore Commands On
Read URLs Off
Read Emotes Off
small queue
reasonable max length
moderate rate
moderate volume
```

TTS must never enable itself automatically after update.

---

# 29. Host/unit tests

Where practical test:

- message filtering;
- URL removal;
- emote handling;
- max length;
- queue bounds;
- stale expiry;
- username formatting;
- drop policy;
- state transitions;
- backend fallback;
- cancellation;
- settings persistence.

Do not create meaningless tests just for coverage.

---

# 30. Quest functional test matrix

Test:

```text
TTS disabled
TTS enabled/idle
single message
username on/off
URL
emote-only
long message
Unicode
rapid message burst
queue overflow
disable while speaking
Twitch disconnect while speaking
scene transition while speaking
start/stop stream
start/stop recording
backend unavailable
shutdown
```

Verify no crash, deadlock, stale callback, or unbounded queue.

---

# 31. Performance benchmark

Use the same disciplined on-headset method already used for Qavatars.

Record:

```text
average FPS
1% low FPS
0.1% low FPS
```

Test:

```text
baseline without TTS
TTS enabled/idle
message every ~10 sec
continuous chat load
TTS + Qavatars
TTS + Big Screen 2K video
TTS + live stream
TTS + recording
TTS + heavy lighting map
full combined workload
```

Do not claim negligible overhead without measurements.

---

# 32. Resource/lifecycle testing

Repeat enable/disable, scene changes, reconnects, and long sessions.

Look for:

- worker leaks;
- JNI/global-reference leaks;
- audio-buffer leaks;
- resampler leaks;
- file-descriptor leaks;
- duplicate callbacks/listeners.

---

# 33. Things to avoid

DO NOT:

- synthesize on Unity/game thread;
- synthesize in Twitch callback;
- use a heavy neural model;
- make cloud TTS mandatory;
- use an unbounded queue;
- log per audio block;
- log credentials;
- keep callbacks alive after teardown;
- silently swallow backend errors;
- make TTS failure break stream/game;
- restructure unrelated UI;
- modify camera/recording/avatar architecture unless genuinely required;
- claim completion after only a successful compile.

---

# 34. Completion gate

Before saying ready:

```text
[ ] pre-feature checkpoint created and SHA reported
[ ] backend abstraction implemented
[ ] Android/system backend investigated on Quest
[ ] lightweight fallback investigated/implemented if needed
[ ] synthesis off game thread
[ ] bounded request queue
[ ] bounded PCM buffering
[ ] sanitation/filtering
[ ] stale/drop policy
[ ] audio routing
[ ] settings persistence
[ ] safe lifecycle shutdown
[ ] contextual error handling
[ ] Big Screen-level diagnostics
[ ] no realtime logging/file I/O
[ ] host tests
[ ] Quest functional tests
[ ] average/1%/0.1% FPS benchmarks
[ ] resource/leak testing
[ ] no unrelated UI rewiring
```

If any required item remains incomplete, state:

```text
NOT READY FOR RUNTIME TESTING
```

Do not deploy a preparation build as though it contains the completed feature.

---

# 35. Final completion report

Provide:

```text
Checkpoint SHA:
Implementation commit SHA:
Backend selected:
Fallback backend:
Audio format:
Queue limits:
Routing modes:
Tests run:
Quest benchmark results:
Known limitations:
Diagnostic/logging additions:
Files changed:
```

Separate future optional ideas such as cloud TTS, better voices, user priorities, phonetic processing, or echo cancellation.

---

# Final objective

> Add a lightweight, local, asynchronous, bounded, crash-safe Twitch chat TTS subsystem to Saber Stage that has negligible Quest 2 performance impact, integrates cleanly with the existing audio mixer, behaves predictably under chat bursts and lifecycle changes, and has Big Screen-quality diagnostics and failure handling from the first implementation.
