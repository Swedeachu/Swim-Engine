# Animation, skinning and morph targets

This covers critical-path item **78**. Animation data flows from glTF through the asset compiler into `.sasset` skeletons, clips and skinned meshes. The CPU runtime (`Systems/Animation`) turns clips into skinning palettes. The GPU (`Renderer/Skinning`) deforms each character once per frame into an ordinary mesh that every renderer pass already knows how to draw, with motion vectors.

```text
Engine/Assets
  SkeletonAsset             joints (name, parent, rest TRS, inverse bind, source node), parents first, + RootTransform
  AnimationClipAsset        name, duration, tracks (target name, path, interpolation, times, values), events
  MeshAsset                 + skin stream (Joints0 UInt16x4, Weights0 Float32x4) + MorphTargets / DefaultMorphWeights
  ModelAsset                + per-node Skin and MorphWeights, + Skeletons and Animations
Tools/AssetCompiler
  GltfImporter              JOINTS_0/WEIGHTS_0, morph targets, mesh/node weights, skins, animation channels
  StaticModelCompiler       skeletons (reordered parents first), normalized/sorted influences, dense morphs, clips
Systems/Animation
  AnimationMath             Vec3/Quat/JointPose/Matrix4/Matrix3x4, slerp/nlerp, compose
  Skeleton                  runtime skeleton (shared, read-only), AnimationPose, BoneMask
  AnimationClip             validated clip, MorphLayout, BindClip, SampleClip/SampleTrack, blend/additive helpers
  Animator                  layered state machines: parameters, transitions, crossfades, events, root motion
  SkeletonInstance          model-space joints, skinning palette + previous palette, morph weights, sockets
  AnimationUpdate           UpdateAnimations: animators + instances, serial or ParallelFor on the job system
Renderer/Skinning
  SkinningRecords           GpuSkinVertex (32 B), GpuMorphDelta (40 B), GpuSkinDispatch (48 B)
  SkinningBindings          SwimSkinning's descriptor contract
  SkinningReference         Skinning::BuildSource / SkinVertex / SkinPosition / ComputeBounds (CPU definition)
  SkinningSystem            source pools, per-instance output meshes, Record (one compute pass)
  SkinningGraphResources    what one Record scheduled
Shaders/Slang/Skinning
  SkinningRecords.slang     mirrors the records
  Skinning.slang            SwimSkinning
```

`Systems/Animation` includes only the standard library, `Jobs/` and the skeleton/clip asset structs; it knows nothing about renderers, scenes or EnTT, and no renderer module includes it. `Renderer/Skinning` depends on RenderGraph, the RHI contract, `Resources/` handles, `Geometry/`, `GpuScene/RenderBounds.h` and `ForwardPlus/StandardVertex.h`; it takes palettes, not animators. `scripts/verify-build-layout.py` enforces both.

## Assets

### glTF import

- **Skins:** joints (node indices), inverse bind matrices (identity when absent), the optional skeleton root. Skins with more than 65,536 joints are rejected (16-bit joint indices).
- **Influences:** `JOINTS_0` (8- or 16-bit) and `WEIGHTS_0` (float or normalized integers). A primitive with only one of them is invalid; `JOINTS_1`/`WEIGHTS_1` are rejected explicitly rather than dropped. Draco primitives decode them too.
- **Morph targets:** `POSITION`, `NORMAL` and `TANGENT` deltas per target (targets are plain accessors even on Draco primitives), the mesh's default `weights`, and the node's `weights`. All primitives of a mesh must agree on the target count.
- **Animations:** every channel with a node target (`KHR_animation_pointer` channels are skipped): translation, rotation, scale and morph weights, with linear, step or cubic-spline samplers. The output count must match the keys.
- **Known limitation:** the pinned fastgltf 0.9 rejects every node-level `weights` array as invalid glTF (an inverted error check in its node parser), so such files fail to import. Mesh-level default weights work. The intermediate model carries node weights, and the compiler handles them.

### Compilation

