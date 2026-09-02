# SaberStage

<p align="center">
  <strong>A Quest-native third-person camera, avatar, recording, and broadcast stage for Beat Saber.</strong>
</p>

<p align="center">
  <img alt="Platform" src="https://img.shields.io/badge/platform-Meta%20Quest%202%20%7C%203%20%7C%203S-00b2ff">
  <img alt="Beat Saber" src="https://img.shields.io/badge/Beat%20Saber-1.40.8-orange">
  <img alt="Language" src="https://img.shields.io/badge/language-C%2B%2B20-blue">
  <img alt="Status" src="https://img.shields.io/badge/status-early%20development-yellow">
</p>

SaberStage is an early-development, clean-room native Quest camera, integrated-avatar, recording, and broadcast mod for Beat Saber.

It is **not ready for normal use**. The current development build has a real `primary` spectator-camera runtime and a Camera2-familiar editor in a native left-side menu: an HMD-independent Unity camera, persisted placement/FOV/output profile, numeric and controller-grab placement, player/head/static anchoring, smoothing, optional anchored float, Camera2-format movement-script evaluation, a movable preview, scene/recenter recovery, and consumer-driven off-screen rendering. Local recording starts immediately, keeps the Primary camera and game audio recording continuously through menus, loading, gameplay, and results until stopped, and finalizes a uniquely named MP4 through Hollywood's FFmpeg muxer. Prompt 7 adds explicit pause/resume state, native pause-menu controls, and an opt-in both-thumbsticks gameplay shortcut. An optional Gameplay Only mode arms from the menu and records only the song.

The `Avatar-Framework` branch now contains a fixed-size native trackerless humanoid solver with an HMD-to-avatar-eye target, visible Beat Saber saber-handle targets during gameplay, calibrated grip-to-hand rotation, analytic arms/legs, a smooth constrained spine, stateful torso yaw, dimension-normalized pelvis lean/translation, crouch-versus-bend estimation, planted feet, predicted-support translation/pivot steps, and conservative hop handling. It also contains a Unity-free VRM 0.x/GLB parser, first-pass Unity hierarchy/mesh/material/texture construction, humanoid `Avatar` creation, expression morph binding, single-avatar ownership, and an on-headset filesystem browser for selecting a `.vrm` from any readable directory. The avatar uses a mandatory spectator-camera layer, and the preview displays the Primary camera's same output texture. Black Heart rendering and full-body movement are verified in an actual map. The anatomy-correction pass is host-tested and ARM64-built but still awaits its same-angle Quest before/after recording; Quest solver time and memory also remain unmeasured.

The repository also contains a PC-only [`SaberStage PC Pose Analyzer`](tools/pc-pose-analyzer/README.md) for documenting how observable Custom Avatars tracking targets relate to the final posed humanoid. **FinalIK is treated as a black box:** the analyzer records targets in and completed avatar transforms out, does not inspect or reproduce FinalIK implementation code, and does not include either FinalIK or Custom Avatars binaries. The resulting normalized pose and calibration measurements provide reproducible development evidence for SaberStage's separate Quest IK implementation.

Recording now offers selectable Hollywood and private Direct FFmpeg hardware backends with 720p/1080p/1440p, 30/60 FPS, bitrate, rate-control, tuning, H.264 profile/level, keyframe, and AAC settings. The right-side `Live Stream` tab implements a bounded single-encode proof of concept for Twitch and advanced custom RTMP/RTMPS endpoints. Service-specific values remain separate. `Use Once` keeps an endpoint/key in memory for the current Beat Saber session, while `Save in Settings` explicitly stores it in SaberStage's local settings; keys and Twitch OAuth tokens are never logged and are redacted from support archives. Twitch OAuth access and refresh tokens are encrypted at rest with an AES-256-GCM key held by Android Keystore rather than serialized as plaintext settings. Twitch-first integration adds optional device authorization through SaberStage's registered public application, title updates, automatic access-token refresh, a movable HMD-only chat panel, and an opt-in current-map announcement posted from the connected streamer's account when gameplay begins. The announcement includes locally available song, artist, difficulty, mapper, duration, NPS, declared map extensions, and rating data when a real local provider supplies it. Broadcasting itself still needs only the Twitch ingest address and stream key; account authorization is required only for title and chat features, and existing links must reconnect once for the new write-chat permission. YouTube and Kick remain visible but are explicitly marked and blocked as unsupported until their implementations are ready. Streaming pause keeps RTMP alive while showing a selected PNG/JPEG/GIF or the embedded SaberStage AFK image with silent audio. This code is host-tested and ARM64-built only until an explicitly authorized Quest pass validates direct video, final MP4s, service connections, chat, AFK pause/resume, A/V sync, and performance. Companion output, TV casting, Discord video, optional FBT, SpringBones, terrain feet, and additional avatar formats are not implemented.

