// Image-based lighting math (critical-path item 61). Mirrors
// Engine/Systems/Renderer/Environment/EnvironmentMath.cpp line for line: the
// Vulkan cube-face table, exact texel solid angles, Hammersley points, GGX
// importance sampling, the split-sum BRDF integral, the prefilter's source lod and
// order-2 spherical harmonics. Directions are world space, +Y up.
#ifndef SWIM_ENVIRONMENT_COMMON_SLANG
#define SWIM_ENVIRONMENT_COMMON_SLANG
#include "Materials/StandardPbr.slang"

static const float EnvironmentPi = 3.14159265358979323846;

float3 EnvironmentCubeFaceDirection(uint face, float s, float t)
{
	switch (face)
	{
	case 0: return float3(1.0, -t, -s);
	case 1: return float3(-1.0, -t, s);
	case 2: return float3(s, 1.0, t);
	case 3: return float3(s, -1.0, -t);
	case 4: return float3(s, -t, 1.0);
	default: return float3(-s, -t, -1.0);
	}
}

float3 EnvironmentCubeTexelDirection(uint face, uint x, uint y, uint size)
{
	const float s = 2.0 * (float(x) + 0.5) / float(size) - 1.0;
	const float t = 2.0 * (float(y) + 0.5) / float(size) - 1.0;
	return normalize(EnvironmentCubeFaceDirection(face, s, t));
}

float EnvironmentAreaElement(float x, float y)
{
	return atan2(x * y, sqrt(x * x + y * y + 1.0));
}

float EnvironmentCubeTexelSolidAngle(uint x, uint y, uint size)
{
	const float step = 2.0 / float(size);
	const float x0 = float(x) * step - 1.0;
	const float y0 = float(y) * step - 1.0;
	const float x1 = x0 + step;
	const float y1 = y0 + step;
	return EnvironmentAreaElement(x0, y0) - EnvironmentAreaElement(x0, y1) - EnvironmentAreaElement(x1, y0) + EnvironmentAreaElement(x1, y1);
}

float2 EnvironmentHammersley(uint index, uint count)
{
	return float2(float(index) / float(count), float(reversebits(index)) * 2.3283064365386963e-10);
}

float3 EnvironmentImportanceSampleGgx(float2 xi, float alpha)
{
	const float phi = 2.0 * EnvironmentPi * xi.x;
	const float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (alpha * alpha - 1.0) * xi.y));
	const float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
	return float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
}

float3 EnvironmentTangentToWorld(float3 value, float3 normal)
{
	const float3 up = abs(normal.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
	const float3 tangent = normalize(cross(up, normal));
	const float3 bitangent = cross(normal, tangent);
	return tangent * value.x + bitangent * value.y + normal * value.z;
}

float2 EnvironmentIntegrateBrdf(float nDotVInput, float perceptualRoughness, uint sampleCount)
{
	const float nDotV = clamp(nDotVInput, 1.0e-4, 1.0);
	const float roughness = clamp(perceptualRoughness, StandardPbrMinPerceptualRoughness, 1.0);
	const float alpha = roughness * roughness;
	const float3 view = float3(sqrt(1.0 - nDotV * nDotV), 0.0, nDotV);
	float a = 0.0;
	float b = 0.0;
	for (uint i = 0; i < sampleCount; ++i)
	{
		const float3 halfVector = EnvironmentImportanceSampleGgx(EnvironmentHammersley(i, sampleCount), alpha);
		const float vDotH = dot(view, halfVector);
		const float3 light = 2.0 * vDotH * halfVector - view;
		const float nDotL = light.z;
		const float nDotH = halfVector.z;
		if (nDotL > 0.0 && nDotH > 0.0 && vDotH > 0.0)
		{
			const float visibility = StandardPbrVisibilitySmithGgxCorrelated(nDotV, nDotL, alpha);
			const float weight = 4.0 * visibility * nDotL * vDotH / nDotH;
			const float fresnel = pow(1.0 - vDotH, 5.0);
			a += (1.0 - fresnel) * weight;
			b += fresnel * weight;
		}
	}
	return float2(a / float(sampleCount), b / float(sampleCount));
}

float EnvironmentPrefilterSourceLod(float nDotH, float alpha, uint sampleCount, uint sourceSize, uint sourceMipCount)
{
	const float pdf = StandardPbrDistributionGgx(nDotH, alpha) * 0.25;
	const float sampleSolidAngle = 1.0 / (float(sampleCount) * pdf + 1.0e-6);
	const float texelSolidAngle = 4.0 * EnvironmentPi / (6.0 * float(sourceSize) * float(sourceSize));
	const float lod = 0.5 * log2(sampleSolidAngle / texelSolidAngle) + 1.0;
	return clamp(lod, 0.0, float(sourceMipCount - 1));
}

float EnvironmentPrefilterLodForRoughness(float perceptualRoughness, uint mipCount)
{
	return clamp(perceptualRoughness, StandardPbrMinPerceptualRoughness, 1.0) * float(mipCount - 1);
}

float3 EnvironmentRotateLookup(float3 d, float rotation)
{
	const float c = cos(rotation);
	const float s = sin(rotation);
	return float3(c * d.x - s * d.z, d.y, s * d.x + c * d.z);
}

void EnvironmentShBasis(float3 d, out float basis[9])
{
	basis[0] = 0.282094792;
	basis[1] = 0.488602512 * d.y;
	basis[2] = 0.488602512 * d.z;
	basis[3] = 0.488602512 * d.x;
	basis[4] = 1.092548431 * d.x * d.y;
	basis[5] = 1.092548431 * d.y * d.z;
	basis[6] = 0.315391565 * (3.0 * d.z * d.z - 1.0);
	basis[7] = 1.092548431 * d.x * d.z;
	basis[8] = 0.546274215 * (d.x * d.x - d.y * d.y);
}

float EnvironmentShIrradianceScale(uint coefficient)
{
	return coefficient == 0 ? 1.0 : (coefficient < 4 ? 2.0 / 3.0 : 0.25);
}

#endif
