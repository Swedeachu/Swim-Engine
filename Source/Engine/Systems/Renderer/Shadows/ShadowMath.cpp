#include "Engine/Systems/Renderer/Shadows/ShadowMath.h"
#include "Engine/Systems/Renderer/Visibility/RenderViewDesc.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Swim::Render::Shadows
{
	namespace
	{
		float Dot(const Float3& a, const Float3& b)
		{
			return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
		}

		Float3 Cross(const Float3& a, const Float3& b)
		{
			return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
		}

		Float3 Normalize(const Float3& v)
		{
			const float length = std::sqrt(Dot(v, v));
			return length > 0.0f ? Float3{ v[0] / length, v[1] / length, v[2] / length } : Float3{ 0, 0, 0 };
		}

		// World point of a view-space point under an affine row-major view: R^T (v - t).
		Float3 ViewToWorld(const Matrix& view, const Float3& v)
		{
			const Float3 d{ v[0] - view[3], v[1] - view[7], v[2] - view[11] };
			Float3 world{};
			for (int c = 0; c < 3; ++c)
			{
				world[c] = view[c] * d[0] + view[4 + c] * d[1] + view[8 + c] * d[2];
			}
			return world;
		}
	} // namespace

	std::vector<float> CascadeSplits(float nearDepth, float farDepth, std::uint32_t count, float lambda)
	{
		if (!(nearDepth > 0.0f) || !(farDepth > nearDepth) || count == 0 || count > MaxShadowCascades || !(lambda >= 0.0f) || lambda > 1.0f)
		{
			throw std::invalid_argument("Cascade splits need 0 < near < far, 1..MaxShadowCascades cascades and lambda in [0, 1]");
		}
		std::vector<float> splits(count + 1);
		for (std::uint32_t i = 0; i <= count; ++i)
		{
			const float t = float(i) / float(count);
			const float logarithmic = nearDepth * std::pow(farDepth / nearDepth, t);
			const float uniform = nearDepth + (farDepth - nearDepth) * t;
			splits[i] = lambda * logarithmic + (1.0f - lambda) * uniform;
		}
		splits.front() = nearDepth;
		splits.back() = farDepth;
		return splits;
	}

	BoundingSphere CascadeSphere(const ShadowCamera& camera, float sliceNear, float sliceFar)
	{
		const float tanHalf = std::tan(camera.VerticalFov * 0.5f);
		const float k2 = tanHalf * tanHalf * (1.0f + camera.Aspect * camera.Aspect); // (corner distance / depth)^2.
		float center = (sliceFar + sliceNear) * (1.0f + k2) * 0.5f;
		float radius = 0.0f;
		if (center >= sliceFar)
		{
			center = sliceFar;
			radius = sliceFar * std::sqrt(k2);
		}
		else
		{
			radius = std::sqrt((sliceFar - center) * (sliceFar - center) + sliceFar * sliceFar * k2);
		}
		return { ViewToWorld(camera.View, { 0.0f, 0.0f, -center }), radius };
	}

	Matrix LookAlong(const Float3& eye, const Float3& forward)
	{
		const auto f = Normalize(forward);
		const Float3 up = std::abs(f[1]) > 0.999f ? Float3{ 0, 0, 1 } : Float3{ 0, 1, 0 };
		const auto r = Normalize(Cross(f, up));
		const auto u = Cross(r, f);
		return { r[0], r[1], r[2], -Dot(r, eye), u[0], u[1], u[2], -Dot(u, eye), -f[0], -f[1], -f[2], Dot(f, eye), 0, 0, 0, 1 };
	}

	std::vector<CascadeView> ComputeCascades(
		const ShadowCamera& camera, const Float3& lightDirection, const CascadeSettings& settings, std::uint32_t resolution)
	{
		if (resolution == 0 || !(settings.CasterExtension >= 0.0f) || !(Dot(lightDirection, lightDirection) > 0.0f))
		{
			throw std::invalid_argument("Cascades need a resolution, a light direction and a nonnegative caster extension");
		}
		const auto splits = CascadeSplits(camera.Near, settings.MaxDistance, settings.Count, settings.SplitLambda);
		const auto lightView = LookAlong({ 0, 0, 0 }, lightDirection);
		const Float3 right{ lightView[0], lightView[1], lightView[2] };
		const Float3 up{ lightView[4], lightView[5], lightView[6] };
		const auto forward = Normalize(lightDirection);
		std::vector<CascadeView> cascades;
		for (std::uint32_t i = 0; i < settings.Count; ++i)
		{
			CascadeView cascade;
			cascade.Near = splits[i];
			cascade.Far = splits[i + 1];
			cascade.Sphere = CascadeSphere(camera, splits[i], splits[i + 1]);
			const float r = cascade.Sphere.Radius;
			cascade.TexelWorldSize = 2.0f * r / float(resolution);
			// Snap the center to whole texels in light space.
			const float texel = cascade.TexelWorldSize;
			const float x = std::round(Dot(right, cascade.Sphere.Center) / texel) * texel;
			const float y = std::round(Dot(up, cascade.Sphere.Center) / texel) * texel;
			const float t = Dot(forward, cascade.Sphere.Center);
			const auto projection = OrthographicReverseZRowMajor(x - r, x + r, y - r, y + r, t - r - settings.CasterExtension, t + r);
			cascade.ViewProjection = MultiplyRowMajor(projection, lightView);
			cascades.push_back(cascade);
		}
		return cascades;
	}

	float SpotShadowFov(const GpuLightRecord& light)
	{
		const float cosOuter = light.SpotScale > 0.0f ? std::clamp(-light.SpotOffset / light.SpotScale, -1.0f, 1.0f) : 0.0f;
		return std::min(2.0f * std::acos(cosOuter), MaxSpotShadowFov);
	}

	Matrix SpotShadowViewProjection(const GpuLightRecord& light, float nearPlane)
	{
		const Float3 position{ light.Position[0], light.Position[1], light.Position[2] };
		const Float3 direction{ light.Direction[0], light.Direction[1], light.Direction[2] };
		return MultiplyRowMajor(PerspectiveReverseZRowMajor(SpotShadowFov(light), 1.0f, nearPlane), LookAlong(position, direction));
	}

	std::array<Matrix, 6> PointShadowViewProjections(const Float3& position, float nearPlane)
	{
		static constexpr std::array<Float3, 6> directions{ { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 },
			{ 0, 0, -1 } } };
		const auto projection = PerspectiveReverseZRowMajor(PointShadowFov, 1.0f, nearPlane);
		std::array<Matrix, 6> faces{};
		for (std::size_t face = 0; face < 6; ++face)
		{
			faces[face] = MultiplyRowMajor(projection, LookAlong(position, directions[face]));
		}
		return faces;
	}

	std::uint32_t PointShadowFace(const Float3& d)
	{
		const float ax = std::abs(d[0]);
		const float ay = std::abs(d[1]);
		const float az = std::abs(d[2]);
		if (ax >= ay && ax >= az)
		{
			return d[0] >= 0.0f ? 0u : 1u;
		}
		if (ay >= az)
		{
			return d[1] >= 0.0f ? 2u : 3u;
		}
		return d[2] >= 0.0f ? 4u : 5u;
	}

	std::optional<ShadowProjection> ProjectToShadowView(const GpuShadowView& view, const Float3& p)
	{
		const auto& m = view.ViewProjection;
		float clip[4]{};
		for (int r = 0; r < 4; ++r)
		{
			clip[r] = m[r * 4] * p[0] + m[r * 4 + 1] * p[1] + m[r * 4 + 2] * p[2] + m[r * 4 + 3];
		}
		if (!(clip[3] > 1.0e-6f))
		{
			return std::nullopt;
		}
		const float x = clip[0] / clip[3];
		const float y = clip[1] / clip[3];
		const float z = clip[2] / clip[3];
		if (!(std::abs(x) <= 1.0f) || !(std::abs(y) <= 1.0f) || !(z >= 0.0f) || !(z <= 1.0f))
		{
			return std::nullopt;
		}
		return ShadowProjection{ view.AtlasRect[0] + (x * 0.5f + 0.5f) * view.AtlasRect[2],
			view.AtlasRect[1] + (0.5f - y * 0.5f) * view.AtlasRect[3], z };
	}

	std::optional<std::uint32_t> SelectShadowView(const GpuShadowRecord& record, const Float3& position, float cameraViewDepth)
	{
		switch (static_cast<ShadowKind>(record.Kind))
		{
		case ShadowKind::Directional:
			for (std::uint32_t i = 0; i < std::min(record.ViewCount, MaxShadowCascades); ++i)
			{
				if (cameraViewDepth < record.CascadeFar[i])
				{
					return record.FirstView + i;
				}
			}
			return std::nullopt;
		case ShadowKind::Spot:
			return record.ViewCount > 0 ? std::optional<std::uint32_t>(record.FirstView) : std::nullopt;
		case ShadowKind::Point:
			if (record.ViewCount < 6)
			{
				return std::nullopt;
			}
			return record.FirstView +
				PointShadowFace({ position[0] - record.LightPosition[0], position[1] - record.LightPosition[1],
					position[2] - record.LightPosition[2] });
		default:
			return std::nullopt;
		}
	}

	float SampleShadowView(const ShadowAtlasImage& atlas, const GpuShadowRecord& record, const GpuShadowView& view, const Float3& shifted)
	{
		const auto projected = ProjectToShadowView(view, shifted);
		if (!projected)
		{
			return 1.0f;
		}
		const int x0 = int(view.AtlasRect[0]);
		const int y0 = int(view.AtlasRect[1]);
		const int x1 = x0 + int(view.AtlasRect[2]) - 1;
		const int y1 = y0 + int(view.AtlasRect[3]) - 1;
		const float receiver = projected->Depth + record.DepthBias;
		const int radius = int(record.PcfRadius);
		if (radius == 0)
		{
			const auto tx = std::uint32_t(std::clamp(int(std::floor(projected->PixelX)), x0, x1));
			const auto ty = std::uint32_t(std::clamp(int(std::floor(projected->PixelY)), y0, y1));
			return receiver >= atlas.At(tx, ty) ? 1.0f : 0.0f;
		}
		const float ux = projected->PixelX - 0.5f;
		const float uy = projected->PixelY - 0.5f;
		const float bx = std::floor(ux);
		const float by = std::floor(uy);
		const float fx = ux - bx;
		const float fy = uy - by;
		float lit = 0.0f;
		int hits = 0;
		for (int dy = -radius; dy <= radius + 1; ++dy)
		{
			const float wy = dy == -radius ? 1.0f - fy : (dy == radius + 1 ? fy : 1.0f);
			const auto ty = std::uint32_t(std::clamp(int(by) + dy, y0, y1));
			for (int dx = -radius; dx <= radius + 1; ++dx)
			{
				const float wx = dx == -radius ? 1.0f - fx : (dx == radius + 1 ? fx : 1.0f);
				const auto tx = std::uint32_t(std::clamp(int(bx) + dx, x0, x1));
				if (receiver >= atlas.At(tx, ty))
				{
					lit += wx * wy;
					++hits;
				}
			}
		}
		// Exactly 0 or 1 when every tap agrees (the weights need not sum to exactly 1 in float).
		const int taps = (2 * radius + 2) * (2 * radius + 2);
		if (hits == 0 || hits == taps)
		{
			return hits == 0 ? 0.0f : 1.0f;
		}
		const float width = float(2 * radius + 1);
		return std::clamp(lit / (width * width), 0.0f, 1.0f);
	}

	float ShadowFactor(const ShadowSampleInputs& inputs, std::uint32_t shadowIndex, const Float3& position, const Float3& normal,
		const Float3& toLight, float cameraViewDepth)
	{
		if (!inputs.Atlas || shadowIndex >= inputs.Records.size())
		{
			return 1.0f;
		}
		const auto& record = inputs.Records[shadowIndex];
		const auto viewIndex = SelectShadowView(record, position, cameraViewDepth);
		if (!viewIndex || *viewIndex >= inputs.Views.size())
		{
			return 1.0f;
		}
		const float nDotL = std::clamp(Dot(normal, toLight), 0.0f, 1.0f);
		// Wider PCF kernels compare texels farther from the receiver: scale the offset.
		const float bias = (record.NormalBias + record.SlopeBias * (1.0f - nDotL)) * float(record.PcfRadius + 1u);
		const auto offsetBy = [&](float offset)
		{
			return Float3{ position[0] + normal[0] * offset, position[1] + normal[1] * offset, position[2] + normal[2] * offset };
		};
		if (static_cast<ShadowKind>(record.Kind) == ShadowKind::Directional)
		{
			const std::uint32_t cascade = *viewIndex - record.FirstView;
			const float farDepth = record.CascadeFar[cascade];
			const float nearDepth = cascade == 0 ? 0.0f : record.CascadeFar[cascade - 1];
			const float band = record.CascadeBlend * (farDepth - nearDepth);
			const float t = band > 0.0f ? std::clamp((cameraViewDepth - (farDepth - band)) / band, 0.0f, 1.0f) : 0.0f;
			const auto& view = inputs.Views[*viewIndex];
			const float lit = SampleShadowView(*inputs.Atlas, record, view, offsetBy(view.TexelWorldSize * bias));
			if (t <= 0.0f)
			{
				return lit;
			}
			const std::uint32_t count = std::min(record.ViewCount, MaxShadowCascades);
			float next = 1.0f;
			if (cascade + 1 < count && *viewIndex + 1 < inputs.Views.size())
			{
				const auto& nextView = inputs.Views[*viewIndex + 1];
				next = SampleShadowView(*inputs.Atlas, record, nextView, offsetBy(nextView.TexelWorldSize * bias));
			}
			return lit + (next - lit) * t;
		}
		const auto& view = inputs.Views[*viewIndex];
		const Float3 fromLight{ position[0] - record.LightPosition[0], position[1] - record.LightPosition[1],
			position[2] - record.LightPosition[2] };
		const float distance = view.Perspective != 0 ? std::sqrt(Dot(fromLight, fromLight)) : 1.0f;
		const Float3 shifted = offsetBy(view.TexelWorldSize * distance * bias);
		// The offset can cross into another cube face (all faces share TexelWorldSize):
		// select again with the shifted point.
		const auto shiftedIndex = SelectShadowView(record, shifted, cameraViewDepth);
		if (!shiftedIndex || *shiftedIndex >= inputs.Views.size())
		{
			return 1.0f;
		}
		return SampleShadowView(*inputs.Atlas, record, inputs.Views[*shiftedIndex], shifted);
	}
} // namespace Swim::Render::Shadows
