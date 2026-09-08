# Chat TTS and Quest microphone audio

SaberStage has two independent optional audio sources in addition to Beat
Saber's game mix:

```text
Chat provider -> normalized panel message -> speech policy -> bounded TTS worker --+
Quest microphone -> AAudio ring -> high-pass -> gate/PTT -> compressor -> limiter --+-> existing recording/stream mixer
Beat Saber game audio --------------------------------------------------------------+
```

TTS never enters the microphone detector or DSP chain. Digital speech therefore
cannot open the voice gate. Both sources are mixed only on the existing audio
worker, preserving the current audio/video clock and sample count.

## Local chat speech

The target Quest currently exposes no service for
`android.intent.action.TTS_SERVICE`, so Android system TTS cannot provide a
portable standalone backend. SaberStage instead embeds the English-only files
from KittenTTS Nano v0.2 fp16 and runs them through sherpa-onnx 1.13.7. The
model, C API, and Android runtime archives are SHA-256 pinned in
`dependencies/kitten-tts.json`; the build prepares a deterministic model ZIP
and private-name ARM64 runtime libraries.

The model and inference libraries load lazily on the existing TTS worker when
the first accepted message needs speech. Scotland2 first mirrors the packaged
runtime libraries from shared ModData storage into Beat Saber's executable
private `files/libs` directory; SaberStage loads that private mirror because
Android mounts `/sdcard` with `noexec`. Disabling TTS destroys the synthesizer
and unloads those libraries, so the off state does not retain the neural model
in memory. KittenTTS produces 24 kHz mono float PCM. The worker converts it once
to 48 kHz and writes separate fixed-capacity headset and broadcast rings. The
headset AAudio callback and recording audio worker only copy from those rings.
They do not allocate, synthesize, access files, make network calls, or write
logs. Eight upstream English voices are available through the `Voice`
selector. SaberStage assigns stable friendly names to the upstream numbered
speaker IDs so a user can recognize and compare them easily; the persisted IDs
remain unchanged.

Each provider converts its protocol payload into the same normalized message
used by the visible SaberStage chat panel. Only messages accepted into that
panel history are offered to Chat TTS, so speech does not depend on Twitch IRC
or any other provider-specific payload. The bounded plain-text policy then
removes control characters, validates UTF-8, suppresses commands, known bots,
URLs and provider-tagged emotes according to settings, collapses repeated
punctuation/characters and adjacent repeated emote names, and applies a
UTF-8-safe length limit. The queue is FIFO and bounded. New messages are
dropped when full; messages older than the configured maximum age are discarded
when reached. Full chat text and credentials are never logged.

Disabling TTS or explicitly clearing its queue advances a cancellation
generation and clears both PCM routes. Disconnecting a chat provider stops new
messages without coupling transport lifecycle to the speech engine or
discarding messages already accepted by the panel. Sherpa's progress callback
observes that generation so cancelled neural synthesis stops without publishing
stale PCM. Shutdown stops and joins the worker before the backend, rings, or
embedded data owner is destroyed. Stream and recording stops clear broadcast
PCM at the next capture boundary without affecting private headset speech.

Defaults are deliberately conservative: TTS is off; usernames, bot filtering,
and command filtering are on; URLs and emote names are off; the queue holds four
pending messages; the message limit is 220 characters; stale speech expires
after 12 seconds; volume is 80%; speed is 1.0; output is headset-only.

## Persistent Quest microphone

The `Quest Microphone` master switch owns one shared AAudio input stream. While
enabled and permitted, capture remains open across recording and stream
transitions. Open-mic, push-to-talk, and voice-activated modes gate PCM after
capture; they never repeatedly open and close the Android device. The grip
button can be bound to the left controller, right controller, or either. A
separate 10-500 ms release-tail setting defaults to 150 ms so releasing the grip
does not clip a word or click. Input is sampled on Unity's main thread and
handed to DSP as one atomic state. An input exception, controller disconnect,
mode change, pause, stop, shutdown, or destroyed controller state clears the PTT
state and fails closed.

AAudio requests 48 kHz, mono, float PCM with the voice-communication input
preset. A two-second SPSC ring separates the realtime Android callback from the
existing audio worker. The callback performs a bounded copy only. Startup
backlog is discarded once so microphone and current game audio begin at the
same mixer instant. Missing microphone frames become silence; no samples are
inserted or removed by DSP. When recording and streaming are idle, the Unity
tick drains bounded available blocks solely to keep the level meter current and
prevent ring pressure.

DSP order is fixed:

1. optional one-pole DC/high-pass cleanup;
2. optional 0-80 ms pre-roll/delay in voice-activated mode only;
3. open/PTT/voice-activation gate with hysteresis and a smooth envelope;
4. envelope compressor;
5. makeup gain;
6. peak limiter with no look-ahead.

