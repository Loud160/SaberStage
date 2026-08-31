# VRM 0.x loader and runtime milestone

## Implemented boundary

The avatar load path is deliberately split into three layers:

1. `Vrm0Parser` reads a self-contained GLB 2.0 file and produces `VrmAsset`, a Unity-free neutral C++ representation.
2. `VrmUnityRuntime` converts that representation into owned Unity objects and builds one humanoid `Animator`.
3. `AvatarManager` binds the Animator to SaberStage's existing static trackerless solver and owns replacement, visibility, expressions, and unload.

When VRM first-person metadata names a valid bone, `VrmUnityRuntime` transforms that bone's declared offset through the live constructed hierarchy and exposes the resulting world-space eye/view anchor to calibration. The solver maps the Quest HMD to this anchor and solves backward to the Head pivot; the parsed offset is no longer merely retained as metadata.

The parser does not include Unity headers. The solver does not know about glTF, VRM, meshes, materials, or texture files. The runtime releases its CPU construction arrays after Unity accepts the mesh data rather than retaining a second full copy of all vertices, indices, bind poses, and morph deltas.

## Supported first-pass VRM data

- GLB 2.0 container with one embedded BIN chunk and a VRM 0.x extension
- node hierarchy with TRS or decomposed matrix transforms
- triangle-list mesh primitives, 16- or 32-bit Unity index buffers, positions, normals, tangents, UV0, indices, skin joints, normalized weights, and inverse bind matrices
- multiple meshes, primitives, skins, and materials (one Unity renderer per primitive for correctness before later optimization)
- embedded PNG and JPEG images decoded through Unity, sampler filtering/wrapping, and a configurable 512-4096 maximum texture dimension
- VRM metadata, humanoid mapping, first-person bone/offset and mesh annotations, blend-shape groups, material-value metadata, collider groups, and SpringBone group metadata
- Unity humanoid Avatar construction and validation before `BindHumanoidAnimator(...)`
- preset expression morph bindings, including ordinary VRM `blink` and `joy` groups when present

The material runtime consumes VRM 0.x MToon properties through SaberStage's Quest shader bundle. It retains authored base and shade colors, main/shade/normal/rim/matcap/emission textures, UV transforms, alpha mode and cutoff, render queue, culling, toon ramp controls, and outline settings. Unsupported custom shaders use a bounded built-in fallback rather than preventing the avatar from loading. SpringBone simulation is implemented independently of this material-diagnostic pass and was deliberately left unchanged here.

The Appearance section exposes a cumulative `Material Stage` diagnostic ladder. `Configured` uses the normal user-facing material switches; numbered stages then add texture color, toon lighting, shade texture, normal maps, rim lighting, matcap, emission, and outlines one feature at a time. This is intended to identify the first failing material stage without changing importer code between headset tests. It is not a claim that every VRM renders identically to its desktop reference.

`Avatar Lighting` selects an avatar-only lighting policy:

- `Environment` follows the active Beat Saber environment most closely.
- `Balanced` retains environment response while applying a conservative minimum light contribution so avatars remain readable in dark environments.
- `Studio` uses stable camera-relative key and fill lighting for appearance comparison.

These modes alter only the avatar material. They do not add or modify Beat Saber environment lights.

`Side-Step Lean Limit` is a torso-motion override expressed as 40-100 percent. The default 100 percent preserves the calibrated/original lateral head-to-pelvis envelope. Lower values scale that envelope so pelvis translation and stepping take over sooner.

`Planted Leg Lean Limit` is a separate 20-100 percent support control. It limits lateral pelvis displacement over the currently planted feet, reapplies that boundary after the spine root correction, and forces the support-step path when exceeded. Keeping this independent prevents a reduced torso limit from reappearing as a full-body pivot around the ankles. `Stance Width` scales the hip-width-derived neutral and ideal foot separation from 75-200 percent; changing it does not change avatar scale. Both controls default to 100 percent to preserve existing settings files and behavior until the user deliberately tunes them.

`Backward Spine Curve Limit` scales only the permitted rearward root-to-head bow from 0-100 percent. Zero prevents backward C-bowing, while forward attack/lunge bending keeps its full existing range. The default 100 percent preserves the prior solver limit.

`Animated Expressions` gates all automatic face work. When enabled, the menu applies a subtle `joy` weight and blinks at randomized 2.4-6.6 second intervals. Gameplay keeps x1/x2 focused, blends a slight smile at x4, a stronger `joy` expression at x8, briefly blends `angry` after a miss, strengthens that reaction for clustered misses, blends `sorrow` as energy becomes low, combines `sorrow` and `angry` after a real fail, and briefly blends `joy`/`fun` after a near-song-end completion. Attack and release transitions are eased over roughly 0.2-0.34 seconds, miss detection has a short retrigger cooldown, and blinking remains an independent blend channel. Missing presets fall back between `joy`/`fun` and `angry`/`sorrow`; an avatar that supplies neither compatible preset simply retains its authored face. Turning the option off clears the automatic weights and performs no ongoing expression work.

