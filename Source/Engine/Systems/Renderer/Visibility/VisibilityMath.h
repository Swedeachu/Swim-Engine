#pragma once
#include "Engine/Systems/Renderer/Geometry/GpuMeshMetadata.h"
#include "Engine/Systems/Renderer/GpuScene/GpuInstanceRecord.h"
#include "Engine/Systems/Renderer/GpuScene/GpuTransformRecord.h"
#include "Engine/Systems/Renderer/GpuScene/RenderBounds.h"
#include "Engine/Systems/Renderer/Visibility/GpuViewRecord.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Swim::Render::VisibilityMath
{
	// The CPU statement of the rules GpuVisibility.slang implements; the CPU
	// reference and the tests use these, the shader mirrors them line for line.

	struct Sphere
	{
		std::array<float, 3> Center{};
		float Radius = 0.0f;
		bool Unbounded = false;
	};

	inline Sphere WorldSphere(const GpuInstanceRecord& instance, const GpuTransformRecord& transform)
	{
		Sphere sphere;
		const float* m = transform.Current;
		for (int r = 0; r < 3; ++r)
		{
			sphere.Center[r] = m[r * 4] * instance.LocalCenter[0] + m[r * 4 + 1] * instance.LocalCenter[1] +
				m[r * 4 + 2] * instance.LocalCenter[2] + m[r * 4 + 3];
		}
		const float* e = instance.LocalExtents;
		sphere.Unbounded = std::max({ e[0], e[1], e[2] }) >= RenderBounds::Unbounded * 0.5f;
		float scale = 0.0f;
		for (int c = 0; c < 3; ++c)
		{
			scale = std::max(scale, std::sqrt(m[c] * m[c] + m[4 + c] * m[4 + c] + m[8 + c] * m[8 + c]));
		}
		sphere.Radius = std::sqrt(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]) * scale;
		return sphere;
	}

	inline bool InsideFrustum(const GpuViewRecord& view, const Sphere& sphere)
	{
		if (sphere.Unbounded || (view.Flags & std::uint32_t(GpuViewFlags::DisableFrustumCulling)) != 0)
		{
			return true;
		}
		for (int p = 0; p < 6; ++p)
		{
			const float* plane = view.FrustumPlanes + p * 4;
			if (plane[0] * sphere.Center[0] + plane[1] * sphere.Center[1] + plane[2] * sphere.Center[2] + plane[3] < -sphere.Radius)
			{
				return false;
			}
		}
		return true;
	}

	// LodCount 0 behaves as one LOD covering the mesh's submesh range.
	inline std::uint32_t LodCount(const GpuMeshMetadata& mesh)
	{
		return std::clamp(mesh.LodCount, 1u, GpuMeshMetadata::MaxLods);
	}

	// Pixels of projected error per world unit of LOD error at this distance.
	inline float ErrorScale(const GpuViewRecord& view, const Sphere& sphere)
	{
		const float dx = sphere.Center[0] - view.CameraPosition[0];
		const float dy = sphere.Center[1] - view.CameraPosition[1];
		const float dz = sphere.Center[2] - view.CameraPosition[2];
		const float distance = std::max(std::sqrt(dx * dx + dy * dy + dz * dz) - sphere.Radius, 1.0e-4f);
		return view.LodScale / distance;
	}

	// Coarsest LOD whose projected error stays within threshold (errors increase with the index).
	inline std::uint32_t SelectLod(const GpuMeshMetadata& mesh, float errorScale, float threshold)
	{
		std::uint32_t lod = 0;
		for (std::uint32_t i = 1; i < LodCount(mesh); ++i)
		{
			if (mesh.Lods[i].Error * errorScale > threshold)
			{
				break;
			}
			lod = i;
		}
		return lod;
	}

	// Hysteresis: move coarser only below threshold * (1 - h), finer only above threshold * (1 + h).
	inline std::uint32_t SelectLodWithHistory(
		const GpuMeshMetadata& mesh, float errorScale, float threshold, float hysteresis, bool hasHistory, std::uint32_t previous)
	{
		const auto desired = SelectLod(mesh, errorScale, threshold);
		if (!hasHistory || previous >= LodCount(mesh))
		{
			return desired;
		}
		if (desired > previous)
		{
			return std::max(previous, SelectLod(mesh, errorScale, threshold * (1.0f - hysteresis)));
		}
		if (desired < previous)
		{
			return std::min(previous, SelectLod(mesh, errorScale, threshold * (1.0f + hysteresis)));
		}
		return previous;
	}

	// Absolute submesh range of the chosen LOD.
	inline std::pair<std::uint32_t, std::uint32_t> LodSubmeshes(const GpuMeshMetadata& mesh, std::uint32_t lod)
	{
		if (mesh.LodCount == 0)
		{
			return { mesh.FirstSubmesh, mesh.SubmeshCount };
		}
		return { mesh.Lods[lod].FirstSubmesh, mesh.Lods[lod].SubmeshCount };
	}
} // namespace Swim::Render::VisibilityMath
