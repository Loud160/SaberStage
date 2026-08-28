# Prompt 3 — Implement the independent spectator camera

Implement the camera subsystem only.

Do not implement recording or streaming yet.

## Requirements

The camera must be completely independent from the HMD cameras.

Its user-visible concepts, terminology, placement workflow, and normal behavior should feel familiar to an experienced Camera2 user wherever Quest constraints permit. Independently engineer the implementation; do not copy Camera2 source, configuration formats, or internal structure.

Implement:

- exactly one user-configurable camera for the first working release;
- static third-person camera;
- head/player-follow camera;
- player-relative/world-relative offsets;
- rotation;
- FOV;
- requested render/output resolution;
- near/far clip where useful;
- positional smoothing;
- rotational smoothing;
- camera profiles;
- stable camera/profile identity and consumer interfaces that can support additional cameras later without implementing or rendering them now;
- optional smooth anchored-float motion driven by bounded/smoothed player look-left/look-right input;
- a clean-room Camera2 movement-script loader, validator, and deterministic evaluator for the supported public format;
- profile persistence;
- reset-to-default;
- spectator-specific visibility/culling;
- menu preview where practical;
- an explicit render-consumer interface for the movable preview added in Prompt 4;
- deterministic scene recreation/teardown.

The spectator camera should render into its own off-screen target.

The user must be able to place the camera anywhere practical, subject only to documented engine/platform safety limits. Do not impose arbitrary creative placement bounds; always provide safe reset/recovery.

Do not continuously render it when no preview/capture consumer exists.

## Set-it-and-forget-it behavior

Saved camera profiles must describe semantic user intent, not stale scene coordinates.

The saved profile owns the base placement. Anchored float and movement scripts are runtime motion layers and must not silently rewrite that base.

On every launch/map transition:

- establish current tracking/reference frame;
- rebuild the runtime camera from the saved profile;
- maintain logical player-relative orientation;
- survive normal Quest recenter where technically possible.

Add:

- `Recenter Camera to Current Forward`
- `Reset Current Camera Profile`

These are recovery tools, not routine requirements.

## Future compatibility

Do not assume the camera always follows only the HMD.

Preserve a generic subject/anchor concept for future:

- player root;
- head;
- waist/root;
- avatar anchor;
- full-body tracking anchors.

Keep avatar, encoding, networking, chat, and Discord out of the camera implementation.

The camera may expose generic subject and visibility interfaces needed by SaberStage's integrated avatar later, but it must not own avatar loading, IK, or calibration.

## Motion composition and scripts

Define and test one deterministic order for base placement, movement-script output, normal smoothing, and anchored float. The script and float option must not compete for the transform.

Camera2-compatible scripts must use authoritative song time and safely handle pause, restart, practice/seek where supported, map transitions, malformed data, unsupported properties, and script completion. Support compatible position, rotation, FOV, and other approved camera properties without copying Camera2 implementation code.

Treat imported scripts as untrusted data. Enforce file-size, collection-size, numeric, interpolation, path, and per-frame work limits; reject executable/private extensions; and never permit a script to perform arbitrary file, network, Unity-object, or native-code operations.

## Frame demand

The future capture system must be able to request frames at 30/60 FPS independently from HMD refresh.

Do not hard-wire spectator rendering to every Unity frame.

## Tests

Host tests:

- smoothing;
- transform math;
- profile serialization;
- config migration;
- recenter/player-relative math where pure logic permits;
- anchored-float response, damping, bounds, and non-mutation of base placement;
- Camera2 script parsing/validation compatibility using independently created fixtures;
- deterministic script timing/interpolation and motion-layer precedence;

Device tests:

- menu;
- map start;
- pause;
- restart;
- finish;
- return to menu;
- repeated transitions;
- Quest recenter;
- camera enable/disable.

Measure 720p and 1080p spectator-render cost where possible.

Stop after this subsystem is stable.
