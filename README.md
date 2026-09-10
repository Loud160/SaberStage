# SaberStage

<p align="center">
  <strong>A Quest-native third-person camera, recording, and livestreaming stage for Beat Saber.</strong>
</p>

<p align="center">
  <img alt="Platform" src="https://img.shields.io/badge/platform-Meta%20Quest%202%20%7C%203%20%7C%203S-00b2ff">
  <img alt="Beat Saber" src="https://img.shields.io/badge/Beat%20Saber-1.40.8-orange">
  <img alt="Language" src="https://img.shields.io/badge/language-C%2B%2B20-blue">
  <img alt="Status" src="https://img.shields.io/badge/status-early%20development-yellow">
</p>

SaberStage is an early-development, clean-room native Quest camera, recording,
and broadcast mod for Beat Saber.

It is **not ready for normal use**. The current development build provides one
real `primary` spectator camera and a Camera2-familiar native editor: persisted
placement, FOV, resolution, FPS, camera MSAA, controller-grab placement,
player/head/static anchoring, smoothing, optional anchored float, bounded
Camera2-format movement scripts, movable and floor previews, and scene/recenter
recovery. Rendering is consumer-driven, so an unused camera does not keep
rendering off-screen frames.

Local recording uses SaberStage's Direct FFmpeg hardware path with
720p/1080p/1440p, 30/60 FPS, bitrate and codec controls, synchronized game
audio, pause/resume, gameplay-only mode, and uniquely named MP4 output.
Recordings continue across ordinary menu, loading, gameplay, and results scenes
until explicitly stopped.

Twitch-first livestreaming reuses the same camera and hardware encode path.
SaberStage supports service-specific RTMP/RTMPS settings, encrypted OAuth tokens
backed by Android Keystore, title updates, automatic token refresh, an HMD-only
movable chat panel, current-map announcements, AFK media, stream audio mixing,
bounded reconnect behavior, optional OBS-style Quest microphone processing, and
fully local bounded neural speech for messages accepted by the chat panel.
Twitch, Kick, YouTube, and custom RTMP/RTMPS destinations can share one hardware
encode while retaining independent connection state. Rich Twitch chat and song-request work is still under
development. TTS defaults off; microphone capture requires Beat Saber to be
patched with MBF's Microphone Access permission.

Discord app sharing is available through the separately installed
**SaberStage Helper** APK. The mod reuses its existing Direct hardware encoder,
feeds bounded H.264 packets and the existing game/microphone/TTS stream-audio
mix over authenticated Quest loopback, and presents them through an ordinary
Android activity named `SaberStage Camera`. This lets the Quest Discord app
select the third-person view and capturable app audio through Android 14's
app-sharing picker without a browser, desktop relay, second encoder, or
persistent boot service. The center menu's **Live Stream** tab checks whether
the helper is installed, offers the newest GitHub release when an explicit
launch proves it is absent, opens Discord before the camera-source window, and
starts or stops the session-only helper service. The status area reports the
exact Android `MediaCodec` decoder selected for the negotiated format so
hardware acceleration can be verified without placing diagnostics inside the
Discord-captured window. See
[Discord screen source](docs/DISCORD_SCREEN_SOURCE.md).

SaberStage no longer owns avatar loading, rendering, IK, calibration, or avatar
profiles. Compatible standalone avatar mods may place their output in the game
scene for the third-person camera to capture, but their behavior and settings
remain independent from SaberStage.

The product boundary and gated build sequence are documented in
[the project charter](docs/planning/00_PROJECT_CHARTER.md), with the runtime
ownership model in [the architecture document](docs/ARCHITECTURE.md).

## Target and toolchain

- Beat Saber Quest target: `1.40.8_7379`
- Scotland2 `^0.1.6`
- beatsaber-hook `^6.4.2`
- bs-cordl `4008.*`
- BSML `^0.4.55`
- custom-types `^0.18.4`
- Quest SongCore `1.1.26`
- [Native Logger Quest](https://github.com/Loud160/NativeLoggerQuest) `1.0.0`
  (SHA-pinned and statically linked)
- QPM CLI `1.5.11`
- Android NDK r27d (`27.3.13750724`)

## Build and diagnostics

See [Build and deploy](docs/BUILD_AND_DEPLOY.md).

SaberStage writes current and previous logs under
`/sdcard/ModData/com.beatgames.beatsaber/Mods/SaberStage/Logs/`. Windows users
can run `Collect-SaberStage-Logs.bat`; Linux users can run
`./Collect-SaberStage-Logs-Linux.sh`. Support archives also retain a filtered
Paper2 excerpt because third-party dependencies may use Paper2, but SaberStage
itself does not depend on Paper2.

Completed recordings are stored under `/sdcard/Oculus/VideoShots` with a
`SaberStage_` filename prefix. They can be copied without deleting the Quest
original by running `python scripts/quest_tool.py pull-recordings` during an
authorized headset session.

## Project layout

```text
include/saberstage/    Public declarations and platform-neutral interfaces
src/                   Quest camera, UI, recording, streaming, and chat runtime
tests/                 Host C++ tests and repository/package invariant tests
scripts/               Dependency, build, package, deploy, removal, and support tools
assets/                Embedded runtime shaders, icons, and default AFK media
docs/                  Architecture, feature, test, and development records
tools/runtime-shaders/ Minimal Unity project for Quest runtime shader bundles
```

## Documentation

- [Architecture](docs/ARCHITECTURE.md)
- [Lifetime and threading rules](docs/LIFETIME_AND_THREADING.md)
- [Build, package, and development deployment](docs/BUILD_AND_DEPLOY.md)
- [Test and device gates](docs/TEST_PLAN.md)
- [Recording pipeline](docs/RECORDING_PIPELINE.md)
- [Direct FFmpeg and livestreaming](docs/DIRECT_FFMPEG_AND_LIVESTREAM.md)
- [Streaming architecture](docs/STREAMING_ARCHITECTURE.md)
- [Discord screen source](docs/DISCORD_SCREEN_SOURCE.md)
- [Future Horizon OS virtual camera publisher concept](docs/planning/24_FUTURE_HORIZON_OS_VIRTUAL_CAMERA_PUBLISHER.md)
- [Twitch chat and song requests](docs/TWITCH_CHAT_AND_REQUESTS.md)
- [Chat TTS and Quest microphone audio](docs/CHAT_TTS_AND_MICROPHONE_AUDIO.md)
- [Third-party notices](docs/THIRD_PARTY_NOTICES.md)
- [Contributing](CONTRIBUTING.md)
- [Security reporting](SECURITY.md)

## Development status

Host tests, an ARM64 link, and package checks prove build invariants; they do
not prove headset visuals, interaction, performance, or long-session
reliability. Device claims remain gated by [the test plan](docs/TEST_PLAN.md).

## License

SaberStage first-party source is licensed under **GPL-3.0-only** with the
project's additional GPLv3 section 7 terms. See [LICENSE](LICENSE) and
[LICENSE-ADDITIONAL-TERMS.md](LICENSE-ADDITIONAL-TERMS.md). Contributor and
dependency licensing is documented in [CONTRIBUTING.md](CONTRIBUTING.md),
[INBOUND_LICENSE.md](INBOUND_LICENSE.md), and
[third-party notices](docs/THIRD_PARTY_NOTICES.md).
