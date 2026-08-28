# Prompt 4 camera and preview UX implementation

Status: host-verified and ARM64-compiled; Quest visual and interaction verification is still required. This is not a release claim.

## Implemented camera workflow

SaberStage now uses the same proven native side-panel structure as Big Screen: an otherwise empty center controller keeps the forward view clear, while one genuine HMUI left-side `ViewController` owns its compact `Close`/`SaberStage` header, native segmented `Camera`, `Place`, `Motion`, and `Preview` tabs, and one native scrollable settings container per tab. The earlier hand-parsed tab surface, center information page, stock center title strip, and bottom docked-preview controller were removed. The center remains available for a later camera workspace and the right screen remains available for tabbed recording and streaming controls.

The left camera tabs now expose:

- camera enable, FOV, requested output resolution, and 30/60 FPS rate;
- numeric X/Y/Z position and pitch/yaw/roll;
- player-relative or world-relative placement and static/player/head following;
- position and rotation smoothing;
- optional anchored float with range and response controls;
- Camera2-compatible movement-script enable/file assignment plus visible validation status;
- recenter, clear motion, reset camera, movable-preview visibility/scale, and reset preview.

Each ordinary setting is validated, saved, and applied immediately. The single visible camera has stable ID `primary`; settings and preview selection remain ID-based so later additional cameras do not require replacing the persistence model.

## Preview surfaces and direct placement

Opening the editor creates a compact, recognizable camera-shaped world-space gizmo at the spectator camera instead of representing it as a line or another video screen. Its visible artwork has no separate grab bar; an invisible controller-grab surface covers the object. Movement and rotation update the camera live and save the new base pose on release.

The optional movable preview is a separate framed native BSML floating screen styled after Big Screen's performance panel. It has an upper `SaberStage | Primary` section, a lower `Grab anywhere to move` section, no visible native grab handle, and an invisible grab surface covering the full panel. Its world position, rotation, scale, selected camera, and visible state use schema 3 persistence. Movement is saved after the screen remains stable for 0.5 seconds. `Reset Preview` places it at a safe position in front of the current HMD while preserving its visibility choice.

Preview graphics use Unity UI layer 5, which the spectator profile excludes by default. They do not change the HMD transform and do not create an encoder. Floating-screen construction is deferred until the active scene has left `GameLoader` and BSML's main-menu dependency container exists; merely deferring from `late_load` to the first runtime update is not sufficient. All screen/image allocations are checked before use.

## Render demand and lifecycle

The movable preview registers render demand against the `primary` camera. Closing the editor destroys the temporary placement surface. Hiding the movable preview removes its demand and destroys that screen. When no later capture consumer exists, `CameraManager` releases the render texture and stops manually rendering the spectator camera.

Scene transitions destroy and reconstruct the spectator camera against the next active HMD camera. Preview objects remain product-owned and reconnect to the newly allocated render target. Abrupt live HMD pose discontinuities rebind the player-forward anchor without calling the unavailable Quest IL2CPP generic XR subsystem enumeration.

## Verification completed

- Platform-neutral C++ tests pass for settings schema 3 persistence/migration/repair, camera profile and transform math, smoothing and anchored float, movement-script compatibility, and render-demand scheduling.
- Tooling tests pass for receipt-safe deployment/removal, package identity, development launchers, the Big Screen-derived native side-panel ownership/layout contract, functional left-side tabs, and reserved center/right expansion slots.
- Quest ARM64 Release compilation and QMOD packaging pass against Beat Saber `1.40.8_7379` generated bindings.

Still required on the Quest: verify side-panel header/tab sizing and scrolling, control separation, the camera-shaped gizmo's appearance/orientation, controller grabbing and release persistence, the movable preview's framing/full-panel interaction/reset, render culling, camera transforms/FOV, menu/game/pause/finish transitions, recenter behavior, restart restoration, performance at each resolution/FPS option, and absence of repeating log errors. None of those visual or runtime results is inferred from the successful build.
