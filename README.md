# SaberStage

SaberStage is an early-development, clean-room native Quest camera, integrated-avatar, recording, and broadcast mod for Beat Saber by **Loud160 (AKA Whisp)**.

It is **not ready for normal use**. The current development build has a real `primary` spectator-camera runtime and a Camera2-familiar editor in a native left-side menu: an HMD-independent Unity camera, persisted placement/FOV/output profile, numeric and controller-grab placement, player/head/static anchoring, smoothing, optional anchored float, Camera2-format movement-script evaluation, a movable preview, scene/recenter recovery, and consumer-driven off-screen rendering. Local recording starts immediately, keeps the Primary camera and game audio recording continuously through menus, loading, gameplay, and results until stopped, and finalizes a uniquely named MP4 through Hollywood's FFmpeg muxer. Prompt 7 now adds explicit pause/resume state, native pause-menu controls, and an opt-in both-thumbsticks gameplay shortcut; those new paths and the spectator-only menu-transition artifact guard have passed host tests and the ARM64 build but still need headset validation. An optional Gameplay Only mode arms from the menu and records only the song. Networking, chat, Discord integration, and the avatar are not implemented yet.

The product goal and gated build sequence are in [`docs/planning/00_PROJECT_CHARTER.md`](docs/planning/00_PROJECT_CHARTER.md). The reviewed architecture starts at [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

## Target/toolchain baseline

- Beat Saber Quest package target: `1.40.8_7379` (version-specific development target; revalidate before release)
- Scotland2 `^0.1.6`
- beatsaber-hook `^6.4.2`
- bs-cordl `4008.*`
- BSML `^0.4.55`
- custom-types `^0.18.4`
- Hollywood `^1.2.2`
- paper2_scotland2 `^4.8.0`
- QPM CLI `1.5.11`
- Android NDK r27d (`27.3.13750724`)

## Build and test

See [`docs/BUILD_AND_DEPLOY.md`](docs/BUILD_AND_DEPLOY.md). No final project license has been selected.

The current camera/preview implementation and its device-test boundary are documented in [`docs/PROMPT_4_IMPLEMENTATION.md`](docs/PROMPT_4_IMPLEMENTATION.md).

Completed recordings are stored under `/sdcard/ModData/com.beatgames.beatsaber/Mods/SaberStage/Recordings`. They can be copied into a timestamped local folder with `python scripts/quest_tool.py pull-recordings` after an authorized headset session.
