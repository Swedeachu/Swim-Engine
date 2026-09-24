#include "Engine/Systems/Renderer/ForwardPlus/ForwardPlusReference.h"
#include "Engine/Systems/Renderer/ClusteredLighting/ClusterReference.h"
#include "Engine/Systems/Renderer/Lights/LightDesc.h"
#include "Engine/Systems/Renderer/Lights/LightMath.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Swim::Render
{
	ForwardViewRecord BuildForwardViewRecord(
		const ForwardPlusView& view, std::uint32_t materialCount, std::uint32_t prefilteredMipCount, bool hasEnvironment, bool hasShadows)
	{
		const auto finite = [](std::span<const float> values)
		{
			return std::all_of(values.begin(), values.end(),
				[](float value)
				{
					return std::isfinite(value);
				});
		};
		const auto& f = view.CameraForward;
		const float length = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
		if (!finite(view.ViewProjection) || !finite(view.CameraPosition) || !finite(view.CameraForward) || !finite(view.Ambient) ||
			!(length > 1.0e-12f) || !std::isfinite(length) || !(view.EnvironmentIntensity >= 0.0f) ||
			!std::isfinite(view.EnvironmentIntensity) || !std::isfinite(view.EnvironmentRotation) ||
			std::any_of(view.Ambient.begin(), view.Ambient.end(),
				[](float value)
				{
					return value < 0.0f;
				}) ||
			(view.DebugMode != ForwardPlusDebugMode::None && view.DebugMode != ForwardPlusDebugMode::ClusterHeatmap))
		{
			throw std::invalid_argument("Forward+ view needs a finite matrix and camera, a nonzero forward vector, nonnegative ambient and "
										"intensity, and a known debug mode");
		}
		ForwardViewRecord record;
		std::copy(view.ViewProjection.begin(), view.ViewProjection.end(), record.ViewProjection);
		for (int c = 0; c < 3; ++c)
		{
			record.CameraPosition[c] = view.CameraPosition[c];
			record.CameraForward[c] = f[c] / length;
			record.Ambient[c] = view.Ambient[c];
		}
		record.EnvironmentIntensity = view.EnvironmentIntensity;
		record.EnvironmentRotation = view.EnvironmentRotation;
		record.MaterialCount = materialCount;
		record.PrefilteredMipCount = std::max(prefilteredMipCount, 1u);
		record.Flags = (hasEnvironment ? ForwardViewFlagEnvironment : 0u) | (hasShadows ? ForwardViewFlagShadows : 0u);
		record.DebugMode = static_cast<std::uint32_t>(view.DebugMode);
		return record;
	}
} // namespace Swim::Render

