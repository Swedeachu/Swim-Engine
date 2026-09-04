# Swim Engine — `.sasset`, Import, Cooking, and Runtime Asset Pipeline

> **Purpose:** define exactly what `.sasset` is for, how loose authoring assets become runtime assets, what happens in development, and what the release/shipping path is supposed to look like.

## 1. What `.sasset` is

`.sasset` is Swim Engine's **compiled runtime asset format**.

Its job is to move expensive, flexible, source-format-specific work out of runtime startup and into an importer/cooker step.

The runtime should not normally have to do this:

```text
open GLB
parse JSON/glTF structure
interpret accessors/extensions
run Draco decode
run WebP decode
rebuild mesh streams
optimize geometry
resolve source image files
construct material graph
then finally create runtime assets
```

Instead, development/build tooling should do that once and write a stable binary representation:

```text
source asset
    |
    v
import / decode / optimize / compile
    |
    v
.sasset files
    |
    v
runtime parse + dependency load + residency
```

The intended shipping runtime consumes `.sasset` or the future packaged `.spack` form, not loose glTF/GLB authoring data.

---

# 2. The current end-to-end pipeline

The current model pipeline is:

```text
.gltf / .glb
    |
    v
fastgltf
    |  parses glTF/GLB structure and extension metadata
    |
    +---- KHR_draco_mesh_compression
    |         |
    |         v
    |       Draco decoder
    |         |
    |         v
    |    ordinary Swim vertex/index data
    |
    +---- EXT_texture_webp
    |         |
    |         v
    |       libwebp
    |
    +---- PNG / JPEG
    |         |
    |         v
    |       stb_image
    |
    +---- KTX2 / Basis
              |
              v
        KTX2/Basis texture compiler path

                |
                v
       Swim IntermediateModel
                |
                v
          meshoptimizer
                |
                v
       StaticModelCompiler
                |
                v
   Mesh / Texture / Sampler /
 MaterialTemplate / MaterialInstance /
              Model
                |
                v
            .sasset
                |
        ==================
            RUNTIME
        ==================
                |
                v
          SwimAssets
          AssetSystem
                |
                v
      renderer/runtime residency
```

The critical architectural point is that **fastgltf is the glTF parser, not the implementation of every codec named by glTF extensions**.

- fastgltf discovers and parses `KHR_draco_mesh_compression`; Draco performs the actual geometry decode.
- fastgltf discovers WebP-backed glTF images; libwebp performs the source decode.
- fastgltf identifies KTX2/Basis-backed images; the texture compiler/runtime-transcoder path handles the payload.
- after import, those third-party types disappear. The intermediate representation and `.sasset` schemas are Swim-owned.

---

# 3. Source-format support versus runtime dependencies

Supporting a source format does **not** mean the shipping executable should carry that source decoder.

## Compiler/import-only formats

### Draco

```text
KHR_draco_mesh_compression
    -> fastgltf extension metadata
    -> compiler-side Draco 1.5.7 decoder
    -> normal Swim SourcePrimitive
    -> meshoptimizer
    -> MeshAsset
    -> .sasset
```

Draco should not be needed when loading an already-cooked mesh at runtime.

### WebP

```text
EXT_texture_webp / WebP source image
    -> libwebp decode in SwimAssetCompiler
    -> compiler image/mips
    -> TextureAsset
    -> .sasset
```

The runtime should not need libwebp to consume that cooked texture.

### PNG/JPEG

These are likewise authoring inputs decoded by compiler-side stb image code and converted into runtime texture payloads.

## Basis/KTX2 is currently a deliberate exception

Basis Universal is still runtime-facing **only because the current cooked texture strategy may intentionally preserve universal KTX2/Basis payloads** and transcode them to a GPU-appropriate format at residency time.

That is different from source importing.

Longer term, platform-specific native texture variants can move that transcoding entirely into build/cook tooling, at which point Basis can disappear from the shipping runtime too.

---

# 4. What the development asset bootstrap does

`RunDevelopmentAssetBootstrap(assetRoot, assets)` is currently the shared development/manual-cooker pipeline.

For an asset root such as:

```text
Assets/
```

it performs the following process.

## Step 1 — Discover source models

The pipeline recursively scans the asset root for:

```text
*.gltf
*.glb
```

Anything already under an `Assets/Cooked/...` directory is ignored during source discovery.

## Step 2 — Check whether the existing cook is current

For each source model, the expected root cooked file is:

```text
Assets/Cooked/<same relative source path>.sasset
```

For example:

```text
Assets/Models/Sponza/sponza-ktx-draco.glb
```

maps to a root model file like:

