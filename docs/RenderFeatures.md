# Render features

A render feature is a self-contained piece of the frame that gameplay code creates, configures and hands to the renderer. The renderer records it every frame at its stage. Volumetric clouds, sun shafts and the lens flare are built this way. Adding one does not touch `FrameRenderer`, the render graph or the RHI:

```text
Engine/Systems/Renderer/Runtime
  RenderFeature.h/.cpp     RenderFeature (the interface), RenderFeatureContext (what Record sees),
                           RenderFeatureComputePass (compute passes bound by Slang parameter name),
                           RenderFeatureView (camera, sun, time), RenderFeatureDrawContext and
                           RenderFeatureDraw (draws added to the geometry passes)
Engine/Systems/Renderer/Features
  VolumetricClouds, SunShafts, LensFlare   the built-in features (Features.cpp)
  Ocean, Underwater, Lightning, WaterQuality   water, weather light and their quality tiers
Shaders/Slang/Features
  FeatureCommon.slang      luminance, view rays, hashes, gradient noise
  VolumetricClouds.slang   VolumetricCloudsMarch + VolumetricCloudsComposite
  SunShafts.slang          SunShaftsMask + SunShaftsComposite
  LensFlare.slang          LensFlare
```

## Using a feature from gameplay

```cpp
#include "Engine/Systems/Renderer/Features/VolumetricClouds.h"
#include "Engine/Systems/Renderer/Runtime/FrameRenderer.h"

auto clouds = std::make_shared<Engine::VolumetricClouds>();
clouds->Settings.Coverage = 0.45f;
clouds->Settings.Wind = { 10.0f, 0.0f, 4.0f };
scene.GetRenderServices()->Renderer->AddFeature(clouds);

// Later, any frame (owner thread):
clouds->Settings.Coverage = 0.8f; // Storm coming.
clouds->Enabled = false;          // Or switch it off.
```

`FrameRenderer::AddFeature`, `RemoveFeature`, `GetFeatures` and `FindFeature<T>()` manage the list. Settings are plain structs on the feature object and are read when the frame is recorded. The sandbox keeps its three features in `Sandbox::AddAtmosphereFeatures` and exposes them on the Rendering tab.

## Stages

| Stage | Scene color | Typical use |
| --- | --- | --- |
| `BeforeTemporal` | Linear HDR (RGBA16Float) after Forward+ and the screen-space effects (particles are drawn over the result afterwards) | Noisy, dithered effects that TAA resolves: volumetric clouds, ray-marched fog |
| `BeforePostProcess` | Linear HDR after TAA | Light that should be exposed and bloom with the scene: sun shafts, lens flares |
| `AfterPostProcess` | Display-referred RGBA8Unorm after tone mapping, before the UI | Overlays, vignettes, fades |

Within a stage features run in ascending `GetOrder()`; ties keep the order they were added. Every stage also gets the frame's reverse-Z depth (`D32Float`, 0 = sky). A feature that throws while recording (a missing program, a wrong binding) is switched off with a log line instead of failing the frame.

## Writing a feature

**1. The shader.** A compute program in `Source/Shaders/Slang/Features/`, entry point `computeMain`, one descriptor space, optional push constants (at most 128 bytes; larger parameter blocks go into a `StructuredBuffer` uploaded with `Graph().CreateUpload`):

```hlsl
#include "Features/FeatureCommon.slang"

struct Constants { float4 Tint; uint4 Size; };
[[vk::push_constant]] ConstantBuffer<Constants> Params;
[[vk::binding(0, 0)]] Texture2D<float4> Color;
[[vk::binding(1, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> Output;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	if (any(id.xy >= Params.Size.xy)) return;
	Output[id.xy] = Color.Load(int3(id.xy, 0)) * Params.Tint;
}
```

**2. One CMake line** next to the other features in `CMakeLists.txt`. It compiles the program and stages it into the runtime set as `<Name>.spv` + `<Name>.reflection.json`:

```cmake
swim_add_runtime_shader(Tint SOURCE Source/Shaders/Slang/Features/Tint.slang)
# Variants of one source: DEFINES MY_VARIANT=1
```

