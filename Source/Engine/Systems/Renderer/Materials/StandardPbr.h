#pragma once
#include <array>
#include <cstdint>
#include <optional>

namespace Swim::Render::StandardPbr
{
	// The CPU definition of the standard metallic-roughness material (critical-path
	// item 60). Shaders/Slang/Materials/StandardPbr.slang mirrors every function
	// line for line; the tests and native smokes compare the two.
	//
	// Model (glTF 2.0 metallic-roughness): Lambert diffuse, GGX distribution,
	// height-correlated Smith visibility, Schlick Fresnel with F0 = 0.04 for
	// dielectrics and the base color for metals. All colors are linear; sRGB
	// textures (base color, emissive) are decoded before use.
	using Float3 = std::array<float, 3>;

	inline constexpr float Pi = 3.14159265358979323846f;
	// Perceptual roughness is clamped here to keep the GGX lobe finite.
	inline constexpr float MinPerceptualRoughness = 0.045f;
	inline constexpr float DielectricF0 = 0.04f;

	float Dot(const Float3& a, const Float3& b);
	Float3 Normalize(const Float3& value);

	float SrgbToLinear(float encoded);
	float LinearToSrgb(float linear);

	float DistributionGgx(float nDotH, float alpha);
	float VisibilitySmithGgxCorrelated(float nDotV, float nDotL, float alpha);
	Float3 FresnelSchlick(const Float3& f0, float vDotH);

	struct Surface
	{
		Float3 BaseColor{ 1, 1, 1 }; // Linear.
		float Metallic = 1.0f;
		float PerceptualRoughness = 1.0f;
	};

	// BRDF times N.L for one light direction (unit vectors pointing away from the
	// surface). Zero when the light is below the surface.
	Float3 EvaluateBrdf(const Surface& surface, const Float3& normal, const Float3& view, const Float3& light);

	// Values a shader samples for one pixel, already decoded: base color and
	// emissive as linear color, the rest as stored (metallic-roughness uses G for
	// roughness and B for metallic; the tangent-space normal is in [-1, 1]).
	struct Texels
	{
		std::array<float, 4> BaseColor{ 1, 1, 1, 1 };
		Float3 MetallicRoughness{ 1, 1, 1 };
		std::optional<Float3> TangentNormal; // Empty: no normal texture.
		float Occlusion = 1.0f;
		Float3 Emissive{ 1, 1, 1 };
	};

	// The StandardMaterialParameters record, decoded (see
	// Shaders/Slang/Materials/StandardMaterialParameters.slang).
	struct Parameters
	{
		std::array<float, 4> BaseColorFactor{ 1, 1, 1, 1 };
		Float3 EmissiveFactor{ 0, 0, 0 };
		float MetallicFactor = 1.0f;
		float RoughnessFactor = 1.0f;
		float NormalScale = 1.0f;
		float OcclusionStrength = 1.0f;
		float AlphaCutoff = 0.5f;
		std::uint32_t Flags = 0;
	};

	inline constexpr std::uint32_t FlagAlphaMask = 1u << 0;
	inline constexpr std::uint32_t FlagDoubleSided = 1u << 1;

	struct Lighting
	{
		Float3 View{ 0, 0, 1 };			  // Toward the camera.
		Float3 LightDirection{ 0, 0, 1 }; // Toward the light.
		Float3 LightRadiance{ 1, 1, 1 };
		Float3 Ambient{ 0, 0, 0 }; // Constant ambient, scaled by base color and occlusion.
	};

	struct Frame
	{
		Float3 Normal{ 0, 0, 1 }; // Geometric normal (front face).
		Float3 Tangent{ 1, 0, 0 };
		Float3 Bitangent{ 0, 1, 0 };
		bool FrontFacing = true;
	};

	// Final linear color (rgb) and alpha of one pixel, or empty when alpha masking
	// discards it. Back faces of double-sided materials use the flipped normal.
	std::optional<std::array<float, 4>> Shade(
		const Parameters& parameters, const Texels& texels, const Frame& frame, const Lighting& lighting);
} // namespace Swim::Render::StandardPbr