The product goal and gated build sequence are in [`docs/planning/00_PROJECT_CHARTER.md`](docs/planning/00_PROJECT_CHARTER.md). The reviewed architecture starts at [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

## Target/toolchain baseline

- Beat Saber Quest package target: `1.40.8_7379` (version-specific development target; revalidate before release)
- Scotland2 `^0.1.6`
- beatsaber-hook `^6.4.2`
- bs-cordl `4008.*`
- BSML `^0.4.55`
- custom-types `^0.18.4`
- Hollywood `^1.2.2`
- [Native Logger Quest](https://github.com/Loud160/NativeLoggerQuest) `1.0.0`
  (SHA-pinned, statically linked; no separately installed logger runtime)
- QPM CLI `1.5.11`
- Android NDK r27d (`27.3.13750724`)

## Build and test

See [`docs/BUILD_AND_DEPLOY.md`](docs/BUILD_AND_DEPLOY.md).

SaberStage writes its own current and previous logs under
`/sdcard/ModData/com.beatgames.beatsaber/Mods/SaberStage/Logs/`. Windows users
can run `Collect-SaberStage-Logs.bat`; Linux users can run
`./Collect-SaberStage-Logs-Linux.sh`. The generated support ZIP also retains a
filtered Paper2 excerpt because third-party dependencies may still use Paper2,
but SaberStage itself neither declares nor dynamically links Paper2.

The current camera/preview implementation and its device-test boundary are documented in [`docs/PROMPT_4_IMPLEMENTATION.md`](docs/PROMPT_4_IMPLEMENTATION.md).

The VRM loader boundary, supported data, development path, budgets, known limitations, and remaining Quest stop point are documented in [`docs/VRM_RUNTIME.md`](docs/VRM_RUNTIME.md).

The guided player calibration, five-profile storage model, and the Setup/Display/Quality/Fit avatar-menu workflow are documented in [`docs/PLAYER_CALIBRATION.md`](docs/PLAYER_CALIBRATION.md).

Completed recordings are stored beside the Quest's built-in captures under `/sdcard/Oculus/VideoShots` with a `SaberStage_` filename prefix. They can be copied into a timestamped local folder with `python scripts/quest_tool.py pull-recordings` after an authorized headset session.

## Avatar inspection camera script

[`examples/MovementScripts/AvatarBodyInspectionOrbit.json`](examples/MovementScripts/AvatarBodyInspectionOrbit.json) is a song-synchronized diagnostic orbit for evaluating trackerless avatar movement. It circles the player once every 60 seconds at a three-meter radius, rises from 0.42 m to 2.05 m over two minutes, retraces the orbit in reverse while descending for the next two minutes, and loops. Every keyframe is aimed at a 1.10 m torso focus point; the low view looks upward and the high view looks downward.

For a headset test, copy the file to `/sdcard/ModData/com.beatgames.beatsaber/Mods/SaberStage/MovementScripts/`, use a Player Relative camera with Player follow, select `AvatarBodyInspectionOrbit.json`, enable the movement script, and disable Anchored Float so it does not offset the diagnostic orbit. The current script selector still accepts a filename rather than presenting a file browser.

## Project layout

```text
include/saberstage/    Public declarations and platform-neutral interfaces
src/                   Quest runtime, UI, camera, avatar, recording, and streaming
tests/                 Host C++ tests and repository/package invariant tests
scripts/               Dependency, build, package, deploy, removal, and support tools
assets/                Embedded Quest shader and default AFK assets
docs/                  Architecture, feature, test, and development records
tools/                  Shader build project and PC pose-analysis tooling
```

## Documentation

- [Architecture](docs/ARCHITECTURE.md)
- [Lifetime and threading rules](docs/LIFETIME_AND_THREADING.md)
- [Build, package, and development deployment](docs/BUILD_AND_DEPLOY.md)
- [Test and device gates](docs/TEST_PLAN.md)
- [Player calibration](docs/PLAYER_CALIBRATION.md)
- [Recording pipeline](docs/RECORDING_PIPELINE.md)
- [Streaming architecture](docs/STREAMING_ARCHITECTURE.md)
- [VRM runtime](docs/VRM_RUNTIME.md)
- [Third-party notices](docs/THIRD_PARTY_NOTICES.md)
- [Contributing](CONTRIBUTING.md)
- [Security reporting](SECURITY.md)

## Development status

SaberStage is not release-ready. Host tests, an ARM64 link, and package checks
prove build invariants; they do not prove headset visuals, interaction,
performance, or long-session reliability. Device claims remain gated by
[`docs/TEST_PLAN.md`](docs/TEST_PLAN.md).

## License

SaberStage first-party source is licensed under **GPL-3.0-only** with the
project's additional GPLv3 section 7 terms. See [LICENSE](LICENSE) and
[LICENSE-ADDITIONAL-TERMS.md](LICENSE-ADDITIONAL-TERMS.md) for the complete
terms. Contributor and dependency licensing is documented in
[CONTRIBUTING.md](CONTRIBUTING.md), [INBOUND_LICENSE.md](INBOUND_LICENSE.md),
and [docs/THIRD_PARTY_NOTICES.md](docs/THIRD_PARTY_NOTICES.md).
