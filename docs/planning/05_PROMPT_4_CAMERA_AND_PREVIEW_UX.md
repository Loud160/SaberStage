# Prompt 4 — Implement Camera2-familiar camera controls and movable preview

Make the independent camera practical to configure directly on Quest.

This phase is camera and preview UX only. Do not implement recording or streaming yet.

## Familiarity goal

An experienced Camera2 user should recognize the concepts and be able to configure a useful camera without learning a meaningfully different workflow.

Keep camera configuration in one genuine native left-side menu panel, using Big Screen's proven structure: a compact header and Close action inside the side panel, a native segmented control with separate `Camera`, `Place`, `Motion`, and `Preview` tabs, and a native scrollable settings container for every tab. Hide the stock center title strip, keep the center clear for a later selected-camera workspace, and keep the right screen completely available for later tabbed recording and streaming controls. Do not fabricate tab surfaces, manually resize HMUI screens, or add a bottom docked-preview controller.

Match familiar user-visible behavior and terminology where practical, including:

- selected camera/profile;
- camera mode;
- position and rotation;
- FOV;
- requested render/output resolution;
- positional and rotational smoothing;
- near/far clip where useful;
- player-relative and world-relative behavior;
- enable/disable;
- anchored-float enable/disable, strength/range, and smoothing where exposed;
- Camera2 movement-script selection, enable/disable, validation status, and clear compatibility errors;
- recenter to current player forward;
- reset current camera;
- clear explanations for any Quest-specific limitation.

Do not copy Camera2 source, configuration files, comments, or internal structure.

The first release exposes one camera. Structure labels, persistence, and selection internally so future additional cameras do not require replacing the UI model, but do not show empty multi-camera controls yet.

## Movable preview panel

Add an optional HMD-only preview panel that renders the selected SaberStage camera.

The user must be able to:

- show or hide it at any time from an appropriate menu;
- move and rotate it to a useful location in menus or gameplay;
- resize/scale it;
- choose the camera/profile being previewed;
- restore its last position, rotation, scale, selected camera, and visibility preference;
- reset the panel to a safe visible default if it is lost or misplaced.

The preview must:

- have no visible grab handle; use a full-panel invisible grab surface;
- include framed upper and lower text sections following Big Screen's proven performance-panel treatment;
- never alter the HMD camera transforms;
- not appear in recordings or broadcast output by default;
- not create an encoder;
- release or suspend expensive rendering when hidden and when no other consumer needs the camera;
- recover across map/menu transitions and Beat Saber restarts;
- remain usable after Quest recenter.

Represent the directly placeable third-person camera with a recognizable camera-shaped object or image. Do not use an abstract line or a second video panel as the camera-placement marker.

## Camera motion persistence and recovery

Persist:

- the single camera's saved base placement;
- FOV and requested render/output resolution;
- ordinary positional/rotational smoothing;
- anchored-float enabled state and approved parameters;
- selected Camera2 movement script and enabled state;
- safe fallback behavior when a previously selected script is missing or invalid.

Provide clear actions to return to the saved base placement, disable motion layers, clear the selected script, and reset all camera-motion settings without deleting unrelated recording or preview configuration.

## Product-wide UI architecture

Organize the settings so later Record, Companion/Outputs, Avatar, Scenes, Broadcast, and Chat sections fit naturally. Do not display fake controls for unfinished features.

Use sensible defaults. Put low-level graphics and encoding details behind future Advanced pages rather than exposing them in the camera workflow.

## Verification

Test camera editing and preview behavior in menus, active gameplay, pause, map completion, restart, return to menu, repeated transitions, Quest recenter, and a full Beat Saber restart.

Obtain real on-headset visual verification. A build or host test alone cannot establish that placement, interaction, sizing, and persistence feel correct.

Stop after camera configuration and preview are stable.
