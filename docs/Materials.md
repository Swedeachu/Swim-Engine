# Materials

This covers critical-path item **58**: `MaterialTemplate` and `MaterialInstance` with reflected parameters. The GPU material buffer (item 59), metallic-roughness shading (item 60), variants and render-state policy build on this layer. The transitional renderer's `MaterialData`/`LegacyRenderBinding` are unaffected.

```text
Shaders/Slang/Materials/*.slang   material parameter struct (std430)
        | reflection (Tools/ShaderCompiler/ShaderMaterialLayout: BuildMaterialTemplateDesc)
Renderer/Materials                MaterialTemplateDesc -> MaterialTemplate (shared, immutable)
                                  MaterialInstance (per material, mutable record + version)
```

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

## Tests

| Suite | What it proves |
| --- | --- |
| `Render.Materials` (2) | every template validation rule; defaults; typed instance writes at the right offsets; type/name/count rejection; version bumps only on change; independent instances |
| `ShaderCompiler.MaterialLayout` (2) | leaf scalar types/component counts from reflection JSON; float/int/uint/texture/sampler mapping; rejection of matrices, non-struct elements and missing bindings; the compiled standard material program produces the expected 15-parameter, 80-byte template |

## Not yet done

- The GPU material buffer, material-set routing to `GpuScene::MaterialSet` and bindless material resources (item 59).
- Feature/variant keys, pass participation, alpha and double-sided render-state policy, hot reload and custom game templates.
- Mapping compiled `MaterialAsset`s to instances (texture residency → bindless indices).
