# Render features

A render feature is a self-contained piece of the frame that gameplay code creates, configures and hands to the renderer. The renderer records it every frame at its stage. Volumetric clouds, sun shafts and the lens flare are built this way. Adding one does not touch `FrameRenderer`, the render graph or the RHI:

```text
Engine/Systems/Renderer/Runtime
  RenderFeature.h/.cpp     RenderFeature (the interface), RenderFeatureContext (what Record sees),
                           RenderFeatureComputePass (compute passes bound by Slang parameter name),
                           RenderFeatureView (camera, sun, time)
Engine/Systems/Renderer/Features
  VolumetricClouds, SunShafts, LensFlare   the built-in features (Features.cpp)
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

## The built-in features

**`VolumetricClouds`** (`BeforeTemporal`). Each pixel of a reduced-resolution target (`ResolutionScale`, default 0.5) marches its view ray through a horizontal layer (`BottomAltitude`–`TopAltitude`, default 450–1300 m). Density is procedural value-noise fBm shaped by `Coverage` and a cumulus height profile, eroded by a detail octave and scrolled by `Wind`. Lighting uses a short sun-shadow march (`ShadowSteps` × `ShadowStepLength`), Beer–Lambert extinction (`Density`), a powder term, a two-lobe Henyey–Greenstein phase (`Anisotropy`, `SilverLining`), a three-octave multiple-scattering approximation (so thick cumulus stays white) and the sky's zenith and horizon colors as ambient light. The integration is energy-conserving, and distant clouds fade into the sky (`HorizonFade`, `MaxDistance`). The march start is dithered per pixel and frame, and TAA resolves the noise. A full-resolution pass composites the result over sky pixels only: `color × transmittance + radiance`. Cost scales with `Steps` (48), `ShadowSteps` (4) and the resolution scale.

**`SunShafts`** (`BeforePostProcess`). The mask pass (at `MaskScale`, default 0.5) keeps sky pixels within `ConeDegrees` of the sun that are brighter than `Threshold`, so geometry and clouds in front of the sun cut the shafts. The composite pass blurs the mask radially toward the sun's screen position (`Samples`, `Density`, `Decay`, `Weight`) and adds it, tinted by the sun's radiance and `Intensity`, to the scene. It fades as the sun leaves the screen (`EdgeFade`) or sets.

**`LensFlare`** (`BeforePostProcess`). An analytic flare: glare and a starburst around the sun (`GlareSize`, `Streaks`, `StarburstIntensity`), up to eight chromatic ghosts mirrored through the screen center (`Ghosts`, `GhostSpacing`, `GhostIntensity`, `ChromaticSpread`) and a halo ring (`HaloRadius`, `HaloIntensity`). Visibility is measured from the frame: a ring of taps around the sun (`OcclusionRadius`) counts bright sky, so occluders and clouds dim the flare without an occlusion query. It runs before exposure and bloom, so the flare blooms with the frame.

## Tests

`Engine.RenderFeature` (2): on the mock device, a compute pass binds by reflected name, rejects missing, unknown and mismatched bindings (including a storage-format mismatch), covers a 100 × 50 grid with 8 × 8 groups and forwards its push constants; `SetColor` requires the scene color's format and size; directions project to the screen like points at infinity. SwiftShader captures of the sandbox check the built-in features end to end.