The default voice preset uses -38/-43 dBFS gate thresholds, independent 10 ms
attack, 200 ms hold, and 150 ms release controls, 40 ms pre-roll, a -18 dBFS
3:1 compressor with independently adjustable 8/120 ms attack/release and +3 dB
makeup, and a -1 dBFS limiter with its own 60 ms release. All of these settings
are exposed in the Audio page. DSP settings are validated both when settings
load and again inside the allocation-free processor. NaN, infinity, unsafe
ratios, invalid time constants, and inverted gate thresholds are repaired
before use. Disabling the compressor also bypasses its makeup stage rather than
quietly applying compressor output gain to an otherwise unprocessed signal.

The `Mic Output` selector routes processed microphone audio to `Local Only`,
`Stream Only`, or `Both`. The separate `Enable Quest Microphone` switch is the
capture master: disabling it releases the Android input regardless of the saved
destination. The saved microphone gain applies to either selected route. Game
sound, processed mic, and TTS are summed and clamped once for each destination.
TTS is added after mic processing. Local game-sound mute affects only game
audio, leaving selected microphone and TTS sources audible.

## UI, permissions, privacy, and diagnostics

The center menu preserves the verified three-page geometry. `Audio` is Tab 2
and groups source/routing and microphone mode/gate controls. Compressor and
limiter settings use separate headings and independent reset actions so the
controls owned by each processor remain unambiguous.
`Chat TTS` is Tab 3 and groups enable/actions, content policy, voice/output,
and queue limits. Controls remain within each page's vertically scrollable
content. Center-page rows use the proven 116-unit span between the outer tab
captions, with related controls paired across the row; they do not inherit the
narrower right-side-menu sizing.

The Audio page includes a ticked -65 to +5 dBFS microphone meter. The full meter
width is divided into a green safe range, an orange -20 through -11 dBFS range,
and a red -10 through 0 dBFS range. Any value at or above 0 dBFS replaces the
entire fill with red to make clipping unmistakable. A dedicated Mic State field
beside the bar is green while Open/PTT Open/Gate Open and red while closed, off,
or unavailable. In Voice Activated mode, the yellow gate-open marker is a
native draggable control; moving it updates the same saved threshold exposed by
the detailed Open slider. The former independent Close control is presented as
a positive Mic Cutoff Offset: an Open value of -38 dBFS and a 5 dB offset closes
the gate below -43 dBFS. Moving either Open control preserves that offset and
moves both internal thresholds together. Unity refreshes this view
at 10 Hz from atomic DSP telemetry. The realtime callback never creates UI
objects or logs meter values.

Audio controls use one typography scale for blue section headings, control
labels, and dropdown values. Microphone Mode shares a compact row with the
high-pass switch, PTT selection is sized to its short grip labels, and the two
three-slider Voice Activated rows consume the full content width. Reset Voice
Activation, Reset Compressor, and Reset Limiter are compact actions and require
confirmation before changing saved values. Only the controls owned by the
selected microphone mode are present in layout: Push to Talk, Voice Activated,
and Open each collapse the unused groups. Disabled compressor and limiter
sections similarly retain their enable switches while removing their inactive
settings from layout. Chat TTS toggle clusters use extra
separation between neighboring controls while keeping each switch beside its
own label. Voice and output selectors share one row, volume and rate share the
next, and the three queue limits fill a single equal-width row. Clearing the TTS
queue requires confirmation.

Android microphone capture requires `android.permission.RECORD_AUDIO`. MBF must
patch Beat Saber with **Microphone Access**. SaberStage distinguishes an
undeclared permission from a normal runtime request: an unpatched APK gets a
specific repatch message, while a declared permission opens Android's prompt.
If capture remains unavailable or fails, recording/streaming continues without
microphone contribution and the UI/log explains the fallback.

No raw microphone PCM is logged or saved separately. It exists only in fixed
memory rings and, when selected, in the user's requested recording or stream
mix. Logs cover low-frequency device, configuration, routing, backend,
overflow/underflow, cancellation, and shutdown events. Realtime callbacks
publish counters/error state for later reporting and never format log lines.

## Validation boundary

Host tests cover settings repair/persistence, speech sanitation, UTF-8 and
filter policy, DSP metering, gating, compression, limiting, and malformed
samples. An ARM64 link and QMOD inspection prove build/package integration only.
The first headset build still requires the functional, audio-quality,
performance (average/1%/0.1% FPS), repeated lifecycle, and long-session matrices
from the two implementation prompts before either subsystem is considered
production-ready.