- **Skeletons:** one per glTF skin. Joints are ordered parents first (stable by depth) and every vertex's joint indices are remapped to that order. A joint's parent is its nearest ancestor that is a joint of the same skin. Non-joint nodes between two joints fold into the child's rest transform, and the world transform above the first root joint becomes `RootTransform`. A mesh instanced with two skins of different joint order is rejected.
- **Influences:** negative and non-finite weights become 0. Weights are sorted by decreasing weight and normalized; unused slots repeat the strongest joint, so they stay valid after remapping. A vertex with no weight binds fully to skin joint 0. They go into a **second vertex stream** (24 bytes: `Joints0` UInt16x4, `Weights0` Float32x4), so stream 0 keeps the static `StandardVertex` layout.
- **Morph targets:** dense `MeshMorphTarget` arrays over the mesh's whole vertex payload (3 floats per vertex; empty when a target does not move that attribute), plus `DefaultMorphWeights`.
- **Clips:** one per glTF animation. Tracks name their target by the node's model-wide unique name (duplicate or empty names get `#index`), so a clip binds to any skeleton with matching joint names. Duration is the last key.

### `.sasset`

- New asset types: `Skeleton` (7) and `AnimationClip` (8). Both decode without handles, so `DecodeSasset` can run them on job workers.
- Mesh payload version 2 appends the morph targets; model payload version 2 appends each node's `Skin` and `MorphWeights` plus the model's `Skeletons` and `Animations`. Static meshes and models keep writing version 1 **byte for byte**; readers accept both.
- Readers validate:
  - skeletons are parents first;
  - tracks have the component count of their path, strictly increasing finite times and `keys × components (× 3 for cubic)` values;
  - events are sorted;
  - morph delta arrays cover the vertices;
  - default weights match the target count.
- The static model compiler profile is now `v2`, so development auto-cook recooks existing models once.

## CPU runtime

### Skeletons, clips and poses

- `Skeleton` validates the asset (parents first, unique names, at most 65,536 joints) and is shared read-only by animators and instances. `AnimationPose` is one `JointPose` (T, R, S) per joint plus the animator's morph weights.
- `BindClip` resolves each track once per skeleton and `MorphLayout`: joint tracks by joint name, morph-weight tracks by channel (target node) name. Unbound tracks are ignored.
- `SampleClip` writes only bound tracks. Joints without tracks keep what the pose held. Times clamp to the keys.
  - **Step** holds the earlier key.
  - **Linear** interpolates; rotations use shortest-path slerp.
  - **Cubic spline** is glTF's Hermite form, with tangents scaled by the key interval; rotations are normalized afterwards.
- `BlendPose` lerps translation and scale and nlerps rotation (shortest path), per joint times a `BoneMask`. `MakeAdditivePose` / `AddPose` apply a delta relative to a reference pose: translation difference, `reference⁻¹ × pose` rotation (slerped from identity by the weight), scale ratio, and morph weight difference.

### Animator

An `Animator` evaluates one or more layers. Each layer is a state machine.

- **States** play one clip (or none) with a speed (negative plays backwards), looping or clamped, and a normalized start time.
- **Parameters** are floats, bools and triggers. Transitions test conditions (`Greater`, `Less`, `Equal`, `NotEqual`, `IsTrue`, `IsFalse`), and a trigger is consumed when a transition using it fires.
- **Transitions** go from a state or from any state (optionally allowed to re-enter their destination). They may also require an exit time: the state's normalized play time, counting loops, so 1.5 is halfway through the second loop. Any-state transitions are evaluated first, then the current state's, in declaration order. Transitions are not evaluated during a fade; `Play(layer, state, crossfade)` starts a state directly and may interrupt one.
- **Crossfade:** the destination fades in linearly over the transition's duration while the source keeps playing. A fade started mid-fade continues from the state that was fading in; there is no inertial blending.
- **Layers:**
  - `Override` layers lerp the pose below toward theirs by their weight × mask. They start from the pose below, so joints their clips do not animate keep it.
  - `Additive` layers add their pose relative to their state's pose at clip time 0.
  - `SetLayerWeight` changes a layer's weight at run time.
