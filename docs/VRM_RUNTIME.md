# VRM 0.x loader and runtime milestone

## Implemented boundary

The avatar load path is deliberately split into three layers:

1. `Vrm0Parser` reads a self-contained GLB 2.0 file and produces `VrmAsset`, a Unity-free neutral C++ representation.
2. `VrmUnityRuntime` converts that representation into owned Unity objects and builds one humanoid `Animator`.
3. `AvatarManager` binds the Animator to SaberStage's existing static trackerless solver and owns replacement, visibility, expressions, and unload.

The parser does not include Unity headers. The solver does not know about glTF, VRM, meshes, materials, or texture files. The runtime releases its CPU construction arrays after Unity accepts the mesh data rather than retaining a second full copy of all vertices, indices, bind poses, and morph deltas.

## Supported first-pass VRM data

- GLB 2.0 container with one embedded BIN chunk and a VRM 0.x extension
- node hierarchy with TRS or decomposed matrix transforms
- triangle-list mesh primitives, 16- or 32-bit Unity index buffers, positions, normals, tangents, UV0, indices, skin joints, normalized weights, and inverse bind matrices
- multiple meshes, primitives, skins, and materials (one Unity renderer per primitive for correctness before later optimization)
- embedded PNG and JPEG images decoded through Unity, sampler filtering/wrapping, and a configurable 256-2048 maximum texture dimension
- VRM metadata, humanoid mapping, first-person bone/offset and mesh annotations, blend-shape groups, material-value metadata, collider groups, and SpringBone group metadata
- Unity humanoid Avatar construction and validation before `BindHumanoidAnimator(...)`
- preset expression morph bindings, including ordinary VRM `blink` and `joy` groups when present

The current material pass consumes VRM 0.x MToon properties but intentionally uses built-in unlit opaque/cutout/transparent shaders. Base color, main texture, emission color/texture, VRM's offset-then-scale texture transform, blend class, and render queue are retained. MToon lighting ramps, rim lighting, matcaps, exact culling behavior, and outlines are not claimed yet. SpringBone metadata is parsed but simulation is not implemented in this milestone.

Sparse accessors, external buffers/images, data URIs, non-triangle primitives, VRM 1.x, Draco compression, mesh merging, advanced SpringBones, terrain-aware feet, continuous locomotion, and FBT are outside this stop point. Unsupported or malformed input fails with a bounded error and does not replace the current avatar.

## Coordinates and skinning

Neutral data remains in glTF's right-handed coordinate system. The Unity adapter performs one documented reflection across Z:

- positions and directions: `(x, y, z) -> (x, y, -z)`
- quaternions: `(x, y, z, w) -> (-x, -y, z, w)`
- inverse bind matrices: `M -> S M S`, where `S = diag(1, 1, -1, 1)`
- triangle winding: swap the second and third index
- tangent direction reflects Z and tangent handedness flips

The [VRM 0.x specification](https://github.com/vrm-c/vrm-specification/blob/master/specification/0.0/README.md#vrm-rules) requires a model to face glTF `-Z`. Reflecting Z maps that forward vector to Unity `+Z`; SaberStage therefore passes positive Unity Z as the avatar's model-forward calibration direction. No arbitrary 180-degree model rotation is hidden in the loader. This must still be visually confirmed on Quest with the supplied assets.

## Texture and memory policy

The default maximum dimension is 1024. The profile permits 512 or 2048 for development. Aspect ratio is preserved. Only images referenced by runtime materials are decoded; the VRM metadata thumbnail and other unused images are excluded. Textures are decoded and capped one at a time to bound transient peak memory, made non-readable after mip generation, and their encoded bytes are released.

Host inspection of the supplied Black Heart fixture found 24 material texture references. At the 1024 cap, their estimated RGBA32 mip-chain storage is about 68.3 MiB, down from about 111.3 MiB of source-resolution decoded pixels. This is an estimate, not a Quest memory measurement. The 512 option is the intended fallback if the first device profile is too expensive.

## On-headset avatar selection

`Choose Avatar File` opens a native in-game filesystem browser. It can start from shared storage (`/sdcard`), browse upward or downward through any directory Beat Saber is permitted to read, and displays directories plus `.vrm` files only. Choosing a file persists its normalized absolute path, so users are not required to type a filename or copy an avatar into a SaberStage-owned directory.

Schema-6 settings that contain only the old `selectedFile` value retain a compatibility fallback under `/sdcard/ModData/com.beatgames.beatsaber/Mods/SaberStage/Avatars`. New selections use the absolute `selectedPath` field. Validation rejects relative paths, non-`.vrm` extensions, embedded nulls, and unreasonably long paths.

`Load Rest Pose` constructs the selected file without IK so import, orientation, skinning, and materials can be judged independently. `Bind Solver` then passes its Animator through the existing binding seam. `Unload` releases it, `Visible` controls the avatar root, `Recalibrate Neutral` refreshes three-point calibration, and the expression buttons exercise morph binding. Successful selection is opt-in for later startup auto-load; a missing, unreadable, or rejected file never prevents camera, preview, or recording startup.

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
