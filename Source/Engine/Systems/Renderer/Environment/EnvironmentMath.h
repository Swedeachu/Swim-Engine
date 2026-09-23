#pragma once
#include <array>
#include <cstdint>

namespace Swim::Render::Environment
{
	// The CPU definition of image-based lighting (critical-path item 61). Every
	// function here is mirrored line for line by Shaders/Slang/Environment/
	// EnvironmentCommon.slang; the tests compare the two and the GPU environment
	// builder against CubeImage references.
	//
	// Conventions:
	//  - Directions are world-space unit vectors, +Y up.
	//  - Cube faces follow the Vulkan/D3D major-axis table (+X, -X, +Y, -Y, +Z, -Z =
	//    layers 0..5); (s, t) in [-1, 1] are the face coordinates before the [0, 1]
	//    remap, with t growing down the image rows.
	//  - Roughness is glTF perceptual roughness; alpha = roughness^2.
	using Float2 = std::array<float, 2>;
	using Float3 = std::array<float, 3>;

	inline constexpr float Pi = 3.14159265358979323846f;
	inline constexpr std::uint32_t CubeFaceCount = 6;

	Float3 Normalize(const Float3& value);
	float Dot(const Float3& a, const Float3& b);
	Float3 Cross(const Float3& a, const Float3& b);

	// Face-local coordinates (s, t) in [-1, 1] to the (unnormalized) direction.
	Float3 CubeFaceDirection(std::uint32_t face, float s, float t);
	// Direction of texel (x, y)'s center on a face of the given size, normalized.
	Float3 CubeTexelDirection(std::uint32_t face, std::uint32_t x, std::uint32_t y, std::uint32_t size);

	struct CubeCoordinate
	{
		std::uint32_t Face = 0;
		float S = 0.0f; // In [-1, 1].
		float T = 0.0f;
	};

	// The face a direction selects (largest magnitude axis; ties prefer X, then Y) and
	// its face coordinates.
	CubeCoordinate DirectionToCube(const Float3& direction);

	// Exact solid angle of texel (x, y) on a face of the given size; the 6 * size^2
	// texels sum to 4 pi.
	float CubeTexelSolidAngle(std::uint32_t x, std::uint32_t y, std::uint32_t size);

	// Low-discrepancy point i of n: (i / n, Van der Corput radical inverse of i).
	Float2 Hammersley(std::uint32_t index, std::uint32_t count);

	// GGX-distributed half vector around +Z for a sample point (pdf over half
	// vectors = D(h) * cos(theta_h)).
	Float3 ImportanceSampleGgx(const Float2& xi, float alpha);
	// Rotates a +Z-relative vector into the frame whose Z axis is the normal.
	Float3 TangentToWorld(const Float3& value, const Float3& normal);

	// Split-sum environment BRDF (Karis 2013) with the height-correlated Smith term
	// used by StandardPbr: returns (A, B) so that the specular directional albedo is
	// F0 * A + B. nDotV and perceptual roughness are clamped like the direct BRDF.
	Float2 IntegrateBrdf(float nDotV, float perceptualRoughness, std::uint32_t sampleCount);

	// Source mip a prefilter sample reads (filtered importance sampling, Colbert &
	// Krivanek 2007, with N = V = R): lod = 0.5 * log2(sampleSolidAngle /
	// texelSolidAngle) + 1, clamped to the source chain. nDotH is the sample's.
	float PrefilterSourceLod(float nDotH, float alpha, std::uint32_t sampleCount, std::uint32_t sourceSize, std::uint32_t sourceMipCount);

	// Perceptual roughness stored in prefiltered mip `mip` of a `mipCount` chain (mip
	// 0 is the mirror reflection), and the lod a shader samples for a roughness.
	float PrefilterMipRoughness(std::uint32_t mip, std::uint32_t mipCount);
	float PrefilterLodForRoughness(float perceptualRoughness, std::uint32_t mipCount);

	// Rotation of the environment around +Y (radians): a world direction looks up
	// the environment at RotateEnvironmentLookup(direction, rotation).
	Float3 RotateEnvironmentLookup(const Float3& direction, float rotation);

	// Order-2 (9 coefficient) real spherical harmonics.
	inline constexpr std::uint32_t ShCoefficientCount = 9;
	std::array<float, ShCoefficientCount> ShBasis(const Float3& direction);
	// Clamped-cosine convolution factors per band, divided by pi: projecting radiance
	// and scaling coefficient i by ShIrradianceScale(i) gives irradiance / pi, the
	// value a Lambert surface of albedo 1 reflects.
	float ShIrradianceScale(std::uint32_t coefficient);

	// Irradiance / pi as 9 RGB coefficients (the GPU layout is 9 float4, see
	// EnvironmentBindings::IrradianceBytes).
	struct IrradianceSh
	{
		std::array<Float3, ShCoefficientCount> Coefficients{};

		Float3 Evaluate(const Float3& normal) const;
	};
} // namespace Swim::Render::Environment