- **Events** fire when playback crosses them: forward in `(previous, next]`, backward in `[next, previous)`, the start of a freshly entered state included. Each loop fires them again, in playback order. Each event records its layer, its state and that state's blend weight at the time.
- **Root motion** (optional, layer 0): each playing state reports the root joint's motion between updates, weighted by the fade. Loops accumulate the clip's net displacement and rotation. The extracted translation axes (and optionally the rotation) are pinned to the rest pose.
- **Morph weights** start at the channels' defaults; clips override them, and `SetMorphWeightOverride` wins over both.

An animator touches only its own state. `UpdateAnimations(characters, dt, jobs)` updates animators and their `SkeletonInstance`s with the job system's `ParallelFor` (or serially), and the result equals the serial update exactly.

### Skeleton instances and sockets

`SkeletonInstance::Update(pose)` computes model-space joint transforms (`RootTransform × parent × local`) and the skinning palette `model × inverseBind` as row-major 3×4 rows, the GPU layout. The previous update's palette and morph weights are kept for motion vectors. The first update after construction or `ResetHistory` (teleports, cuts) has no motion: previous = current. A `Socket` (joint + local offset, `MakeSocket`) gives an attachment's model transform.

## GPU skinning

### Why compute skinning

Skinning runs once per character per frame in a compute pass, writing a regular `StandardVertex` mesh that every later pass reads: GPU visibility, shadow depth views, Clustered Forward+, and any future pre-pass. A vertex-shader path would re-skin in every pass and view (for example up to 10 shadow views), and would need a skinned variant of every program. The cost is one output copy per instance (2 × vertices × 48 bytes).

### Data

- **Source pools** (persistent, device-local), one range per skinned mesh (first-fit `GeometryRangeAllocator`; ranges retire after `lastUse`):
  - bind-pose `StandardVertex` rows;
  - `GpuSkinVertex` rows: 4 packed 16-bit joints, 4 weights, and a per-vertex range of morph deltas;
  - sparse `GpuMorphDelta` rows (target, position, normal and tangent delta). Deltas whose nine components are all zero are dropped: a face target on a body mesh costs nothing on the body.
- **Output meshes:** `CreateInstance` creates a `GeometryHeap` mesh with the source's indices, submeshes and LODs over **2 × VertexCount** vertices. The first half is this frame's skinned vertices; the second half holds the previous frame's positions (floats 0–2 of each slot). Both start at the bind pose.
- **Per frame:** a `GpuSkinDispatch` row per skinned instance, the palettes (current then previous, three `float4` rows per matrix) and the morph weights (current then previous).

### The pass

`Record(graph, geometry)` runs after `GeometryHeap::Import` on the same graph, and before the passes that draw. It:

1. uploads pending sources;
2. selects instances: those given a pose since the last committed frame, plus those settling;
3. sorts them by output page;
4. records one compute pass, `Skinning skin`, with one dispatch per page: 64-thread groups over the vertices (x), one row per instance (y), and the first row as a push constant.

For each vertex, SwimSkinning:

1. adds the vertex's morph deltas in bind space (current and previous weights);
2. blends up to four joints' current and previous rows;
3. writes the skinned position, the normal (cofactor of the blended 3×3, like `ForwardPlus::TransformNormal`, normalized), the tangent (normalized, sign kept) and the texture coordinate;
4. writes the previous position `PreviousOffset` vertices further on.

`Skinning::SkinVertex` and `SkinPosition` are the CPU definition. The program has 28 of the 75 buffer accesses GPU-assisted validation instruments.

