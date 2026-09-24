#include "Engine/Systems/Renderer/ScreenSpace/ScreenSpaceReference.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace Swim::Render
{
	namespace
	{
		bool Finite(float value)
		{
			return std::isfinite(value);
		}

		template <std::size_t N> bool AllFinite(const std::array<float, N>& values)
		{
			return std::all_of(values.begin(), values.end(), Finite);
		}

		bool NonNegative(const std::array<float, 3>& color)
		{
			return AllFinite(color) && color[0] >= 0.0f && color[1] >= 0.0f && color[2] >= 0.0f;
		}
	} // namespace

	void ValidateScreenSpaceSettings(const ScreenSpaceSettings& settings)
	{
		const auto& ao = settings.AmbientOcclusion;
		if (!Finite(ao.Radius) || ao.Radius <= 0.0f)
		{
			throw std::invalid_argument("AO radius must be finite and positive");
		}
		if (!Finite(ao.Falloff) || ao.Falloff <= 0.0f || ao.Falloff > 1.0f)
		{
			throw std::invalid_argument("AO falloff must be in (0, 1]");
		}
		if (!Finite(ao.Power) || ao.Power <= 0.0f || ao.Power > 8.0f)
		{
			throw std::invalid_argument("AO power must be in (0, 8]");
		}
		if (!Finite(ao.MaxRadiusPixels) || ao.MaxRadiusPixels < 1.0f || ao.MaxRadiusPixels > 256.0f)
		{
			throw std::invalid_argument("AO max radius must be 1 .. 256 pixels");
		}
		if (ao.SliceCount < 1 || ao.SliceCount > MaxAoSlices || ao.StepCount < 1 || ao.StepCount > MaxAoSteps)
		{
			throw std::invalid_argument("AO slice count must be 1 .. MaxAoSlices and step count 1 .. MaxAoSteps");
		}
		if (!Finite(ao.BlurDepthTolerance) || ao.BlurDepthTolerance <= 0.0f || ao.BlurDepthTolerance > 1.0f)
		{
			throw std::invalid_argument("AO blur depth tolerance must be in (0, 1]");
		}
		const auto& fog = settings.Fog;
		if (!Finite(fog.Density) || fog.Density < 0.0f || !Finite(fog.HeightFalloff) || fog.HeightFalloff < 0.0f || !Finite(fog.BaseHeight))
		{
			throw std::invalid_argument("fog density and height falloff must be finite and non-negative, and the base height finite");
		}
		if (!NonNegative(fog.Color) || !NonNegative(fog.SunColor))
		{
			throw std::invalid_argument("fog colors must be finite and non-negative");
		}
		const float sunLength = std::sqrt(fog.SunDirection[0] * fog.SunDirection[0] + fog.SunDirection[1] * fog.SunDirection[1] +
			fog.SunDirection[2] * fog.SunDirection[2]);
		if (!AllFinite(fog.SunDirection) || !(sunLength > 1.0e-6f))
		{
			throw std::invalid_argument("fog sun direction must be finite and non-zero");
		}
		if (!Finite(fog.Anisotropy) || fog.Anisotropy < -0.95f || fog.Anisotropy > 0.95f)
		{
			throw std::invalid_argument("fog anisotropy must be in [-0.95, 0.95]");
		}
		if (!Finite(fog.StartDistance) || fog.StartDistance < 0.0f || !Finite(fog.MaxDistance) || fog.MaxDistance <= fog.StartDistance)
		{
			throw std::invalid_argument("fog start distance must be >= 0 and below a finite max distance");
		}
		const auto& ssr = settings.Reflections;
		if (!Finite(ssr.MaxDistance) || ssr.MaxDistance <= 0.0f || !Finite(ssr.Thickness) || ssr.Thickness <= 0.0f)
		{
			throw std::invalid_argument("reflection max distance and thickness must be finite and positive");
		}
		if (!Finite(ssr.Stride) || ssr.Stride < 1.0f || ssr.Stride > 64.0f)
		{
			throw std::invalid_argument("reflection stride must be 1 .. 64 pixels");
		}
		if (ssr.MaxSteps < 1 || ssr.MaxSteps > MaxReflectionSteps || ssr.RefineSteps > MaxReflectionRefineSteps)
		{
			throw std::invalid_argument("reflection steps must be 1 .. MaxReflectionSteps and refine steps 0 .. MaxReflectionRefineSteps");
		}
		if (!Finite(ssr.MaxRoughness) || ssr.MaxRoughness <= 0.0f || ssr.MaxRoughness > 1.0f || !Finite(ssr.RoughnessFade) ||
			ssr.RoughnessFade <= 0.0f || ssr.RoughnessFade > ssr.MaxRoughness)
		{
			throw std::invalid_argument("reflection max roughness must be in (0, 1] and its fade in (0, max roughness]");
		}
		if (!Finite(ssr.EdgeFade) || ssr.EdgeFade <= 0.0f || ssr.EdgeFade > 0.5f || !Finite(ssr.DistanceFade) || ssr.DistanceFade <= 0.0f ||
			ssr.DistanceFade > 1.0f)
		{
			throw std::invalid_argument("reflection edge fade must be in (0, 0.5] and distance fade in (0, 1]");
		}
	}

	GpuScreenSpaceParams BuildScreenSpaceParams(const ScreenSpaceSettings& settings, const ScreenSpaceView& view, std::uint32_t width,
		std::uint32_t height, std::uint32_t noiseFrame)
	{
		ValidateScreenSpaceSettings(settings);
		if (!width || !height)
		{
			throw std::invalid_argument("screen-space effects need a non-empty viewport");
		}
		if (!AllFinite(view.Jitter))
		{
			throw std::invalid_argument("screen-space jitter must be finite");
		}
		const auto& p = view.Projection;
		if (p[12] != 0.0f || p[13] != 0.0f || p[14] != -1.0f || p[15] != 0.0f || !(p[0] > 0.0f))
		{
			throw std::invalid_argument("screen-space effects need a perspective projection (row 3 = 0, 0, -1, 0)");
		}
		const auto inverseProjection = ScreenSpace::Inverse(view.Projection);
		const auto inverseView = ScreenSpace::Inverse(view.View);
		if (!inverseProjection || !inverseView)
		{
			throw std::invalid_argument("screen-space view and projection must be finite and invertible");
		}
		GpuScreenSpaceParams params;
		std::copy(inverseProjection->begin(), inverseProjection->end(), params.InverseProjection);
		std::copy(view.View.begin(), view.View.begin() + 12, params.ViewRows);
		std::copy(inverseView->begin(), inverseView->begin() + 12, params.InverseViewRows);
		params.Jitter[0] = view.Jitter[0];
		params.Jitter[1] = view.Jitter[1];
		params.Width = width;
		params.Height = height;
		const auto& ao = settings.AmbientOcclusion;
		params.AoRadius = ao.Radius;
		params.AoFalloff = ao.Falloff;
		params.AoPower = ao.Power;
		params.AoMaxRadiusPixels = ao.MaxRadiusPixels;
		params.AoRadiusToPixels = p[0] * float(width) * 0.5f;
		params.AoBlurDepthTolerance = ao.BlurDepthTolerance;
		params.AoSliceCount = ao.SliceCount;
		params.AoStepCount = ao.StepCount;
		params.NoiseFrame = noiseFrame % 64u;
		params.AoEnabled = ao.Enabled ? 1u : 0u;
		const auto& fog = settings.Fog;
		params.FogEnabled = fog.Enabled ? 1u : 0u;
		const float sunLength = std::sqrt(fog.SunDirection[0] * fog.SunDirection[0] + fog.SunDirection[1] * fog.SunDirection[1] +
			fog.SunDirection[2] * fog.SunDirection[2]);
		for (int c = 0; c < 3; ++c)
		{
			params.FogColor[c] = fog.Color[c];
			params.FogSunColor[c] = fog.SunColor[c];
			params.FogSunDirection[c] = fog.SunDirection[c] / sunLength;
		}
		params.FogDensity = fog.Density;
		params.FogHeightFalloff = fog.HeightFalloff;
		params.FogBaseHeight = fog.BaseHeight;
		params.FogAnisotropy = fog.Anisotropy;
		params.FogStartDistance = fog.StartDistance;
		params.FogMaxDistance = fog.MaxDistance;
		const auto& ssr = settings.Reflections;
		std::copy(view.Projection.begin(), view.Projection.end(), params.Projection);
		params.SsrEnabled = ssr.Enabled ? 1u : 0u;
		params.SsrMaxDistance = ssr.MaxDistance;
		params.SsrThickness = ssr.Thickness;
		params.SsrStride = ssr.Stride;
		params.SsrMaxRoughness = ssr.MaxRoughness;
		params.SsrRoughnessFade = ssr.RoughnessFade;
		params.SsrEdgeFade = ssr.EdgeFade;
		params.SsrDistanceFade = ssr.DistanceFade;
		params.SsrMaxSteps = ssr.MaxSteps;
		params.SsrRefineSteps = ssr.RefineSteps;
		// Near plane: where the projected depth z_ndc = (p10 z + p11) / -z reaches 1.
		const float nearZ = -p[11] / (p[10] + 1.0f);
		if (ssr.Enabled && (!Finite(nearZ) || !(nearZ < 0.0f)))
		{
			throw std::invalid_argument("screen-space reflections need a projection whose near plane is in front of the camera");
		}
		params.SsrNearZ = Finite(nearZ) && nearZ < 0.0f ? nearZ : -1.0e-3f;
		return params;
	}
} // namespace Swim::Render

