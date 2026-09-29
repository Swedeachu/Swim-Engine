#pragma once
#include "Engine/Systems/Renderer/Environment/EnvironmentBindings.h"
#include "Engine/Systems/Renderer/Environment/EnvironmentGraphResources.h"
#include "Engine/Systems/Renderer/Environment/ProceduralSky.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraph.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace Swim::Render
{

	// One compiled environment program.
	struct EnvironmentProgram
	{
		Rhi::ComputePipeline* Pipeline = nullptr;
		Rhi::PipelineLayout* Layout = nullptr;
		std::uint32_t Space = 0;
	};

	struct EnvironmentBuilderDesc
	{
		EnvironmentProgram Sky;		   // EnvironmentSky.slang
		EnvironmentProgram Downsample; // EnvironmentDownsample.slang
		EnvironmentProgram Prefilter;  // EnvironmentPrefilter.slang
		EnvironmentProgram Irradiance; // EnvironmentIrradiance.slang
		EnvironmentProgram BrdfLut;	   // EnvironmentBrdfLut.slang
		// Optional (EnvironmentOverlay.slang): needed only to record overlays.
		EnvironmentProgram Overlay;
		// Linear min/mag/mip, clamp-to-edge: the prefilter reads the source through it.
		Rhi::Sampler* Sampler = nullptr;
		std::string DebugName = "Environment";
	};

	// Sizes of one environment. Cube sizes are powers of two; every map is RGBA16Float.
	struct EnvironmentMapDesc
	{
		std::uint32_t SourceSize = 128;			 // Source cube face size (>= 16); mips stop at 4x4.
		std::uint32_t PrefilteredSize = 64;		 // Prefiltered mip 0 face size.
		std::uint32_t PrefilteredMipCount = 5;	 // Mip m holds roughness m / (count - 1); >= 2.
		std::uint32_t PrefilterSampleCount = 64; // GGX samples per prefiltered texel.
		std::uint32_t IrradianceFaceSize = 16;	 // Source mip the SH projection reads.
	};

	// Optional caller-owned outputs (for example imported persistent resources) in
	// place of transient ones; they must match the map desc.
	struct EnvironmentTargets
	{
		std::optional<GraphTexture> Prefiltered;
		std::optional<GraphBuffer> Irradiance;
	};

	// Builds image-based lighting (critical-path item 61) as graph-scheduled compute
	// passes: a procedural sky or any RGBA16Float source cube with its mip chain, the
	// GGX-prefiltered specular cube (filtered importance sampling), order-2 SH
	// irradiance and the split-sum BRDF LUT. Environment/EnvironmentReference.h is the
	// CPU definition of every pass.
	class EnvironmentBuilder
	{

	  public:

		explicit EnvironmentBuilder(EnvironmentBuilderDesc desc);

		// A transient source cube holding the sky: mip 0 evaluated per texel, then RecordMips.
		GraphTexture RecordSky(RenderGraph& graph, const Environment::ProceduralSky& sky, const EnvironmentMapDesc& desc) const;

		// Fills mips 1.. of a cube (Sampled | Storage, RGBA16Float) from its mip 0 by 2x2 box filtering.
		std::vector<GraphPass> RecordMips(RenderGraph& graph, GraphTexture cube) const;

		// Prefiltered cube + SH irradiance from a complete source cube
		// (EnvironmentSourceMipCount mips, Sampled, RGBA16Float).
		// irradianceSource, when given, is the (equally complete) cube the SH irradiance is
		// projected from instead of the source.
		EnvironmentGraphResources RecordFromSource(RenderGraph& graph, GraphTexture source, const EnvironmentMapDesc& desc,
			const EnvironmentTargets& targets = {}, std::optional<GraphTexture> irradianceSource = {}) const;

		// RecordSky followed by RecordFromSource. Each overlay (OverlayDesc(desc.SourceSize):
		// face f of the cube in rows f * size.., premultiplied rgb, alpha = transmittance) is
		// folded into the sky's mip 0 in order, cube = cube * a + rgb, before the mips and the
		// prefilter: features that draw into the sky (volumetric clouds) show up in
		// reflections and ambient light. Overlays need the Overlay program. With
		// overlaysInIrradiance false they reach only the prefiltered (specular) cube: the
		// SH irradiance is projected from a second, clear sky cube.
		EnvironmentGraphResources Record(RenderGraph& graph, const Environment::ProceduralSky& sky, const EnvironmentMapDesc& desc,
			const EnvironmentTargets& targets = {}, std::span<const GraphTexture> overlays = {}, bool overlaysInIrradiance = true) const;

		// The split-sum LUT (environment independent; build once and keep it). The
		// target, when given, must be a size x size RGBA16Float Storage texture.
		GraphTexture RecordBrdfLut(
			RenderGraph& graph, std::uint32_t size, std::uint32_t sampleCount, std::optional<GraphTexture> target = {}) const;

		// Descriptors of the transient resources the builder creates.
		static Rhi::TextureDesc SourceCubeDesc(std::uint32_t size);

		static Rhi::TextureDesc PrefilteredCubeDesc(const EnvironmentMapDesc& desc);

		static Rhi::TextureDesc BrdfLutDesc(std::uint32_t size);

		static Rhi::BufferDesc IrradianceBufferDesc();

		// A size x 6 * size RGBA16Float overlay atlas (Sampled | Storage).
		static Rhi::TextureDesc OverlayDesc(std::uint32_t size);

		// Throws std::invalid_argument when the sizes break the contract above.
		static void Validate(const EnvironmentMapDesc& desc);

	  private:

		GraphTexture RecordSky(RenderGraph& graph, const Environment::ProceduralSky& sky, const EnvironmentMapDesc& desc,
			std::vector<GraphPass>& passes, std::span<const GraphTexture> overlays = {}) const;

		EnvironmentBuilderDesc desc;

	};

} // namespace Swim::Render