**3. The feature class:**

```cpp
class Tint final : public Engine::RenderFeature
{
  public:
	std::array<float, 4> Color{ 1.0f, 0.9f, 0.8f, 1.0f };

	std::string_view GetName() const override { return "Tint"; }
	Engine::RenderFeatureStage GetStage() const override { return Engine::RenderFeatureStage::BeforePostProcess; }

	void Record(Engine::RenderFeatureContext& context) override
	{
		const auto& view = context.View();
		struct { std::array<float, 4> Tint; std::uint32_t Size[4]; } params{ Color, { view.Width, view.Height, 0, 0 } };
		const auto output = context.CreateColorTarget("Tinted");
		context.Compute("Tint")
			.Texture("Color", context.Color())
			.Storage("Output", output)
			.Constants(params)
			.Dispatch(view.Width, view.Height);
		context.SetColor(output); // Later features and the rest of the frame see it.
	}
};
```

`RenderFeatureContext::Compute` loads the program on first use (cached for the renderer's lifetime) and binds resources by their Slang parameter names:

- `.Texture(name, texture)` for `Texture2D` (depth textures bind their depth aspect);
- `.Storage(name, texture)` for `RWTexture2D`; the texture format must match the shader's `image_format`;
- `.Buffer(name, buffer)` / `.StorageBuffer(name, buffer)` for structured buffers;
- `.Sampler(name, "LinearClamp" | "LinearRepeat" | "PointClamp")`;
- `.Constants(value)` for push constants;
- `.Dispatch(width, height[, depth])` covers the grid with the program's thread-group size.

The render-graph states (sampled read, storage write) are declared automatically, and every reflected parameter must be bound: a missing, unknown or mismatched binding throws `std::invalid_argument` naming the parameter. `CreateTexture` and `CreateColorTarget` create transient graph textures (Sampled | Storage, plus ColorAttachment for color formats). `View()` gives the unjittered camera (matrices, basis, field of view, viewport size), the sun (the procedural sky's direction and radiance) and time; `View().ProjectDirection(d)` puts a world direction (such as the sun) on screen.

**Environment contribution.** A feature that draws into the sky can also draw into the image-based lighting cube. Override `ContributesToEnvironment()` and `RecordEnvironment(context, faceSize)`, and return a `faceSize` × 6·`faceSize` RGBA16Float atlas (`EnvironmentBuilder::OverlayDesc`): face *f* in rows *f*·faceSize.., premultiplied radiance in the cube's units (lighting multiplies it by `EnvironmentIntensity`) and alpha = transmittance. `EnvironmentCubeTexelDirection` (Environment/EnvironmentCommon.slang) gives each texel's direction; rotate it with `EnvironmentRotateLookup(d, -rotation)`. `Color()` and `Depth()` are not valid in that context. An exception drops the feature from the environment (with one log line); the feature keeps drawing.

## Drawing geometry into the engine passes

A feature can also add draws to the engine's own geometry passes: the depth prepass, the Forward+ opaque and transparent passes, and every rendered shadow-atlas tile. The geometry is lit by the clustered lights, receives and casts shadows, writes motion vectors and the screen-space targets, and is resolved by TAA like the scene. The intended shape is GPU-driven: a compute pass culls the feature's data into indirect arguments, and the vertex stage pulls its vertices from a storage buffer (no vertex or index buffers). `Game::BoxField` in the sandbox is a complete example (`Source/Game/Features/BoxField.*`, `Source/Game/Shaders/BoxField*.slang`, bookmark "Box field").

**1. Say which passes.** `GetGeometryPasses()` returns a mask of `RenderGeometryPassBit(RenderGeometryPass::DepthPrepass | Opaque | Transparent | Shadow)` (`RenderGeometryAllPasses` for all four). `GetStage()` and `Record()` have defaults, so a geometry-only feature overrides neither.

**2. Prepare once per frame.** `PrepareGeometry(context)` runs after the shadow plan and before the shadow, visibility and Forward+ passes. `context.View()` is the camera (`ViewProjection` and `PreviousViewProjection` unjittered, `Jitter` the TAA offset in NDC, `Cut` true on a camera cut or the first frame, when a feature with its own history resets it); `context.ShadowViews()` lists this frame's shadow tiles (`Index` = the row the Shadow pass names, `ViewProjection`, and `Rendered` = false for a cascade the cache kept). Record compute passes here (`context.Compute(...)`) that write the draw arguments; `.WriteBuffer(name, buffer)` declares a buffer the pass writes without reading, so a transient buffer can start there (counters reset, arguments generated). `context.CreateBuffer(size, name)` makes a transient Storage | Indirect buffer. Persistent buffers and textures are created with `context.Device()` and imported every frame with `context.Graph().ImportBuffer/ImportTexture`. Return every graph resource the draws read: `{ Buffers, IndirectBuffers, Textures }`. They are declared as reads in each pass of the mask.

**3. Draw inside the passes.** `DrawGeometry(context)` runs while each pass executes, after the engine's own draws: rendering has begun and the viewport is set (the tile for shadows). `context.Pass()` says which pass. `context.Program(name, state)` gives a draw of a program compiled for that pass, with the pass's pipeline state (targets, depth test and writes, blending) and the feature's `RenderGeometryState` (cull mode; the winding is counter-clockwise). Resources are bound by Slang name, like compute passes:

```cpp
context.Program("Terrain")
	.Buffer("Faces", faces)              // StructuredBuffer, returned from PrepareGeometry
	.Texture("Albedo", albedoArrayView)  // a persistent Rhi::TextureView (texture arrays), or a graph texture
	.Sampler("AlbedoSampler", "LinearRepeat")
	.Constants(constants)                // [[vk::push_constant]]
	.DrawIndirectCount(arguments, 0, count, 0, maxDraws);  // or DrawIndirect(...), Draw(...)
```

Arguments are `Swim::Rhi::DrawIndirectCommand` records (16 bytes). `DepthPrepassed()` is true in the opaque pass when the depth prepass ran and the feature drew into it: the pipeline then tests equal-or-nearer without writing depth, so the opaque program's vertex stage must produce exactly the prepass's depths (the same code, `precise`). In the Shadow pass, `ShadowView()` is the views-buffer row of the tile (pass it to the shader in your push constants) and `ShadowViewProjection()` its matrix. An exception in `PrepareGeometry` or `DrawGeometry` disables the feature with one log line.

**4. The shaders.** Descriptor space 0 is the pass's frame and space 1 the bindless textures; the renderer binds both. The feature's own parameters go in space 2 (`[[vk::binding(n, 2)]]`), and every one of them must be bound. Compile one program per pass family from one source with defines:

- forward passes include `ForwardPlus/ForwardFrame.slang`. The vertex stage projects with `ForwardProjectWorld(world, previousWorld)` (jittered position plus the clips for motion vectors). The opaque fragment builds a `StandardPbrSurface`, lights it with `ForwardLightSurface(surface, worldPosition, svPosition.xy)` (or `ForwardLightSurface(view, grid, surface, ...)` when it already holds copies of `Views[0]` and `Grid[0]`: fewer buffer loads) and returns `ForwardOpaqueWrite(lit, surface, objectId, currentClip, previousClip)` (`ForwardNoObject` for geometry that is no scene object); the transparent one returns `ForwardTransparentWrite(lit, alpha)`. The depth-prepass program uses the same vertex stage and a fragment stage that writes nothing (or discards cut-out texels);
- the shadow program includes `Shadows/ShadowFrame.slang` and projects with `ShadowProjectWorld(viewIndex, world)`; its fragment stage writes nothing (or discards).

The names those files declare (`Instances`, `Views`, `Materials`, `Samplers`, `Textures` and the rest) are reserved in the feature's program.

**5. Build the programs** with `swim_add_runtime_shader`, from the engine's `CMakeLists.txt` or from an application's own `CMakeLists.txt` after `add_subdirectory(Swim-Engine)` (sources relative to the calling directory; the engine's Slang directory is on the include path). Programs declared after the engine's catalog exists are staged into the same runtime set and found by name at run time:

```cmake
swim_add_runtime_shader(TerrainCull SOURCE Shaders/TerrainCull.slang)
swim_add_runtime_shader(Terrain SOURCE Shaders/Terrain.slang)
swim_add_runtime_shader(TerrainDepth SOURCE Shaders/Terrain.slang DEFINES TERRAIN_DEPTH_ONLY=1)
swim_add_runtime_shader(TerrainShadow SOURCE Shaders/Terrain.slang DEFINES TERRAIN_SHADOW=1)
```

**Occlusion culling (two phases).** A feature that returns true from `UsesOcclusion()` gets `CullGeometry(context, occlusion)` once per frame between the depth prepass and the opaque pass. `occlusion.Pyramid` is the hierarchical depth (HZB) of this frame's prepass (the engine's opaque draws and every feature's prepass draws): an R32Float texture whose mip i keeps the farthest depth of each 2^(i+1)-texel square, with `Width`/`Height` (the depth size), `MipCount` (0 when no prepass ran: nothing is occluded) and the unjittered `ViewProjection` it was drawn from. Compute passes recorded there run before the opaque pass. The pattern:

1. `PrepareGeometry` culls into the prepass arguments only what was visible last frame (everything when `context.View().Cut`), and resets the counts of a second, late argument set (both returned in `IndirectBuffers`).
2. `CullGeometry` tests everything in the frustum against the HZB (`HzbOccluded` in `Features/HzbOcclusion.slang`; bind the pyramid with `.Texture("Hzb", occlusion.Pyramid)`), appends what is visible and was not drawn early to the late set, and writes each object's visibility for the next frame into a persistent buffer.
3. `DrawGeometry` draws the early set in the depth prepass and the opaque pass as usual, and the late set in the opaque pass with `RenderGeometryState::SkipsPrepass = true` (depth test and writes, as if there were no prepass).

Newly visible objects are drawn in the same frame (a phase later), so nothing pops in. `Game::BoxField` culls its camera view this way (`BoxFieldLateCull` in `BoxFieldCompute.slang`). The engine's own scene objects are still frustum-culled only.

**Large persistent buffers.** `Swim::Render::StreamingBuffer` (`Resources/StreamingBuffer.h`) is one device-local buffer a feature suballocates and streams into, for data that is much larger than one frame's changes (a world's packed geometry, per-object records):

```cpp
Swim::Render::StreamingBufferDesc desc;
desc.Capacity = 512ull << 20;          // Fixed; Allocate fails when no range fits.
desc.Alignment = 16;                   // Every range's offset and size.
desc.Usage = Swim::Rhi::BufferUsage::None; // Extra usage on top of Storage | TransferSource | TransferDestination.
faces = std::make_unique<Swim::Render::StreamingBuffer>(*context.Device(), desc);
faces->SetDefragmentBudget(8ull << 20);                    // Bytes moved per frame (0: never).
faces->SetRelocationCallback([&](Swim::Render::StreamingRange range, std::uint64_t from, std::uint64_t to) { /* update offsets */ });

auto range = faces->Allocate(bytes.size());                // std::optional<StreamingRange>
faces->Write(*range, bytes);                               // Copied now, uploaded later; never blocks.
faces->Free(*range);                                       // Reusable at once (pending writes are dropped).

// In PrepareGeometry, once per frame, before the passes that read it:
const auto buffer = faces->Import(context.Graph(), context.Settings().StreamingUploadBudget);
// faces->GetOffset(range) is valid for this frame's GPU data; return `buffer` in RenderGeometryResources::Buffers.
```

`Import` records this frame's defragmentation moves (the highest ranges into lower holes, through a scratch copy, within the move budget, reported through the callback before it returns) and then the queued writes in FIFO order up to the upload budget (at least one write per frame). `IsUploaded(range)` says whether a range has writes left; `GetStats()` reports capacity, allocated bytes, the largest free range, free-range count, pending bytes and the last frame's uploaded and moved bytes. The budget is `RenderSettings::StreamingUploadBudget`, set by the saved console variable `render_upload_budget_mb` (default 16). Copies run on the graph's queue in frame order, so there is no separate upload fence to wait for. Destroy the buffer only after the renderer is idle (features normally live as long as the renderer).

**Texture arrays.** `Swim::Render::TextureArray` (`Resources/TextureArray.h`) is a `Texture2DArray` of same-size layers (RGBA8 or BC7, sRGB or linear, a full or capped mip chain) whose layers are replaced at run time:

```cpp
Swim::Render::TextureArrayDesc desc;
desc.Width = desc.Height = 256;
desc.Layers = 512;
desc.Format = Swim::Rhi::Format::BC7UnormSrgb;  // Or RGBA8UnormSrgb / RGBA8Unorm / BC7Unorm.
desc.Placeholder = { 255, 0, 255, 255 };        // What layers show until they have an image.
tiles = std::make_unique<Swim::Render::TextureArray>(*context.Device(), desc);

tiles->SetLayer(7, data);                       // TextureLayerData: Format, Width, Height, Mips (RGBA8: missing mips generated, other sizes resampled).
tiles->LoadLayer(8, "C:/content/stone.ktx2", 10); // Background read + decode; higher priority first; the old image stays until it lands.
tiles->CancelLayer(8);                          // Drops a load (and an upload that has not started).
tiles->ClearLayer(9);                           // Back to the placeholder.
tiles->SetHotReload(true, 0.5);                 // Loaded files are reloaded when their modification time changes.
tiles->SetDecoder(myDecoder);                   // Optional: any file format; DecodeFile is the default.

// In PrepareGeometry, once per frame:
const auto texture = tiles->Import(context.Graph(), context.Settings().StreamingUploadBudget);
const auto minLod = tiles->ImportMinLod(context.Graph());     // StructuredBuffer<float>, one per layer.
return { { minLod /* , buffers */ }, { /* indirect */ }, { texture } };

// In DrawGeometry:
draw.Texture("Tiles", texture).Sampler("TileSampler", "LinearRepeat").Buffer("TileMinLod", minLod);
```

```hlsl
#include "Resources/TextureArray.slang"
[[vk::binding(1, 2)]] Texture2DArray Tiles;
[[vk::binding(2, 2)]] SamplerState TileSampler;
[[vk::binding(3, 2)]] StructuredBuffer<float> TileMinLod;
// In the fragment stage:
float4 albedo = TextureArraySample(Tiles, TileSampler, uv, float(layer), TileMinLod[layer]);
```

New images go up smallest mip first within the upload budget, so a large layer sharpens over a few frames; `GetMinLod(layer)` (and the `ImportMinLod` buffer) is the finest mip of the current image on the GPU, and `TextureArraySample` scales the derivatives so no finer (stale) mip is read. `GetLayerState` reports `Placeholder`, `Loading`, `Uploading`, `Resident` or `Failed` (`GetLayerError`); `GetStats` reports counts, memory and pending bytes. The default decoder reads uncompressed KTX2 (one 2D image with every mip, BC7 or RGBA8 to match the array) and, in builds with the development asset pipeline, PNG and JPEG (compiled to the array's format; BC7 arrays need images of the array's size). The whole array is allocated when it is created; a layer can be replaced but not evicted. All calls belong on the render thread; the loader thread only reads files and decodes. The sandbox box field samples a four-layer array: three generated patterns (one repainted every two seconds) and a PNG copied to the temporary folder (`BoxFieldFace.png`), which reloads when it is saved.

Feature geometry is drawn in the main view only (reflection probes and planar captures do not see it; their frames drop the feature draws), opaque feature draws turn `DeferredLocalLights` off for the frame, and cached shadow cascades pick it up on their normal refresh (at most four frames for the far cascades).

## Editing shaders while the engine runs

In a development build (one with `DevPaths.cfg` next to the executable) the engine watches the source of every program declared with `swim_add_runtime_shader`, including an application's own, and every file it includes. Saving one recompiles the programs that use it on a background thread with the build's `slangc` command line, and the next frame uses them; the old programs and pipelines are destroyed once the GPU is done with them. A compile error prints the compiler output and keeps the running program. Results are cached by a hash of the command line and the input files (in the cache root, `Shaders/`), so undoing an edit is instant. `shader_hot_reload 0` stops watching and `shader_reload <name|all>` recompiles on demand. Feature compute and geometry programs are swapped; a program held by an engine pass is reported and takes effect after a restart.

A scalar push-constant field marked `[Tweak(min, max)]` becomes a console variable `shader_<program>_<field>` (lower case) the first time the program dispatches:

```slang
#include "Tweak.slang"

struct TintConstants
{
	uint2 Size;
	[Tweak(0.0, 2.0)] float Strength;
};
```

While the variable is at its default (the minimum) the value the feature writes is used; any other value replaces it in every dispatch (`cvar_reset` hands control back). The screen anti-aliasing programs expose `Threshold` and `Strength` this way.

## The built-in features

**`VolumetricClouds`** (`BeforeTemporal`). Each pixel of a reduced-resolution target (`ResolutionScale`, default 0.5) marches its view ray through a horizontal layer (`BottomAltitude`–`TopAltitude`, default 450–1300 m). Density is procedural value-noise fBm shaped by `Coverage` and a cumulus height profile, eroded by a detail octave and scrolled by `Wind`. Lighting uses a short sun-shadow march (`ShadowSteps` × `ShadowStepLength`), Beer–Lambert extinction (`Density`), a powder term, a two-lobe Henyey–Greenstein phase (`Anisotropy`, `SilverLining`), a three-octave multiple-scattering approximation (so thick cumulus stays white) and the sky's zenith and horizon colors as ambient light. The integration is energy-conserving, and distant clouds fade into the sky (`HorizonFade`, `MaxDistance`). The march start is dithered per pixel and frame, and TAA resolves the noise. A full-resolution pass composites the result over sky pixels only: `color × transmittance + radiance`. Cost scales with `Steps` (48), `ShadowSteps` (4) and the resolution scale. With `Environment` on (the default), the same march also fills the environment cube (`VolumetricCloudsEnvironment`, `EnvironmentSteps` = 64, a fixed dither, from the camera position), so chrome, water-like floors and SSR misses reflect the clouds. At the default 128² source cube that is 98k rays every `EnvironmentRefreshSeconds`.

**`SunShafts`** (`BeforePostProcess`). The mask pass (at `MaskScale`, default 0.5) keeps sky pixels within `ConeDegrees` of the sun that are brighter than `Threshold`, so geometry and clouds in front of the sun cut the shafts. The composite pass blurs the mask radially toward the sun's screen position (`Samples`, `Density`, `Decay`, `Weight`) and adds it, tinted by the sun's radiance and `Intensity`, to the scene. It fades as the sun leaves the screen (`EdgeFade`) or sets.

**`LensFlare`** (`BeforePostProcess`). An analytic flare: glare and a starburst around the sun (`GlareSize`, `Streaks`, `StarburstIntensity`), up to eight chromatic ghosts mirrored through the screen center (`Ghosts`, `GhostSpacing`, `GhostIntensity`, `ChromaticSpread`) and a halo ring (`HaloRadius`, `HaloIntensity`). Visibility is measured from the frame: a ring of taps around the sun (`OcclusionRadius`) counts bright sky, so occluders and clouds dim the flare without an occlusion query. It runs before exposure and bloom, so the flare blooms with the frame.

**`Weather`** (transparent geometry pass). Rain or snow drawn procedurally around the camera: no particle buffers, every particle is a hash of its world-stable column cell and index, falling through a band (`Radius`, `Height`) and drifting with `Wind`, so the field does not swim when the camera moves. `Intensity` (0–1) picks how many of `MaxParticles` draw; `Snow` blends streaks into wobbling flakes. Rain streaks are aligned to the fall velocity and widened to at least a pixel. The game supplies an occlusion height field (`SetOcclusion`: a square grid of the highest blocking surface per cell around the camera) so particles stop under roofs and `Splashes` land on the top surfaces. Particles are lit with the forward lights and written premultiplied. `Advance(dt)` integrates a 0–1 wetness (`WetnessRise`, `WetnessDry`) and `GetFogDensity()` suggests how much to thicken fog; both are for the game to feed its own materials. `ApplyQuality` sets particle and splash counts and the radius for a `WeatherQuality` tier.

**Wind** (`Features/Wind.h`, `Features/Wind.slang`). Not a feature: one shared wind model, `WindSettings` (direction, speed, gustiness, gust scale, turbulence) packed into a 32-byte `GpuWind` with the time. `WindVelocity` and `WindGust` agree between C++ and Slang, so CPU effects and shaders move together. `WindSway(wind, anchor, weight, flexibility, time)` bends a vertex downwind with gusts and flutter, scaled by how far up a flexible object the vertex sits; the result is clamped so geometry does not stretch. Evaluate it at the previous frame's time as well for motion vectors.

**Camera: `GravitationalLensing` (order 5), `DepthOfField` (20), `CameraLens` (30)** (`BeforePostProcess`) **and `FilmSensor`** (`AfterPostProcess`, the RGBA8 display frame): black-hole shadows and weak-field lensing, thin-lens depth of field, lens geometry and optics (distortion, fisheye, chromatic aberration, softness, cos⁴ vignetting, halation, filter), and grain and sharpening. Presets are `CameraLook` descriptions that `DeriveCameraLook` turns into effect values. All start disabled. See [Camera.md](Camera.md).

**Water** (`Ocean.h`, `Underwater.h`, `Lightning.h`, `WaterQuality.h`; shaders in `Shaders/Slang/Water` and `Features/Lightning.slang`).

- `OceanSimulation` is a GPU Tessendorf ocean with no draw of its own. Each frame it builds a JONSWAP spectrum with cos^p directional spreading (`WindSpeed`, `WindDirectionDegrees`, `Fetch`, `Spreading`, `SmallWaveCutoff`), evolves it and runs radix-2 Stockham inverse FFTs in group-shared memory (programs `OceanSpectrum`, `OceanFft`, `OceanAssemble`). It does this for up to three cascades (`TileSizes`, `Cascades`, `Resolution` 64–512). The output per cascade is choppy displacement plus foam (`Choppiness`, `Amplitude`, `Detail`), and slopes and Jacobian terms. Whitecaps build where the surface compresses past `FoamThreshold` and fade with `FoamDecay`; that foam history is the only persistent state. Any feature can call `Update(context)` and sample the returned `OceanMaps`. `Water/WaterCommon.slang` has sampling, footprint fading and normal helpers.
- `OceanSurface` (`Opaque` geometry pass) draws a simulation as an endless camera-centred grid. The quads are dense under the camera and grow toward `Extent`. Displacement is applied in the vertex shader, with motion vectors from the previous frame's maps. Shading covers Fresnel reflection, footprint-faded normals, subsurface light through crests, streaked foam and horizon fog.
- `Underwater` adds two features:
  - The medium (`BeforeTemporal`): per-channel extinction and in-scatter by distance and depth below `SurfaceHeight`, total internal reflection outside Snell's window, animated caustics on lit surfaces (optionally split into colour), and dithered light shafts (`ShaftSteps`).
  - A lens pass (`BeforePostProcess`): bulge, refraction wobble and radial chromatic aberration.

  Both scale with `Submersion`, so a game blends them as the camera crosses the surface.
- `Lightning` (`Transparent`) generates branching bolts on the CPU with midpoint displacement and draws them as additive camera-facing ribbons. They flicker through their return strokes. `GetFlash()` reports the brightness envelope for sky flashes, a flash light and thunder timing.
- `WaterQuality` presets (Off, Low, Medium, High, Ultra) set only costs: FFT size, cascades, grid density, which underwater passes run, and shaft steps. `ApplyWaterQuality` applies a preset to any of the objects above.

## Tests

`Engine.RenderFeature` (3): on the mock device, a compute pass binds by reflected name, rejects missing, unknown and mismatched bindings (including a storage-format mismatch), covers a 100 × 50 grid with 8 × 8 groups and forwards its push constants; `SetColor` requires the scene color's format and size; directions project to the screen like points at infinity; the clouds march a 32 × 192 environment atlas only while they contribute. `Engine.CameraEffects` (4) and `Engine.GravitationalLensing` (2) cover the camera features ([Camera.md](Camera.md#tests)). SwiftShader and RTX 4070 captures of the sandbox check the built-in features end to end.
