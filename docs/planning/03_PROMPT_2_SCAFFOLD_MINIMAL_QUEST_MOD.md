# Prompt 2 — Scaffold a minimal loadable Quest mod from scratch

Using the approved architecture, create the minimum professional Quest mod scaffold.

Use Camera2's public menu layout as the direct UI reference while implementing the controls with Quest-native BSML APIs. Preserve required attribution and do not copy unrelated camera/runtime implementation.

Use current verified Quest Beat Saber tooling and dependency versions.

## Required result

- root `.gitignore` appropriate to the verified toolchain;
- root `README.md` stating that SaberStage is early development and not ready for normal use;
- reviewed `docs/PROVENANCE.md` from the research phase;
- CMake/QPM/QMOD files;
- clear `src/` and `include/` layout;
- small bootstrap/load path;
- deterministic application/service root;
- centralized logging;
- config/settings skeleton with versioning;
- basic mod-menu entry that follows Camera2's screen topology: camera navigation on the left, selected-camera settings on the native center screen with Beat Saber's title and Back control, bottom reserved for preview, and right available for future panels;
- product identity and attribution for `SaberStage` by `Loud160 (AKA Whisp)`;
- version metadata;
- build instructions;
- development deployment instructions;
- host-test target for platform-neutral logic;
- no camera/preview/encoder/network/chat/Discord implementation yet.

At this scaffold stage the license decision was intentionally deferred. The
owner later selected GPL-3.0-only with the repository's additional GPLv3
section 7 terms.

Do not put future functionality into one giant `main.cpp`.

Lay out the menu, settings, and subsystem boundaries so the finished product can present coherent Camera, Preview, Record, Companion/Outputs, Scenes, Broadcast, and Chat areas. Preserve Camera2's left camera-list/main settings/bottom preview organization and keep the right screen available as features are added. Do not show controls for unfinished features.

## Set-it-and-forget-it groundwork

Implement the settings service so it already supports:

- config schema version;
- validation;
- migration hook;
- defaults;
- per-subsystem reset;
- factory reset;
- atomic/safe save where practical.

Do not persist fragile runtime Unity objects or absolute scene state.

## Verification

Build/package the QMOD.

If a Quest is connected, deploy and verify:

- Beat Saber starts;
- mod loads;
- menu entry appears;
- settings file is created/read;
- restart restores settings;
- reset works;
- logs show mod/game/toolchain versions;
- restart/exit does not crash.

Commit only after verified.

Report exactly what was tested and any environment limitations.
