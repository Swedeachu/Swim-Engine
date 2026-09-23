# GPU Scene and render extraction

This covers critical-path items **46** (persistent GPU Scene records), **47** (EnTT render extraction → RenderObject updates) and **48** (100k-object stress with dirty-only updates). The GPU Scene is the renderer's persistent object database. It is **not** EnTT: EnTT is one producer, and procedural systems, tools or tests can create render objects directly.

```text
Scene/RenderExtraction  RenderExtractor (EnTT MeshRenderer + TransformSystem dirty list)
        |                                   AssetResidencyService::ResolveRenderMesh
Renderer/GpuScene       GpuScene, GpuRecordBuffer<T>, GpuInstanceRecord, GpuTransformRecord
        |
Renderer/Resources      GpuResourceRegistry (RenderObjectHandle rows, timeline retirement)
Renderer/RenderGraph    staged uploads, graph passes
```

`Renderer/GpuScene` compiles with the backend-neutral renderer sources. It includes no backend, EnTT, scene, asset, IO or job header, and RenderGraph/Resources/Geometry never include it; `scripts/verify-build-layout.py` enforces both directions.

## Records

Two device-local std430 buffers are allocated for `GpuSceneDesc::MaxObjects` rows. Row index = `RenderObjectHandle::Index`.

| Record | Bytes | Contents |
| --- | ---: | --- |
| `GpuInstanceRecord` | 64 | local bounds center/half extents, `MeshIndex` + `MeshGeneration` (GeometryHeap metadata row), `TransformIndex`, `MaterialSet`, `ObjectId`, `Flags`, `SkinIndex`, `LodBias`, `Generation` |
| `GpuTransformRecord` | 96 | `Current` and `Previous` world transform, each three row-major float4 rows (`RenderAffine`) |

Flags have two owners:

- **Producers** control the low 16 bits: `Visible`, `CastShadows`, `Static` and `Selectable`.
- **GpuScene** owns `Live` (the row holds an object) and `HasMesh` (the mesh fields are valid).

A GPU pass should draw a row only when `Live | HasMesh | Visible` are all set. A dead row has `Flags == 0` and `Generation == 0`. `MeshGeneration` can be compared with `GpuMeshMetadata::Generation` to reject stale meshes on the GPU.

Bounds are kept in local space so that transform-only updates never touch instance rows. Culling transforms them with the current transform, using `RenderAffine::MaxScale` for spheres. `RenderBounds::Infinite()` marks an object that must never be culled; an empty `AssetBounds` produces it.

`Shaders/Slang/GpuScene/GpuSceneRecords.slang` declares the same records along with flag constants, `GpuSceneIsDrawable` and `GpuSceneTransformPoint`. The shader compiler now reflects structured-buffer element layouts (`ShaderBindingReflection::ElementFields`/`ElementSize`). `ShaderCompiler.GpuSceneLayout` compares the compiled probe shader's reflection with the C++ `offsetof` values, so a layout drift fails a CPU test.

## GpuScene

- **Creating objects.** `Create`/`TryCreate(RenderObjectDesc)` return a stable handle and write both rows. A new object's `Previous` equals `Current`, so it has no motion.
- **Setters.** `SetTransform`, `SetMesh(mesh, localBounds)`, `SetBounds`, `SetMaterialSet`, `SetFlags` (producer bits only), `SetObjectId`, `SetSkin` and `SetLodBias` mark a row dirty only if its contents actually change. Stale handles return false.
- **Previous transforms.** The first `SetTransform` in a frame copies `Current` into `Previous`; later moves in the same frame update only `Current`. An object that moved in frame N but not in N+1 is re-uploaded once in N+1 with `Previous = Current` (it "settles"), so motion vectors drop to zero without a per-frame walk.
- **`Import(graph)` ends the frame.** It uploads the dirty rows of each buffer: sorted rows become contiguous runs, and each buffer gets one staging allocation and one transfer pass with one `CopyBuffer` per run. The result is `GpuSceneGraphResources{Instances, Transforms, UploadPasses, RowCount, …}`, and both buffers rest in `ShaderRead` for later passes.
- **After the graph runs.** Call `CommitUploads()` when `Execute` succeeds and `AbortUploads()` when it fails; aborted rows upload again on the next import. A second `Import` before commit or abort throws.
- **Destroying objects.** `Destroy(handle, lastUse)` invalidates the handle immediately and writes the row as dead at the next upload. The row index is reused only after `lastUse` completes (`Collect`/`Drain`). `RowCount` is a high-water mark that bounds GPU iteration.
- **Statistics.** `GetStats()` reports live/retiring objects, dirty rows, rows and bytes uploaded by the last import, runs, total bytes, settling transforms and the frame count.