namespace Swim::Render::ScreenSpace
{
	namespace
	{
		constexpr float Pi = std::numbers::pi_v<float>;

		float Dot(const Float3& a, const Float3& b)
		{
			return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
		}

		float Length(const Float3& v)
		{
			return std::sqrt(Dot(v, v));
		}

		Float3 Scale(const Float3& v, float s)
		{
			return { v[0] * s, v[1] * s, v[2] * s };
		}

		Float3 Subtract(const Float3& a, const Float3& b)
		{
			return { a[0] - b[0], a[1] - b[1], a[2] - b[2] };
		}

		Float3 Cross(const Float3& a, const Float3& b)
		{
			return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
		}

		float Fraction(float v)
		{
			return v - std::floor(v);
		}

		// Rows 0-2 of a row-major affine transform applied to a point.
		Float3 TransformPoint(const float (&rows)[12], const Float3& p)
		{
			Float3 result{};
			for (int r = 0; r < 3; ++r)
			{
				result[r] = rows[r * 4] * p[0] + rows[r * 4 + 1] * p[1] + rows[r * 4 + 2] * p[2] + rows[r * 4 + 3];
			}
			return result;
		}

		std::optional<Float3> ViewPositionAt(const GpuScreenSpaceParams& params, const ScalarImage& depth, std::uint32_t x, std::uint32_t y)
		{
			return ViewPosition(params, float(x) + 0.5f, float(y) + 0.5f, depth.At(x, y));
		}
	} // namespace