namespace Swim::Render::ForwardPlus
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

		// Column j of the linear part.
		Float3 Column(const float (&rows)[12], int j)
		{
			return { rows[j], rows[4 + j], rows[8 + j] };
		}
	} // namespace

	ForwardPlusBin MaterialBin(const StandardPbr::Parameters& parameters)
	{
		return (parameters.Flags & StandardPbr::FlagAlphaBlend) != 0 ? ForwardPlusBin::Transparent : ForwardPlusBin::Opaque;
	}

	Float3 TransformPoint(const float (&rows)[12], const Float3& p)
	{
		Float3 result{};
		for (int r = 0; r < 3; ++r)
		{
			result[r] = rows[r * 4] * p[0] + rows[r * 4 + 1] * p[1] + rows[r * 4 + 2] * p[2] + rows[r * 4 + 3];
		}
		return result;
	}

	Float3 TransformDirection(const float (&rows)[12], const Float3& d)
	{
		Float3 result{};
		for (int r = 0; r < 3; ++r)
		{
			result[r] = rows[r * 4] * d[0] + rows[r * 4 + 1] * d[1] + rows[r * 4 + 2] * d[2];
		}
		return result;
	}

	float Determinant(const float (&rows)[12])
	{
		return Dot(Column(rows, 0), Cross(Column(rows, 1), Column(rows, 2)));
	}

	Float3 TransformNormal(const float (&rows)[12], const Float3& n)
	{
		// cofactor(M) = det(M) * M^-T, whose columns are cross products of M's columns.
		const auto c0 = Column(rows, 0);
		const auto c1 = Column(rows, 1);
		const auto c2 = Column(rows, 2);
		const auto x = Cross(c1, c2);
		const auto y = Cross(c2, c0);
		const auto z = Cross(c0, c1);
		Float3 result{};
		for (int c = 0; c < 3; ++c)
		{
			result[c] = x[c] * n[0] + y[c] * n[1] + z[c] * n[2];
		}
		// det * M^-T n flips with a negative determinant; undo that so the normal keeps its side.
		if (Determinant(rows) < 0.0f)
		{
			result = { -result[0], -result[1], -result[2] };
		}
		return result;
	}

	StandardPbr::Frame BuildFrame(const Float3& normal, const Float4& tangent, bool rasterFrontFacing, bool mirrored)
	{
		StandardPbr::Frame frame;
		frame.Normal = Normalize(normal);
		const Float3 t{ tangent[0], tangent[1], tangent[2] };
		const float along = Dot(frame.Normal, t);
		frame.Tangent =
			Normalize({ t[0] - frame.Normal[0] * along + 1.0e-20f, t[1] - frame.Normal[1] * along, t[2] - frame.Normal[2] * along });
		const auto b = Cross(frame.Normal, frame.Tangent);
		const float sign = tangent[3] < 0.0f ? -1.0f : 1.0f;
		frame.Bitangent = { b[0] * sign, b[1] * sign, b[2] * sign };
		frame.FrontFacing = rasterFrontFacing != mirrored;
		return frame;
	}

	bool CullsFace(const StandardPbr::Parameters& parameters, bool frontFacing)
	{
		return !frontFacing && (parameters.Flags & StandardPbr::FlagDoubleSided) == 0;
	}

	float SortDepth(const GpuInstanceRecord& instance, const GpuTransformRecord& transform, const ForwardViewRecord& view)
	{
		const auto center =
			TransformPoint(transform.Current, { instance.LocalCenter[0], instance.LocalCenter[1], instance.LocalCenter[2] });
		return (center[0] - view.CameraPosition[0]) * view.CameraForward[0] + (center[1] - view.CameraPosition[1]) * view.CameraForward[1] +
			(center[2] - view.CameraPosition[2]) * view.CameraForward[2];
	}

	bool SortsBefore(const ForwardSortEntry& a, const ForwardSortEntry& b)
	{
		if (a.Depth != b.Depth)
		{
			return a.Depth > b.Depth;
		}
		if (a.InstanceRow != b.InstanceRow)
		{
			return a.InstanceRow < b.InstanceRow;
		}
		return a.SubmeshRow < b.SubmeshRow;
	}

	std::vector<std::uint32_t> SortTransparentDraws(std::span<const GpuDrawRecord> records, std::uint32_t firstSlot,
		std::span<const GpuInstanceRecord> instances, std::span<const GpuTransformRecord> transforms, const ForwardViewRecord& view)
	{
		std::vector<ForwardSortEntry> entries;
		entries.reserve(records.size());
		for (std::size_t i = 0; i < records.size(); ++i)
		{
			const auto& instance = instances[records[i].InstanceRow];
			const float depth = SortDepth(instance, transforms[instance.TransformIndex], view);
			entries.push_back({ depth, records[i].InstanceRow, records[i].SubmeshRow, firstSlot + static_cast<std::uint32_t>(i) });
		}
		std::stable_sort(entries.begin(), entries.end(), SortsBefore);
		std::vector<std::uint32_t> order;
		order.reserve(entries.size());
		for (const auto& entry : entries)
		{
			order.push_back(entry.Slot);
		}
		return order;
	}

	float ViewDepth(const ClusterGridRecord& grid, const Float3& world)
	{
		const auto& row = grid.ViewRows[2];
		return -(row[0] * world[0] + row[1] * world[1] + row[2] * world[2] + row[3]);
	}

	float CameraDepth(const ForwardViewRecord& view, const Float3& world)
	{
		return (world[0] - view.CameraPosition[0]) * view.CameraForward[0] + (world[1] - view.CameraPosition[1]) * view.CameraForward[1] +
			(world[2] - view.CameraPosition[2]) * view.CameraForward[2];
	}

	float LightShadow(const LightingInputs& inputs, const ForwardViewRecord& view, const GpuLightRecord& light, const Float3& position,
		const Float3& normal, const Float3& toLight)
	{
		if ((view.Flags & ForwardViewFlagShadows) == 0 || !inputs.Shadows || light.ShadowIndex == GpuLightNoShadow ||
			(light.Flags & static_cast<std::uint32_t>(LightFlags::CastsShadows)) == 0)
		{
			return 1.0f;
		}
		return Shadows::ShadowFactor(*inputs.Shadows, light.ShadowIndex, position, normal, toLight, CameraDepth(view, position));
	}

	Float4 Shade(const LightingInputs& inputs, const ForwardViewRecord& view, const StandardPbr::ResolvedSurface& surface,
		const Float3& position, float pixelX, float pixelY)
	{
		const auto toCamera =
			Normalize({ view.CameraPosition[0] - position[0], view.CameraPosition[1] - position[1], view.CameraPosition[2] - position[2] });
		const StandardPbr::Surface brdfSurface{ surface.BaseColor, surface.Metallic, surface.PerceptualRoughness };
		Float3 color{ 0, 0, 0 };
		const auto add = [&](const GpuLightRecord& light)
		{
			const auto sample = Lights::EvaluateLight(light, position);
			const auto brdf = StandardPbr::EvaluateBrdf(brdfSurface, surface.Normal, toCamera, sample.Direction);
			const float shadow = LightShadow(inputs, view, light, position, surface.Normal, sample.Direction);
			for (int c = 0; c < 3; ++c)
			{
				color[c] += brdf[c] * sample.Radiance[c] * shadow;
			}
		};
		for (std::uint32_t i = 0; i < inputs.Header.DirectionalCount; ++i)
		{
			add(inputs.Lights[i]);
		}
		if (inputs.Grid)
		{
			const auto& record = inputs.Records[ClusterIndexFor(*inputs.Grid, pixelX, pixelY, ViewDepth(*inputs.Grid, position))];
			for (std::uint32_t i = 0; i < record.Count; ++i)
			{
				add(inputs.Lights[inputs.Header.FirstLocalRow + inputs.Indices[record.Offset + i]]);
			}
		}
		else
		{
			for (std::uint32_t i = 0; i < inputs.Header.LocalCount; ++i)
			{
				add(inputs.Lights[inputs.Header.FirstLocalRow + i]);
			}
		}
		Float3 ibl{ 0, 0, 0 };
		if ((view.Flags & ForwardViewFlagEnvironment) != 0 && inputs.Environment)
		{
			const auto terms = inputs.Environment->Lookup(surface, toCamera, { view.EnvironmentIntensity, view.EnvironmentRotation });
			ibl = StandardPbr::EvaluateEnvironment(surface, toCamera, terms);
		}
		Float4 result{};
		for (int c = 0; c < 3; ++c)
		{
			result[c] = color[c] + view.Ambient[c] * surface.BaseColor[c] * surface.Occlusion + ibl[c] + surface.Emissive[c];
		}
		result[3] = surface.Alpha;
		return result;
	}

	Float4 DebugColor(const ClusterGridRecord& grid, std::span<const ClusterRecord> records, float pixelX, float pixelY, float viewDepth)
	{
		const auto& record = records[ClusterIndexFor(grid, pixelX, pixelY, viewDepth)];
		const auto color = Clustering::HeatmapColor(record.Count, record.RawCount, grid.Limits[2]);
		return color[3] > 0.0f ? color : Float4{ 0, 0, 0, 1 };
	}

	Float4 Over(const Float4& source, const Float4& destination)
	{
		const float a = source[3];
		return { source[0] * a + destination[0] * (1.0f - a), source[1] * a + destination[1] * (1.0f - a),
			source[2] * a + destination[2] * (1.0f - a), a + destination[3] * (1.0f - a) };
	}
} // namespace Swim::Render::ForwardPlus
