# Twitch TTS and Quest microphone audio

SaberStage has two independent optional audio sources in addition to Beat
Saber's game mix:

```text
Twitch message -> speech policy -> bounded TTS worker -> eSpeak PCM -> 48 kHz ring --+
Quest microphone -> AAudio ring -> high-pass -> gate/PTT -> compressor -> limiter --+-> existing recording/stream mixer
Beat Saber game audio --------------------------------------------------------------+
```

TTS never enters the microphone detector or DSP chain. Digital speech therefore
cannot open the voice gate. Both sources are mixed only on the existing audio
worker, preserving the current audio/video clock and sample count.

## Local Twitch speech

The target Quest currently exposes no service for
`android.intent.action.TTS_SERVICE`, so Android system TTS cannot provide a
portable standalone backend. SaberStage instead statically links eSpeak NG
1.52.0 at revision `4870adfa25b1a32b4361592f1be8a40337c58d6c`. The source
archive is SHA-256 pinned in `dependencies/espeak-ng.json`; the build prepares a
deterministic English data archive and embeds it into `libsaberstage.so`.

The backend produces signed 16-bit, mono eSpeak PCM at the engine's native
sample rate (normally 22,050 Hz). A dedicated TTS worker converts it once to
48 kHz mono float PCM and writes separate fixed-capacity headset and broadcast
rings. The headset AAudio callback and recording audio worker only copy from
those rings. They do not allocate, synthesize, access files, make network calls,
or write logs.

Incoming Twitch events pass through a bounded plain-text policy before they are
queued. It removes control characters, validates UTF-8, suppresses commands,
known bots, URLs and Twitch-tagged emotes according to settings, collapses
repeated punctuation/characters and adjacent repeated emote names, and applies a
UTF-8-safe length limit. The queue is FIFO and bounded. New messages are
dropped when full; messages older than the configured maximum age are discarded
when reached. Full chat text and credentials are never logged.

Disabling TTS, clearing its queue, or disconnecting the Twitch account advances
a cancellation generation and clears both PCM routes. Synthesis already running
may return from its one bounded synchronous eSpeak operation, but its stale
result cannot be published. Shutdown stops and joins the worker before the
backend, rings, or embedded data owner is destroyed. Stream and recording stops
clear broadcast PCM at the next capture boundary without affecting private
headset speech.

Defaults are deliberately conservative: TTS is off; usernames, bot filtering,
and command filtering are on; URLs and emote names are off; the queue holds four
pending messages; the message limit is 220 characters; stale speech expires
after 12 seconds; volume is 80%; speed is 1.0; output is headset-only.

## Persistent Quest microphone

The `Quest Microphone` master switch owns one shared AAudio input stream. While
enabled and permitted, capture remains open across recording and stream
transitions. Open-mic, push-to-talk, and voice-activated modes gate PCM after
capture; they never repeatedly open and close the Android device. The grip
button can be bound to the left controller, right controller, or either. Input
is sampled on Unity's main thread and handed to DSP as one atomic state. An
input exception, disconnect, disabled mic, or destroyed controller state fails
closed.

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

The default voice preset uses -38/-43 dBFS gate thresholds, 10 ms attack,
200 ms hold, 150 ms release, 40 ms pre-roll, a -18 dBFS 3:1 compressor with
8/120 ms attack/release and +3 dB makeup, and a -1 dBFS limiter with 60 ms
release. DSP settings are validated both when settings load and again inside
the allocation-free processor. NaN, infinity, unsafe ratios, invalid time
constants, and inverted gate thresholds are repaired before use.

Microphone routing to local recordings and live streams is independent. The
saved microphone gain applies to either selected route. Game sound, processed
mic, and TTS are summed and clamped once for each destination. TTS is added
after mic processing. Local game-sound mute affects only game audio, leaving
selected microphone and TTS sources audible.

## UI, permissions, privacy, and diagnostics

The center menu preserves the verified three-page geometry. `Audio` is Tab 2
and groups source/routing, microphone mode/gate, and dynamics/safety controls.
`Twitch TTS` is Tab 3 and groups enable/actions, content policy, voice/output,
and queue limits. Controls remain within each page's vertically scrollable
content and paired controls use the available width before adding rows.

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
