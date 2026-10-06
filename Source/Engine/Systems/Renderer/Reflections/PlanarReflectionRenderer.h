#pragma once
#include "Engine/Systems/Renderer/Environment/EnvironmentBuilder.h"
#include "Engine/Systems/Renderer/Reflections/PlanarReflections.h"
#include "Engine/Systems/Renderer/Reflections/ReflectionProbeRenderer.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraph.h"

#include <cstdint>
#include <memory>
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
		static constexpr std::uint32_t Sampler = 4;		// SamplerState: linear clamp.
		static constexpr std::uint32_t ThreadGroupSize = 8;
		static constexpr std::uint32_t PushConstantBytes = 96;
	};

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
		};

		// Imports the atlas (once per graph); a fresh one is cleared first. Export it as
		// ShaderRead after the last use.
		Atlas Import(RenderGraph& graph);

		// Resolves a capture (color RGBA16Float and depth D32Float, Width x Height) into its layer.
		GraphPass RecordResolve(RenderGraph& graph, const Atlas& atlas, const PlanarReflections::Capture& capture, GraphTexture color,
			GraphTexture depth, const ReflectionProbeCaptureSky& sky = {}) const;

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
