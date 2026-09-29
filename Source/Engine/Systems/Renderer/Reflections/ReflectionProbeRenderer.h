#pragma once
#include "Engine/Systems/Renderer/Environment/EnvironmentBuilder.h"
#include "Engine/Systems/Renderer/Reflections/ReflectionProbeTypes.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraph.h"

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
		static constexpr std::uint32_t Sampler = 4;		// SamplerState: linear clamp.
		static constexpr std::uint32_t ThreadGroupSize = 8;
		static constexpr std::uint32_t PushConstantBytes = 32; // Size, Mode, Near, Face, Environment, scale, rotation, reserved.
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
		};

		using CaptureSky = ReflectionProbeCaptureSky;

		// Imports both atlases into the graph (once per graph); freshly created ones are
		// cleared first. Export them as ShaderRead after the last use.
		Atlases Import(RenderGraph& graph);

		// Resolves a captured face (color RGBA16Float and depth D32Float, both
		// Resolution^2) into cube `slot`, face `face`, mip 0 of the source atlas.
		GraphPass RecordResolve(RenderGraph& graph, const Atlases& atlases, std::uint32_t slot, std::uint32_t face, GraphTexture color,
			GraphTexture depth, float nearPlane, const CaptureSky& sky = {}) const;

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