```text
Assets/Cooked/Models/Sponza/sponza-ktx-draco.sasset
```

The existing cook is accepted as current only if all of the following are valid:

1. the root `.sasset` exists;
2. it parses and passes chunk-hash validation;
3. it is a `Model` asset;
4. its compiler-profile hash matches the current compiler policy/version;
5. the current source dependency graph hashes to the stored source hash;
6. every declared cooked dependency object exists;
7. every dependency `.sasset` parses and validates recursively.

If all of that is true, the source is counted as **current** and no import/optimization/compile is performed.

This is the main incremental-cook fast path.

## Step 3 — Import stale/missing source

If the cook is not current, `GltfImporter` parses the source through fastgltf and the required compiler-side codecs.

The result is a Swim-owned `IntermediateModel`, not a fastgltf object graph.

## Step 4 — Optimize geometry

The intermediate model is passed through the offline `MeshOptimizer` stage before runtime serialization.

This is where source geometry is normalized/optimized before it becomes a `MeshAsset`.

## Step 5 — Build source provenance

The compiler records the root source plus external source dependencies and computes content hashes for them.

That provenance is serialized into the root `.sasset` and drives incremental invalidation later.

Changing a referenced `.bin`, image, or other tracked source input can therefore invalidate the old cook even when the root `.gltf` text itself has not changed.

## Step 6 — Compile the runtime asset graph

A single source model can produce several independent runtime assets:

```text
ModelAsset
  |
  +-- MeshAsset(s)
  +-- MaterialInstanceAsset(s)
  |     |
  |     +-- MaterialTemplateAsset
  |     +-- TextureAsset(s)
  |     `-- SamplerAsset(s)
  |
  `-- hierarchy / node transforms / material slots
```

This separation is intentional. A material does not own a mesh, and textures/materials/meshes have independent identities.

## Step 7 — Write `.sasset` files

The root model gets a human-path-addressable cooked file:

```text
Assets/Cooked/<source-relative-path>.sasset
```

Non-root dependency objects are stored by stable `AssetId`:

```text
Assets/Cooked/.objects/<16-hex-digit-asset-id>.sasset
```

The publisher uses a replace pattern involving temporary `.new` and `.old` paths so an existing valid cook is not casually destroyed by a partial replacement.

## Step 8 — Load the cooked graph

The development pipeline then loads the `.sasset` graph recursively:

```text
load dependency objects first
        |
        v
publish typed assets into AssetSystem
        |
        v
load/publish root ModelAsset
```

`AssetSystem` becomes the authoritative CPU runtime asset owner. The renderer should consume those typed assets/handles through residency adapters rather than re-opening the source GLB.

---

# 5. `.sasset` v1 binary structure

The current format is deliberately small, versioned, hashed, and chunk-addressable.

## Header

Current constants:

```text
schema version:        1
payload version:       1
header size:           160 bytes
chunk table entry:      72 bytes
```

The file begins with an 8-byte Swim `.sasset` magic value and records metadata including:

- schema version;
- asset type;
- stable `AssetId`;
- content hash of the asset payload;
- compiler-profile hash;
- source-graph hash;
- dependency table location/count;
- chunk table location/count;
- total file size.

## Asset types currently represented

```text
Mesh
Texture
Sampler
MaterialTemplate
MaterialInstance
Model
```

The enum reserves room for the format to grow without making the runtime infer asset type from filenames.

## Dependency table

A `.sasset` can declare other `AssetId`s it depends on.

The loader uses those IDs to load child object files before publishing the parent. This is why the model root does not need to embed every texture/material/mesh blob into one monolithic file.

## Chunk table

Current chunk kinds are:

```text
LogicalPath
SourceProvenance
AssetPayload
```

Each chunk descriptor records:

- type;
- compression mode;
- file offset;
- stored size;
- uncompressed size;
- alignment;
- SHA-256 content hash.

Current alignment policy is approximately:

```text
LogicalPath       1-byte alignment
SourceProvenance  8-byte alignment
AssetPayload     16-byte alignment
```

The parser can validate chunk ranges and hashes before trusting the payload.

## Compression

The format enum already defines:

```text
None
Zstandard
```

but the current general `.sasset` writer writes its top-level chunks as **uncompressed** (`None`).

That is intentional to keep v1 simple while the runtime/package layout matures. Texture payloads can still contain their own KTX2/Basis/supercompression representation and metadata.

Future `.spack` packaging and/or chunk compression can add storage compression without changing the meaning of the asset schemas.

---

# 6. What is actually inside the runtime payloads

## MeshAsset

The current compiled mesh schema stores runtime-oriented data such as:

