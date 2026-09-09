# Discord screen source

## Purpose and platform boundary

SaberStage can present its Primary third-person camera as an Android app window
named **SaberStage Camera**. The Quest Discord app can then offer that window in
Android 14's single-app screen-sharing picker. This is deliberately not a
virtual camera, Discord API integration, browser page, Discord hook, or
MediaProjection implementation inside Beat Saber.

The feature has two separately installed parts:

- SaberStage owns the spectator render, Direct MediaCodec encode, stream-audio
  mix, settings UI, and bounded packet fan-out.
- The `com.saberstage.helper` companion owns an ordinary resizable Android
  activity, session-only foreground service, loopback receiver, and hardware
  H.264 decoder plus capturable PCM playback.

Discord retains ownership of capture consent, the selected app, network
transport, and stream controls. SaberStage transports the same selected game
sound, Quest microphone, and Chat TTS mix used by its direct livestream path;
the helper publishes that mix as Android app audio associated with the selected
`SaberStage Camera` window.

## User flow

1. In SaberStage, open **Live Stream** and press **Discord Live Steam**.
2. If the helper is absent, accept the download prompt. SaberStage opens the
   stable current-release URL for `SaberStage-Helper.apk` in the Quest browser.
3. Open the downloaded APK with Quest Package Manager and follow its install
   prompts. Sideloaded APK installation requires Unknown Sources to be enabled.
4. Return to Beat Saber and press **Discord Live Steam** again. SaberStage
   opens Discord first, then launches **SaberStage Camera** and begins one
   authenticated local session.
5. Select the Discord panel that SaberStage already opened; do not relaunch it
   from the application library. Start screen sharing, select **SaberStage
   Camera**, and enable application audio in Discord's share controls.
6. Return to Beat Saber. Press **Stop Discord Source** when finished.

The download action targets
`https://github.com/Loud160/SaberStage-Helper/releases/latest/download/SaberStage-Helper.apk`.
Every published helper release must therefore attach its APK using that exact
asset name. The mod never silently downloads or installs an APK.

The helper launch is intentionally visible because Android and Discord require
the user to select an actual app window. SaberStage does not automate that
consent or selection. Discord is opened first because launching it from the
Quest library after the source is visible can remove the helper's root task
before Discord creates its source list. Launching the source second leaves both
panels available without requiring a fragile manual startup order.

Android 11+ package visibility can hide a sideloaded helper from Beat Saber's
`PackageManager` query. A null query result is therefore treated as unknown,
not proof that the APK is missing. SaberStage attempts the explicit exported
activity launch and shows the installation workflow only when Android returns
`ActivityNotFoundException`. Other launch failures produce a normal diagnostic
error instead of incorrectly asking the user to reinstall the helper.

## Media and performance design

The Discord sink requires SaberStage's Direct hardware encoder. It reuses an
already active Direct local recording or Twitch stream; otherwise it starts a
stream-only Direct capture with no local files. One existing bounded PCM worker
mixes game sound, the optional Quest microphone, and Chat TTS, then fans the
same mix out to Twitch and/or the helper. Encoded H.264 Annex-B access units and
signed 16-bit PCM packets are copied into one 8 MiB bounded transport queue. A
private worker performs all socket I/O. Overflow drops queued packets, waits for
a new video keyframe, and increments separate visible video/audio diagnostics
instead of blocking the Unity, audio, or encoder callback threads.

The helper decodes through Android `MediaCodec` directly to its activity
surface. It does not decode and re-encode video in software. Audio is written on
a separate bounded worker to an Android `AudioTrack` using `USAGE_GAME` and
`ALLOW_CAPTURE_BY_ALL`; the helper manifest also opts into playback capture.
No AAC encoder is added for Discord. A random 256-bit session token is delivered
by the explicit activity launch and must match the first loopback `HELLO`
message before media is accepted.

The helper binds specifically to IPv4 `127.0.0.1:39781`. Using Android's
generic loopback resolver could select IPv6 `::1` while SaberStage's native
client connects to IPv4, producing a connection failure even though both sides
are on the same headset.

The protocol-v2 success acknowledgement remains backward compatible with the
original `ready` payload and may append one line describing the decoder. The
current helper selects an AVC decoder from Android's `MediaCodec` registry,
creates that exact decoder when the first usable keyframe arrives, and reports
its codec name, Android acceleration classification, resolution, and frame
rate. SaberStage displays this line in the Live Stream status area rather than
overlaying it on the Discord-captured helper surface.

## Lifecycle and failure behavior

The helper has no boot receiver, scheduled job, or permanent background
listener. Its foreground service exists only for an active camera-source
session. An intentional stop sends a protocol `STOP`; loss of Beat Saber or the
socket ends the helper after a short reconnect allowance; a five-second message
timeout catches a wedged sender. The helper also closes when its source task is
explicitly dismissed.

Starting without the companion installed returns a user-facing error. Helper
connection and decoder failures are isolated from an intentional local
recording or Twitch stream. A Discord-only failure releases its stream-only
camera demand and creates no local media file.

The movable Stream control panel treats Twitch and Discord as live outputs.
Pause keeps the active transport connected, replaces the video with the user's
AFK image (or SaberStage's built-in default), and sends silent audio. Resume
restores the camera and stream mix. Stop ends every active live output and sends
the helper's authenticated `STOP` message so its foreground service and source
activity close instead of remaining resident.

## Verification boundary

Host tests and a successful ARM64 link prove state and package invariants only.
Building and signing the helper proves that Android accepts the APK structure.
They do not prove that a particular Quest/Discord release lists, captures, or
keeps the helper activity alive. On-device acceptance must separately verify:

- the helper appears as **SaberStage Camera** in Discord's source picker;
- the automatic Discord-first, helper-second launch leaves both panels
  available without requiring the user to discover a strict startup order;
- video continues after returning to Beat Saber;
- the decoder line shown by SaberStage matches the codec Android actually
  creates and classifies it as hardware accelerated on supported devices;
- Discord receives the helper application's audio when application audio is
  enabled, with game sound, microphone, and TTS following SaberStage's existing
  stream-audio settings;
- helper playback does not create an unacceptable doubled local-audio path on
  the tested Quest/Discord versions;
- orientation/aspect ratio and latency are acceptable;
- stop, Beat Saber termination, and transport loss remove the helper service;
- local recording and Twitch sharing continue independently when attached;
- helper audio remains synchronized closely enough with the decoded video for
  the intended Discord stream.

The loopback wire format is documented in the companion repository's
`docs/PROTOCOL.md`.

Meta's experimental Horizon OS Virtual Camera Publisher may eventually provide
a direct system camera path, but it is not used here. Its current requirements,
Discord compatibility questions, and proof-of-concept gates are recorded in
[`planning/24_FUTURE_HORIZON_OS_VIRTUAL_CAMERA_PUBLISHER.md`](planning/24_FUTURE_HORIZON_OS_VIRTUAL_CAMERA_PUBLISHER.md).