VRM 0 avatar files do not ordinarily carry reusable gameplay jump or celebration animation clips, and SaberStage currently imports no animation clips. A completion jump therefore cannot be honestly described as avatar-dependent data already available in the file. It would require a separate tracked-body animation layer with grounding, solver blending, and an explicit SaberStage animation asset; no inert toggle is exposed for that unimplemented path.

Sparse accessors, external buffers/images, data URIs, non-triangle primitives, VRM 1.x, Draco compression, mesh merging, advanced SpringBones, terrain-aware feet, continuous locomotion, and FBT are outside this stop point. Unsupported or malformed input fails with a bounded error and does not replace the current avatar.

## Coordinates and skinning

Neutral data remains in glTF's right-handed coordinate system. The Unity adapter performs one documented reflection across Z:

- positions and directions: `(x, y, z) -> (x, y, -z)`
- quaternions: `(x, y, z, w) -> (-x, -y, z, w)`
- inverse bind matrices: `M -> S M S`, where `S = diag(1, 1, -1, 1)`
- triangle winding: swap the second and third index
- tangent direction reflects Z and tangent handedness flips
- texture coordinates: `(u, v) -> (u, 1 - v)` for both UV sets. glTF UVs use a
  top-left origin; Unity samples from a bottom-left origin. Without this flip,
  uniformly colored regions still look plausible but every alpha-cutout
  silhouette cuts along mirrored contours (missing collar/neck accessories,
  stair-stepped stocking tapers) and atlased models sample the wrong cells.
  Consequently `KHR_texture_transform` values (authored in glTF space) are
  re-based as `offsetY' = 1 - offsetY - scaleY` with the rotation negated,
  while VRM 0.x `vectorProperties` texture ST values are applied verbatim
  because the exporter captured them from Unity materials (already
  bottom-left origin).

