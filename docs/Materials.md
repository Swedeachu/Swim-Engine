# Materials

This covers critical-path items **58** (`MaterialTemplate`/`MaterialInstance` with reflected parameters), **59** (the GPU material table with bindless material resources) and **60** (metallic-roughness PBR). Image-based lighting (item 61) and the PBR gallery (item 62) are in [Environment](Environment.md). Variants, pass participation and render-state policy build on these layers. The transitional renderer's `MaterialData`/`LegacyRenderBinding` are unaffected.

```text
Shaders/Slang/Materials/*.slang   material parameter struct (std430)
        | reflection (Tools/ShaderCompiler/ShaderMaterialLayout: BuildMaterialTemplateDesc)
Renderer/Materials                MaterialTemplateDesc -> MaterialTemplate (shared, immutable)
                                  MaterialInstance (per material, mutable record + version)
                                  StandardMaterial (built-in template), StandardPbr (CPU model)
        |
Renderer/GpuMaterials             GpuMaterialTable: one GPU row per instance, dirty-row uploads
        |  row index = GpuInstanceRecord::MaterialSet; texture/sampler fields = bindless indices
Shaders/Slang/Materials/StandardPbr.slang   the GPU model, mirroring StandardPbr.cpp
```

`Renderer/GpuMaterials` may use Materials, RenderGraph, Resources and the GPU Scene's record-upload helper. Nothing below it includes it.

`Renderer/Materials` is a pure data layer. It includes no RHI, graph, GPU Scene, visibility, backend or EnTT header, and it compiles with the backend-neutral renderer sources. `scripts/verify-build-layout.py` enforces this.

## Parameter types

| `MaterialParameterType` | Slang field | Size | std430 alignment |
| --- | --- | ---: | ---: |
| `Float`, `Float2`, `Float3`, `Float4` | `float`..`float4` | 4–16 | 4, 8, 16, 16 |
| `Uint` | `uint` | 4 | 4 |
| `Int` | `int` | 4 | 4 |
| `TextureIndex` | `uint` whose name ends in `Texture` | 4 | 4 |
| `SamplerIndex` | `uint` whose name ends in `Sampler` | 4 | 4 |

Texture and sampler parameters hold `BindlessResourceTable` indices. They start at 0, the table's permanent fallback element, so an unassigned texture is always valid. Matrices, arrays, 16- and 64-bit types and `bool` are rejected when converting reflection.

## MaterialTemplate

- **Building a template.** `MaterialTemplate(MaterialTemplateDesc)` validates the desc and throws `std::invalid_argument` on:
  - an empty name;
  - a record size that is zero or not a multiple of 16;
  - an empty or duplicate parameter name;
  - an offset that breaks std430 alignment;
  - overlapping parameters, or parameters outside the record.
- **Reading it.** Templates are immutable once shared (`std::shared_ptr<const MaterialTemplate>`). `FindParameter`, `GetParameters` and `GetRecordSize` describe the layout.
- **Defaults.** `SetDefault` (float span, `uint32_t` or `int32_t`) writes the template's default record while it is still owned mutably. Defaults are type-checked like instance writes.
- **From a shader.** `ShaderCompiler::BuildMaterialTemplateDesc(reflection, "Materials", name)` converts the element struct of a structured buffer. Structured-element reflection now records each leaf's `ScalarType` and `ComponentCount` for this.

## MaterialInstance

- **Creating one.** A new instance copies the template's default record.
- **Writing values.** Use `SetFloat`, `SetVector` (exact component count), `SetUint`, `SetInt`, `SetTexture` and `SetSampler`. Each setter only accepts its own type; for example, a texture index cannot be written with `SetUint`. Unknown names and type mismatches throw `std::invalid_argument`.
- **Reading values.** Use `GetFloat`, `GetVector` (unused components are zero), `GetUint` (also reads texture/sampler indices) and `GetInt`.
- **Uploading.** `GetRecord()` returns the bytes to upload. `GetVersion()` advances only when a write actually changes the record, so a GPU material table (item 59) can upload dirty records only.

## Standard material record

`Shaders/Slang/Materials/StandardMaterialParameters.slang` is the metallic-roughness record (80 bytes) that item 60 will shade with:

- base color, emissive, metallic, roughness, normal scale, occlusion strength and alpha cutoff factors;
- five texture indices and one sampler index;
- flags (`AlphaMask`, `DoubleSided`).

`StandardMaterialProbe.slang` exists only so the tests can reflect it.

## GPU material table (item 59)

`GpuMaterialTable(device, { Template, Capacity, DebugName })` owns one device-local buffer of `Capacity` records in the template's layout.

