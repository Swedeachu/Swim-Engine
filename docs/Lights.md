# GPU lights

This covers critical-path item **63**: `GpuLightBuffer` and the `GpuLight` schema that Clustered Forward+ builds on:

- item 64: the cluster grid;
- item 65: GPU light assignment and compaction;
- items 66–67: opaque and transparent Forward+.

It shades with the [standard material](Materials.md) and sits beside [image-based lighting](Environment.md).

```text
Renderer/Lights
  LightDesc          scene-facing description (type, position, direction, color, intensity,
                     range, cone angles, shadow index, flags)
  GpuLightRecord     64-byte std430 row + 32-byte GpuLightHeader (counts)
  LightMath          CPU definition: encode, attenuation, evaluation, bounding spheres,
                     brute-force shading over every light
  GpuLightBuffer     persistent device-local rows, dense per type, dirty-row uploads
Shaders/Slang/Lights/GpuLightRecords.slang   mirrors GpuLightRecord and LightMath
```

`Renderer/Lights` uses only RenderGraph, the RHI contract, `StandardPbr` and the GPU Scene's record-upload helpers. It never includes visibility, environment, the material table, residency or assets, and none of those layers include it. `scripts/verify-build-layout.py` enforces this.

## Light model

The light model follows glTF `KHR_lights_punctual`. Positions and directions are in world space with +Y up; colors are linear.

| Type | Intensity | Falloff |
| --- | --- | --- |
| Directional | lux | none; the direction is where light travels |
| Point | candela | inverse square with a smooth window reaching 0 at `Range` |
| Spot | candela | point falloff × saturate(cos θ · scale + offset)², with scale = 1 / max(cos inner − cos outer, 10⁻³) and offset = −cos outer · scale |

- **Range window:** saturate(1 − (d² / r²)²)² / max(d², 10⁻⁴). It is continuous, monotonic, exactly 0 at the range, and clamped at 1 cm so a light stays finite at its own position.
- **Validation:** `ValidateLight` / `EncodeLight` reject:
  - non-finite values;
  - negative color or intensity;
  - a zero direction on a directional or spot light;
  - a local light without a positive range;
  - a spot cone outside 0 ≤ inner < outer ≤ π/2.
- **`LightBoundingSphere`** bounds everything a local light can reach. For a point light that is its range sphere. For a spot it is the tighter of the range sphere and the cone's own sphere:
  - cone half-angle ≤ π/4: center at h / (2 cos a) along the axis, with that radius;
  - wider cones: center at h cos a along the axis, radius h sin a.

  Clustered assignment tests these spheres.
- **`ShadeAllLights`** sums `StandardPbr::EvaluateBrdf` × radiance over every light. It is the brute-force reference clustered lighting must reproduce.

## GPU layout

`GpuLightRecord` is 64 bytes:

| Offset | Field |
| --- | --- |
| 0 | `Position` float3, `Range` |
| 16 | `Direction` float3 (unit), `Type` uint |
| 32 | `Color` float3 (color × intensity), `InverseRangeSquared` |
| 48 | `SpotScale`, `SpotOffset`, `ShadowIndex` (`0xffffffff` = none), `Flags` |

- **Row ranges:** directional lights occupy rows [0, `DirectionalCount`). Local lights occupy rows [`FirstLocalRow`, `FirstLocalRow` + `LocalCount`), where `FirstLocalRow` equals the directional capacity. Both ranges are dense.
- **Header:** the 32-byte `GpuLightHeader` storage buffer carries the counts. Shaders loop without holes, and the clustered assignment reads only the local range; directional lights stay outside cluster lists.
- **Shadows:** the record carries a shadow index and flags only. It knows nothing about shadow internals (Phase 16).

## GpuLightBuffer

- **Handles.** `Create`/`TryCreate` return generational `GpuLightHandle`s. A full range returns empty from `TryCreate` and throws `std::length_error` from `Create`; an invalid desc throws `std::invalid_argument`.
- **Updates.** `Update` re-encodes a light. When its type changes between directional and local, the light moves between ranges; if the target range is full it throws and the light is unchanged.
- **Release.** `Release` swaps the last row of the range into the hole, and both rows re-upload. `GetRow` reports a light's current row.
  - This is safe without timeline retirement: uploads are graph copies on the same queue, ordered after every earlier frame's reads.
- **Import.** `Import(graph)` records one batched `RecordRowRunsUpload` pass for the changed rows, plus a header upload when counts changed. The first import always uploads the header.
  - Call `CommitUploads` after a successful `Execute`. `AbortUploads` marks the rows and header dirty again.
  - A second `Import` before either throws `std::logic_error`.
- **CPU mirror.** `GetRecords` / `GetHeader` expose the mirror that `ShadeAllLights` consumes.

## Tests

| Suite | What it proves |
| --- | --- |
| `Render.Lights` (5) | Encoding and validation; the inverse-square window and glTF spot falloff; evaluation per type; bounding spheres contain every lit point (25k random probes) and are tight for spots; brute-force shading sums each counted light once |
| `Render.GpuLights` (4) | Dense packing by type, dirty-row and header uploads (checked on the mock device's buffer bytes), swap-remove, generations, type moves, full ranges, abort/retry, and 40 steps of random churn keeping the GPU copy exact |
| `ShaderCompiler.LightLayout` (1) | `GpuLightRecords.slang` field offsets and sizes equal the C++ records; the probe's bindings |
| Native `GpuLightBufferMatchesBruteForceShading` | 2 directional + 2,000 point/spot lights; a compute probe shades 512 points and matches `ShadeAllLights`; churn uploads only touched rows; a steady frame uploads nothing |