- **Settling:** an instance whose pose is not updated in a frame is skinned once more with previous = current, so its motion vectors stop. After that it is not recorded until the next `SetPose`. This matches the GPU Scene's transform settling.
- **Skipped:** instances whose output mesh is still `PendingUpload` in the heap (created after this graph's import) wait for the next frame.
- **Frames:** `CommitFrame` / `AbortFrame` after execution. Abort puts uploads and recorded poses back.

### Drawing a skinned character

1. Create the render object with `Mesh = GetOutputMesh(instance)` and `PreviousVertexOffset = GetPreviousVertexOffset(instance)`.
2. Each frame call `SetPose` with the instance's palettes and weights, and `GpuScene::SetBounds(ComputeBounds(instance))`.

Clustered Forward+ reads the previous local position through `GpuInstanceRecord::PreviousVertexOffset`, so TAA and other motion-vector consumers see the deformation, not only the object's transform ([Clustered Forward+](ForwardPlus.md#motion-vectors-and-jitter-item-75)).

**Bounds.** `Skinning::ComputeBounds` is conservative. Each vertex is a convex combination of its joints' transforms of its morphed bind position. It therefore lies inside the union of each joint's transformed bind-space box, expanded by `Σ|w_t| × reach(t, joint)` (the largest displacement target t applies to that joint's vertices). The CPU test checks 50 random poses with scale and negative morph weights, and the smoke checks every GPU vertex.

## Not yet

- **Engine wiring:** an ECS animator/skinned-mesh component and asset residency of skeletons and clips (item 56). `AssetResidencyService` still interleaves a skinned mesh's two streams into a 72-byte layout that Forward+ does not draw; skinned meshes reach the GPU through `SkinningSystem`.
- **Animation features:** retargeting between skeletons with different proportions, blend trees (1D/2D), inertial blending, IK and ragdoll bridges.
- **Skinning features:** dual-quaternion skinning, more than four influences, compressed (quantized) clips and streams, and sparse morph storage in the asset (the GPU side is already sparse).
- **GPU efficiency:** skipping output copies for off-screen characters (a visibility-driven skinning list) and sharing one output across views of the same frame are later optimizations.

## Tests

| Suite | Covers |
| --- | --- |
| `AssetCompiler.GltfImporter` (+1) | A generated skinned, morphing, animated glTF: skins, influences, targets, mesh weights, every channel path and interpolation |
| `AssetCompiler.StaticModelCompiler` (+2) | Parents-first skeletons with remapped joints and inverse binds, root transform, normalized/sorted influences in stream 1, dense morphs, name-bound clips, the whole graph loaded through `AssetSystem` with typed handles |
| `AssetCompiler.SassetFormat` (+2) | Version 1 kept for static payloads, version 2 round trips, and malformed skeletons, clips, events and morphs rejected |
| `Animation.*` (11) | Quaternion math and composition; skeleton validation and masks; step/linear/cubic/rotation sampling and clamping; binding by name and morph channels; blending, masks and additive round trips; palettes, history and sockets; state machines with crossfades, triggers, exit times and any-state; events across loops, in reverse and without looping; root motion across loops and per axis; additive masked layers and morph overrides; jobified updates equal to serial ones |
| `Render.Skinning.Reference` (5) | Bind-pose identity, rigid and blended joints, morphs in bind space, cofactor normals under non-uniform scale, source validation, bounds containing every vertex of 50 random poses |
| `Render.SkinningSystem` (4) | Output meshes (2 × vertices, the standard layout), one-time source uploads, dirty then settling then idle frames, instances waiting for their output mesh, abort replay, one dispatch per output page with row push constants, lifetimes, capacities and pose validation |
| `ShaderCompiler.SkinningLayout` | SwimSkinning's bindings, group size, push constant and every record offset |
| `ShaderCompiler.GpuAvBudget` | SwimSkinning (28) and the opaque Forward+ program (73) |

### Native smoke

`RHI.Vulkan.Smoke.GpuSkinningMatchesTheCpuReference` animates three characters (two meshes, different speeds, one playing backwards) with `Animator` → `SkeletonInstance` → `SkinningSystem`, plus oscillating morph weights. It skins them for 10 frames and reads each output mesh back, comparing every vertex (position, normal, tangent, texture coordinate) and every previous position with the CPU reference. One character stops posing at frame 6: it must settle once, then keep its settled contents. Every skinned vertex must lie inside `ComputeBounds`. A last frame times 64 characters of 1,032 vertices.

The Forward+ smoke covers the renderer side: its gold cube switches to a mesh whose previous positions are shifted, and the velocity of those pixels is compared with the five-argument `ForwardPlus::MotionVector`.