- index format;
- bounds;
- vertex-stream descriptors;
- vertex attribute layout;
- primitive ranges/material slots;
- LOD descriptors;
- meshlet metadata/storage fields;
- packed vertex bytes;
- packed index bytes;
- meshlet byte payloads when present.

The static-model compiler currently produces an interleaved static vertex stream with position, normal, tangent, and UV0 data and `uint32` indices.

The key property is that the runtime does not need to reinterpret glTF accessors to reconstruct this layout.

## TextureAsset

The texture schema stores:

- dimension;
- color space;
- semantic (color/data/normal etc.);
- dimensions/layers;
- one or more runtime payload descriptions;
- mip descriptors with offsets/sizes;
- container/native format metadata;
- payload bytes.

This is where KTX2/Basis or native mip data becomes a runtime texture payload rather than a source image file.

## MaterialTemplate / MaterialInstance

Material identity is separate from mesh identity.

Material instances reference texture/sampler handles and their template rather than owning geometry.

## ModelAsset

The model is the assembly object:

- root nodes;
- node hierarchy;
- local transforms;
- mesh handles;
- material-slot handles.

It references independently compiled mesh/material assets instead of becoming one giant source-import object graph.

---

# 7. Asset identity and filenames

The runtime identity is `AssetId`, not a raw source path or pointer.

Logical authoring paths are normalized through `AssetDatabase` and mapped to IDs. Typed `AssetHandle<T>` adds generation tracking so forgotten/replaced identities cannot silently resolve through stale handles.

The `.objects` store uses the numeric `AssetId` in the filename because dependency edges are expressed in IDs:

```text
Assets/Cooked/.objects/0123456789abcdef.sasset
```

The root source-relative `.sasset` exists mainly as the authoring/dev entry point for finding the compiled model graph.

Longer-term `.spack` can replace many individual filesystem opens with an indexed package while preserving these same asset identities.

---

# 8. Compiler profile and stale-cook invalidation

The static-model compiler has a compiler-profile hash that represents the rules used to produce the cooked data.

The current profile includes important policy/version inputs such as:

- `.sasset` version;
- fastgltf version;
- Draco version;
- meshoptimizer version;
- mesh layout policy;
- texture cook policy.

If those rules change, the profile hash changes and an existing root cook is considered stale even if its source files are unchanged.

This avoids accidentally loading an old `.sasset` whose binary schema/policy no longer matches what the runtime/compiler expects.

---

# 9. Development workflow

The development executable currently supports automatic source cooking.

CMake option:

```text
SWIM_ENABLE_DEV_ASSET_AUTOCOOK=ON
```

When it is enabled and `SwimAssetCompiler` is available:

1. `SwimEngine` links the asset compiler;
2. startup scans loose source models;
3. current cooked roots are reused immediately;
4. stale/missing roots are imported/cooked;
5. `.sasset` graphs are loaded into `AssetSystem`;
6. legacy renderer residency adapters consume the cooked CPU assets.

This is developer convenience. It lets an artist/programmer drop a GLB into `Assets` and run the engine without separately invoking a build tool every time.

The important rule is that **even development auto-cook should transition back onto the compiled `.sasset` path before rendering the model**. The source importer is not supposed to become the normal renderer loader again.

---

# 10. Standalone/CI cook workflow

The standalone tool runs the same bootstrap implementation:

```text
SwimAssetCooker.exe Assets
```

This is useful for:

- pre-cooking assets after source changes;
- CI validation;
- producing cooked output before packaging;
- testing asset compilation without starting graphics/physics/gameplay;
- eventually building release asset packages.

Because the standalone cooker and development startup share the same implementation, they should make the same decisions about current/stale assets and produce the same `.sasset` graph.

---

# 11. Intended release/shipping workflow

The final release model should be:

```text
BUILD / CONTENT PIPELINE

source .gltf/.glb/images
       |
       v
SwimAssetCooker
       |
       v
Cooked .sasset graph
       |
       v
future .spack packaging / platform variants
       |
       v
ship compiled data

-------------------------------------------

SHIPPING RUNTIME

compiled .sasset/.spack
       |
       v
SwimAssets / async IO
       |
       v
CPU asset residency
       |
       v
GPU/audio/physics residency
```

For that mode:

```text
SWIM_ENABLE_DEV_ASSET_AUTOCOOK=OFF
```

should be used so `SwimEngine` does not link source import merely for startup convenience.

A shipping-oriented configuration may also use:

```text
SWIM_BUILD_ASSET_COMPILER=OFF
```

for the runtime build after the assets have already been produced by a tooling/build stage.

The release runtime should not need:

- fastgltf;
- Draco source decode;
- libwebp source decode;
- loose PNG/JPEG model-texture decode;
- source glTF JSON/accessor interpretation;
- meshoptimizer source processing.

