# SaberStage

SaberStage is an early-development, clean-room native Quest camera, integrated-avatar, recording, and broadcast mod for Beat Saber by **Loud160 (AKA Whisp)**.

It is **not ready for normal use**. The current development build has a real `primary` spectator-camera runtime and a Camera2-familiar editor in a native left-side menu: an HMD-independent Unity camera, persisted placement/FOV/output profile, numeric and controller-grab placement, player/head/static anchoring, smoothing, optional anchored float, Camera2-format movement-script evaluation, a movable preview, scene/recenter recovery, and consumer-driven off-screen rendering. Local recording starts immediately, keeps the Primary camera and game audio recording continuously through menus, loading, gameplay, and results until stopped, and finalizes a uniquely named MP4 through Hollywood's FFmpeg muxer. Prompt 7 adds explicit pause/resume state, native pause-menu controls, and an opt-in both-thumbsticks gameplay shortcut. An optional Gameplay Only mode arms from the menu and records only the song.

The `Avatar-Framework` branch now contains a fixed-size native trackerless humanoid solver through static planted legs and pelvis lean/crouch, a Unity-free VRM 0.x/GLB parser, first-pass Unity hierarchy/mesh/material/texture construction, humanoid `Avatar` creation, expression morph binding, single-avatar ownership, and an on-headset filesystem browser for selecting a `.vrm` from any readable directory. The avatar uses a mandatory spectator-camera layer, and the preview displays the Primary camera's same output texture. A first headset pass confirmed construction and camera visibility for both supplied VRMs, broadly correct Black Heart texturing, and its blink morph; fixes from that pass still need device verification for compact-avatar texturing, live solver tracking, movable-preview rendering, and right-panel recording controls. Frame time and memory also remain unmeasured. Networking, chat, and Discord integration are not implemented.

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

The VRM loader boundary, supported data, development path, budgets, known limitations, and remaining Quest stop point are documented in [`docs/VRM_RUNTIME.md`](docs/VRM_RUNTIME.md).

Completed recordings are stored under `/sdcard/ModData/com.beatgames.beatsaber/Mods/SaberStage/Recordings`. They can be copied into a timestamped local folder with `python scripts/quest_tool.py pull-recordings` after an authorized headset session.
