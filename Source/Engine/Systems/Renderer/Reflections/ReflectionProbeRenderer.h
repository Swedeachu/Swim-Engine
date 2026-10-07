#pragma once
#include "Engine/Systems/Renderer/Environment/EnvironmentBuilder.h"
#include "Engine/Systems/Renderer/Reflections/ReflectionProbeTypes.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraph.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace Swim::Render
{

	// ReflectionProbeResolve.slang.
	struct ReflectionProbeResolveBindings
	{
		static constexpr std::uint32_t Color = 0;		// Texture2D<float4>: the captured face (RGBA16Float).
		static constexpr std::uint32_t Depth = 1;		// Texture2D<float>: its reverse-Z depth (D32Float depth aspect).
		static constexpr std::uint32_t Destination = 2; // RWTexture2D<float4> rgba16f: one face of one atlas mip.
		static constexpr std::uint32_t Environment = 3; // TextureCube<float4>: the global environment (the sky with clouds).
		static constexpr std::uint32_t Sampler = 4;		// SamplerState: linear clamp with mips.
		// The capture's surface (reflections of reflections, CaptureShading.slang):
		static constexpr std::uint32_t Normal = 5;		  // Texture2D<float4>: world normal + roughness.
		static constexpr std::uint32_t Reflectance = 6;	  // Texture2D<float4>: split-sum specular reflectance.
		static constexpr std::uint32_t Specular = 7;	  // Texture2D<float4>: specular IBL radiance.
		static constexpr std::uint32_t ObjectId = 8;	  // Texture2D<float>: ObjectId + 1.
		static constexpr std::uint32_t ProbeCubes = 9;	  // TextureCubeArray<float4>: the prefiltered atlas as it stands.
		static constexpr std::uint32_t ProbeRecords = 10; // StructuredBuffer<GpuReflectionProbeRecord>.
		static constexpr std::uint32_t Count = 11;
		static constexpr std::uint32_t ThreadGroupSize = 8;
		// Size, Mode, Near, Face, Environment, scale, rotation, reserved; position; probe count, mips, reserved x 2.
		static constexpr std::uint32_t PushConstantBytes = 64;
	};

	// A capture's Forward+ targets (planar or probe face): its radiance and depth, and the
	// surface its reflective texels are re-shaded with (CaptureShading.slang).
	struct ReflectionCaptureTargets
	{
		GraphTexture Color;		  // RGBA16Float.
		GraphTexture Depth;		  // D32Float.
		GraphTexture Normal;	  // RGBA16Float.
		GraphTexture Reflectance; // RGBA16Float.
		GraphTexture Specular;	  // RGBA16Float.
		GraphTexture ObjectId;	  // R32Float.
	};

	// The reflection probes reflective texels of a capture reflect: GpuReflectionProbeRecords
	// over a prefiltered cube array (Cubes; probe captures use the probe atlas itself).
	struct ReflectionCaptureProbes
	{
		std::optional<GraphTexture> Cubes;
		GraphBuffer Records;
		std::uint32_t Count = 0;
		std::uint32_t MipCount = 1;
	};

	// The sky probe captures show (optional): the global prefiltered environment, whose mip 0
	// holds the sky with its feature overlays (clouds); radiance x Scale, looked up rotated.
	struct ReflectionProbeCaptureSky
	{
		std::optional<GraphTexture> Environment;
		float Scale = 1.0f;
		float Rotation = 0.0f;
	};

	struct ReflectionProbeRendererDesc
	{
		EnvironmentProgram Resolve;	   // ReflectionProbeResolve.slang
		EnvironmentProgram Downsample; // EnvironmentDownsample.slang
		EnvironmentProgram Prefilter;  // EnvironmentPrefilter.slang with PREFILTER_KEEP_ALPHA=1
		Rhi::Sampler* Sampler = nullptr; // Linear, clamp-to-edge, with mips.
		std::string DebugName = "Reflection probes";
	};

	// The GPU side of reflection probes: two persistent cube-array atlases (MaxProbes cubes
	// of Resolution^2 RGBA16Float faces with full mip chains down to 4x4), the source holding
	// the captured radiance and distances, the prefiltered one what shading samples (mip m:
	// roughness m / (mips - 1), alpha: the captured distance). Captures themselves are
	// ordinary forward renders the frame renderer records; this class resolves them into
	// the atlas and filters a probe after its faces changed. ReflectionProbes.h is the CPU
	// definition of the math.
	class ReflectionProbeRenderer
	{

	  public:

		ReflectionProbeRenderer(Rhi::Device& device, ReflectionProbeRendererDesc desc);

		// (Re)creates the atlases when the size or slot count changes; true when recreated
		// (every probe must then be captured again).
		bool Ensure(std::uint32_t resolution, std::uint32_t maxProbes);

		struct Atlases
		{
			GraphTexture Source;
			GraphTexture Prefiltered;
			GraphTexture EnvironmentStandIn; // A black 1x1 cube bound when no environment is given.
			GraphBuffer ProbeRecordStandIn;	 // One zero record, bound without probe records.
		};

		using CaptureSky = ReflectionProbeCaptureSky;

		// Imports both atlases into the graph (once per graph); freshly created ones are
		// cleared first. Export them as ShaderRead after the last use.
		Atlases Import(RenderGraph& graph);

		// Resolves a captured face (targets Resolution^2) into cube `slot`, face `face`, mip 0 of
		// the source atlas, captured at `position`. With `probes` (Records and Count; the cubes
		// are the prefiltered atlas as it stands), reflective texels show their probe's reflection.
		GraphPass RecordResolve(RenderGraph& graph, const Atlases& atlases, std::uint32_t slot, std::uint32_t face,
			const ReflectionCaptureTargets& targets, const std::array<float, 3>& position, float nearPlane, const CaptureSky& sky = {},
			const std::optional<ReflectionCaptureProbes>& probes = std::nullopt) const;

		// Rebuilds the source mips of cube `slot` and its prefiltered cube.
		void RecordFilter(RenderGraph& graph, const Atlases& atlases, std::uint32_t slot, std::uint32_t sampleCount) const;

		std::uint32_t GetResolution() const { return resolution; }

		std::uint32_t GetMaxProbes() const { return maxProbes; }

		std::uint32_t GetMipCount() const { return mipCount; }

		static Rhi::TextureDesc AtlasDesc(std::uint32_t resolution, std::uint32_t maxProbes);

	  private:

		Rhi::Device& device;
		ReflectionProbeRendererDesc desc;
		std::unique_ptr<Rhi::Texture> source;
		std::unique_ptr<Rhi::Texture> prefiltered;
		std::uint32_t resolution = 0;
		std::uint32_t maxProbes = 0;
		std::uint32_t mipCount = 0;
		bool initialized = false;

	};

} // namespace Swim::Render