	std::optional<std::array<float, 16>> Inverse(const std::array<float, 16>& m)
	{
		if (!std::all_of(m.begin(), m.end(),
				[](float v)
				{
					return std::isfinite(v);
				}))
		{
			return std::nullopt;
		}
		// Cofactor expansion in double precision.
		std::array<double, 16> a{};
		for (int i = 0; i < 16; ++i)
		{
			a[i] = m[i];
		}
		std::array<double, 16> inv{};
		inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] + a[13] * a[6] * a[11] -
			a[13] * a[7] * a[10];
		inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] - a[12] * a[6] * a[11] +
			a[12] * a[7] * a[10];
		inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] + a[12] * a[5] * a[11] -
			a[12] * a[7] * a[9];
		inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] - a[12] * a[5] * a[10] +
			a[12] * a[6] * a[9];
		inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] - a[9] * a[3] * a[14] - a[13] * a[2] * a[11] +
			a[13] * a[3] * a[10];
		inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] + a[12] * a[2] * a[11] -
			a[12] * a[3] * a[10];
		inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] - a[12] * a[1] * a[11] +
			a[12] * a[3] * a[9];
		inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] + a[12] * a[1] * a[10] -
			a[12] * a[2] * a[9];
		inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] + a[13] * a[2] * a[7] -
			a[13] * a[3] * a[6];
		inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] - a[12] * a[2] * a[7] +
			a[12] * a[3] * a[6];
		inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] + a[12] * a[1] * a[7] -
			a[12] * a[3] * a[5];
		inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] - a[12] * a[1] * a[6] +
			a[12] * a[2] * a[5];
		inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] - a[9] * a[2] * a[7] +
			a[9] * a[3] * a[6];
		inv[7] =
			a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] + a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
		inv[11] =
			-a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] - a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
		inv[15] =
			a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] + a[8] * a[1] * a[6] - a[8] * a[2] * a[5];
		const double det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
		double scale = 0.0;
		for (const double v : a)
		{
			scale = std::max(scale, std::abs(v));
		}
		if (!std::isfinite(det) || std::abs(det) <= 1.0e-12 * scale * scale * scale * scale)
		{
			return std::nullopt;
		}
		std::array<float, 16> result{};
		for (int i = 0; i < 16; ++i)
		{
			result[i] = float(inv[i] / det);
		}
		return result;
	}

	float InterleavedGradientNoise(float x, float y, std::uint32_t frame)
	{
		const float shift = 5.588238f * float(frame % 64u);
		return Fraction(52.9829189f * Fraction(0.06711056f * (x + shift) + 0.00583715f * (y + shift)));
	}

	std::optional<Float3> ViewPosition(const GpuScreenSpaceParams& params, float px, float py, float depth)
	{
		if (!(depth > 0.0f))
		{
			return std::nullopt;
		}
		const float ndcX = 2.0f * px / float(params.Width) - 1.0f - params.Jitter[0];
		const float ndcY = 1.0f - 2.0f * py / float(params.Height) - params.Jitter[1];
		const std::array<float, 4> ndc{ ndcX, ndcY, depth, 1.0f };
		std::array<float, 4> clip{};
		for (int r = 0; r < 4; ++r)
		{
			clip[r] = params.InverseProjection[r * 4] * ndc[0] + params.InverseProjection[r * 4 + 1] * ndc[1] +
				params.InverseProjection[r * 4 + 2] * ndc[2] + params.InverseProjection[r * 4 + 3] * ndc[3];
		}
		if (!(std::abs(clip[3]) > 1.0e-20f))
		{
			return std::nullopt;
		}
		const float inverseW = 1.0f / clip[3];
		return Float3{ clip[0] * inverseW, clip[1] * inverseW, clip[2] * inverseW };
	}

	std::optional<Float3> ViewNormal(const GpuScreenSpaceParams& params, const Float3& worldNormal)
	{
		Float3 n{};
		for (int r = 0; r < 3; ++r)
		{
			n[r] = params.ViewRows[r * 4] * worldNormal[0] + params.ViewRows[r * 4 + 1] * worldNormal[1] +
				params.ViewRows[r * 4 + 2] * worldNormal[2];
		}
		const float length = Length(n);
		if (!(length > 1.0e-6f))
		{
			return std::nullopt;
		}
		return Scale(n, 1.0f / length);
	}

	float GtaoTexel(
		const GpuScreenSpaceParams& params, const ScalarImage& depth, const ColorImage& normal, std::uint32_t x, std::uint32_t y)
	{
		const auto center = ViewPositionAt(params, depth, x, y);
		const auto& encoded = normal.At(x, y);
		const auto n3 = center ? ViewNormal(params, { encoded[0], encoded[1], encoded[2] }) : std::nullopt;
		if (!center || !n3 || !(-(*center)[2] > 0.0f))
		{
			return 1.0f;
		}
		const Float3 p = *center;
		const Float3 normalV = *n3;
		const Float3 v = Scale(p, -1.0f / Length(p));
		const float radiusPixels = std::min(params.AoRadius * params.AoRadiusToPixels / -p[2], params.AoMaxRadiusPixels);
		if (radiusPixels < 1.0f)
		{
			return 1.0f;
		}
		const float noise = InterleavedGradientNoise(float(x), float(y), params.NoiseFrame);
		const float stepNoise = InterleavedGradientNoise(float(x) + 17.0f, float(y) + 29.0f, params.NoiseFrame);
		const float falloffRange = params.AoRadius * params.AoFalloff;
		const std::uint32_t slices = params.AoSliceCount;
		const std::uint32_t steps = params.AoStepCount;
		float visibility = 0.0f;
		for (std::uint32_t slice = 0; slice < slices; ++slice)
		{
			const float phi = (float(slice) + noise) * Pi / float(slices);
			const float dx = std::cos(phi);
			const float dy = std::sin(phi);
			const Float3 direction{ dx, -dy, 0.0f }; // Screen y points down, view y up.
			const Float3 ortho = Subtract(direction, Scale(v, Dot(direction, v)));
			const Float3 axisRaw = Cross(direction, v);
			const Float3 axis = Scale(axisRaw, 1.0f / Length(axisRaw));
			const Float3 projected = Subtract(normalV, Scale(axis, Dot(normalV, axis)));
			const float projectedLength = Length(projected);
			if (!(projectedLength > 1.0e-6f))
			{
				continue;
			}
			const float cosN = std::clamp(Dot(projected, v) / projectedLength, -1.0f, 1.0f);
			const float n = (Dot(projected, ortho) < 0.0f ? -1.0f : 1.0f) * std::acos(cosN);
			const float lowPositive = std::cos(n + 0.5f * Pi);
			const float lowNegative = std::cos(n - 0.5f * Pi);
			float horizonPositive = lowPositive;
			float horizonNegative = lowNegative;
			for (std::uint32_t step = 0; step < steps; ++step)
			{
				const float t = (float(step) + stepNoise) / float(steps) * radiusPixels;
				for (int side = 0; side < 2; ++side)
				{
					const float sign = side == 0 ? 1.0f : -1.0f;
					const float sx = std::floor(float(x) + 0.5f + sign * dx * t);
					const float sy = std::floor(float(y) + 0.5f + sign * dy * t);
					if (sx < 0.0f || sy < 0.0f || sx >= float(params.Width) || sy >= float(params.Height))
					{
						continue;
					}
					const auto ix = static_cast<std::uint32_t>(sx);
					const auto iy = static_cast<std::uint32_t>(sy);
					if (ix == x && iy == y)
					{
						continue;
					}
					const auto samplePosition = ViewPositionAt(params, depth, ix, iy);
					if (!samplePosition)
					{
						continue;
					}
					const Float3 delta = Subtract(*samplePosition, p);
					const float distance = Length(delta);
					if (!(distance > 1.0e-6f))
					{
						continue;
					}
					const float cosSample = Dot(delta, v) / distance;
					const float weight = std::clamp((params.AoRadius - distance) / falloffRange, 0.0f, 1.0f);
					if (side == 0)
					{
						horizonPositive = std::max(horizonPositive, lowPositive + (cosSample - lowPositive) * weight);
					}
					else
					{
						horizonNegative = std::max(horizonNegative, lowNegative + (cosSample - lowNegative) * weight);
					}
				}
			}
			float h1 = std::acos(std::clamp(horizonPositive, -1.0f, 1.0f));
			float h0 = -std::acos(std::clamp(horizonNegative, -1.0f, 1.0f));
			h1 = n + std::min(h1 - n, 0.5f * Pi);
			h0 = n + std::max(h0 - n, -0.5f * Pi);
			const float sinN = std::sin(n);
			const float arc1 = (cosN + 2.0f * h1 * sinN - std::cos(2.0f * h1 - n)) * 0.25f;
			const float arc0 = (cosN + 2.0f * h0 * sinN - std::cos(2.0f * h0 - n)) * 0.25f;
			visibility += projectedLength * (arc0 + arc1);
		}
		// Unclamped: single slices of open surfaces can exceed 1, and clamping before the blur
		// would bias them down.
		return std::max(visibility / float(slices), 0.0f);
	}

	ScalarImage Gtao(const GpuScreenSpaceParams& params, const ScalarImage& depth, const ColorImage& normal)
	{
		ScalarImage result(depth.Width, depth.Height);
		for (std::uint32_t y = 0; y < depth.Height; ++y)
		{
			for (std::uint32_t x = 0; x < depth.Width; ++x)
			{
				result.At(x, y) = GtaoTexel(params, depth, normal, x, y);
			}
		}
		return result;
	}

	float BlurTexel(const GpuScreenSpaceParams& params, const ScalarImage& ao, const ScalarImage& depth, std::uint32_t x, std::uint32_t y)
	{
		const auto finish = [&](float value)
		{
			return std::pow(std::clamp(value, 0.0f, 1.0f), params.AoPower);
		};
		const auto center = ViewPositionAt(params, depth, x, y);
		if (!center)
		{
			return finish(ao.At(x, y));
		}
		const float centerDepth = -(*center)[2];
		const float tolerance = params.AoBlurDepthTolerance * std::abs(centerDepth);
		float sum = 0.0f;
		float weights = 0.0f;
		for (int dy = -2; dy <= 2; ++dy)
		{
			for (int dx = -2; dx <= 2; ++dx)
			{
				const int sx = int(x) + dx;
				const int sy = int(y) + dy;
				if (sx < 0 || sy < 0 || sx >= int(params.Width) || sy >= int(params.Height))
				{
					continue;
				}
				const auto position = ViewPositionAt(params, depth, std::uint32_t(sx), std::uint32_t(sy));
				if (!position)
				{
					continue;
				}
				const float weight = std::max(0.0f, 1.0f - std::abs(-(*position)[2] - centerDepth) / tolerance);
				sum += weight * ao.At(std::uint32_t(sx), std::uint32_t(sy));
				weights += weight;
			}
		}
		return finish(weights > 0.0f ? sum / weights : ao.At(x, y));
	}

	ScalarImage Blur(const GpuScreenSpaceParams& params, const ScalarImage& ao, const ScalarImage& depth)
	{
		ScalarImage result(ao.Width, ao.Height);
		for (std::uint32_t y = 0; y < ao.Height; ++y)
		{
			for (std::uint32_t x = 0; x < ao.Width; ++x)
			{
				result.At(x, y) = BlurTexel(params, ao, depth, x, y);
			}
		}
		return result;
	}

	float FogOpticalDepth(const GpuScreenSpaceParams& params, const Float3& camera, const Float3& direction, float distance)
	{
		if (!(params.FogDensity > 0.0f) || !(distance > params.FogStartDistance))
		{
			return 0.0f;
		}
		const float startHeight = camera[1] + direction[1] * params.FogStartDistance;
		const float length = distance - params.FogStartDistance;
		const float density =
			params.FogDensity * std::exp(std::clamp(-params.FogHeightFalloff * (startHeight - params.FogBaseHeight), -80.0f, 80.0f));
		const float k = params.FogHeightFalloff * direction[1] * length;
		// (1 - e^-k) / k, with its series near 0.
		const float shape = std::abs(k) < 1.0e-4f ? 1.0f - 0.5f * k + k * k / 6.0f : (1.0f - std::exp(std::clamp(-k, -80.0f, 80.0f))) / k;
		return density * length * shape;
	}

	float HenyeyGreenstein(float g, float cosTheta)
	{
		const float denominator = std::max(1.0f + g * g - 2.0f * g * cosTheta, 1.0e-6f);
		return (1.0f - g * g) / (denominator * std::sqrt(denominator));
	}

	FogSample EvaluateFog(const GpuScreenSpaceParams& params, const Float3& camera, const Float3& direction, float distance)
	{
		FogSample sample;
		sample.Transmittance = std::exp(-FogOpticalDepth(params, camera, direction, distance));
		const Float3 sun{ params.FogSunDirection[0], params.FogSunDirection[1], params.FogSunDirection[2] };
		const float phase = HenyeyGreenstein(params.FogAnisotropy, -Dot(sun, direction));
		for (int c = 0; c < 3; ++c)
		{
			sample.Inscatter[c] = params.FogColor[c] + params.FogSunColor[c] * phase;
		}
		return sample;
	}

	ScreenPoint ProjectToScreen(const GpuScreenSpaceParams& params, const Float3& view)
	{
		std::array<float, 4> clip{};
		for (int r = 0; r < 4; ++r)
		{
			clip[r] = params.Projection[r * 4] * view[0] + params.Projection[r * 4 + 1] * view[1] + params.Projection[r * 4 + 2] * view[2] +
				params.Projection[r * 4 + 3];
		}
		const float inverseW = 1.0f / clip[3];
		ScreenPoint point;
		point.X = (clip[0] * inverseW + 1.0f + params.Jitter[0]) * 0.5f * float(params.Width);
		point.Y = (1.0f - clip[1] * inverseW - params.Jitter[1]) * 0.5f * float(params.Height);
		point.W = clip[3];
		return point;
	}

	std::optional<ReflectionHit> TraceReflection(
		const GpuScreenSpaceParams& params, const ScalarImage& depth, const ColorImage& normal, std::uint32_t x, std::uint32_t y)
	{
		const auto center = ViewPositionAt(params, depth, x, y);
		const auto& encoded = normal.At(x, y);
		const auto n3 = center ? ViewNormal(params, { encoded[0], encoded[1], encoded[2] }) : std::nullopt;
		if (!center || !n3 || !(-(*center)[2] > 0.0f))
		{
			return std::nullopt;
		}
		const float roughness = encoded[3];
		if (!(roughness < params.SsrMaxRoughness))
		{
			return std::nullopt;
		}
		const float roughnessFade = std::clamp((params.SsrMaxRoughness - roughness) / params.SsrRoughnessFade, 0.0f, 1.0f);
		const Float3 p = *center;
		const Float3 n = *n3;
		const Float3 v = Scale(p, 1.0f / Length(p));
		const Float3 reflectedRaw = Subtract(v, Scale(n, 2.0f * Dot(v, n)));
		const Float3 r = Scale(reflectedRaw, 1.0f / Length(reflectedRaw));
		float rayLength = params.SsrMaxDistance;
		if (p[2] + r[2] * rayLength > params.SsrNearZ)
		{
			rayLength = (params.SsrNearZ - p[2]) / r[2]; // Clipped to the near plane.
		}
		if (!(rayLength > 0.0f))
		{
			return std::nullopt;
		}
		const Float3 end{ p[0] + r[0] * rayLength, p[1] + r[1] * rayLength, p[2] + r[2] * rayLength };
		const auto s0 = ProjectToScreen(params, p);
		const auto s1 = ProjectToScreen(params, end);
		const float k0 = 1.0f / s0.W;
		const float k1 = 1.0f / s1.W;
		const Float3 q0 = Scale(p, k0);
		const Float3 q1 = Scale(end, k1);
		const float dx = s1.X - s0.X;
		const float dy = s1.Y - s0.Y;
		const float pixelLength = std::sqrt(dx * dx + dy * dy);
		const auto steps = std::min(params.SsrMaxSteps, static_cast<std::uint32_t>(std::ceil(pixelLength / params.SsrStride)));
		const float jitter = InterleavedGradientNoise(float(x) + 23.0f, float(y) + 41.0f, params.NoiseFrame);
		const float width = float(params.Width);
		const float height = float(params.Height);

		struct Sample
		{
			bool Inside = false;
			std::uint32_t X = 0;
			std::uint32_t Y = 0;
			float RayDepth = 0.0f;
		};

		const auto sampleAt = [&](float t)
		{
			Sample sample;
			const float sx = s0.X + dx * t;
			const float sy = s0.Y + dy * t;
			if (!(sx >= 0.0f && sy >= 0.0f && sx < width && sy < height))
			{
				return sample;
			}
			sample.Inside = true;
			sample.X = static_cast<std::uint32_t>(std::floor(sx));
			sample.Y = static_cast<std::uint32_t>(std::floor(sy));
			sample.RayDepth = -(q0[2] + (q1[2] - q0[2]) * t) / (k0 + (k1 - k0) * t);
			return sample;
		};

		float previous = 0.0f;
		for (std::uint32_t i = 1; i <= steps; ++i)
		{
			const float t = std::min((float(i) + jitter) / float(steps), 1.0f);
			const auto sample = sampleAt(t);
			if (!sample.Inside)
			{
				return std::nullopt; // Left the screen.
			}
			const auto scene = (sample.X == x && sample.Y == y) ? std::nullopt : ViewPositionAt(params, depth, sample.X, sample.Y);
			if (!scene || !(sample.RayDepth >= -(*scene)[2] && sample.RayDepth <= -(*scene)[2] + params.SsrThickness))
			{
				previous = t;
				continue;
			}
			// Bisect (previous, t] toward the first sample behind the depth buffer.
			float lo = previous;
			float hi = t;
			for (std::uint32_t k = 0; k < params.SsrRefineSteps; ++k)
			{
				const float mid = 0.5f * (lo + hi);
				const auto probe = sampleAt(mid);
				const auto probeScene = probe.Inside ? ViewPositionAt(params, depth, probe.X, probe.Y) : std::nullopt;
				if (probeScene && probe.RayDepth >= -(*probeScene)[2])
				{
					hi = mid;
				}
				else
				{
					lo = mid;
				}
			}
			auto hit = sampleAt(hi);
			if (!hit.Inside || (hit.X == x && hit.Y == y) || !ViewPositionAt(params, depth, hit.X, hit.Y))
			{
				hit = sample;
				hi = t;
			}
			const auto& hitEncoded = normal.At(hit.X, hit.Y);
			const auto hitNormal = ViewNormal(params, { hitEncoded[0], hitEncoded[1], hitEncoded[2] });
			if (!hitNormal || Dot(*hitNormal, r) > 0.0f)
			{
				return std::nullopt; // A back face.
			}
			const float k = k0 + (k1 - k0) * hi;
			const Float3 point{ (q0[0] + (q1[0] - q0[0]) * hi) / k, (q0[1] + (q1[1] - q0[1]) * hi) / k,
				(q0[2] + (q1[2] - q0[2]) * hi) / k };
			const float travelled = Length(Subtract(point, p));
			const float u = (float(hit.X) + 0.5f) / width;
			const float w = (float(hit.Y) + 0.5f) / height;
			const float edge = std::min(std::min(u, 1.0f - u), std::min(w, 1.0f - w));
			const float edgeFade = std::clamp(edge / params.SsrEdgeFade, 0.0f, 1.0f);
			const float distanceFade =
				std::clamp((params.SsrMaxDistance - travelled) / (params.SsrMaxDistance * params.SsrDistanceFade), 0.0f, 1.0f);
			const float confidence = roughnessFade * edgeFade * distanceFade;
			if (!(confidence > 0.0f))
			{
				return std::nullopt;
			}
			return ReflectionHit{ hit.X, hit.Y, travelled, confidence };
		}
		return std::nullopt;
	}

	Float3 HitRadiance(const GpuScreenSpaceParams& params, const ColorImage& color, const ColorImage& indirect, const ScalarImage* ao,
		std::uint32_t x, std::uint32_t y)
	{
		const auto& c = color.At(x, y);
		Float3 radiance{ c[0], c[1], c[2] };
		if (params.AoEnabled != 0u && ao)
		{
			const float occlusion = ao->At(x, y);
			const auto& i = indirect.At(x, y);
			for (int k = 0; k < 3; ++k)
			{
				radiance[k] = std::max(radiance[k] - (1.0f - occlusion) * i[k], 0.0f);
			}
		}
		return radiance;
	}

	Float4 ReflectionTexel(const GpuScreenSpaceParams& params, const ScalarImage& depth, const ColorImage& normal, const ColorImage& color,
		const ColorImage& indirect, const ScalarImage* ao, std::uint32_t x, std::uint32_t y)
	{
		const auto hit = TraceReflection(params, depth, normal, x, y);
		if (!hit)
		{
			return { 0, 0, 0, 0 };
		}
		const auto radiance = HitRadiance(params, color, indirect, ao, hit->X, hit->Y);
		return { radiance[0], radiance[1], radiance[2], hit->Confidence };
	}

	ColorImage Reflections(const GpuScreenSpaceParams& params, const ScalarImage& depth, const ColorImage& normal, const ColorImage& color,
		const ColorImage& indirect, const ScalarImage* ao)
	{
		ColorImage result(depth.Width, depth.Height);
		for (std::uint32_t y = 0; y < depth.Height; ++y)
		{
			for (std::uint32_t x = 0; x < depth.Width; ++x)
			{
				result.At(x, y) = ReflectionTexel(params, depth, normal, color, indirect, ao, x, y);
			}
		}
		return result;
	}

	Float4 CompositeTexel(const GpuScreenSpaceParams& params, const Float4& color, const Float4& indirect, float ao,
		const ReflectionSample& reflection, float depth, std::uint32_t x, std::uint32_t y)
	{
		Float3 c{ color[0], color[1], color[2] };
		if (params.AoEnabled != 0u)
		{
			for (int i = 0; i < 3; ++i)
			{
				c[i] = std::max(c[i] - (1.0f - ao) * indirect[i], 0.0f);
			}
		}
		if (params.SsrEnabled != 0u && reflection.Reflection[3] > 0.0f)
		{
			const float weight = reflection.Reflection[3] * (params.AoEnabled != 0u ? ao : 1.0f);
			for (int i = 0; i < 3; ++i)
			{
				c[i] = std::max(c[i] + weight * (reflection.Reflectance[i] * reflection.Reflection[i] - reflection.Specular[i]), 0.0f);
			}
		}
		if (params.FogEnabled != 0u)
		{
			const Float3 camera{ params.InverseViewRows[3], params.InverseViewRows[7], params.InverseViewRows[11] };
			const float px = float(x) + 0.5f;
			const float py = float(y) + 0.5f;
			// The sky is fogged along its view ray (through the near plane) at the max distance.
			const auto view = ViewPosition(params, px, py, depth > 0.0f ? depth : 1.0f);
			if (view)
			{
				const Float3 delta = Subtract(TransformPoint(params.InverseViewRows, *view), camera);
				const float length = Length(delta);
				if (length > 1.0e-6f)
				{
					const Float3 direction = Scale(delta, 1.0f / length);
					const float distance = depth > 0.0f ? std::min(length, params.FogMaxDistance) : params.FogMaxDistance;
					const auto fog = EvaluateFog(params, camera, direction, distance);
					for (int i = 0; i < 3; ++i)
					{
						c[i] = c[i] * fog.Transmittance + fog.Inscatter[i] * (1.0f - fog.Transmittance);
					}
				}
			}
		}
		return { c[0], c[1], c[2], color[3] };
	}

	Float4 CompositeTexel(const GpuScreenSpaceParams& params, const Float4& color, const Float4& indirect, float ao, float depth,
		std::uint32_t x, std::uint32_t y)
	{
		return CompositeTexel(params, color, indirect, ao, ReflectionSample{}, depth, x, y);
	}

	ColorImage Composite(const GpuScreenSpaceParams& params, const ColorImage& color, const ColorImage& indirect, const ScalarImage* ao,
		const ScalarImage& depth, const ReflectionImages& reflections)
	{
		ColorImage result(color.Width, color.Height);
		for (std::uint32_t y = 0; y < color.Height; ++y)
		{
			for (std::uint32_t x = 0; x < color.Width; ++x)
			{
				ReflectionSample reflection;
				if (reflections.Reflection && reflections.Reflectance && reflections.Specular)
				{
					reflection = { reflections.Reflection->At(x, y), reflections.Reflectance->At(x, y), reflections.Specular->At(x, y) };
				}
				result.At(x, y) =
					CompositeTexel(params, color.At(x, y), indirect.At(x, y), ao ? ao->At(x, y) : 1.0f, reflection, depth.At(x, y), x, y);
			}
		}
		return result;
	}
} // namespace Swim::Render::ScreenSpace
