#pragma once
#include "Engine/Systems/Renderer/Environment/EnvironmentBuilder.h"
#include "Engine/Systems/Renderer/Reflections/PlanarReflections.h"
#include "Engine/Systems/Renderer/Reflections/ReflectionProbeRenderer.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraph.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace Swim::Render
{

	// PlanarReflectionResolve.slang.
	struct PlanarReflectionResolveBindings
	{
		static constexpr std::uint32_t Color = 0;		// Texture2D<float4>: the capture (RGBA16Float).
		static constexpr std::uint32_t Depth = 1;		// Texture2D<float>: its reverse-Z depth (D32Float depth aspect).
		static constexpr std::uint32_t Destination = 2; // RWTexture2D<float4> rgba16f: one atlas layer.
		static constexpr std::uint32_t Environment = 3; // TextureCube<float4>: the global environment (sky with clouds).
		static constexpr std::uint32_t Sampler = 4;		// SamplerState: linear clamp with mips.
		// The capture's surface (reflections of reflections, CaptureShading.slang):
		static constexpr std::uint32_t Normal = 5;		// Texture2D<float4>: world normal + roughness.
		static constexpr std::uint32_t Reflectance = 6; // Texture2D<float4>: split-sum specular reflectance.
		static constexpr std::uint32_t Specular = 7;	// Texture2D<float4>: specular IBL radiance.
		static constexpr std::uint32_t ObjectId = 8;	// Texture2D<float>: ObjectId + 1.
		static constexpr std::uint32_t ProbeCubes = 9;	// TextureCubeArray<float4>: prefiltered probes (1x1 stand-in without).
		static constexpr std::uint32_t ProbeRecords = 10; // StructuredBuffer<GpuReflectionProbeRecord>.
		static constexpr std::uint32_t Count = 11;
		static constexpr std::uint32_t ThreadGroupSize = 8;
		static constexpr std::uint32_t PushConstantBytes = 128;
		static constexpr std::uint32_t MaxSupersample = 4;
	};

	// One capture's Forward+ targets (RenderWidth x RenderHeight) and the probes its reflective
	// texels reflect (ReflectionProbeRenderer.h).
	using PlanarCaptureTargets = ReflectionCaptureTargets;
	using PlanarCaptureProbes = ReflectionCaptureProbes;

	struct PlanarReflectionRendererDesc
	{
		EnvironmentProgram Resolve;		 // PlanarReflectionResolve.slang
		Rhi::Sampler* Sampler = nullptr; // Linear, clamp-to-edge.
		std::string DebugName = "Planar reflections";
	};

	// The GPU side of planar reflections: one persistent RGBA16Float 2D-array atlas, a layer
	// per capture slot (AtlasResolution^2; a capture uses the top-left Width x Height of its
	// layer, so its LOD changes without reallocating), holding the radiance and the distance
	// from the capture position to what each texel sees (the lookup's depth march). Captures
	// are ordinary Forward+ renders the frame renderer records; this class resolves them into
	// their layers. Layers keep their content between captures (temporal reuse).
	class PlanarReflectionRenderer
	{

	  public:

		PlanarReflectionRenderer(Rhi::Device& device, PlanarReflectionRendererDesc desc);

		// (Re)creates the atlas when the size or layer count changes; true when recreated
		// (every slot must then be captured again).
		bool Ensure(std::uint32_t resolution, std::uint32_t layers);

		struct Atlas
		{
			GraphTexture Texture;
			GraphTexture EnvironmentStandIn; // A black 1x1 cube bound when no environment is given.
			GraphTexture ProbeStandIn;		 // A black 1x1 cube array bound without probes.
			GraphBuffer ProbeRecordStandIn;	 // One zero record.
		};

		// Imports the atlas (once per graph); a fresh one is cleared first. Export it as
		// ShaderRead after the last use.
		Atlas Import(RenderGraph& graph);

		// Resolves a capture (its targets RenderWidth x RenderHeight, an integer multiple of
		// Width x Height) into its layer: supersampled texels averaged down, reflective texels
		// given their probe's reflection when `probes` are given.
		GraphPass RecordResolve(RenderGraph& graph, const Atlas& atlas, const PlanarReflections::Capture& capture,
			const PlanarCaptureTargets& targets, const ReflectionProbeCaptureSky& sky = {},
			const std::optional<PlanarCaptureProbes>& probes = std::nullopt) const;

		std::uint32_t GetResolution() const { return resolution; }

		std::uint32_t GetLayers() const { return layers; }

		static Rhi::TextureDesc AtlasDesc(std::uint32_t resolution, std::uint32_t layers);

	  private:

		Rhi::Device& device;
		PlanarReflectionRendererDesc desc;
		std::unique_ptr<Rhi::Texture> atlas;
		std::uint32_t resolution = 0;
		std::uint32_t layers = 0;
		bool initialized = false;

	};

} // namespace Swim::Render
