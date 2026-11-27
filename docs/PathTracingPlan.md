# Real-time path tracing plan

Status: **planning only**. Nothing here is implemented. This document plans a real-time path tracer for Swim-Engine: a third lighting tier, **Path traced**, next to **Classic** and **Enhanced**. It is built on D3D12 first and then brought to parity on Vulkan through the shared RHI.

It is written against the engine as it stands (see [Rhi.md](Rhi.md), [RenderGraph.md](RenderGraph.md), [ForwardPlus.md](ForwardPlus.md), [RenderFeatures.md](RenderFeatures.md), [PostProcess.md](PostProcess.md), [TemporalAntiAliasing.md](TemporalAntiAliasing.md), [ScreenSpace.md](ScreenSpace.md), [GpuScene.md](GpuScene.md), [GpuResidency.md](GpuResidency.md), [Materials.md](Materials.md), [Lights.md](Lights.md), [Particles.md](Particles.md), [Animation.md](Animation.md), [Shadows.md](Shadows.md), [Reflections.md](Reflections.md), [Environment.md](Environment.md)). Bracketed tags such as [R6] point into *References* at the end. Hardware and SDK facts were checked against primary sources as of 2026-11-27. Re-check versions before pinning anything.

Contents:

1. Goals, non-goals, tiers, targets and budgets
2. Hardware and API baseline
3. RHI additions
4. Scene representation
5. Light transport
6. Denoising and reconstruction
7. Upscaling and frame generation
8. Integration with the existing renderer
9. Third-party SDKs and licensing
10. Milestones
11. Debug and profiling tooling
12. Open questions
13. References

---

## 1. Goals, non-goals, tiers, targets and budgets

### 1.1 Goals

- **Full real-time path tracing** as a selectable lighting tier. Direct light, shadows, reflections, refraction, global illumination and emissive lighting all come from one unified light-transport integrator. Shadow maps, SSR, GTAO, probes and planar captures are not stacked on top. Soft shadows, colour bleeding, emissive bounce and glossy inter-reflection fall out of the integrator.
- **Live switching** between Classic, Enhanced and Path traced while the engine runs, with no restart and no device recreation. The first switch may cost one hitch, which must stay under 250 ms. Later switches must not hitch.
- **Works with what exists.** Render features (clouds, weather, water, fire, camera effects, lens flare), GPU particles, skinned entities, the first-person overlay, post-processing, the screen blur and the UI all keep working in the Path traced tier. Where an existing effect is made redundant (shadow atlas, SSR, GTAO, probes), it switches off in that tier and its settings are kept.
- **Content the engine actually renders.** A dense voxel world: many small cubes, alpha-tested foliage and cut-out blocks, translucent glass and water, emissive blocks, many dynamic lights. Plus skinned and animated entity models and particles. The plan is tuned for that scene shape, but the APIs stay general.
- **Bleeding edge, with honest fallbacks.** Use DXR 1.2 (SER, OMMs), ReSTIR, radiance caching and neural reconstruction where the hardware has them. Each one degrades gracefully when it is missing.
- **Backend parity.** D3D12 ships first. Vulkan follows on the same RHI contracts, with no renderer code that checks the API.

### 1.2 Non-goals

- Offline or reference-quality rendering as a product. The engine gets an accumulating *reference mode* as a correctness tool (section 11), not as a feature.
- Path tracing on GPUs without hardware ray tracing. A compute-only software BVH fallback (such as a distance-field tracer [R58]) is out of scope. Those GPUs stay on Enhanced.
- Replacing the raster renderer. Classic and Enhanced remain the default tiers and keep receiving fixes.
- Spectral rendering, caustics through arbitrary refractive chains, and subsurface scattering beyond a thin-surface approximation in v1.
- Building BVHs in software for rasterized geometry that has no RT representation. A feature that does not provide RT geometry is drawn in primary visibility only (section 8.4).
- Frame generation counting toward the frame-rate target. Targets are measured on rendered frames.

### 1.3 Tiers

The engine has no global lighting-tier setting today. Quality is spread over many `RenderSettings` flags (`Shadows`, `ScreenSpace`, `ReflectionProbes`, `PlanarReflections`, `Environment` and others), and `VfxSettings` already uses Off / Classic / Enhanced names per effect family. This plan adds one setting on top of those flags, `RenderSettings::Lighting`, of type `LightingTier`:

| Tier | What renders | Direct light | Indirect light | Reflections | AA / upscale |
|---|---|---|---|---|---|
| **Classic** | Forward+ with a minimal effect set; the cheapest look | clustered lights, sun cascades (low) | ambient + SH sky | none / cube IBL | FXAA, SMAA or TAA (native) |
| **Enhanced** | today's full raster stack | clustered lights + shadow atlas, cascade cache | GTAO + IBL + probes | SSR + probes + planar | TAA (+ MSAA), optional DLSS SR / FSR / XeSS (M9) |
| **Path traced** | this plan | ReSTIR DI over every light and emissive surface, traced visibility | path-traced GI with radiance cache, ReSTIR GI or PT | traced (glossy and mirror), traced refraction | DLSS Ray Reconstruction, or NRD + an upscaler |

- A tier is a **preset**. Choosing it writes the individual flags. Changing a flag afterwards is allowed and shows the tier as "Custom" in tools.
- *Optional, later (M11):* Enhanced may use the same acceleration structures for traced sun shadows or traced reflections, as hybrid options. They are listed as stretch work, not as part of the core plan.

### 1.4 Performance targets

Reference hardware: **laptop GeForce RTX 4070**. That is AD106 with 4,608 shaders, 36 RT cores and 144 tensor cores, and **8 GB GDDR6 on a 128-bit bus (256 GB/s)**, at 35–115 W TGP plus dynamic boost [R67]. The 8 GB of VRAM and the bandwidth are the binding constraints, more than RT throughput. Every budget below is measured at a 100 W+ TGP configuration, with the laptop plugged in.

| Target | Output | Internal (render) | Reconstruction | Frame time |
|---|---|---|---|---|
| **Floor** (must hit) | 1920×1080 | 960×540 (DLSS Performance, 50%) | DLSS RR | ≤ 33.3 ms (30 fps), 1% low ≥ 24 fps |
| **Goal** | 1920×1080 | 1280×720 (DLSS Balanced/Quality, 58–67%) | DLSS RR | ≤ 20 ms (50 fps) |
| **Stretch** | 1920×1080 | 960×540 | DLSS RR | ≤ 16.7 ms (60 fps) |
| **Panel native** (if the laptop is 1440p) | 2560×1440 | 1280×720 (Performance) | DLSS RR | ≤ 33.3 ms |

These targets apply to the *stress scenes* in section 10.2, not to an empty world. Frame generation (section 7.3) can be layered on top for perceived smoothness. It does not count toward these numbers.

### 1.5 Per-pass GPU budget (Floor and Goal, laptop RTX 4070)

The baseline is 1080p output from 960×540 internal (≈0.52 Mpx). The Goal column uses 720p internal (≈0.92 Mpx). Ray costs scale roughly with internal pixels. Reconstruction cost scales with output pixels.

| # | Pass | Floor (540p) ms | Goal (720p) ms | Notes |
|---|---|---:|---:|---|
| 1 | Acceleration-structure maintenance | 1.0 | 1.0 | BLAS builds for edited chunks (budgeted), skinned refits, TLAS rebuild. Hard cap 1.5 ms; overflow defers to later frames |
| 2 | Raster primary: depth prepass + G-buffer + overlay | 1.2 | 1.8 | existing Forward+ geometry path in a G-buffer variant; HZB |
| 3 | Light structure update (light BVH / presampling / ReGIR grid) | 0.3 | 0.3 | local lights + emissive clusters |
| 4 | ReSTIR DI: initial candidates + temporal + spatial + visibility | 2.0 | 3.2 | 1 shadow ray/px final, plus temporal visibility reuse |
| 5 | Indirect path tracing (1 path/px, SER, cache termination) | 4.5 | 7.5 | 1–3 bounces typical; uber raygen |
| 6 | ReSTIR GI (M8: ReSTIR PT) resampling | 1.5 | 2.4 | temporal + 1 spatial pass |
| 7 | Radiance cache update + resolve (SHaRC) | 0.8 | 1.0 | ~3–4% of pixels trace update paths |
| 8 | Translucent layer (water/glass reflection + refraction) | 1.2 | 1.8 | only where the translucent mask is set |
| 9 | Volumetrics (fog, sun in-scatter, underwater) | 0.8 | 1.0 | froxel grid with traced sun visibility |
| 10 | Particles + feature passes on the noisy input | 1.0 | 1.2 | lit from the radiance cache / ReGIR (section 5.10) |
| 11 | Reconstruction: DLSS Ray Reconstruction 540p/720p → 1080p | 2.5 | 2.5 | estimate for the transformer model on this GPU; measure in M6 |
| 12 | Post (exposure, bloom, grading, tone map) + camera features | 0.7 | 0.7 | unchanged modules at output resolution |
| 13 | UI, screen blur, presentation | 0.3 | 0.3 | unchanged |
| | **Total** | **17.8** | **24.7** | Floor leaves ~15 ms headroom for scene variance; Goal at 720p sits at ~40 fps, so 50 fps needs the M9 optimisation pass |

The fallback path (no DLSS RR) replaces row 11 with NRD (ReBLUR or ReLAX diffuse + specular, SIGMA shadow, ≈1.8–2.5 ms at 540p) plus an upscaler (DLSS SR / FSR / XeSS / engine TAA, ≈0.8–1.2 ms). Every row gets a measured column in M9. A pass that exceeds its budget by 25% for 10 s in a stress scene is a bug against that milestone.

**CPU budget:** ≤ 2 ms of render-thread time for path-tracing recording on top of today's frame. That covers BLAS build scheduling, TLAS instance upload and shader table updates. The work is GPU-driven: instance lists are written by compute from the GPU Scene, not by the CPU.

### 1.6 VRAM budget (8 GB card)

| Item | Budget | Notes |
|---|---:|---|
| BLAS (compacted) for voxel chunks within RT distance | 450 MB | section 4.2; RT distance shorter than raster distance on 8 GB |
| BLAS for entities, rigid meshes, skinned refit targets | 80 MB | |
| TLAS + instance descriptors (double-buffered) | 16 MB | ~64 B/instance × up to 64k instances × 2, plus the AS |
| Build scratch (shared, transient) | 64 MB | pooled; builds batched to fit |
| Opacity micromaps | 16 MB | shared per block texture, so tiny (section 4.6) |
| RT geometry records, material/light tables | 32 MB | |
| ReSTIR reservoirs (DI + GI, current + previous) | 60 MB (540p) / 110 MB (720p) | 32–48 B per reservoir |
| Radiance cache (SHaRC hash + resolved) | 64 MB | 4M entries |
| G-buffer + guide buffers + histories | 120 MB | |
| DLSS RR (or NRD) internal resources | 250 MB | measure in M6 |
| **Total added by Path traced** | **≈1.2–1.3 GB** | must fit beside the world's textures and geometry; residency rules in section 3.8 |

---

## 2. Hardware and API baseline

### 2.1 D3D12 requirements by capability

| Capability | Required for | API / level | Notes |
|---|---|---|---|
| DXR tier 1.1 | **minimum for Path traced** | `D3D12_RAYTRACING_TIER_1_1`, inline `RayQuery` (SM 6.5+), `ExecuteIndirect` dispatch rays | the backend already detects it (`D3D12Features.cpp` sets `GraphicsCapabilities::RayQuery` / `RayTracingPipeline`) and already holds `ID3D12Device5` and `ID3D12GraphicsCommandList7` |
| Shader Model 6.6+ | bindless heap indexing (optional path) | `ResourceDescriptorHeap[]` | the engine keeps descriptor-table bindless as primary for Vulkan symmetry (section 3.6) |
| **Shader Model 6.9** | SER (`HitObject`, `MaybeReorderThread`) and OMM HLSL | Agility SDK **1.619+** (SM 6.9 retail), DXC **1.9.2602+** [R2][R4][R5] | SER is accepted by every SM 6.9 driver, but only some reorder: RTX 40+ and Arc B-series do, RX 9000 does not [R2]. The engine queries whether reordering is actually performed and reports it |
| DXR tier 1.2 | opacity micromaps | `D3D12_RAYTRACING_TIER_1_2` [R3] | OMM: hardware on RTX 40+, emulated on older RTX; not exposed on AMD/Intel at SM 6.9 retail [R2] |
| Enhanced barriers | AS build sync | already used by the backend | `D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE`, `ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_READ/WRITE` |
| Clustered geometry (CLAS) + partitioned TLAS | M11 bleeding edge | Agility **1.722-preview**, SM 6.10 preview, Developer Mode [R6] | not retail at the time of writing; SM 6.10 retail is reported for 1.621 [R9]. Prototype only, behind a dev flag |
| Work graphs 1.0 | optional (M11) | already reported by drivers | possible use: GPU-driven BLAS build scheduling and material-sorted shading. Not on the critical path [R10] |

**Agility SDK.** [Rhi.md](Rhi.md) records that the D3D12 backend needs no Agility SDK today and runs on the in-box runtime (SM 6.8, raytracing tier 1.1). The Path traced tier changes that:

- SER and OMM HLSL need SM 6.9, which needs Agility SDK ≥ 1.619. **1.620** is the current retail release [R9]; 1.619 is the minimum.
- The executable exports `D3D12SDKVersion` and `D3D12SDKPath` and ships `D3D12Core.dll` in a subfolder (for example `D3D12\`) [R8].
- The in-box runtime remains a supported fallback. If the Agility runtime fails to load, the device comes up at SM 6.8 and Path traced runs without SER and OMM: inline ray queries plus DXR 1.0/1.1 pipelines. That costs roughly 20–25% of traversal and shading time in published shipping data [R35][R36].
- No vendor extension API (NVAPI) is planned. Everything needed now ships in DXR 1.2.

**DXC.** `SlangDxil.cmake` drives the DXC from the Windows SDK. SM 6.9 libraries need DXC 1.9.2602.16 or newer [R2]. That build must be pinned beside the pinned Slang (2026.22.1) the same way, by SHA-256. Slang's SER support is documented as *preliminary* [R22], so the M4 spike must confirm that Slang emits `MaybeReorderThread` / `HitObject` DXIL and the EXT SPIR-V.

### 2.2 Feature detection and fallbacks

New `GraphicsCapabilities` flags, filled by both backends. Renderer code checks only these flags, never the API, as the RHI rules require:

| Flag | D3D12 source | Vulkan source |
|---|---|---|
| `RayQuery` (exists) | tier ≥ 1.1 | `VK_KHR_ray_query` |
| `RayTracingPipeline` (exists) | tier ≥ 1.0 | `VK_KHR_ray_tracing_pipeline` |
| `AccelerationStructureCompaction` | always with DXR | always with `VK_KHR_acceleration_structure` |
| `RayTracingInvocationReorder` (API accepted) | SM ≥ 6.9 | `VK_EXT_ray_tracing_invocation_reorder` or `VK_NV_…` |
| `RayTracingReorderPerformed` (hardware reorders) | SM 6.9 SER caps query | the `rayTracingInvocationReorderReorderingHint` property |
| `OpacityMicromaps` | tier ≥ 1.2 | `VK_EXT_opacity_micromap` |
| `RayTracingPositionFetch` | SM 6.10 `TriangleObjectPositions` (preview) | `VK_KHR_ray_tracing_position_fetch` |
| `PartitionedTlas`, `ClusterAccelerationStructures` | 1.722-preview options | `VK_NV_partitioned_acceleration_structure`, `VK_NV_cluster_acceleration_structure` |
| `ShaderTableLimits` (struct) | 32 B identifiers, 32 B record / 64 B table alignment (DXR constants) | `shaderGroupHandleSize`, `shaderGroupBaseAlignment`, `shaderGroupHandleAlignment` |
| `MaxRayRecursionDepth` | 31 (DXR) | `maxRayRecursionDepth` |

The tier gate is `PathTracedAvailability`, computed once per device:

| Result | Condition | Behaviour |
|---|---|---|
| `Full` | RayQuery + pipelines + SER reorder + OMM | every feature |
| `Reduced` | RayQuery + pipelines, no reorder or no OMM | same integrator; alpha test in any-hit only; no reordering. Expect ≈1.3–1.6× trace cost |
| `Unavailable` | no RayQuery | tier hidden in the menu; a setting saved as Path traced falls back to Enhanced with one log line |

**Reconstruction availability** is detected separately (section 7): DLSS RR (RTX 20+), FSR Ray Regeneration (RX 9000+, DX12 only [R56]), otherwise NRD.

### 2.3 Vulkan parity plan

| D3D12 | Vulkan equivalent | Notes |
|---|---|---|
| `BuildRaytracingAccelerationStructure` | `vkCmdBuildAccelerationStructuresKHR` (`VK_KHR_acceleration_structure`, needs `VK_KHR_deferred_host_operations` enabled, plus buffer device address) [R15] | device builds only; host builds never used |
| AS postbuild info (compacted size) | `vkCmdWriteAccelerationStructuresPropertiesKHR` into a query pool | maps onto the RHI `QueryPool` |
| `CopyRaytracingAccelerationStructure` (compact) | `vkCmdCopyAccelerationStructureKHR` `COMPACT` | |
| `RayQuery` in compute | `VK_KHR_ray_query` [R13] | already detected in `VulkanAdapterInfo.cpp` |
| state object + `DispatchRays` | `VK_KHR_ray_tracing_pipeline`, `vkCmdTraceRaysKHR` / `…Indirect2KHR` [R14] | |
| SER | `VK_EXT_ray_tracing_invocation_reorder` (ratified 2025-11, promoted from NV) [R16]; `VK_NV_ray_tracing_invocation_reorder` as fallback [R17] | the EXT adds fused reorder + execute |
| OMM | `VK_EXT_opacity_micromap` [R18] | |
| Partitioned TLAS / clusters | `VK_NV_partitioned_acceleration_structure`, `VK_NV_cluster_acceleration_structure` [R20][R21] | NV-only on Vulkan today; M11 |
| TLAS SRV descriptor | `VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR` | `Rhi::DescriptorType::AccelerationStructure` already exists |

Vulkan follows in **M10**, after the D3D12 path reaches M6 quality. Every M1–M4 smoke is written backend-neutral from the start, matching the existing `AddSmoke` dual registration. Vulkan stubs throw `std::logic_error("not implemented")` until M10, so no D3D12-only shortcut gets into the contracts. DLSS RR, NRD, RTXDI, SHaRC and NRC all support Vulkan [R27][R29][R37]. FSR Ray Regeneration does not [R56].

---

## 3. RHI additions

The RHI already reserves `BufferUsage::AccelerationStructureStorage / AccelerationStructureBuildInput`, `ResourceState::AccelerationStructureRead / Write` and `DescriptorType::AccelerationStructure`. Vulkan maps the buffer usages already (`VulkanFormatUtils.cpp`). What follows completes that groundwork in the style of `RhiContracts.h`: `*Desc` structs, `std::unique_ptr` objects owned by the device, and deferred release on the GPU timeline.

### 3.1 Acceleration structures

```cpp
namespace Swim::Rhi
{
	enum class AccelerationStructureType : std::uint8_t { BottomLevel, TopLevel };

	enum class AccelerationStructureBuildFlags : std::uint32_t
	{
		None = 0, AllowUpdate = 1u << 0, AllowCompaction = 1u << 1,
		PreferFastTrace = 1u << 2, PreferFastBuild = 1u << 3, MinimizeMemory = 1u << 4,
		AllowOpacityMicromapUpdate = 1u << 5, AllowDataAccess = 1u << 6, // position fetch
	};

	enum class GeometryFlags : std::uint8_t { None = 0, Opaque = 1, NoDuplicateAnyHit = 2 };

	struct TriangleGeometryDesc
	{
		const Buffer* Vertices = nullptr; std::uint64_t VertexOffset = 0; std::uint32_t VertexStride = 0;
		std::uint32_t VertexCount = 0; Format VertexFormat = Format::RGB32Float; // or RGBA16Float/RG16 snorm-style compressed
		const Buffer* Indices = nullptr; std::uint64_t IndexOffset = 0; IndexType IndexFormat = IndexType::Uint32;
		std::uint32_t IndexCount = 0; // 0 = non-indexed
		const Buffer* Transform = nullptr; std::uint64_t TransformOffset = 0; // optional 3x4
		GeometryFlags Flags = GeometryFlags::Opaque;
		const OpacityMicromapArray* Omm = nullptr; // + per-triangle index buffer, see 3.5
		const Buffer* OmmIndices = nullptr; std::uint64_t OmmIndexOffset = 0; IndexType OmmIndexFormat = IndexType::Uint16;
	};

	struct AabbGeometryDesc { const Buffer* Aabbs; std::uint64_t Offset; std::uint32_t Count; std::uint32_t Stride; GeometryFlags Flags; };

	struct AccelerationStructureSizes { std::uint64_t Result, BuildScratch, UpdateScratch; };

	struct AccelerationStructureDesc { AccelerationStructureType Type; std::uint64_t Size; std::string_view DebugName; };

	class AccelerationStructure // owns or suballocates an AccelerationStructureStorage buffer
	{
	  public:
		virtual ~AccelerationStructure() = default;
		virtual AccelerationStructureType GetType() const = 0;
		virtual std::uint64_t GetGpuAddress() const = 0; // TLAS instance reference / root SRV
		virtual std::uint64_t GetSize() const = 0;
	};

	// Device:
	//   AccelerationStructureSizes GetAccelerationStructureSizes(const BlasBuildInputs&|const TlasBuildInputs&, flags);
	//   std::unique_ptr<AccelerationStructure> CreateAccelerationStructure(const AccelerationStructureDesc&, Buffer* placement = nullptr, std::uint64_t offset = 0);
	// CommandList:
	//   void BuildAccelerationStructures(std::span<const AccelerationStructureBuild> builds); // batch; Update = refit with Source
	//   void CopyAccelerationStructure(const AccelerationStructure& src, AccelerationStructure& dst, AccelerationStructureCopyMode mode); // Clone | Compact
	//   void WriteAccelerationStructureCompactedSizes(std::span<const AccelerationStructure* const>, QueryPool&, std::uint32_t firstQuery);
}
```

- **TLAS instances** are a plain `Buffer` of `AccelerationStructureInstance` records, 64 bytes. The layout is identical in both APIs: a 3×4 transform, a 24-bit custom index, an 8-bit mask, a 24-bit hit-group offset, 8 bits of flags, and the BLAS address. Compute writes them from the GPU Scene, so the CPU never touches per-instance data. The RHI exposes the record as a C++ struct with `static_assert`s and a Slang mirror, like `GpuSceneRecords.slang`.
- **Suballocation.** BLASes for voxel chunks are small and numerous. The D3D12 backend places them in large `AccelerationStructureStorage` pool buffers, using the same placed-heap and deferred-release machinery the backend already has, at 256-byte AS alignment. This follows NVIDIA's advice to pool small AS allocations so that compaction pays off [R31][R32]. `RenderAccelerationStructurePool` (renderer-level, above the RHI) owns the free lists and defragmentation, mirroring `StreamingBuffer`.
- **Deferred release.** AS objects join the existing deferred-release queue keyed to the GPU timeline. The rule "never free memory that submitted work may still use" covers them unchanged.
- **States.** `AccelerationStructureWrite` for build destinations; `AccelerationStructureRead` for trace sources and build sources (refit, compaction). A BLAS-build → TLAS-build dependency is a same-state barrier on the AS pool: a D3D12 UAV-type barrier, or a Vulkan memory barrier on `ACCELERATION_STRUCTURE_WRITE` → `ACCELERATION_STRUCTURE_READ`. The render graph emits these (section 8.2).
- **D3D12 mapping:** `ID3D12Device5::GetRaytracingAccelerationStructurePrebuildInfo`, `ID3D12GraphicsCommandList4::BuildRaytracingAccelerationStructure`, `EmitRaytracingAccelerationStructurePostbuildInfo`, `CopyRaytracingAccelerationStructure`. The backend's `list7` already derives from `CommandList4`.

### 3.2 Ray query in compute

No new pipeline type. A compute program that declares `RaytracingAccelerationStructure` reflects a `DescriptorType::AccelerationStructure` binding. `DescriptorWrite` gets an `AccelerationStructure*` member:

- **D3D12:** an SRV with `D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE`, written into the table. It can also be a root SRV, which fits the existing root-descriptor budget rule ("single structured buffers become root SRVs").
- **Vulkan:** `VkWriteDescriptorSetAccelerationStructureKHR`.
- **Reflection:** `ShaderCompiler::BuildNativeBindings` must match `RaytracingAccelerationStructure` parameters by name, like the other classes. A `ShaderCompiler.RayQueryLayout` test pins it.

Inline ray queries carry the light work: shadow and visibility rays, ReSTIR visibility tests, AO probes and radiance-cache update rays. All of them run as ordinary `RenderFeatureComputePass`-style dispatches.

### 3.3 Ray tracing pipelines and shader tables

```cpp
struct RayTracingShaderGroup { enum Kind { General, TrianglesHitGroup, ProceduralHitGroup } Type;
                               std::string_view General, ClosestHit, AnyHit, Intersection; std::string Name; };
struct RayTracingPipelineDesc
{
	const ShaderProgram* Library;              // one DXIL library / SPIR-V module with every RT entry point
	std::span<const RayTracingShaderGroup> Groups;
	const PipelineLayout* Layout;              // global layout only: no local root signatures
	std::uint32_t MaxPayloadBytes, MaxAttributeBytes = 8, MaxRecursionDepth = 1;
	bool AllowOpacityMicromaps = false;        // D3D12_RAYTRACING_PIPELINE_FLAG_ALLOW_OPACITY_MICROMAPS / VK pipeline flag
	bool SkipProceduralPrimitives = true;
	std::string_view DebugName;
};
class RayTracingPipeline { public: virtual std::span<const std::byte> GetGroupHandle(std::uint32_t group) const = 0; };

struct ShaderTableDesc { const RayTracingPipeline* Pipeline; std::span<const std::uint32_t> RayGen, Miss, HitGroups, Callable; };
class ShaderTable;   // one device-local buffer laid out with the backend's alignment; immutable after creation
// CommandList: BindRayTracingPipeline(RayTracingPipeline&), DispatchRays(const ShaderTable&, w, h, d),
//              DispatchRaysIndirect(const ShaderTable&, Buffer& args, std::uint64_t offset)
```

Rules:

- **Global bindings only. No local root arguments.** Each hit, miss and raygen record holds only the group handle. Per-geometry data is found through `InstanceID()` + `GeometryIndex()` into the RT geometry record buffer (section 4.4). Shader tables then never need rebuilding when geometry changes, only when the pipeline changes. D3D12 and Vulkan tables stay identical apart from alignment. The existing descriptor-table binding state (`Rhi::DescriptorBindingState`) gains a third bind point, `RayTracing`, with the same compatibility rules.
- **Few hit groups.** The integrator uses an *uber-raygen* design (section 5.2). Hit groups contain at most an any-hit shader for alpha test. Shading happens in raygen after `HitObject` reordering. So there are ~4 hit groups (opaque, alpha-tested, alpha-tested shadow, translucent), selected by the instance's hit-group contribution and the ray's contribution multiplier.
- **Pipeline creation cost.** RT state objects compile slowly. They go through the existing pipeline cache: `ID3D12PipelineLibrary` cannot store state objects, so D3D12 relies on the driver cache, and the pipeline-cache blob covers Vulkan. They are created on a background job when the device reports `PathTracedAvailability != Unavailable`, not when the user first selects the tier. That job is part of the "no hitch after the first switch" goal.
- **D3D12 mapping:** `CreateStateObject` (DXIL library + hit groups + shader config + pipeline config 1 with the OMM flag + global root signature), `ID3D12StateObjectProperties::GetShaderIdentifier`, `SetPipelineState1`, `DispatchRays`. Indirect dispatch goes through `ExecuteIndirect` with a `DISPATCH_RAYS` command signature.

### 3.4 Shader execution reordering

There is no RHI surface. SER is a shader-language feature (`HitObject`, `MaybeReorderThread` / Slang `ReorderThread`) [R4][R5][R22]. The RHI's only jobs:

- report `RayTracingInvocationReorder` / `RayTracingReorderPerformed`;
- enable the right Vulkan extension and SPIR-V capability;
- compile the RT library at SM 6.9 when the flag is set.

The shader build produces two variants of the integrator library, `*_ser` and `*_noser`. The no-SER variant is the same source with `SWIM_SER=0` making the reorder call a no-op. Slang exposes SER as a preliminary API [R22], so the engine wraps it in a small `Rt/Reorder.slang` shim that can be adjusted when the API settles.

`MaybeReorderThread` is legal **only in ray generation shaders** [R5]. That requirement is why the integrator's heavy passes are raygen pipelines, not inline ray queries in compute.

### 3.5 Opacity micromaps

```cpp
struct OpacityMicromapDesc { std::uint32_t SubdivisionLevel; OmmFormat Format /* TwoState | FourState */; std::uint32_t Count; };
class OpacityMicromapArray; // built on the GPU from a packed bit buffer + descriptor buffer
// Device: GetOpacityMicromapArraySizes(...), CreateOpacityMicromapArray(...)
// CommandList: BuildOpacityMicromapArray(const OpacityMicromapBuild&)
```

- D3D12 maps this to the tier 1.2 OMM array build [R3]. Vulkan maps it to `vkCmdBuildMicromapsEXT` [R18].
- OMMs attach to triangle geometry through `TriangleGeometryDesc::Omm` + `OmmIndices`. The special indices `FullyTransparent`, `FullyOpaque`, `FullyUnknownTransparent` and `FullyUnknownOpaque` are supported, so a triangle that is entirely opaque or empty costs no micromap at all.
- The ray flag that forces two-state evaluation (`RAY_FLAG_FORCE_OMM_2_STATE` / `gl_RayFlagsForceOpacityMicromap2StateEXT`) is used on indirect rays, following the shipping practice in [R36].

### 3.6 Bindless resource model

The engine's bindless model is descriptor tables: space 1 is the shared `BindlessResourceTable` (textures and samplers, partially bound, update-after-bind). D3D12 maps it to a range of the shader-visible heap with null-descriptor initialisation. Path tracing needs **every geometry and material resource reachable from any hit**, so it extends the model rather than replacing it:

| Binding (space 1) | Contents | Status |
|---|---|---|
| `Textures[]` | every material texture (`Texture2D`), plus `Texture2DArray` views for block texture arrays | exists (textures); arrays added |
| `Samplers[]` | `GpuSamplerCache` | exists |
| `Buffers[]` (new) | `ByteAddressBuffer` views of GeometryHeap vertex and index pages, feature streaming buffers (voxel faces), skinned output pages | new; same timeline-safe slot reuse rule as textures |

- One space-2 table per RT pass holds the TLAS, the RT geometry record buffer, the material table, the light structures and the pass outputs.
- `ResourceDescriptorHeap[]` (SM 6.6 dynamic resources) would be simpler on D3D12 but has no exact Vulkan twin without descriptor heaps or descriptor buffers. The engine keeps descriptor-table bindless on both backends, so programs reflect identically. That matches the existing rule that the SPIR-V reflection is the interface for both.
- **Descriptor heap size.** The D3D12 shader-visible CBV/SRV/UAV heap limit is 1,000,000 descriptors at binding tier 3, which is ample. The `Buffers[]` array is sized from `GraphicsCapabilities::Descriptors`, for example 65,536.

### 3.7 Queues and async compute

The render graph runs every pass on one graphics queue today ([RenderGraph.md](RenderGraph.md)). AS builds and the radiance-cache update are natural async-compute candidates. That is the open Phase 10 queue-ownership work and is *not* required. The budgets in section 1.5 assume serial execution. Async overlap is an M9 optimisation if the queue work lands by then.

### 3.8 Residency and memory

- `RhiMemoryBudget` already reports per-segment usage. The Path traced tier registers its allocations under an `RtBudget` category (AS pool, scratch, reservoirs, caches, reconstruction), shown in `RenderStats`.
- Under memory pressure (local segment usage above 90% of budget for 2 s):
  1. shrink the RT distance by one chunk ring and evict the outer BLASes;
  2. drop reservoir history to DI only;
  3. lower the internal resolution one step;
  4. log and stay there until pressure clears for 30 s.

  BLASes are never evicted for chunks inside the primary-visibility frustum near the camera.
- Compaction is mandatory for static BLASes (voxel chunks, rigid meshes). Typical savings are ≥ 50% [R33]. Shipping titles also compact refit-only BLASes and report a 41% saving [R36].

---

## 4. Scene representation

### 4.1 Overview

```text
              GpuScene rows (instances, transforms)      Feature RT geometry (voxel chunks, custom)
                         |                                             |
        RtSceneBuilder (compute): visible + RT-distance filter, LOD, instance masks, camera-relative transforms
                         |                                             |
       BLAS: one per mesh (rigid)          BLAS: one per chunk section (static, compacted)
             one per skinned instance      BLAS: per section "translucent" + "alpha" geometries
             (refit every frame)
                         \______________________   _______________________/
                                                 TLAS (rebuilt every frame, camera-relative)
                                                   |
        RT geometry records (InstanceID+GeometryIndex -> vertex/index buffers, format, material base)
        Material table (GpuMaterialTable rows + RT extensions)    Light structures (section 5.4)
```

### 4.2 Voxel chunks

**Granularity.** One BLAS per *chunk section*: a cube of 16³ blocks by default, configurable to 32³. A section holds up to three geometries:

| Geometry | Flags | Contents |
|---|---|---|
| 0 Opaque | `Opaque` | full and partial solid block faces, hidden faces removed |
| 1 Alpha-tested | none (any-hit), OMM attached | foliage, cut-out blocks, cross-shaped plants |
| 2 Translucent | none (any-hit for tinted shadow), separate hit group | glass, ice-like blocks, water surfaces (water may instead be its own instance, see 4.8) |

**Triangle count.** Path tracing does not need vertex lighting, so the RT mesh can be **greedy-meshed independently of the raster mesh**. Coplanar opaque faces with the same material merge into larger quads. The shader rebuilds per-block UVs from the hit's world position (`frac` along the face's tangent axes) and the material's texture-array layer. That typically cuts opaque triangle counts 3–8× in natural terrain. It cuts BLAS memory and build time in proportion, and it improves traversal (fewer, larger triangles). Alpha-tested and translucent faces are not merged. Their UVs come from vertex data.

**Build policy.**

| Event | Action |
|---|---|
| Chunk section meshed for the first time | build with `PreferFastTrace | AllowCompaction`, then compact one frame later (compacted-size query → `CopyAccelerationStructure(Compact)`) |
| Block edit | **rebuild**, never refit (topology changes). The old BLAS stays in the TLAS until the new one is compacted, then it is released through the deferred-release queue |
| Section unloaded / leaves RT distance | removed from the TLAS; BLAS released |
| Many edits in one frame (explosions, fills) | a priority queue keyed by distance to the camera and screen footprint, plus a per-frame build budget (default 1.0 ms or 64 sections, whichever comes first); sections near the camera jump the queue |

**Coordinates and precision.** Every BLAS is built in *section-local* coordinates: a 0–16 range with exact integer vertex positions, which suits compressed vertex formats. The TLAS instance transform carries the section origin **relative to the camera**, rebased every frame. The TLAS is rebuilt every frame anyway, so the rebase costs nothing. Large world coordinates never reach the BVH, and ray origins stay near zero. That avoids self-intersection error at large distances. A partitioned TLAS's `TRANSLATE_PARTITION` operation [R6] does the same job cheaply when it arrives (M11).

**RT distance.** It is separate from the raster view distance. Default 8 sections horizontally around the camera, full height of loaded sections, within the 450 MB BLAS budget (section 1.6). Beyond it:

- *secondary* rays that leave the RT volume fall back to the sky/environment with distance-faded fog. The radiance cache stores far-field lighting;
- *primary* visibility of far terrain still comes from raster (section 5.1), so the horizon does not shrink.
- **Far-field LOD (M9):** a coarse BLAS per 4×4 group of far sections, built from a 2× or 4× downsampled voxel grid (majority material per 2³ cell), so mountains still occlude the sun and bounce light. That follows the cascaded-proxy idea used by sparse voxel tracers [R52][R58].

**Sizing estimate (to verify in M2):** 25×25 columns × ~6 non-empty sections ≈ 3,750 sections. Greedy-merged opaque surfaces at ~600–1,500 triangles per section, plus foliage, comes to ≈ 4–6 M triangles. At roughly 60–90 bytes per triangle compacted, that is ≈ 300–500 MB. This is the reason for the 450 MB budget and the shorter RT distance on 8 GB GPUs.

### 4.3 Entities, rigid meshes and particles

- **Rigid meshes** (GPU Scene objects with `HasMesh`): one BLAS per `GpuMeshHandle` LOD 0 (or a chosen RT LOD), built from the GeometryHeap pages (`StandardVertex`, 48 B, position at offset 0), compacted, and shared by every instance. `GpuInstanceRecord::Flags` gains `RayTraced` (default on; off for decals and helpers).
- **Skinned entities.** `Renderer/Skinning` already writes deformed vertices into the vertex page, with previous positions at `PreviousVertexOffset`. Each skinned instance gets its own BLAS:
  - built once with `AllowUpdate | AllowCompaction | PreferFastBuild`, then refit every frame from the skinned output;
  - rebuilt every 30 frames, or when the bounding box grows by more than 50% since the last build. Large deformation degrades a refit BVH [R32].
  - Entity models in a voxel game are low-poly (hundreds to a few thousand triangles), so refitting 200 of them costs ≈ 0.2–0.4 ms. They are refit only when animated and within RT distance. Far entities (beyond 32 m) refit every second frame.
- **First-person overlay** (`RenderObjectFlags::Overlay`): not in the TLAS for camera and indirect rays, because it uses its own field of view and near plane. A world-space proxy instance with mask `ShadowOnly` lets the body or held item cast shadows if the game enables it.
- **GPU particles:** **not in the TLAS** in v1. They are lit and composited as a translucency layer (section 5.10). Large or emissive particles feed the light structures as virtual point lights (section 5.4). Putting particles in the BVH (camera-facing quads rebuilt every frame) costs a lot for little visible gain at this content scale.
- **Feature geometry** ([RenderFeatures.md](RenderFeatures.md) `DrawGeometry`): a new opt-in interface lets a feature provide RT geometry (section 8.4). Features that do not opt in are drawn in primary visibility only. They receive light but do not cast or reflect.

### 4.4 RT geometry records and instance encoding

TLAS instance fields:

| Field | Bits | Use |
|---|---|---|
| `InstanceID` (custom index) | 24 | index of the instance's first row in `RtGeometryRecords`; `+ GeometryIndex()` gives the geometry row |
| `InstanceMask` | 8 | `0x01` opaque world, `0x02` alpha-tested, `0x04` translucent, `0x08` entities, `0x10` water volume, `0x20` shadow-only proxies, `0x40` far-field LOD, `0x80` reserved |
| `InstanceContributionToHitGroupIndex` | 24 | 0 opaque, 1 alpha-tested, 2 translucent |
| Flags | 8 | `TRIANGLE_FACING_CULL_DISABLE` for double-sided materials; `FORCE_OPAQUE` for opaque geometry so any-hit is never invoked |

`RtGeometryRecord` (32 bytes, std430, mirrored in Slang with an `offsetof` test like the GPU Scene's):

```text
uint VertexBuffer;     // Buffers[] bindless index
uint VertexOffset;     // bytes
uint IndexBuffer;      // Buffers[] index, or ~0 for procedural quads (2 triangles per quad, index = PrimitiveIndex()/2)
uint IndexOffset;
uint Format;           // RtVertexFormat: StandardVertex48 | QuadFace16 | feature-registered formats
uint MaterialBase;     // GpuMaterialTable row, or a material palette base for voxel geometry
uint ObjectId;         // matches the raster ObjectId for picking and debug
uint Flags;            // double-sided, emissive, has previous positions (motion), ...
```

**Surface fetch.** `Rt/SurfaceFetch.slang` turns `(record, PrimitiveIndex, barycentrics)` into a `StandardPbrSurface` (the same struct Forward+ shades with), so materials shade identically in raster and RT. `RtVertexFormat` is a closed set compiled into the integrator:

- `StandardVertex48` covers GeometryHeap meshes and skinned output.
- `QuadFace16` is a generic packed voxel face: a 16-byte record with section-local integer position, axis, extent, a material palette index, a tint index and AO-free shading flags. Positions for the BLAS are expanded at build time into a transient position buffer. Or, with `RayTracingPositionFetch`, the BLAS is the only copy of the positions [R19].
- An application registers more formats at build time. A CMake option adds Slang modules to a generated switch (`swim_add_rt_vertex_format(NAME SOURCE ...)`), because there are no runtime-linked shaders inside one RT library. This mirrors how `swim_add_runtime_shader` works for applications.

### 4.5 Materials and textures

- The RT path reads the same `GpuMaterialTable` rows (`StandardPbr`: base colour, metallic-roughness, normal, emission, alpha mode, double-sided) through the existing bindless texture indices.
- RT extensions, appended as a second record type (`GpuRtMaterialExtension`, 32 B, row = material row):
  - **transmission** (thin or solid), IOR, absorption colour and distance (Beer–Lambert);
  - an emissive-light flag and emissive power estimate (for light sampling);
  - a specular-lobe clamp (firefly control);
  - an any-hit alpha mode: none / cut-out / stochastic.
- **Voxel material palette:** one table of block materials (texture-array layer per face direction, roughness/metallic/emission scalars or maps, tint mode). Voxel `MaterialBase` points into it, so a section's geometry never stores per-face texture indices beyond the palette index.
- **Texture LOD in rays.** Use ray cones for mip selection on secondary hits: the cone spread angle comes from the G-buffer curvature at the primary hit and is accumulated per bounce. Never sample mip 0 on far secondary hits, which matters for cache hit rate and bandwidth on a 128-bit bus. Block textures are tiny (often 16² or 32² texels), so secondary hits can sample a fixed coarse mip (`TextureArraySample` already honours the per-layer `MinLod`).
- **Animated textures** (flipbooks for water, lava, fire): the current frame index is a per-material scalar updated per frame, read by both raster and RT.

### 4.6 Alpha-tested geometry

Any-hit shaders are the most expensive part of tracing vegetation-heavy scenes. Shipping data shows ≈17% of trace samples in any-hit before OMMs and a 55% drop in the main trace pass with OMMs [R36]. The plan:

1. **OMMs for every alpha-tested voxel face.** Block cut-out textures are tiny and shared by thousands of faces, and every face of one block type maps the texture the same way (or with one of a few rotations or flips). An OMM array per **(texture layer, UV orientation)** is baked once, on the GPU, when the texture array layer becomes resident. Each alpha-tested triangle references it through the OMM index buffer, so OMM memory is essentially independent of world size (a few MB in total, versus the per-asset baking and size caps shipping titles needed [R36]).
   - Subdivision level: a 16×16-texel quad split into two triangles has 128 texels per triangle, so level 4 (256 micro-triangles) resolves it almost exactly; level 5 for 32² textures.
   - **4-state** for shadow and visibility rays, with unknown micro-triangles resolved by any-hit. **2-state forced** for indirect rays, exactly the split that shipped in [R36].
   - Greedy-merged quads are never alpha-tested, so the per-texture mapping always holds.
2. **Meshes** (entities, props) with alpha-cut materials: OMMs baked on the GPU at load from the mesh's UVs and alpha mask, with a per-asset size cap (0.5 MB). Bake time scales with texel coverage [R36]. Without OMM support, any-hit only.
3. **Any-hit shader:** minimal. Fetch UV, sample the alpha mip chosen from the ray cone, compare, `IgnoreHit`. Shadow rays use `ACCEPT_FIRST_HIT_AND_END_SEARCH`. The flag `NoDuplicateAnyHit` is set only where stochastic alpha needs it.
4. **Flipbook-animated cut-outs** (rare): any-hit fallback, no OMM.

### 4.7 Emissive geometry

- **Emissive blocks** (light-emitting voxels, lava, glowing ores and similar) are the main indirect-light source of a voxel world. During section meshing, each section also produces an **emissive face list**: face centre, axis, extent, emitted radiance index and a power estimate. This feeds the light structures in section 5.4. It is never a list of triangles on the CPU per frame.
- **Emissive meshes** (entity eyes, props): emissive triangles enumerated at BLAS build time into the same light list with their power (area × average emission luminance from a low mip).
- **Emission is always hit-able.** BRDF-sampled rays that hit an emissive surface add its radiance with MIS against light sampling (section 5.3), so small or dim emitters that light sampling under-represents still contribute.

### 4.8 Water and translucent volumes

- Water surfaces are a separate geometry class: a translucent hit group, with water medium parameters stored per volume. When the game uses `OceanSurface` (an endless displaced grid), its RT representation is a **coarse displaced grid around the camera** rebuilt or refit each frame (≈ 64×64 quads, refit). Fine wave detail comes from the normal maps at hit time, not from geometry.
- Voxel water (block-shaped liquid with sloped tops) is meshed like translucent voxel faces (geometry 2, or its own instance mask `0x10`, so camera-under-water and above-water ray rules can differ).

### 4.9 TLAS policy

- **Rebuild every frame**, `PreferFastTrace`. It is simpler and traces better than refitting a TLAS [R32]. Instances: ≈ 4k sections + ≈ 1k entities and props + ≈ 0.5k misc ≈ 6k typical, 64k cap. At this size a TLAS build costs ≈ 0.1–0.3 ms.
- **Instance culling:** RT distance only, not the camera frustum (off-screen geometry must still cast shadows and reflect). Tiny distant props (projected solid angle below a threshold) use mask `0x40` and are skipped by indirect rays after the first bounce.
- **Double-buffered instance buffers**, so the CPU and GPU never race. The instance buffer is written by `RtSceneBuilder` compute from GPU Scene rows plus the feature-provided section list.
- **Partitioned TLAS (M11):** spatial partitions per region of 4×4 columns plus a global partition for moving entities, so a block edit rewrites only one partition [R6][R20]. In Microsoft's preview sample this cut a 2.88 ms full rebuild to 0.79 ms [R6]. It is gated on retail availability.

---

## 5. Light transport

### 5.1 Primary visibility: raster G-buffer

**Decision:** primary visibility is **rasterized** into a G-buffer by the existing geometry path, not traced.

| | Raster primary (chosen) | Traced primary |
|---|---|---|
| Feature geometry without RT data | drawn (`DrawGeometry` already exists) | missing |
| Far terrain beyond RT distance | drawn | missing |
| Alpha-tested foliage | depth prepass with discard + HZB, cheap | any-hit / OMM cost on 100% of pixels |
| Motion vectors, ObjectId, overlay | exist today | must be rebuilt |
| Cost at 540p | ≈ 1.2 ms | ≈ 1.0–2.0 ms + any-hit |
| Deep reflections / refraction through glass | handled by secondary rays | same |

**G-buffer (render resolution).** It reuses Forward+ targets where they exist:

| Target | Format | Source |
|---|---|---|
| Depth | D32 (reverse-Z) | existing |
| Velocity | RG16F | existing (UV delta, y down) |
| Normal + roughness | RGBA16F | existing `ForwardPlusTargets::Normal` |
| ObjectId | R32F | existing |
| Base colour + metallic | RGBA8 (sRGB-decoded on write → store linear in RGBA16F if needed by RR; see 6.2) | new |
| Emission | R11G11B10F | new |
| Geometric normal (oct) + material flags | RG16 + R8 | new: self-intersection offset, translucency mask |
| Translucent layer: depth + normal + material id of the nearest translucent surface | D32 + RG16 + R16 | new (section 5.8) |

A `ForwardPlusDebugMode`-style permutation `FORWARD_GBUFFER=1` of `ClusteredForward.slang` writes these without running the lighting loop. Features that draw opaque geometry compile a G-buffer variant through the same `context.Program(...)` mechanism. If a feature has no G-buffer variant, its opaque program runs in Forward+ mode and its lit colour is used as emission, so nothing disappears.

**Ray origins** are reconstructed from depth plus the geometric normal, with the offset method from *Ray Tracing Gems* ("A fast and robust method for avoiding self-intersection") [R53].

### 5.2 Integrator structure

```text
G-buffer (raster)
   |
[DI]  ReSTIR DI: light candidates (light BVH / presampled tiles / sun / sky) -> temporal -> spatial -> visibility -> shade
   |
[PT]  Path tracer (raygen, SER): from each primary surface, sample BSDF -> trace -> SER reorder by material/hit kind ->
      shade hit (surface fetch + NEE with a cheap ReGIR light sample + shadow ray) -> radiance-cache query or Russian roulette -> next bounce
   |
[GI]  ReSTIR GI (later ReSTIR PT): resample secondary paths temporally and spatially
   |
[TL]  Translucent layer: reflection/refraction paths through the nearest glass/water surface
   |
[VOL] Volumetrics: froxel in-scatter with traced sun visibility
   |
compose noisy diffuse/specular radiance + guide buffers -> reconstruction (section 6)
```

**Uber-raygen with SER** follows the approach published for a shipping id Tech title [R35]:

- one raygen shader per pass;
- `HitObject::TraceRay` with any-hit only;
- `MaybeReorderThread(hit, coherenceHint, bits)`, where the hint packs material class (opaque voxel, alpha voxel, mesh, translucent, sky-miss) in 3 bits plus a quantised ray direction or a Russian-roulette survival bit;
- shading inline in raygen through `SurfaceFetch` and `StandardPbr`.

**Live-state reduction** matters as much as SER. [R35] reports ≈25% from SER *and* live-state work together. So:

- keep the path state ≤ 64 bytes (packed throughput RGB as FP16, packed direction, RNG state);
- avoid large arrays across `TraceRay`;
- profile register counts in Nsight (section 11).

**Sampling.** Blue-noise-scrambled low-discrepancy sequences per pixel and frame (a spatiotemporal blue-noise texture, 64×64×64), so noise is well distributed for the denoisers. Never white noise.

**Bounce budget** (real time): diffuse and glossy up to 3 bounces, then a radiance-cache lookup (section 5.6). Specular-mirror chains up to 4. Russian roulette from bounce 2 using throughput luminance.

**Firefly control:** clamp secondary-bounce radiance by the path's accumulated roughness ("path regularisation": raise the roughness of glossy lobes after a diffuse bounce), and a luminance clamp on indirect only (≈ 10× the frame's exposure-scaled average). It is documented as bias and switched off in reference mode.

### 5.3 Direct lighting: ReSTIR DI

ReSTIR DI (Bitterli et al. 2020) [R43][R47], implemented through **RTXDI** [R25] when the SDK is approved. The fallback is an in-house implementation of the same algorithm; the math is public.

- **Initial candidates per pixel:** 8 light samples from the presampled light tiles (section 5.4), 1 sun sample, 1 environment sample and 1 BSDF sample, combined with MIS. Target function: unshadowed contribution with the full BRDF.
- **Temporal reuse:** reproject through the velocity target, M-cap 20, with the normal/depth/material similarity tests the RTXDI bridge (`RAB_*`) expects.
- **Spatial reuse:** 1 pass at 540p (2 at 720p), 5–8 neighbours, disocclusion boost (more spatial samples where temporal history is rejected).
- **Visibility:** 1 final shadow ray per pixel (inline ray query, `ACCEPT_FIRST_HIT_AND_END_SEARCH`, OMM 4-state), plus temporal visibility reuse, so a sample's visibility is re-traced only every N frames when the pixel is stable. Shadows from every light, including emissive blocks, come from this one ray. There is no shadow atlas.
- **Output:** separate diffuse and specular demodulated radiance for NRD, or combined noisy radiance for DLSS RR (section 6).

### 5.4 Many lights

Light sources in this scene class:

1. Up to ~10k dynamic punctual lights: the existing `GpuLightBuffer`, with types directional, point and spot as in [Lights.md](Lights.md).
2. Emissive voxel faces, possibly hundreds of thousands, but spatially clustered by section.
3. Emissive mesh triangles.
4. Particle virtual lights (fire, explosions; `VfxDirector` already spawns lights for the nearest).
5. The sun and the sky.

**Structure (rebuilt per frame on the GPU, ≈ 0.3 ms):**

- **Emissive clusters.** Each section's emissive faces are grouped (≤ 64 faces per cluster) at meshing time into a cluster record: bounds, normal cone, total power. Section edits update only that section's clusters.
- **Light BVH** over punctual lights + emissive clusters + mesh emitters, built with a GPU LBVH (Morton order) each frame. Nodes store bounds, an orientation cone and power. Traversal uses the importance metric of Conty Estevez and Kulla [R49], adapted to real time as in Moreau, Pharr and Clarberg [R50]. Leaves expand clusters to individual faces by power.
- **Presampled light tiles** (RTXDI's "local light PDF" approach [R25]): each frame, draw ~128 tiles × 1024 light samples by power from the light BVH, so per-pixel candidate generation is a cheap, cache-coherent tile read.
- **ReGIR world grid** (RTXDI): a camera-centred grid of cells (for example 32³ cells of 4 m), each holding a small reservoir of lights important there. It is used by:
  - secondary-bounce next-event estimation in the path tracer (cheap, good enough off the primary surface);
  - particles and the first-person overlay (section 5.10);
  - volumetric in-scatter from local lights.
- **Sun:** a directional light with a 0.53° angular diameter, sampled as a cone, so soft sun shadows emerge from it.
- **Sky:** the procedural sky cube from `EnvironmentBuilder` is already rendered (and receives cloud overlays). An **importance map** (2D equal-area octahedral, 128², with a mip PDF chain) is built when the environment refreshes (`EnvironmentRefreshSeconds`). Misses sample the cube at full resolution. Cloud shadowing of the sun on the ground uses the clouds feature's transmittance (section 8.5).

### 5.5 Indirect lighting: ReSTIR GI, then ReSTIR PT

- **v1 (M5): 1 path per pixel** from the primary surface. BSDF-sampled bounce, NEE through ReGIR at each vertex, terminated into the radiance cache. That alone produces a noisy but unbiased-ish GI signal.
- **M8a: ReSTIR GI** (Ouyang et al. 2021) [R44]. The sample is the first secondary vertex with its outgoing radiance. Temporal + spatial resampling uses a reconnection shift with a Jacobian, gives a large reduction in GI noise, and is cheap: one reservoir per pixel plus one visibility ray for spatial validation. The same family of technique is described in the GDC 2024 talk on a shipping open-world path tracer [R62].
- **M8b: ReSTIR PT** (Lin et al. 2022, GRIS) [R45], via the RTXDI ReSTIR PT path [R26] with its **hybrid shift**: random replay through glossy prefixes, reconnection at the first rough, sufficiently distant vertex. Unlike ReSTIR GI it handles low-roughness specular [R26]. Notes from the RTXDI documentation that shape the plan:
  - temporal reuse correlates samples and can confuse DLSS RR, so the duplication-map decorrelation must be on;
  - mirror bounces need Primary Surface Replacement (section 6.4);
  - firefly replacement adds a little bias [R26].
- **ReSTIR PT Enhanced** (Lin, Kettunen, Wyman 2026) [R46] reports 2–3× speedups through reciprocal neighbour selection, footprint-based reconnection and unified DI+GI reservoirs. If RTXDI ships it by M8, it replaces separate DI and GI reservoirs. Gate it on a measured win on the laptop 4070, not on the paper's workstation numbers.

### 5.6 Radiance caching

| Cache | Pros | Cons | Plan |
|---|---|---|---|
| **SHaRC** (spatially hashed radiance cache) [R27][R28] | compute only, any DXR GPU, DX12 + Vulkan, small; shipped in AAA path tracers (it appears as `SharcUpdate` in [R36]) | some bias and light leaking at coarse levels; needs an update pass | **default** from M5 |
| **NRC** (neural radiance cache) [R27][R48] | higher quality on glossy and complex light; fewer update rays | RTX + tensor cores; experimental in RTXGI 2.x; adds a training dispatch; memory | **M8 experiment** on RTX only; keep it if it wins on image quality at equal cost |
| FSR Radiance Caching (preview) [R57] | AMD equivalent | RX 9000 + DX12 only, preview | track; not planned |

**SHaRC in this scene:**

- **Update:** a stride of ~1 in 24 pixels (≈ 4%) traces a longer path (up to 4 bounces) and writes radiance into the hash grid cells it crosses. **Resolve** merges and ages entries. **Query:** path vertices from bounce 2 onward (or bounce 1 when the cone footprint is larger than the cell) read the cache and terminate.
- **Hash key:** position quantised with a level of detail chosen by distance to the camera, plus the surface normal's major axis. Including the normal is important for voxel worlds, where 1 m walls with light on one side only are common.
- **Capacity** 4M entries (64 MB), camera-centred. Entries are reset on large block edits in their cells (the edit list already exists for BLAS rebuilds).

### 5.7 Specular, reflections and mirrors

- Glossy reflections are just the specular lobe of the path tracer's first bounce, resampled by ReSTIR GI / PT and reconstructed with the specular guide inputs (specular albedo, hit distance or specular motion vectors) in section 6.
- **Mirror and near-mirror surfaces** (polished blocks, still water, glass): the specular hit is traced deterministically, without lobe sampling. Primary Surface Replacement writes the *reflected* surface's guide data (albedo, normal, depth along the virtual ray, virtual motion vector) into the reconstruction inputs. That is the technique used by the RTXPT reference path tracer [R24] and required for DLSS RR quality on mirrors.
- SSR, planar reflections and reflection probes are disabled in this tier (section 8.3). Their settings are kept.

### 5.8 Translucency: glass, water and refraction

- **Nearest translucent layer.** The raster transparent pass, in a G-buffer variant, writes the depth, normal and material of the nearest translucent surface per pixel. Opaque surfaces behind it get the normal G-buffer.
- **At translucent pixels the path tracer traces:**
  - a **reflection** ray (Fresnel-weighted, Schlick or exact dielectric);
  - a **refraction** ray through the interface (Snell; thin glass refracts with no offset and is just tinted);
  - total internal reflection handled.

  Both are traced deterministically for smooth interfaces (stochastically selected by Fresnel when roughness > 0.1), shaded with full path tracing at the hit, and composited with absorption.
- **Absorption:** Beer–Lambert by distance inside the medium. Water uses per-channel extinction coefficients (the `Underwater` feature's `per-channel extinction` values are reused). Stained glass is thin, with a tint × (1 − Fresnel).
- **Water surface:**
  - the refraction ray carries underwater fog and absorption;
  - the reflection ray picks up sky, sun glint and scene;
  - caustics are approximated by projecting the sun through the water normal map onto the bed (the existing analytic caustics, applied to sun NEE below the surface). Real caustic paths are not a v1 goal.
- **Layer limit:** at most 2 translucent interfaces per path in real time (for example glass → water, or window → window). Further translucent hits use the material's average transmittance as a shadow-ray tint: any-hit accumulates transmittance on shadow rays through glass, so coloured shadows are emergent.
- **Camera under water:** detected from the water volume mask at the camera position. The primary medium becomes water (fog and absorption apply to every path segment), and the surface seen from below uses total internal reflection, which the `Underwater` feature already models in raster.

### 5.9 Volumetrics

- **Froxel grid:** 160×90×64 at 540p internal (scaled with resolution). It holds a fog density from the height-fog model (the same parameters as `ScreenSpace` exponential height fog) plus weather-driven density (`Weather::GetFogDensity`).
- **Sun in-scatter:** per froxel, a traced sun visibility ray (inline, OMM 2-state) from a jittered position, temporally accumulated in the froxel grid. Sun shafts through foliage and windows emerge without the `SunShafts` screen-space feature.
- **Local lights in fog:** a ReGIR cell sample + shadow ray per froxel at a 1/4 rate, accumulated over frames.
- **Integration:** front-to-back along view rays; applied to primary radiance before reconstruction. Path segments beyond the first apply only analytic transmittance.
- **Later (M11):** volumetric ReSTIR (Lin, Wyman and Yuksel 2021) [R51] for heterogeneous media (smoke columns, explosions). `PyroVolumes` remains a ray-marched feature in v1 (section 8.5).

### 5.10 Particles, overlay and other unlit-by-the-integrator content

- **Particles:** drawn in a separate *translucency layer* pass after the path tracer, at render resolution, premultiplied (the existing particle render program with an `PT_LIGHTING=1` variant). Lighting per particle comes from:
  - the **ReGIR** cell at the particle position (direct, unshadowed, or with one shadow ray per emitter per frame for large particles);
  - the **radiance cache** (indirect).

  The layer is composited into the reconstruction input and also supplied to DLSS RR as `TransparencyLayer` with `ColorBeforeTransparency` [R38], or composited after NRD denoising for the NRD path.
- **First-person overlay:** rasterized with the forward program and a `PT_LIGHTING=1` light-gathering path. Direct light from the ReGIR cell at the hand's world position with traced shadow rays (inline queries from the fragment shader, legal at SM 6.5+ in pixel shaders), indirect light from the radiance cache. It is drawn into the reconstruction input with its own motion vectors, as today.
- **Weather (rain and snow):** the existing transparent feature, lit by ReGIR + cache like particles.

---

## 6. Denoising and reconstruction

### 6.1 Options

| | **DLSS Ray Reconstruction** (primary) | **NRD** (fallback, cross-vendor) | **FSR Ray Regeneration** |
|---|---|---|---|
| What it is | neural denoiser + upscaler in one, replaces TAA/SR [R38][R39] | spatio-temporal denoisers: ReBLUR (diffuse/specular), ReLAX (tuned for RTXDI), SIGMA (shadows) [R29] | AMD ML denoiser, separate from upscaling [R56] |
| Hardware | any GeForce RTX (RTX 20+); DLSS 4.5 RR (second-generation transformer, preset F) ships to all RTX generations [R41] | any GPU (compute) | RX 9000+, DX12 only [R56] |
| APIs | DX12, Vulkan (Streamline) | DX12, Vulkan (API-agnostic) | DX12 |
| Inputs | noisy combined colour + guides (6.2) | demodulated diffuse and specular radiance + hit distance, normal/roughness, view Z, motion | similar to NRD-class guides |
| Resolution | internal → output; **no dynamic resolution scaling** [R38] | internal; separate upscaler | internal; FSR upscaler after |
| Quality | best for path-traced input; still not a fully solved problem per independent review [R41] | good; more tuning; ghosting/blur trade-offs | unknown at this content; evaluate |

**Policy:** `Denoiser = Auto` picks DLSS RR when available, otherwise FSR Ray Regeneration on RX 9000 (D3D12), otherwise NRD + upscaler. Users can force NRD on RTX hardware (for A/B and bug isolation). **The engine integrator never depends on a specific denoiser.** It writes one canonical set of guide buffers (6.2), and each reconstruction backend adapts from them.

### 6.2 Guide buffers (canonical set, render resolution)

| Buffer | Format | DLSS RR tag [R38] | NRD input | Notes |
|---|---|---|---|---|
| Noisy radiance (combined) | RGBA16F, HDR, pre-exposure | `ScalingInputColor` | — | RR needs HDR input (`colorBuffersHDR = true`) [R38] |
| Noisy diffuse radiance + hit distance | RGBA16F | — | `IN_DIFF_RADIANCE_HITDIST` | demodulated by diffuse albedo; hit distance normalised per NRD helpers |
| Noisy specular radiance + hit distance | RGBA16F | — | `IN_SPEC_RADIANCE_HITDIST` | demodulated by specular albedo |
| Diffuse albedo | RGBA8 linear (or RGB10A2) | `Albedo` (linear; sRGB textures not supported) | used to remodulate | after PSR on mirrors |
| Specular albedo | RGBA8 linear | `SpecularAlbedo` (EnvBRDF approximation per guide) | used to remodulate | |
| Normal + roughness | RGBA16F | `NormalRoughness` (packed, roughness in alpha) | `IN_NORMAL_ROUGHNESS` (NRD packing helper) | the existing `ForwardPlusTargets::Normal` layout matches the packed form |
| Depth | D32 hardware or R32F linear view Z | `Depth` or `LinearDepth` | `IN_VIEWZ` | both written |
| Motion vectors | RG16F | `MotionVectors` (`mvecScale` = (1, 1) for UV-space, sign per convention) | `IN_MV` | the existing velocity target: `uv − velocity = previousUv`; scale (−1, −1) depending on SL's convention; verified by a test |
| Specular hit distance | R16F | `SpecularHitDistance` (with `worldToCameraView` / `cameraViewToWorld` in `DLSSDOptions`) | in spec radiance alpha | origin on the primary surface [R38] |
| Specular motion vectors | RG16F | `SpecularMotionVectors` (alternative to hit distance) | — | optional; prefer hit distance |
| Transparency layer + colour before transparency | RGBA16F | `TransparencyLayer`, `ColorBeforeTransparency` | composited after denoise | particles, weather, UI-in-world |
| Disocclusion / reactive hints | R8 | (RR has none; use history reset) | `IN_DISOCCLUSION_THRESHOLD_MIX` | |
| Object id | R32F | — | — | debug, picking (exists) |

Jitter: the existing Halton(2, 3) sequence (`Temporal::JitterPixels`). The phase count is raised to match the upscale ratio: ≥ 8 × (output/render)² phases, as DLSS guides recommend for the ratio. The jitter offset in pixels is passed to Streamline per frame.

### 6.3 History and disocclusion

- **Camera cut / first frame / tier switch:** `RenderFeatureView::Cut` already exists. On a cut, reset ReSTIR temporal reservoirs, SHaRC ageing (soft), the NRD `CommonSettings::accumulationMode = CLEAR_AND_RESTART`, and the DLSS `reset` flag. The same applies when the internal resolution changes, after teleport, and on large world edits under the camera (≥ 30% of pixels change depth).
- **Disocclusion:** ReSTIR temporal rejection uses normal (cos > 0.9), depth (plane distance < 1% of view Z) and material-id tests, with spatial reuse boosted where history is rejected. NRD gets the disocclusion threshold. DLSS RR handles disocclusion internally from the motion and depth guides.
- **Animated textures and emissive flicker:** a per-pixel "emission changed" flag (from material flags) shortens ReSTIR temporal M and NRD history (`IN_DIFF_CONFIDENCE`), so flickering fire and lava do not smear.
- **Block edits:** the BLAS rebuild list is turned into a world-space "changed AABB" list each frame. Pixels whose primary or reservoir sample positions fall in a changed AABB discard temporal reservoir history, which prevents light leaking through freshly placed walls.

### 6.4 Translucency, water and mirrors in reconstruction

- **Primary Surface Replacement (PSR)** for mirror reflection and refraction through smooth glass/water. The guide buffers describe the *virtual* surface seen through the mirror or interface:
  - albedo × the Fresnel/tint of the interface;
  - the virtual normal;
  - virtual depth (primary distance + secondary distance);
  - virtual motion vectors, computed by projecting the virtual hit with the previous frame's camera reflected through the interface plane.

  This follows the RTXPT reference implementation [R24]. RTXDI states that mirror bounces need PSR and that Russian roulette must not apply on mirror bounces, because it would make the guide buffers noisy [R26].
- **Rough glass, ripply water:** no PSR. Their contribution is part of the noisy colour, with the translucent surface's own guides. DLSS RR copes; NRD may need a separate denoise of the translucent layer, an optional "transparency" pass using ReBLUR's specular-only mode at 0.5× resolution.
- **Particles and alpha-blended content:** never denoised. They go through the transparency-layer inputs (6.2).

### 6.5 Temporal stability goals

- No visible boiling on static diffuse walls lit by a single emissive block at 540p internal. That boiling is the typical ReSTIR artifact; ReSTIR boiling-filter and decorrelation (duplication map) settings are on by default [R26].
- No light leaks through 1-block-thick walls after an edit settles (≤ 0.5 s).
- No ghost trails behind moving entities beyond 2 frames (FLIP-measured on the stress scenes, section 10.2).

---

## 7. Upscaling and frame generation

### 7.1 Primary: DLSS via Streamline

- **Streamline SDK** [R37][R39] is the single integration point: `sl.dlss` (Super Resolution), `sl.dlss_d` (Ray Reconstruction), `sl.dlss_g` (Frame Generation), and `sl.reflex` (latency; required by frame generation).
- Integration order per NVIDIA's DLSS 4 guidance: DLSS SR first, then RR. Both run at the start of post-processing; other denoisers are disabled while RR is active [R39].
- **Placement in the frame:** RR/SR consumes the HDR noisy input + guides at render resolution and writes an output-resolution HDR colour that takes the place of `TemporalAntiAliasing::Record`'s output as the post-process `Source`. Everything after (camera features in `BeforePostProcess`, post, `AfterPostProcess` features, UI, blur) runs at output resolution, unchanged.
- **Streamline and the RHI:** Streamline hooks the D3D12 device and swapchain (and Vulkan instance and device) through its interposer. That conflicts with the RHI owning device creation. The integration happens **inside the backends**, behind a backend-neutral `Rhi::Reconstruction` interface (`Backends/D3D12/D3D12Streamline.cpp`, later `Backends/Vulkan/VulkanStreamline.cpp`):
  - the D3D12 backend creates the DXGI factory and device through `slUpgradeInterface` / the SL interposer when DLSS is enabled;
  - resources are tagged with `slSetTagForFrame` from RHI `Texture`s (native handles stay inside the backend);
  - constants are set per frame (camera matrices, jitter, motion vector scale, reset);
  - Streamline manages resource states for tagged resources [R38], but the render graph still needs a declared pass with read and write states, so the pass declares its inputs `ShaderRead` and its output `ShaderWrite` and the backend restores states after evaluation.
- The renderer sees only `Rhi::ReconstructionCapabilities` (DLSS SR/RR/FG available, recommended render sizes per mode) and `CommandList::EvaluateReconstruction(const ReconstructionInputs&)`.

### 7.2 Fallbacks

| Vendor / case | Upscaler | Denoiser | Notes |
|---|---|---|---|
| NVIDIA RTX | DLSS RR | (included) | default |
| NVIDIA, RR forced off | DLSS SR | NRD | A/B and debugging |
| AMD RX 9000 (D3D12) | FSR 4 upscaling (FSR SDK) | FSR Ray Regeneration, or NRD | [R55][R56] |
| AMD older / Intel Arc | FSR 3.1 or XeSS 2 [R60] | NRD | |
| No vendor upscaler | engine TAA as a TAAU (add an upscale path to `TemporalResolve`) | NRD | the engine's TAA is native-resolution today; a TAAU mode is an M9 item |

### 7.3 Frame generation (optional)

- DLSS Frame Generation (2×) is available on RTX 40, so the laptop 4070 has it. Multi Frame Generation needs RTX 50. It requires Reflex and HUD-less colour, plus UI colour and alpha tagging. The engine draws the UI last in its own pass, so tagging the pre-UI colour is straightforward.
- FSR 3.1/4 frame generation as the non-NVIDIA alternative.
- Off by default; a user setting. Never counted toward performance targets (section 1.4). Interaction with the screen blur and UI layers (`Blur.AfterUiLayer`) must be checked in M9.

### 7.4 Render-resolution policy

- Fixed quality modes (DLAA, Quality 67%, Balanced 58%, Performance 50%, Ultra Performance 33%) chosen by the user or by preset. **No dynamic resolution while DLSS RR is active,** since RR does not support DRS [R38].
- An *auto* mode picks the mode from measured GPU time over the last 2 s against a target frame time, with hysteresis, changing at most every 10 s and resetting histories on change. Under NRD + FSR/XeSS, true DRS is allowed.
- Defaults on the laptop 4070 at 1080p: Performance for the Floor, Balanced for the Goal. At 1440p: Performance.

---

## 8. Integration with the existing renderer

### 8.1 Frame structure in the Path traced tier

Today's order (`FrameRenderer::Impl::Render`): environment → sky → scene (shadows → visibility → forward → probes → planar) → screen space → TAA → post → UI → presentation.

In the Path traced tier:

```text
RecordEnvironment                 (unchanged: sky cube + feature overlays; + env importance map, 5.4)
RecordRtScene            NEW      RtSceneBuilder: BLAS build/compact queue, skinned refits, OMM bakes, TLAS build
RecordVisibility                  (unchanged GPU visibility, HZB; shadow views not planned)
RecordGBuffer            NEW      Forward+ G-buffer variants + feature G-buffer draws + translucent layer + overlay
RecordPathTracing        NEW      light structures -> ReSTIR DI -> path trace (SER) -> ReSTIR GI/PT -> SHaRC -> translucent -> volumetrics
RecordNoisyComposite     NEW      emission + DI + GI + translucent + volumetric -> noisy HDR + guides
  BeforeTemporal features         (run on the noisy composite; see 8.5)
  particles / weather layer       transparency layer (5.10)
RecordReconstruction     NEW      DLSS RR | NRD (+ upscaler)   -> output-resolution HDR   (replaces RecordScreenSpace + TAA)
  BeforePostProcess features      (unchanged, output resolution)
RecordPost                        (unchanged)
  AfterPostProcess features, UI, screen blur, presentation (unchanged)
```

Skipped in the tier: `RecordShadows` (shadow planning and atlas), `RecordReflectionProbes`, `RecordPlanarReflections`, `RecordScreenSpace` (GTAO, SSR; fog moves into volumetrics), clustered light assignment (lights go to the light BVH), and the Forward+ lighting loop for opaque geometry. MSAA is forced to 1 sample (the G-buffer is single-sample; reconstruction handles edges). FXAA/SMAA are off.

### 8.2 Render graph additions

- **AS resources in the graph.** AS pools and TLAS buffers are imported per frame (`ImportBuffer`, initial state `AccelerationStructureRead`). Builds declare `Write`/`ReadWrite` with `AccelerationStructureWrite`. Trace passes declare `Read` with `AccelerationStructureRead`. Scratch buffers are transient graph buffers with `Storage | AccelerationStructureBuildInput`, pooled by the compiler like other transients.
- **New command-context helpers:** `RenderCommandContext::Commands().BuildAccelerationStructures(...)`, `.BindRayTracingPipeline(...)`, `.DispatchRays(...)`. The graph's barrier synthesis gains the AS state pair (D3D12 enhanced barriers: `SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE` / `SYNC_RAYTRACING`; Vulkan: `ACCELERATION_STRUCTURE_BUILD_BIT_KHR` / `RAY_TRACING_SHADER_BIT_KHR` stages).
- **`RenderFeatureComputePass` gains `.AccelerationStructure(name, tlas)`**, so features can trace inline queries (for example a feature's own volumetric shadowing) with the same name-bound API.
- **New pass helper:** `RenderRayTracingPass` mirrors `RenderFeatureComputePass` for raygen pipelines, with binding by Slang parameter name and automatic state declaration.
- `CompiledRenderGraph::Dump()` lists AS states, and `ReadTimings()` covers RT passes unchanged.

### 8.3 Coexistence with Forward+, TAA and effects

| Existing module | In Path traced | Mechanism |
|---|---|---|
| Forward+ opaque / transparent | G-buffer variants; the lighting loop is unused | `FORWARD_GBUFFER`, `FORWARD_TRANSLUCENT_LAYER` permutations |
| Depth prepass, HZB, two-phase occlusion | kept | unchanged |
| Clustered lights | replaced by the light BVH / ReSTIR | `GpuLightBuffer` is the input to both |
| Shadow atlas, cascade cache, static layer | off | emergent traced shadows |
| GTAO, SSR | off | emergent |
| Height fog | moved into volumetrics | same parameters |
| Probes, planar reflections | off | emergent |
| IBL / environment cube | kept as the sky and miss source + importance map | |
| TAA / FXAA / SMAA / MSAA | replaced by reconstruction | `AntiAliasing` setting ignored in tier, kept |
| Sharpening (CAS) | off with DLSS (RR ignores sharpness [R38]); kept for NRD + TAAU | |
| Post (exposure, bloom, grading, tone map) | unchanged | auto exposure meters the reconstructed HDR |
| `MinimumLight` (full-bright floor) | emulated: added as a constant ambient term into the path tracer's miss/termination | keeps gameplay readability |
| Screen blur, UI, text | unchanged | |
| Glint | an emission term in `SurfaceFetch`, same function | |

### 8.4 Render features and RT geometry

A new optional interface on `RenderFeature`:

```cpp
// Opt-in: give the path tracer geometry for secondary rays (shadows, reflections, GI).
virtual bool ProvidesRayTracingGeometry() const { return false; }
// Called once per frame in RecordRtScene, before the TLAS build. The feature declares or updates
// BLASes through the RtGeometryContext (it never touches the TLAS) and lists this frame's instances.
virtual void PrepareRayTracingGeometry(RtGeometryContext& context) {}
// The G-buffer variant of the feature's opaque program (FORWARD_GBUFFER define), or none.
virtual bool HasGBufferVariant() const { return false; }
```

- `RtGeometryContext` offers:
  - `CreateBlas(desc)` / `UpdateBlas(handle, buffers)` / `RebuildBlas(handle)` (queued under the engine's build budget, with priority);
  - `AddInstance(blas, transform, mask, recordBase)`;
  - `RegisterGeometryRecords(span)`;
  - `OpacityMicromaps()` (shared OMM cache keyed by texture layer + UV orientation, 4.6);
  - `AddEmissiveClusters(span)`.
- **The voxel world is just a feature using this interface.** The engine supplies generic building blocks (`QuadFace16` format, section-local BLAS helper, emissive cluster builder, OMM cache), so a voxel game wires its chunk mesher to them without the engine knowing what a block is.
- `Game::BoxField` in the sandbox becomes the first RT feature example. Its GPU-driven boxes provide a procedural-quad BLAS per field.

### 8.5 Existing features in the tier

| Feature | Behaviour |
|---|---|
| `VolumetricClouds` | still marches clouds into the environment cube and composites over sky pixels; also writes a top-down **cloud transmittance map** (new, 256², updated with the environment) that sun NEE samples, so clouds shadow the world |
| `SunShafts` | off by default (emergent from volumetrics); still selectable |
| `LensFlare` | unchanged (`BeforePostProcess`) |
| `Weather` | transparency layer, lit by ReGIR + cache; wetness feeds material roughness (game-side) as today |
| `OceanSurface` | G-buffer variant + coarse RT grid (4.8) |
| `Underwater` medium | its extinction parameters drive the path tracer's water medium; its screen-space medium pass is skipped; the lens pass (`BeforePostProcess`) stays |
| `Lightning` | additive ribbons in the transparency layer + its flash light as a punctual light in the light BVH |
| `PyroVolumes` | stays ray-marched (`BeforeTemporal`) on the noisy composite; its fire emission also registers short-lived punctual lights (already done by `VfxDirector` for the nearest) |
| `LavaSurface`, `BlockFire` | G-buffer variants with emission; emissive clusters registered so they light the world |
| Camera features (DOF, lens, film) | unchanged at output resolution; DOF optionally supplies the RR DOF guide if moved before RR (not planned) |
| `BeforeTemporal` stage in general | runs on the **noisy render-resolution composite**. Noisy dithered output is acceptable to DLSS RR and NRD only if it is part of the radiance signal. A feature can override `GetStage()` per tier through a new `RenderFeature::PreferredStage(LightingTier)` |

### 8.6 Live tier switching

1. `RenderSettings::Lighting` changes on the owner thread. `RenderSettings::Sanitize` validates availability (falls back with a log line).
2. The next frame builds a graph with the new tier's passes. Graph compilation is CPU-only and cheap.
3. **Path traced resources are lazy** and kept warm:
   - RT pipelines are created on a background job at device start when `PathTracedAvailability != Unavailable`;
   - BLASes for the world are built progressively from the first frame in the tier: near sections first, under the build budget. Until a section's BLAS exists, secondary rays miss it, and a short fade-in on GI (≈ 0.3 s ramp) hides pop-in;
   - after leaving the tier, AS and caches are kept for 30 s (or until memory pressure), so toggling back is instant. Then they are released through deferred release;
   - Streamline features (DLSS RR) are created once and kept, with `mode = eOff` when unused. `slFreeResources` runs only on memory pressure or device loss [R38].
4. History resets on switch (8.1 cut rules); auto exposure keeps its state.
5. A `render.tier classic|enhanced|pathtraced` console command plus `render.set pt.*` variables, consistent with the existing `render.set planar.*` style.

### 8.7 Settings API

```cpp
enum class LightingTier : std::uint8_t { Classic, Enhanced, PathTraced };

struct PathTracingSettings
{
	PathTracingQuality Quality = PathTracingQuality::Balanced; // Low | Balanced | High | Ultra | Custom
	std::uint32_t MaxDiffuseBounces = 3, MaxSpecularBounces = 4;
	RestirMode DirectLighting = RestirMode::Restir;          // Off (NEE only) | Restir
	RestirMode IndirectLighting = RestirMode::RestirGi;      // Off | RestirGi | RestirPt
	RadianceCacheMode RadianceCache = RadianceCacheMode::Sharc; // Off | Sharc | Nrc
	ReconstructionMode Reconstruction = ReconstructionMode::Auto; // Auto | DlssRayReconstruction | Nrd | FsrRayRegeneration
	UpscalerMode Upscaler = UpscalerMode::Auto;              // Auto | Dlss | Fsr | Xess | Taau | Native
	ResolutionMode Resolution = ResolutionMode::Performance; // Dlaa | Quality | Balanced | Performance | UltraPerformance | Auto
	bool FrameGeneration = false;
	float RtDistanceSections = 8.0f;     // secondary-ray geometry distance (raster draws further)
	float IndirectClamp = 10.0f;         // firefly clamp, x exposure-relative average; 0 = off
	bool Volumetrics = true, TranslucentRefraction = true, Ser = true, OpacityMicromaps = true;
	float BlasBuildBudgetMs = 1.0f;
	PathTracingDebugView Debug = PathTracingDebugView::None;
	bool ReferenceMode = false;          // accumulate, no denoise, no clamps (section 11)
};
// RenderSettings gains: LightingTier Lighting = LightingTier::Enhanced; PathTracingSettings PathTracing;
// RenderStats gains: Path traced availability, BLAS count/bytes, builds this frame, TLAS instances,
// reorder performed, OMM coverage, reconstruction backend, internal resolution, RT VRAM by category.
```

### 8.8 Shader build

- `swim_add_raytracing_shader(NAME SOURCE ... ENTRIES raygen:..., miss:..., anyhit:... [SER])`:
  - Slang → SPIR-V (one module with all RT entry points; `SPV_KHR_ray_tracing`, `SPV_KHR_ray_query`; SER via the EXT capability);
  - Slang → **DXIL library** (`-profile lib_6_9`, or `lib_6_5` for the no-SER variant), plus the register reflection.
- Today's DXIL path concatenates per-entry containers *precisely to avoid* libraries ([Rhi.md](Rhi.md), *Shaders*), so RT programs need this separate path. Inline-ray-query compute programs keep using `swim_add_runtime_shader`, with `cs_6_5+` (6.8 default is fine).
- Hot reload: RT libraries are recompiled like other programs, and the pipeline is recreated on the next frame. They are "held by an engine pass", so they follow the rule that such programs swap after restart, unless the RT pipeline is made swappable (M5 nicety).

---

## 9. Third-party SDKs and licensing

**Nothing below may be downloaded, vendored or added to the build without the user's explicit approval at the time.** Each item gets its own approval: name, source, version, size and licence. Pin by SHA-256 in `cmake/*Dependencies.cmake` like Slang and the other dependencies.

| SDK | Use | Source | Licence / redistribution notes (verify the shipped licence file before use) |
|---|---|---|---|
| D3D12 Agility SDK ≥ 1.619 (1.620 retail current) | SM 6.9, DXR 1.2 HLSL (SER, OMM) | NuGet `Microsoft.Direct3D.D3D12` [R7] | Microsoft licence; `D3D12Core.dll` is redistributable with the app; the debug layer (`d3d12SDKLayers.dll`) is for development only |
| DXC ≥ 1.9.2602 | SM 6.9 DXIL | GitHub `microsoft/DirectXShaderCompiler` | open source (LLVM/NCSA-style); `dxcompiler.dll` and `dxil.dll` redistributable |
| NVIDIA Streamline (+ DLSS SR/RR/FG, Reflex plugins) | reconstruction, FG | GitHub `NVIDIA-RTX/Streamline` [R37] | the SL core and the DLSS/Reflex binaries carry separate terms; the DLSS binaries have their own licence text next to each DLL; redistribution must follow the NVIDIA terms; check attribution requirements [R42] |
| NVIDIA DLSS SDK | headers/docs; binaries via SL | GitHub `NVIDIA/DLSS` [R40] | NVIDIA DLSS / RTX SDK licence |
| NRD | fallback denoiser | GitHub `NVIDIA-RTX/NRD` [R29] | NVIDIA RTX SDKs licence (licence file in the repo); the RTX SDK licence forbids combining with software in a way that makes it subject to an open-source licence [R42] |
| RTXDI | ReSTIR DI/GI/PT | GitHub `NVIDIA-RTX/RTXDI` [R25] | NVIDIA RTX SDKs licence [R42] |
| RTXGI 2.x (SHaRC, NRC) | radiance cache | GitHub `NVIDIA-RTX/RTXGI`, `NVIDIA-RTX/SHARC` [R27][R28] | NVIDIA RTX SDKs licence; NRC marked experimental |
| Opacity Micromap SDK | optional CPU/GPU OMM baker for meshes | GitHub `NVIDIA-RTX/OMM` [R30] | check repo licence; the engine's own GPU baker for voxel textures needs no SDK |
| AMD FSR SDK (FSR 3.1/4, Ray Regeneration) | AMD fallback | GPUOpen [R55] | FSR SDK source parts open source (MIT-style); ML components ship as signed DLLs under AMD terms; Ray Regeneration DLL optional [R56] |
| Intel XeSS SDK | Intel fallback | GitHub `intel/xess` [R60] | Intel licence; binaries redistributable |
| RTXPT | reference only (not linked) | GitHub `NVIDIA-RTX/RTXPT` [R24] | read for design; no code copied |

Policy notes:

- **Engine licence compatibility.** If Swim-Engine is or becomes open source under a copyleft licence, the NVIDIA RTX SDK terms are a problem [R42]. Keep every NVIDIA SDK behind an optional CMake switch (`SWIM_WITH_STREAMLINE`, `SWIM_WITH_NRD`, `SWIM_WITH_RTXDI`, `SWIM_WITH_RTXGI`). The integrator must build and run without any of them, using in-house ReSTIR DI/GI, in-house SHaRC-like hashing and an in-house fallback denoiser (an SVGF-class à-trous filter), at lower quality. This is also the cross-vendor guarantee.
- Third-party notices go in one `THIRD_PARTY_NOTICES` file generated from the dependency list.
- No SDK code is copied into engine sources. Integration code wraps the SDK APIs.

---

## 10. Milestones

### 10.1 Plan

Ordered, D3D12 first. Each milestone ends with its smokes passing on D3D12 (strict debug layer, as `RHI.D3D12.Smoke.*` already requires). Performance is measured on the laptop RTX 4070 (100 W+, plugged in) at 1080p output unless noted, in short soaks of 5 minutes or less.

| # | Milestone | Deliverables | Visual acceptance | Perf acceptance | Main risks |
|---|---|---|---|---|---|
| **M0** | Approvals and spikes | user approval of SDK list (section 9); DXC 1.9 + Agility 1.619/1.620 spike (load, SM 6.9 caps, SER-performed query); Slang SER/OMM codegen spike (DXIL + SPIR-V); BLAS-size measurement on a captured voxel world | n/a | n/a | Slang SER preliminary [R22]; DXC/Slang pin interplay |
| **M1** | RHI ray tracing foundation (D3D12) | AS objects, size queries, build/refit/compact/copy, AS pool suballocation, TLAS instance struct, AS descriptors + root SRV, ray query reflection, AS states in render graph barriers, `RenderFeatureComputePass::AccelerationStructure`; Vulkan stubs | smoke: ray-query compute renders a primary-hit ObjectId image of a test scene that matches the raster ObjectId target ≥ 99.9% of pixels | build 1M-triangle static BLAS ≤ 15 ms; TLAS of 10k instances ≤ 0.3 ms | barrier and state mapping; placed AS alignment; GPU-based validation noise |
| **M2** | Scene acceleration structures | `RtSceneBuilder`: GPU Scene → TLAS (compute-written instances, camera-relative), rigid mesh BLAS cache, skinned refit/rebuild, `RtGeometryContext` feature interface, `QuadFace16` + section BLAS helper + greedy RT meshing helper, edit queue with budget, RT geometry records, debug views (instance, BLAS id, triangle density, traversal heatmap) | traced AO + sun-shadow debug views look right on voxel test worlds; edits appear within 2 frames near the camera | AS maintenance ≤ 1.0 ms at 1000 edits/s burst (with deferral); world BLAS ≤ 450 MB at RT distance 8 | BLAS memory over budget; edit storms; precision |
| **M3** | Reference path tracer | accumulating multi-bounce path tracer (inline queries, no ReSTIR), full materials via `SurfaceFetch`, sky/sun/emissive, alpha any-hit, glass/water dielectrics, reference mode UI, FLIP comparison tool hookup [R54] | converges to plausible ground truth; raster vs reference comparison gallery for the PBR scenes (`Environment.md` gallery) | n/a (not real time) | material mismatches raster vs RT (one `StandardPbr` definition prevents most) |
| **M4** | RT pipelines, SER, OMM | `RayTracingPipeline`, `ShaderTable`, `DispatchRays(+Indirect)`, `swim_add_raytracing_shader` (DXIL library + SPIR-V), uber-raygen with `HitObject` + reorder hints, SER/no-SER variants, OMM arrays + GPU baker for block textures + OMM index generation in meshing, 2-state/4-state ray flags | identical images with SER on/off and OMM on/off (FLIP ≤ 0.01 mean) | trace of 1 path/px in the foliage scene: SER ≥ 15% faster than no-SER; OMM ≥ 35% faster than any-hit only | Slang codegen; register pressure; OMM bake correctness for rotated faces |
| **M5** | Real-time integrator (first playable) | raster G-buffer + translucent layer, light BVH + presampled tiles + ReGIR, ReSTIR DI, 1-path indirect with SHaRC termination, NRD (ReBLUR + SIGMA) + engine TAA (native res) as the first reconstruction, tier setting (no live switching polish yet) | path-traced look on all stress scenes; soft shadows from all lights and emissive blocks; no boiling on static walls at native 1080p | ≤ 40 ms at **native 1080p** (no upscale) in the cave/emissive scene; per-pass budgets tracked | ReSTIR tuning; NRD tuning on voxel edges; light BVH build cost with many emissive clusters |
| **M6** | DLSS SR + RR via Streamline | backend-internal Streamline integration (`Rhi::Reconstruction`), guide buffers per 6.2, PSR for mirrors/water/glass, jitter policy, resolution modes, history resets | RR output ≥ NRD output on FLIP vs reference in 6/8 stress scenes; mirrors and water stable under camera motion | **Floor met: ≤ 33.3 ms** at 1080p from 540p in every stress scene | interposer vs RHI ownership; guide-buffer conventions (motion sign, depth); RR on stylised low-texel content |
| **M7** | Translucency, volumetrics, particles, features | 2-interface glass/water paths, underwater medium, froxel volumetrics with traced sun, particle/weather transparency layer with ReGIR + cache lighting, overlay lighting, feature migrations (8.5), cloud transmittance map | glass houses, aquariums, rain at night with lights, explosions; coloured shadows through stained glass | Floor still met with all on; volumetrics ≤ 1.0 ms | translucent denoising; RR transparency guides; cost creep |
| **M8** | ReSTIR GI → ReSTIR PT, NRC trial | ReSTIR GI (M8a), ReSTIR PT with hybrid shift + decorrelation (M8b), unified DI+GI if available [R46], NRC experiment on RTX | GI noise visibly lower at equal time; glossy inter-reflections stable | Goal trending: ≤ 25 ms at 720p internal | correlation artifacts under RR; ReSTIR PT cost at 4070 laptop scale |
| **M9** | Productisation and performance | live tier switching (8.6), settings and console, memory-pressure policy, far-field LOD BLAS, auto resolution, FSR/XeSS/TAAU fallbacks, frame generation (optional), async compute (if the queue work landed), budget pass to section 1.5 | no hitches > 250 ms on first switch, none afterwards; all features usable in tier | **Goal met: ≤ 20 ms** at 1080p in the typical scene, Floor in every stress scene; VRAM added ≤ 1.3 GB | 8 GB VRAM pressure; thermals and TGP variance between laptops |
| **M10** | Vulkan parity | Vulkan AS/pipelines/SBT/SER (EXT, NV fallback)/OMM, Streamline Vulkan, NRD/RTXDI/SHaRC on Vulkan | the same images as D3D12 (FLIP ≤ 0.02 mean in reference mode) | within 10% of D3D12 GPU time | extension availability per driver; Streamline Vulkan hooks vs the engine's WSI layer |
| **M11** | Bleeding edge (as retail support lands) | partitioned TLAS (section regions), clustered BLAS for voxel sections if it beats compaction, position fetch to drop duplicate positions, volumetric ReSTIR, DLSS 4.5 RR presets, hybrid RT options for Enhanced (traced sun shadows/reflections), work-graph experiments | no regressions | TLAS maintenance ≤ 0.3 ms with edits; AS memory −30% | preview APIs change [R6]; vendor coverage |

### 10.2 Stress scenes (generic sandbox bookmarks to add)

| Scene | Stresses |
|---|---|
| Emissive cave | only emissive blocks light a large cave; ReSTIR DI + emissive clusters |
| Dense forest at noon | alpha-tested foliage, OMMs, sun shadows, sky light |
| Night town | 2,000 dynamic point/spot lights + emissive windows; light BVH |
| Glass house + pond | translucency, refraction, PSR, coloured shadows, water |
| Ocean + underwater | water medium, caustic approximation, underwater fog |
| Crowd | 200 skinned entities moving; BLAS refit cost, disocclusion |
| Explosion spam | particles, transient lights, block edits, history resets |
| Edit storm | 1,000 block edits/s near the camera; BLAS queue, light-leak prevention |
| Flyover | fast camera at max RT distance; streaming, AS builds, far field |

Each scene has a fixed camera path for repeatable timing and FLIP captures against the reference mode (M3).

### 10.3 Cross-cutting risks

| Risk | Likelihood | Mitigation |
|---|---|---|
| 8 GB VRAM too tight | high | RT distance below raster distance, greedy RT meshes, compaction, memory-pressure ladder (3.8), measured in M0/M2 |
| Laptop TGP variance (35–115 W) | high | targets defined at 100 W+; auto resolution; Low quality preset with 1 bounce + DI-only ReSTIR |
| Slang SER/OMM codegen gaps | medium | M0 spike; `Reorder.slang` shim; no-SER variant always built |
| Streamline interposer vs RHI device ownership | medium | backend-internal integration; fall back to the NGX direct path if needed |
| Denoiser quality on low-texel, high-contrast voxel content | medium | NRD + RR A/B from M6; guide-buffer quality (PSR, accurate albedo); reference comparisons |
| ReSTIR artifacts (boiling, correlation) | medium | decorrelation, boiling filter, M caps; FLIP + temporal metrics in the stress suite |
| Preview APIs (CLAS/PTLAS, SM 6.10) slip or change | medium | isolated in M11 behind dev flags |
| SDK licensing incompatible with engine licence | unknown | optional CMake switches + in-house fallbacks (section 9); question for the user |

---

## 11. Debug and profiling tooling

### 11.1 Debug views (`PathTracingDebugView`, also `render.set pt.debug <name>`)

| View | Shows |
|---|---|
| `GBuffer.*` | each G-buffer and guide target (albedo, spec albedo, normal, roughness, linear depth, motion, hit distance, translucent mask) |
| `Instances` / `Blas` / `Geometry` | false colour by instance, BLAS and geometry index (RT path, not raster) |
| `TraversalHeatmap` | traversal steps per ray (RayQuery iteration count / any-hit invocations) |
| `OmmCoverage` | rays resolved without any-hit (green) vs needing any-hit (purple); the verification approach used in [R36] |
| `AnyHitCount` | any-hit invocations per pixel |
| `SerHint` | coherence hint value per pixel |
| `LightBvh` | node bounds/levels in world space; chosen light per pixel |
| `ReservoirM` / `ReservoirWeight` / `TemporalRejected` | ReSTIR state |
| `RadianceCache` | cache cell colour/level; hit vs miss |
| `NoisyDiffuse` / `NoisySpecular` / `Denoised*` | before/after reconstruction |
| `Psr` | primary-surface-replacement guides |
| `EditAabbs` | this frame's history-reset regions |
| `RtDistance` | sections inside RT distance, built vs pending BLASes |
| `ReferenceDiff` | FLIP error map vs the accumulated reference (frozen camera) |

**Reference mode:** freezes animation (optional), accumulates N samples per pixel with no clamps, no ReSTIR temporal reuse and no denoiser, up to 8 bounces. It saves EXR captures for offline FLIP comparison [R54]. It is the ground truth for every visual acceptance criterion.

### 11.2 Profilers and capture tools

- **Per-pass GPU timings:** the existing render-graph timestamps (`ReadTimings`, `RenderStats::GpuPasses`, `TopPasses`) cover RT passes automatically. Add `RenderStats` counters: rays per frame (estimated), BLAS builds, refits and compactions, bytes built, TLAS instances, OMM-covered triangles, reorder performed.
- **PIX on Windows** [R68]: DXR capture and replay, AS viewer (BLAS/TLAS inspection), shader table viewer, timing captures. Extended PIX markers are in Agility 1.620 [R9]. Debug labels already exist on every graph pass.
- **NVIDIA Nsight Graphics** [R69]: GPU Trace (RT core utilisation, SM occupancy, L1/L2 hit rates, which matter on the 128-bit bus), Ray Tracing Inspector (AS overlap, instance bounds quality), shader profiler with register/live-state counts (the live-state work in [R35]), SER effectiveness.
- **Crash diagnostics:** DRED is already enabled in the D3D12 backend. Add **Nsight Aftermath** [R70] markers (optional, NVIDIA) for TDRs inside long raygen dispatches. DirectX Dump Files in Agility 1.620 [R9] as the vendor-neutral path.
- **Validation:** the D3D12 debug layer strict mode for RT smokes. GPU-based validation runs on AS-heavy smokes only under `SWIM_RHI_VALIDATION=all`. Vulkan validation `gpuav_validate_ray_query` is disabled today (`VulkanValidationSettings.cpp`), so revisit it in M10.
- **DLSS RR and NRD debug:** the DLSS development binaries show an on-screen overlay of inputs and preset. NRD has a validation output layer (`NRD::ValidationLayer`) showing its inputs and history. Both are wired to debug views.
- **Short soaks only.** Performance captures use ≤ 5-minute runs on a quiet machine: no parallel compiles or other GPU work during a run.

---

## 12. Open questions for the user

1. **Laptop panel and target resolution:** is the target laptop's panel 1080p or 1440p (or 1600p 16:10)? That decides the default DLSS mode and whether the Floor is 540p or 720p internal.
2. **Vendor scope of the Path traced tier:** is it acceptable that the best path (DLSS RR, SER reordering, OMMs) is NVIDIA-first, with AMD/Intel on NRD + FSR/XeSS at lower quality? Or should the tier be RTX-only at first, with cross-vendor support later?
3. **SDK approvals:** which of Agility SDK 1.620, DXC 1.9, Streamline/DLSS, NRD, RTXDI, RTXGI (SHaRC/NRC), the OMM SDK, the FSR SDK and the XeSS SDK may be downloaded, and when? The NVIDIA RTX SDK licence is click-through and must be accepted by you.
4. **Engine licence:** is Swim-Engine (or will it be) distributed under an open-source licence? That decides whether the NVIDIA SDKs can be linked by default or must stay optional switches.
5. **Agility SDK adoption:** shipping `D3D12Core.dll` changes the backend's current no-Agility stance. Is that acceptable for all builds, or only for builds with the Path traced tier enabled?
6. **Minimum hardware:** should the tier be offered on RTX 20/30 and RX 7000 (no SER reordering, no hardware OMM), as `Reduced`, or hidden below RTX 40 / RX 9000?
7. **RT distance vs raster view distance:** is a shorter path-traced distance (default 8 sections) with raster-only far terrain acceptable on 8 GB GPUs, given that far terrain then does not cast GI or reflect?
8. **Content:** do block textures have (or will they get) roughness/metal/emissive/normal maps? Path tracing looks much better with them. What are typical block texture sizes (16², 32², higher)? That affects OMM subdivision and texture LOD.
9. **Frame generation:** wanted at all? It affects input latency and needs Reflex integration.
10. **Hybrid options in Enhanced:** should Enhanced gain traced sun shadows or reflections using the same acceleration structures (M11), or stay pure raster?
11. **Particles in the BVH:** is "particles are lit but do not cast or reflect" acceptable for v1?
12. **Ownership of RT geometry:** should the engine ship a reusable "voxel section" RT helper module (generic, as planned in 8.4), or should the application own all of its RT meshing and only use the low-level `RtGeometryContext`?

---

## 13. References

All links accessed 2026-11-27. Talks marked † have no public recording or slides confirmed; only the session listing or coverage was available.

**Microsoft (D3D12 / DXR / HLSL)**

- [R1] DirectX Raytracing (DXR) Functional Spec: <https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html>
- [R2] DirectX Developer Blog, *Shader Model 6.9 Retail and More* (Agility SDK 1.619, SER and OMM vendor support), 2026-02-26: <https://devblogs.microsoft.com/directx/shader-model-6-9-retail-and-more/>
- [R3] DirectX Developer Blog, *D3D12 Opacity Micromaps*: <https://devblogs.microsoft.com/directx/d3d12-opacity-micromaps/>
- [R4] DirectX Developer Blog, *Shader Execution Reordering*: <https://devblogs.microsoft.com/directx/shader-execution-reordering/>
- [R5] HLSL proposal 0027, Shader Execution Reordering: <https://microsoft.github.io/hlsl-specs/proposals/0027-shader-execution-reordering>
- [R6] DirectX Developer Blog, *Clustered Geometry and Partitioned TLAS preview* (Agility 1.722-preview, SM 6.10 preview), 2026-10-28: <https://devblogs.microsoft.com/directx/clas-ptlas-preview/>
- [R7] DirectX 12 Agility SDK downloads: <https://devblogs.microsoft.com/directx/directx12agility/>
- [R8] Getting started with the Agility SDK: <https://devblogs.microsoft.com/directx/gettingstarted-dx12agility/>
- [R9] Visual Studio Magazine, *DirectX Agility SDK adds retail GPU diagnostics, previews new raytracing* (1.620 retail / 1.722 preview), 2026-10-28: <https://visualstudiomagazine.com/articles/2026/10/28/directx-agility-sdk-adds-retail-gpu-diagnostics-previews-new-raytracing.aspx>
- [R10] D3D12 Work Graphs spec: <https://microsoft.github.io/DirectX-Specs/d3d/WorkGraphs.html>
- [R11] DirectX Developer Blog, *Agility SDK 1.717-preview and 1.616 retail*: <https://devblogs.microsoft.com/directx/agility-sdk-1-717-preview-and-1-616-retail>

**Khronos (Vulkan)**

- [R12] Khronos, *Ray Tracing in Vulkan*: <https://www.khronos.org/blog/ray-tracing-in-vulkan>
- [R13] `VK_KHR_ray_query`: <https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_ray_query.html>
- [R14] `VK_KHR_ray_tracing_pipeline`: <https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_ray_tracing_pipeline.html>
- [R15] `VK_KHR_acceleration_structure`: <https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_acceleration_structure.html>
- [R16] `VK_EXT_ray_tracing_invocation_reorder` proposal: <https://docs.vulkan.org/features/latest/features/proposals/VK_EXT_ray_tracing_invocation_reorder.html>
- [R17] `VK_NV_ray_tracing_invocation_reorder`: <https://docs.vulkan.org/refpages/latest/refpages/source/VK_NV_ray_tracing_invocation_reorder.html>
- [R18] `VK_EXT_opacity_micromap`: <https://docs.vulkan.org/refpages/latest/refpages/source/VK_EXT_opacity_micromap.html>
- [R19] `VK_KHR_ray_tracing_position_fetch`: <https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_ray_tracing_position_fetch.html>
- [R20] `VK_NV_partitioned_acceleration_structure` proposal: <https://docs.vulkan.org/features/latest/features/proposals/VK_NV_partitioned_acceleration_structure.html>
- [R21] NVIDIA, *RTX Mega Geometry now available with new Vulkan samples*: <https://developer.nvidia.com/blog/nvidia-rtx-mega-geometry-now-available-with-new-vulkan-samples>
- [R22] Slang, *Shader Execution Reordering (SER)*: <https://shader-slang.github.io/slang/shader-execution-reordering.html>

**NVIDIA SDKs, guides and engineering posts**

- [R23] NVIDIA RTX Kit: <https://developer.nvidia.com/rtx-kit>
- [R24] RTX Path Tracing (RTXPT): <https://github.com/NVIDIA-RTX/RTXPT> and <https://developer.nvidia.com/rtx/path-tracing/>
- [R25] RTXDI: <https://github.com/NVIDIA-RTX/RTXDI>
- [R26] RTXDI, *ReSTIR PT* documentation: <https://github.com/NVIDIA-RTX/RTXDI/blob/main/Doc/RestirPT.md>
- [R27] RTXGI 2.x (NRC + SHaRC): <https://github.com/NVIDIA-RTX/RTXGI> and <https://developer.nvidia.com/rtxgi>
- [R28] SHaRC: <https://github.com/NVIDIA-RTX/SHARC>; integration guide: <https://github.com/NVIDIAGameWorks/Spatial-Hash-Radiance-Cache/blob/main/docs/Integration.md>
- [R29] NVIDIA Real-Time Denoisers (NRD): <https://github.com/NVIDIA-RTX/NRD>; sample: <https://github.com/NVIDIA-RTX/NRD-Sample>
- [R30] Opacity Micromap SDK: <https://github.com/NVIDIA-RTX/OMM>
- [R31] NVIDIA, *Reducing Acceleration Structure Memory with NVIDIA RTXMU*: <https://developer.nvidia.com/blog/reducing-acceleration-structure-memory-with-nvidia-rtxmu/>
- [R32] NVIDIA, *Best Practices for Using NVIDIA RTX Ray Tracing (Updated)*: <https://developer.nvidia.com/blog/best-practices-for-using-nvidia-rtx-ray-tracing-updated/>
- [R33] NVIDIA, *Tips: Acceleration Structure Compaction*: <https://developer.nvidia.com/blog/tips-acceleration-structure-compaction>
- [R34] NVIDIA, *Improve Shader Performance and In-Game Frame Rates with Shader Execution Reordering*: <https://developer.nvidia.com/blog/improve-shader-performance-and-in-game-frame-rates-with-shader-execution-reordering>
- [R35] NVIDIA, *Path Tracing Optimization in Indiana Jones and the Great Circle: Shader Execution Reordering and Live State Reductions*: <https://developer.nvidia.com/blog/path-tracing-optimization-in-indiana-jones-shader-execution-reordering-and-live-state-reductions/>
- [R36] NVIDIA, *Path Tracing Optimizations in Indiana Jones and the Great Circle: Opacity Micromaps and Compaction of Dynamic BLASs*: <https://developer.nvidia.com/blog/path-tracing-optimizations-in-indiana-jones-opacity-micromaps-and-compaction-of-dynamic-blass>
- [R37] NVIDIA Streamline: <https://github.com/NVIDIA-RTX/Streamline>; get started: <https://developer.nvidia.com/rtx/streamline/get-started>
- [R38] Streamline, *DLSS Ray Reconstruction programming guide*: <https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS_RR.md>
- [R39] NVIDIA, *How to Integrate NVIDIA DLSS 4 into Your Game with NVIDIA Streamline*: <https://developer.nvidia.com/blog/how-to-integrate-nvidia-dlss-4-into-your-game-with-nvidia-streamline>
- [R40] NVIDIA DLSS SDK: <https://github.com/NVIDIA/DLSS>
- [R41] Tom's Hardware, *DLSS 4.5 Ray Reconstruction update* (second-generation transformer, all RTX GPUs): <https://www.tomshardware.com/video-games/pc-gaming/dlss-4-5-ray-reconstruction-update-arrives-in-august-for-better-ray-tracing-visuals-broader-training-data-set-and-second-gen-transformer-architecture-combine-for-improved-image-quality>; PC Gamer review: <https://www.pcgamer.com/hardware/graphics-cards/dlss-4-5-ray-reconstruction-is-a-definite-improvement-but-path-traced-fidelity-is-still-not-a-100-percent-solved-issue/>
- [R42] NVIDIA RTX SDKs License (2023-01-23): <https://developer.download.nvidia.com/gameworks/NVIDIA-RTX-SDKs-License-23Jan2023.pdf>

**Research papers and courses**

- [R43] Bitterli et al., *Spatiotemporal Reservoir Resampling for Real-Time Ray Tracing with Dynamic Direct Lighting* (SIGGRAPH 2020): <https://research.nvidia.com/publication/2020-07_spatiotemporal-reservoir-resampling-real-time-ray-tracing-dynamic-direct>
- [R44] Ouyang et al., *ReSTIR GI: Path Resampling for Real-Time Path Tracing* (HPG 2021): <https://research.nvidia.com/publication/2021-06_restir-gi-path-resampling-real-time-path-tracing>
- [R45] Lin et al., *Generalized Resampled Importance Sampling: Foundations of ReSTIR* (SIGGRAPH 2022): <https://research.nvidia.com/publication/2022-07_generalized-resampled-importance-sampling-foundations-restir>; project page: <https://graphics.cs.utah.edu/research/projects/gris>
- [R46] Lin, Kettunen, Wyman, *ReSTIR PT Enhanced: Algorithmic Advances for Faster and More Robust ReSTIR Path Tracing* (2026): <https://research.nvidia.com/labs/rtr/publication/lin2026restirptenhanced/>
- [R47] Wyman et al., *A Gentle Introduction to ReSTIR* (SIGGRAPH 2023 course): <https://intro-to-restir.cwyman.org/>
- [R48] Müller et al., *Real-time Neural Radiance Caching for Path Tracing* (SIGGRAPH 2021): <https://research.nvidia.com/publication/2021-06_real-time-neural-radiance-caching-path-tracing>
- [R49] Conty Estevez, Kulla, *Importance Sampling of Many Lights with Adaptive Tree Splitting* (HPG 2018): <https://dl.acm.org/doi/10.1145/3233305>
- [R50] Moreau, Pharr, Clarberg, *Dynamic Many-Light Sampling for Real-Time Ray Tracing* (HPG 2019): <https://research.nvidia.com/publication/2019-07_dynamic-many-light-sampling-real-time-ray-tracing>
- [R51] Lin, Wyman, Yuksel, *Fast Volume Rendering with Spatiotemporal Reservoir Resampling* (SIGGRAPH Asia 2021): <https://research.nvidia.com/publication/2021-11_fast-volume-rendering-spatiotemporal-reservoir-resampling>
- [R52] Laine, Karras, *Efficient Sparse Voxel Octrees* (I3D 2010), voxel tracing reference: <https://research.nvidia.com/publication/2010-02_efficient-sparse-voxel-octrees>
- [R53] *Ray Tracing Gems* / *Ray Tracing Gems II* (free editions; self-intersection offsets, ray cones, many-light sampling chapters): <https://www.realtimerendering.com/raytracinggems/>
- [R54] NVIDIA FLIP image-difference metric: <https://github.com/NVlabs/flip>

**AMD and Intel**

- [R55] AMD FSR SDK (formerly FidelityFX SDK): <https://gpuopen.com/amd-fsr-sdk/>
- [R56] AMD, *FSR Redstone for developers* (Ray Regeneration, Radiance Caching; DX12, RX 9000+): <https://gpuopen.com/learn/amd-fsr-redstone-developers-neural-rendering/>
- [R57] AMD FSR SDK, Radiance Cache technique: <https://gpuopen.com/manuals/fsr_sdk/techniques/radiance-cache/>
- [R58] AMD FidelityFX Brixelizer / Brixelizer GI (sparse distance-field tracing, cascaded voxel bricks): <https://gpuopen.com/fidelityfx-brixelizer/>
- [R59] AMD, *GI with AMD FidelityFX Brixelizer* (GDC 2024): <https://gpuopen.com/gdc-presentations/2024/GDC2024_GI_with_AMD_FidelityFX_Brixelizer.pdf>
- [R60] Intel XeSS SDK: <https://github.com/intel/xess>

**Shipping path tracers (talks and write-ups)**

- [R61] † NVIDIA, *'Cyberpunk 2077' Brings Beautiful Path-Traced Visuals to GDC* (GDC 2023 session on the RT: Overdrive path tracer): <https://blogs.nvidia.com/blog/2023/03/22/cyberpunk-2077-path-traced-visuals-gdc/>
- [R62] *RT: Overdrive in Cyberpunk 2077 Ultimate Edition: Pushing Path Tracing One Step Further* (GDC 2024; ReSTIR GI, radiance caching, DLSS RR): <https://www.nvidia.cn/on-demand/session/gdc24-gdc1002>
- [R63] † *Alan Wake 2: A Deep Dive into Path Tracing Technology* (GDC 2024 session listing; acceleration structures, materials, light sampling, OMM, SER, denoising): <https://schedule.gdconf.com/session/alan-wake-2-a-deep-dive-into-path-tracing-technology-presented-by-nvidia/903204>
- [R64] id Software, SIGGRAPH 2025 Advances in Real-Time Rendering course slides: <https://www.advances.realtimerendering.com/s2025/content/SOUSA_SIGGRAPH_2025_Final.pdf>
- [R65] NVIDIA RTX Remix runtime (open source path-tracing runtime used for remastered classics): <https://github.com/NVIDIAGameWorks/dxvk-remix>
- [R66] † NVIDIA / Microsoft, *Crafting a Real-Time Path Tracer for Gaming* (GTC/GDC 2020, a fully path-traced voxel world; denoising overview; NVIDIA developer blog, GTC Digital 2020)

**Hardware and tools**

- [R67] Notebookcheck, *NVIDIA GeForce RTX 4070 Laptop GPU* (8 GB GDDR6, 128-bit, 35–115 W TGP): <https://www.notebookcheck.net/NVIDIA-GeForce-RTX-4070-Laptop-GPU-Benchmarks-and-Specs.675690.0.html>
- [R68] PIX on Windows: <https://devblogs.microsoft.com/pix/>
- [R69] NVIDIA Nsight Graphics: <https://developer.nvidia.com/nsight-graphics>
- [R70] NVIDIA Nsight Aftermath: <https://developer.nvidia.com/nsight-aftermath>