Basis transcoding may remain temporarily if the shipped texture payloads intentionally remain universal KTX2/Basis data.

### Current limitation

The repository is **not yet a final shipping packager**. The current legacy post-build path still copies the `Assets` tree beside the executable, development auto-cook defaults to on, and `.spack`/memory-mapped package streaming is later work.

So the architecture and switches for a cooked-only runtime exist, but a final release preset/content-packaging step still needs to make the shipping policy automatic and foolproof.

---

# 12. Why `.sasset` is faster than loading source GLB

The important speedup is not merely "binary instead of JSON".

Cooking removes entire classes of runtime work:

- no glTF document parsing;
- no source extension negotiation;
- no Draco geometry decode;
- no WebP/JPEG/PNG source decode;
- no source accessor/component conversion;
- no offline mesh optimization;
- no authoring-path dependency discovery;
- no model-to-runtime-schema translation;
- no repeated material/mesh identity reconstruction.

The runtime gets data already expressed in its own schemas.

That is the correct meaning of "optimized binary file to insta read."

However, the current v1 loader is **not yet the theoretical zero-copy final form**. It still:

- opens individual `.sasset` files;
- validates/parses headers/tables;
- recursively opens dependency object files;
- deserializes payload fields into runtime vectors/objects;
- may copy payload bytes into CPU asset structures;
- uses compatibility residency adapters for the current renderer.

Future work can make the path even cheaper with:

- `.spack` indexed packages;
- memory mapping;
- async/range IO;
- directly usable packed arrays/views;
- fewer runtime allocations/copies;
- platform-native texture variants;
- GPU-upload-friendly blob placement;
- streaming residency/budgets.

The current `.sasset` format is designed so those optimizations can be added **without going back to source-format parsing at runtime**.

---

# 13. Current renderer relationship

The new asset system is authoritative for identity and CPU data, but the old Vulkan/OpenGL renderer still has transitional residency compatibility layers such as `MeshPool`, `TexturePool`, `MaterialPool`, and `LegacyRenderBinding`.

The intended current path is:

```text
.sasset
   |
   v
AssetSystem typed CPU assets
   |
   v
legacy renderer residency adapter
   |
   v
current Vulkan/OpenGL resources
```

It is **not** intended to be:

```text
MaterialPool -> parse GLB directly -> decode source codecs -> render
```

The future RHI/GeometryHeap/texture-residency work will replace the compatibility pools while preserving the same cooked asset identities and schemas.

---

# 14. Current validation gate

The source/cook design is implemented, including compiler-side Draco support, but the newest Draco integration still has one real-platform gate to close.

A dependency-enabled Windows build exposed that Draco 1.5.7's CMake target did not publish the include roots required by embedded consumers. The current repository fixes that behind:

```text
Swim::AssetCompilerDraco
```

which owns:

- the pinned Draco target;
- the `<draco/...>` source include root;
- the generated `draco/draco_features.h` include root;
- the pinned dependency's local CMake compatibility policy.

Before moving the architecture critical path forward, the next Windows build should confirm:

1. `SwimAssetCompiler` compiles through the adapter;
2. the Draco importer tests compile/run;
3. `sponza-ktx-draco.glb` cooks rather than being skipped/rejected;
4. the WebP couch cooks successfully;
5. normal Sponza/barrel GLB paths still cook;
6. a second unchanged startup reports those sources as **current** rather than recooking them;
7. the resulting root/dependency `.sasset` files load into `AssetSystem` and render through the compatibility residency path.

Once that passes, the asset-source-codec checkpoint is closed and the architecture guide returns to the next Scene task (`SceneCommandBuffer`).

---

# 15. Rules that should not regress

- Do not reintroduce tinygltf/source parsing into `MaterialPool` or renderer runtime code.
- Do not solve a Draco/WebP/KTX issue by linking every codec directly to `SwimEngine`.
- Do not treat a source filename as the durable runtime identity when an `AssetId` exists.
- Do not let material ownership collapse back into mesh ownership.
- Do not make `.sasset` depend on fastgltf/Draco/libwebp types.
- Do not silently accept a cooked graph if its compiler profile/source graph/dependency objects are stale or corrupt.
- Do not make development auto-cook the only way to create runtime assets; the standalone cooker must remain authoritative too.
- Do not require source `.gltf/.glb` files in the final shipping runtime.
- Do not confuse runtime Basis transcoding with loose-source importing.
- Keep `.sasset` as a runtime-oriented, versioned, hash-validated binary contract so future `.spack`/streaming work builds on it instead of replacing it with another source importer.