- **Row 0 is the fallback.** It holds the template defaults and is never released. Shaders clamp `MaterialSet` to the table size and use row 0 for anything out of range. Unused and retired rows also hold the defaults, so no index can read garbage.
- **Registering materials.** `Create`/`TryCreate(std::shared_ptr<const MaterialInstance>)` registers an instance of exactly this template (the same shared template object) and returns a `GpuMaterialHandle`. `GetIndex(handle)` is the row, and returns 0 for invalid handles; put it in `RenderObjectDesc::MaterialSet`.
- **Uploading.** `Import(graph)` compares every live instance's version with the last uploaded one, copies the changed records into a CPU mirror and records one batched transfer pass for the dirty rows. It returns the buffer (`ShaderRead` afterwards), `MaterialCount` and `RecordSize`. Call `CommitUploads` after a successful `Execute`; `AbortUploads` marks the rows dirty again. The first import uploads every row.
- **Releasing.** `Release(handle, lastUse)` invalidates the handle at once. `Collect`/`Drain` reset retired rows to the defaults (uploaded by the next import) and reuse them oldest-first, like the bindless table.
- **Texture indices.** Texture and sampler fields are `BindlessResourceTable` indices chosen by the caller. The table does not own textures.

## Standard metallic-roughness PBR (item 60)

`StandardPbr` (CPU) and `StandardPbr.slang` (GPU) implement the glTF 2.0 metallic-roughness model:

- **BRDF:** Lambert diffuse `(1 − F) · baseColor · (1 − metallic) / π`, plus GGX `D`, height-correlated Smith `V` and Schlick `F` with `F0 = lerp(0.04, baseColor, metallic)`. The perceptual roughness is clamped to [0.045, 1] and squared.
- **Colors:** everything is linear. sRGB textures (base color, emissive) are created as `RGBA8UnormSrgb`, so the sampler decodes them; `SrgbToLinear`/`LinearToSrgb` are the exact transfer functions used by the CPU model.
- **Texture channels:**
  - The metallic-roughness texture uses G for roughness and B for metallic, multiplied by the factors.
  - The occlusion texture uses R, blended by `OcclusionStrength`, and scales only the ambient term.
  - Emission is `EmissiveFactor × emissive texel`.
- **Normal maps:** a nonzero `NormalTexture` is a tangent-space map, `n = normalize(xy · NormalScale, z)`. Without a tangent stream, the draw shader derives T = ∂p/∂u and B = ∂p/∂v from screen-space derivatives by inverting the UV Jacobian, which is exact for planar, affinely mapped surfaces and safe with the flipped viewport.
- **Alpha and double-sided:** `FlagAlphaMask` discards below `AlphaCutoff`. `FlagDoubleSided` shades back faces with the flipped normal.
- **Lighting:** one directional light per call, plus a constant ambient term (`Ambient × baseColor × occlusion`). Image-based lighting replaces the ambient term in item 61; clustered lights arrive with items 63–66.

`CreateStandardMaterialTemplate()` builds the built-in template with the glTF defaults. `ReadStandardParameters`/`ReadStandardTextures` decode an instance for the CPU model.

## Tests

| Suite | What it proves |
| --- | --- |
| `Render.Materials` (3) | every template validation rule; defaults; typed instance writes at the right offsets; type/name/count rejection; version bumps only on change; independent instances; the standard template's glTF defaults and decoding |
| `Render.StandardPbr` (4) | sRGB transfer functions; GGX normalization (numerical integral = 1); Fresnel limits; BRDF reciprocity over 200 random cases; directional albedo ≤ 1; metals without diffuse; roughness clamping; every shading rule (textures, normal maps, occlusion, emission, alpha mask, double-sided) |
| `Render.GpuMaterials` (2) | the fallback row and default rows; uploads of only changed instances (runs, bytes); no-op edits; abort/re-upload; release → retire → reset to defaults; foreign templates, exhaustion and reuse after retirement |
| `ShaderCompiler.MaterialLayout` (3) | leaf scalar types/component counts from reflection JSON; float/int/uint/texture/sampler mapping; rejection of matrices, non-struct elements and missing bindings; the compiled standard material program produces the expected 15-parameter, 80-byte template, equal to the built-in `StandardMaterialTemplateDesc`; the smoke programs' material, view and probe layouts |
| `RHI.Vulkan.Smoke.StandardMaterialsShadeFromTheGpuMaterialTable` (opt-in) | GPU BRDF = CPU BRDF for 96 random inputs; six GPU-driven quads with bindless textures (sRGB base color, metallic-roughness, derivative-frame normal map, occlusion + emission, alpha mask, out-of-range → fallback) match `StandardPbr::Shade` per pixel; editing one material, replacing another and retiring a row upload exactly 2 and then 1 rows |

## Not yet done

- Feature/variant keys, pass participation, alpha and double-sided render-state policy, hot reload and custom game templates.
- Mapping compiled `MaterialAsset`s to instances (texture residency → bindless indices).
- Blend/additive passes, cull-mode selection per material, IBL (item 61), the image gallery (item 62) and multiple lights (items 63–66).