The [VRM 0.x specification](https://github.com/vrm-c/vrm-specification/blob/master/specification/0.0/README.md#vrm-rules) requires a model to face glTF `-Z`. Reflecting Z maps that forward vector to Unity `+Z`; SaberStage therefore passes positive Unity Z as the avatar's model-forward calibration direction. No arbitrary 180-degree model rotation is hidden in the loader. This must still be visually confirmed on Quest with the supplied assets.

## Texture and memory policy

The default maximum dimension is 1024. The profile permits 512, 2048, or 4096 for diagnosis and quality testing. Aspect ratio is preserved. Only images referenced by runtime materials are decoded; the VRM metadata thumbnail and other unused images are excluded. Textures are decoded and capped one at a time to bound transient peak memory, made non-readable after mip generation, and their encoded bytes are released. Color textures are created as sRGB while normal, shading-grade, and outline-width data textures are created as linear. Runtime textures use trilinear filtering with anisotropic level 4: bilinear-only sampling snaps between mip levels, and without aniso, glancing-angle surfaces drop to deep mips whose box-filtered alpha erodes cutout boundaries into chunky steps on camera. If one source image is assigned to both color and data roles, the loader logs the ambiguity and preserves its visible color interpretation rather than silently creating inconsistent duplicate material inputs.

Load diagnostics record each source and runtime texture size, its material roles and color space, each material's shader and mapped features, alpha/cull/render-queue state, and a summary of MToon versus fallback materials. Applying a material stage or lighting mode logs only when the effective controls change; it does not emit per-frame logging.

Host inspection of the supplied Black Heart fixture found 24 material texture references. At the 1024 cap, their estimated RGBA32 mip-chain storage is about 68.3 MiB, down from about 111.3 MiB of source-resolution decoded pixels. This is an estimate, not a Quest memory measurement. The 512 option is the intended fallback if the first device profile is too expensive.

## First-person wear view and the display clone

Wear Avatar is implemented with layers only. Every renderer is classified at build time, while skin weights are still in CPU memory, by the fraction of its vertices whose dominant joint lies in the humanoid head subtree (and separately the neck subtree), plus a hair-name flag. When wear is on, body renderers move to the Default layer (rendered by both the HMD and the spectator camera) and coverage-selected head geometry stays on the spectator-only avatar layer, so recordings always contain the complete avatar. Coverage tiers: face only; face plus hair and head accessories; everything neck-attached. Single-mesh atlas avatars cannot be split at renderer granularity and degrade to showing everything; per-triangle weight splitting is the documented follow-up.

Up to three display clones can be active (settings slot keys keep the original single-clone names for slot 1); each is placed independently, all share one visibility layer and scale. Each display clone is `Object.Instantiate` of the built avatar hierarchy — meshes, materials, and textures are shared, the cloned humanoid Animator is removed — plus a per-frame copy of every node's local position and rotation after the solver, expressions, and SpringBones have written the frame. Blend-shape weights are copied lazily through a dirty flag set on expression changes. Visibility is one layer choice: Default (both views), the avatar layer (camera only), or Beat Saber's first-person layer (headset only). The clone deactivates whenever the source avatar is hidden or unloaded rather than freezing mid-pose. Placement uses an invisible body-sized physics grab handle (a FloatingScreen with no visuals, its handle box scaled to the clone's body and refitted when the scale setting changes), so the clone can be grabbed anywhere on its body; the menu controller drives placement through `SetStandinWorldPose` with debounced persistence. The handle stays on the UI layer because the game's pointer raycast mask must hit it.

## On-headset avatar selection

`Choose Avatar File` opens a native in-game filesystem browser. It can start from shared storage (`/sdcard`), browse upward or downward through any directory Beat Saber is permitted to read, and displays directories plus `.vrm` files only. Choosing a file persists its normalized absolute path, so users are not required to type a filename or copy an avatar into a SaberStage-owned directory.

Schema-6 settings that contain only the old `selectedFile` value retain a compatibility fallback under `/sdcard/ModData/com.beatgames.beatsaber/Mods/SaberStage/Avatars`. New selections use the absolute `selectedPath` field. Validation rejects relative paths, non-`.vrm` extensions, embedded nulls, and unreasonably long paths.

`Load Avatar` constructs the selected file without IK so import, orientation, skinning, and materials can be judged independently. `Attach Tracking` then passes its Animator through the existing binding seam. `Unload Avatar` releases it, `Visible` controls the avatar root, `Resync Player Pose` refreshes the transient standing-height/floor/tracking reference without deleting the saved Basic or Advanced player profile, and the expression buttons exercise morph binding. Successful selection is opt-in for later startup auto-load; a missing, unreadable, or rejected file never prevents camera, preview, or recording startup.

## Cache recommendation

Do not introduce a SaberStage-specific avatar cache before Quest measurements. The neutral representation already creates a clean future cache boundary, but a cache adds versioning, invalidation, disk usage, and another untrusted-input surface. If measured load time is unacceptable, a later cache should be keyed by the source file hash plus importer version and contain validated mesh streams, capped Quest-ready textures, material metadata, humanoid mapping, expression bindings, and preserved SpringBone metadata. It must be written only under SaberStage's mod-data directory and regenerated rather than treated as the user's source avatar.

The two user-supplied VRMs remain external read-only test assets and are not packaged or committed:

| Fixture | SHA-256 | Host parser result |
|---|---|---|
| `5063571287661799621.vrm` (`Black Heart`) | `0C7FC424011BA03E09949C0F6DF93C9F405125BC4096E057D071642A98DD8140` | 275 nodes, 54 humanoid mappings, 72 meshes, 87 primitives, 14 materials, 26 textures, 57,589 triangles, 456 primitive morph targets, first-person Y offset 0.06 m, 31 SpringBone groups, 22 colliders |
| `5213193455022713860.vrm` | `45F2E42B204DBE5AA9B967B07B2AE794D105802D3F98196910B1FE5F5E49D0F8` | 75 nodes, 53 humanoid mappings, one mesh/primitive, one material, two textures, 2,881 triangles, 31 morph targets, four SpringBone groups/colliders |

## Robustness and current verification

The parser validates GLB length/chunks, configured byte and count budgets, buffer-view and accessor ranges/strides/types, finite floats, image headers/dimensions, vertex attribute counts, triangle indices, skin/joint references, node parents/cycles, material/texture references, required humanoid bones, blend-shape targets, and SpringBone references. Host tests cover a valid synthetic VRM plus corrupt GLB length, an accessor overrun, and a missing required humanoid bone. Both supplied files parse through the same production entry point.

Host tests and ARM64 compilation are necessary but cannot establish runtime correctness. The first device pass confirmed that both supplied files construct and appear in the Primary camera, Black Heart is broadly textured as expected, and its blink morph works. It also exposed four corrected defects that still require a second device pass: zero-scale texture transforms on the compact avatar, a menu-state tracking lookup that left the solver uncalibrated, nested recording-button rows that collapsed on the right panel, and capture-time deactivation that blanked the movable preview. The required next device stop point is:

1. load the small fixture and confirm hierarchy, orientation, scale, skinning, textures, and humanoid binding;
2. confirm it is visible in the SaberStage camera and not incorrectly visible to the HMD;
3. confirm blink/joy morph controls and trackerless head/hands/body pose;
4. repeat with Black Heart and check material failures or missing geometry;
5. measure parse time, Unity construction time, steady frame time with avatar visible/hidden, and process memory before/load/visible/unload;
6. verify unloading returns memory close to baseline and camera/recording remain functional after any loader failure.

Until those checks pass, this is an implemented and packaged development milestone, not a verified visible-avatar feature.
