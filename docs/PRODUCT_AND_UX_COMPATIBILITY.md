# Product and Camera2-familiar UX contract

SaberStage should feel recognizable to a Camera2 user: choose a camera/profile, place it directly, see a preview while editing, adjust transform/FOV/output quality and smoothing, save once, and have it restore. Clean-room implementation does not require new terminology for familiar user concepts.

The menu uses Beat Saber's native screen system and Big Screen's proven side-panel structure. One genuine left-side `ViewController` owns the native segmented camera tabs and scrollable settings pages. The center remains the main control area. A genuine right-side `ViewController` owns tabbed recording controls now and will gain streaming tabs later. The product has coherent areas: **Camera**, **Preview**, **Record**, **Companion & Outputs**, **Avatar**, **Scenes**, **Broadcast**, and **Chat**. Only completed areas and controls are visible. Advanced controls stay within their owning area.

Stage 1 exposes one camera but calls it `Primary` and stores a stable ID. It supports practical free placement, numeric editing, controller grab/move, FOV, requested resolution/FPS, optional smoothing, optional anchored float, and validated Camera2 script assignment. Ordinary controls save immediately, controller placement saves on release, and movable-preview placement saves after a short stable debounce. Reset actions restore the camera or preview independently. Failed loads repair individual fields and show a concise notice, never a broken blank state.

The preview is movable/resizable, HMD-only by default, and has `Reset Preview to Visible`. Its upper and lower framed text sections are part of one panel, the native grab bar is invisible, and the complete panel is the grab target. Camera placement uses a recognizable camera-shaped gizmo rather than an abstract line. Recording has a clear state, elapsed time, actual output size/FPS, remaining-space warning, and recoverable stop. Normal launch restores camera, preview, last recording preset, and safe nonsecret output selections without a calibration flow.

Deliberate Quest differences are labeled: requested versus actual resolution, conservative Quest 2 presets, one camera initially, platform-dependent visibility features, and scripts limited to the published compatible subset. The HMD view remains untouched.

Camera2 public behavior was reviewed from its [repository and wiki](https://github.com/kinsi55/CS_BeatSaber_Camera2/wiki). SaberStage compatibility fixtures will be independently written from the public schema, not copied files.