`GpuRecordBuffer<Record>` is the reusable part: a persistent record buffer with a mirror, deduplicated dirty rows and staged run uploads. Lights, views, skins and material records (Phases 14–15, item 59) should be further tables built on it.

## RenderExtractor (EnTT producer)

`Engine::MeshRenderer` (in `Components/MeshRenderer.h`) holds `Parts` (a mesh `AssetHandle` and a material set each), `Flags`, `LodBias` and `SkinIndex`.

- Each part becomes one render object keyed by `(scene, entity, part index)`. `Find(entity, part)` returns the handle.
- Change a `MeshRenderer` with `registry.patch`, `replace` or `emplace_or_replace`; plain writes through `registry.get` are not observed. The legacy `Material` component is untouched.

`RenderExtractor(registry, transformSystem, gpuScene, RenderExtractorDesc{Scene, ResolveMesh, WorldTransform, ObjectId})` connects to the component's construct, update and destroy signals, and queues existing components. `Extract(lastUse)` applies only the following, in this order:

1. **Removals**, for removed components or destroyed entities. A component removed and re-added within a frame is rebuilt.
2. **Changed components**, in signal order so row assignment is deterministic. Parts are reconciled in place, keeping their rows; added parts are created, dropped parts are destroyed, and a changed mesh is re-resolved.
3. **Moved transforms.** Entities on this frame's `TransformSystem` dirty list get `SetTransform`. `Transform` queues the children of a moved parent itself.
4. **Pending parts.** Parts whose mesh is not resident yet, or that found the scene full, are retried.

Static objects cost nothing after their first extraction. Call `Extract` once per frame after scene updates and before the scene's `BeginFrameTransformTracking`, then `GpuScene::Import`.

- **Scene unload:** `ReleaseAll(lastUse)` retires every object through the GPU Scene's deferred path. `Rescan()` queues all components again, and `RefreshMeshes()` re-resolves every mesh.
- **Mesh resolution:** `AssetResidencyService::ResolveRenderMesh` returns the GPU mesh of a Resident mesh together with the local bounds captured when it was staged, which stay valid after the CPU asset unloads.
- **World transforms:** `RenderExtraction/Runtime/MakeTransformWorldSource()` reads `Engine::Transform::GetWorldMatrix`. It is compiled into SwimEngine only, because the extraction core must stay testable without the Transform/Scene runtime.
- **Object ids:** `ObjectId` should come from a durable identity. The verifier forbids raw EnTT integrals in scene code.

## Stress (item 48)

| Case | Objects | Per-frame change | Asserted |
| --- | ---: | --- | --- |
| `Render.GpuSceneStress.HundredThousandObjectsUploadOnlyDirtyRows` | 100,000 | 1% moved + 10 material edits | exactly 1,000 (then 2,000 with settling) transform rows and 10 instance rows per frame; bytes < 2% of the initial upload; idle frame uploads nothing; GPU bytes equal the mirror |
| `Scene.RenderExtraction.HundredThousandEntitiesExtractDirtyOnly` | 100,000 entities | 1% moved | 1,000 transforms written per frame, no component work, transform-only uploads |
| `RHI.Vulkan.Smoke.GpuSceneHundredThousandObjectsDirtyUploads` (opt-in) | 100,000 | 1% moved, 100 destroyed, 50 hidden; then settle; then idle | exact upload bytes each frame; a compute probe reads every row on the GPU and matches the mirror (world centers, previous centers, drawability, dead rows, object ids) |

The Linux Debug container measured about 140 ms to create and upload 100k objects on the mock device, about 1.8 ms per 1% dirty frame for the GPU Scene, and about 6.5 ms per frame for extraction plus upload. These timings are informational; the tests assert volume, not time.

## Not in this checkpoint

- The engine runtime does not construct a `GpuScene` or `RenderExtractor` yet. The sandbox still renders through the transitional renderer and `SceneBVH`.
- There is no GPU culling or draw generation over the scene yet (Phase 13, items 49–57).
- Material sets are producer-defined ids until GPU material records exist (items 58–59). Lights, views and skins are not tables yet.
- `ModelAsset` graphs are not expanded into `MeshRenderer` parts automatically.
