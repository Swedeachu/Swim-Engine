#pragma once
#include "Engine/Systems/Renderer/Environment/CubeImage.h"
#include "Engine/Systems/Renderer/Environment/ProceduralSky.h"
#include "Engine/Systems/Renderer/Materials/StandardPbr.h"

namespace Swim::Render::Environment
{
	// CPU references of the GPU environment passes (EnvironmentBuilder) and of the
	// shader-side lookup (EnvironmentLighting.slang). The native smokes feed them the
	// GPU's own inputs (read back) so each stage is compared in isolation.

	enum class CubeSampling : std::uint8_t
	{
		Nearest,
		Trilinear,
	};

	// Mip 0 evaluated at texel centers, the rest box-filtered down to 4x4
	// (EnvironmentSourceMipCount; EnvironmentSky.slang + EnvironmentDownsample.slang).
	CubeImage BuildSkyCube(const ProceduralSky& sky, std::uint32_t size);

	// One prefiltered texel (EnvironmentPrefilter.slang): the GGX lobe of the given
	// roughness around `direction` (N = V = R), importance-sampled with sampleCount
	// Hammersley points and filtered importance sampling from the source mips.
	// Roughness 0 is a single lod-0 sample.
	Float3 PrefilterDirection(const CubeImage& source, const Float3& direction, float perceptualRoughness, std::uint32_t sampleCount,
		CubeSampling sampling = CubeSampling::Trilinear);
	CubeImage BuildPrefilteredCube(const CubeImage& source, std::uint32_t size, std::uint32_t mipCount, std::uint32_t sampleCount,
		CubeSampling sampling = CubeSampling::Trilinear);

	// Irradiance / pi projected from one mip of a cube (EnvironmentIrradiance.slang),
	// every texel weighted by its exact solid angle.
	IrradianceSh ProjectIrradianceSh(const CubeImage& cube, std::uint32_t mip);

	// The split-sum LUT (EnvironmentBrdfLut.slang): texel (x, y) holds (A, B, 0, 1)
	// for N.V = (x + 0.5) / size and roughness = (y + 0.5) / size.
	Image2D BuildBrdfLut(std::uint32_t size, std::uint32_t sampleCount);

	// Per-view environment controls (Phase 17 "environment rotation/intensity").
	struct EnvironmentLighting
	{
		float Intensity = 1.0f;
		float Rotation = 0.0f; // Radians around +Y; see RotateEnvironmentLookup.
	};

	// The lookups a shader performs for one pixel: SH irradiance at the normal,
	// trilinear prefiltered radiance at the reflection vector with lod = roughness *
	// (mips - 1), and the bilinear LUT at (N.V, roughness); radiance terms scale by
	// the intensity. Mirrors EnvironmentLookup in EnvironmentLighting.slang.
	class EnvironmentProbe
	{
	  public:
		EnvironmentProbe(IrradianceSh irradiance, CubeImage prefiltered, Image2D brdfLut);

		StandardPbr::EnvironmentTerms Lookup(
			const StandardPbr::ResolvedSurface& surface, const Float3& view, const EnvironmentLighting& lighting = {}) const;

		const IrradianceSh& GetIrradiance() const { return irradiance; }

		const CubeImage& GetPrefiltered() const { return prefiltered; }

		const Image2D& GetBrdfLut() const { return brdfLut; }

	  private:
		IrradianceSh irradiance;
		CubeImage prefiltered;
		Image2D brdfLut;
	};
} // namespace Swim::Render::Environment
