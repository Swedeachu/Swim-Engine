#pragma once
#include "Engine/Systems/Renderer/Geometry/GpuMeshMetadata.h"
#include "Engine/Systems/Renderer/GpuScene/GpuInstanceRecord.h"
#include "Engine/Systems/Renderer/GpuScene/GpuTransformRecord.h"
#include "Engine/Systems/Renderer/GpuScene/RenderBounds.h"
#include "Engine/Systems/Renderer/Visibility/DepthConvention.h"
#include "Engine/Systems/Renderer/Visibility/GpuViewRecord.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

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

	inline DepthConvention ViewDepthConvention(const GpuViewRecord& view)
	{
		return (view.Flags & std::uint32_t(GpuViewFlags::ForwardDepth)) != 0 ? DepthConvention::Forward : DepthConvention::ReverseZ;
	}

	// Size of the HZB's depth level 0 (the depth buffer) and its mip count; mip i is level i + 1.
	struct HzbDims
	{
		std::uint32_t Width = 0;
		std::uint32_t Height = 0;
		std::uint32_t MipCount = 0;
	};

	// Occlusion test against an HZB built from this frame's depth with the same view.
	// The sphere's world AABB is projected; objects crossing the near plane (or behind
	// the camera), entirely off screen, or bounded by nothing are never occluded. The
	// footprint picks the finest level where it spans at most 2x2 texels; the object
	// is occluded when its nearest depth is strictly farther than the farthest depth
	// stored there. fetch(mip, x, y) returns HZB mip `mip` (level mip + 1).
	template <typename Fetch> bool OccludedByHzb(const GpuViewRecord& view, const Sphere& sphere, const HzbDims& hzb, Fetch&& fetch)
	{
		if (sphere.Unbounded || hzb.MipCount == 0 || hzb.Width == 0 || hzb.Height == 0)
		{
			return false;
		}
		const DepthConvention convention = ViewDepthConvention(view);
		const bool reverse = convention == DepthConvention::ReverseZ;
		const float* m = view.ViewProjection;
		const float width = float(hzb.Width);
		const float height = float(hzb.Height);
		float minX = 3.0e38f;
		float minY = 3.0e38f;
		float maxX = -3.0e38f;
		float maxY = -3.0e38f;
		float nearest = reverse ? -3.0e38f : 3.0e38f;
		for (std::uint32_t i = 0; i < 8; ++i)
		{
			const float px = sphere.Center[0] + ((i & 1u) != 0 ? sphere.Radius : -sphere.Radius);
			const float py = sphere.Center[1] + ((i & 2u) != 0 ? sphere.Radius : -sphere.Radius);
			const float pz = sphere.Center[2] + ((i & 4u) != 0 ? sphere.Radius : -sphere.Radius);
			const float cx = m[0] * px + m[1] * py + m[2] * pz + m[3];
			const float cy = m[4] * px + m[5] * py + m[6] * pz + m[7];
			const float cz = m[8] * px + m[9] * py + m[10] * pz + m[11];
			const float cw = m[12] * px + m[13] * py + m[14] * pz + m[15];
			if (cw <= 1.0e-5f)
			{
				return false;
			}
			const float z = cz / cw;
			if (reverse ? z > 1.0f : z < 0.0f)
			{
				return false; // In front of the near plane.
			}
			const float sx = (cx / cw * 0.5f + 0.5f) * width;
			const float sy = (0.5f - cy / cw * 0.5f) * height; // +Y-up NDC: row 0 is the top.
			minX = std::min(minX, sx);
			maxX = std::max(maxX, sx);
			minY = std::min(minY, sy);
			maxY = std::max(maxY, sy);
			nearest = reverse ? std::max(nearest, z) : std::min(nearest, z);
		}
		if (maxX < 0.0f || maxY < 0.0f || minX >= width || minY >= height)
		{
			return false;
		}
		const std::uint32_t x0 = std::uint32_t(std::floor(std::max(minX, 0.0f)));
		const std::uint32_t y0 = std::uint32_t(std::floor(std::max(minY, 0.0f)));
		const std::uint32_t x1 = std::uint32_t(std::floor(std::min(maxX, width - 1.0f)));
		const std::uint32_t y1 = std::uint32_t(std::floor(std::min(maxY, height - 1.0f)));
		std::uint32_t level = 1;
		while (level < hzb.MipCount && (((x1 >> level) - (x0 >> level)) > 1u || ((y1 >> level) - (y0 >> level)) > 1u))
		{
			++level;
		}
		float farthest = reverse ? 3.0e38f : -3.0e38f;
		for (std::uint32_t ty = y0 >> level; ty <= (y1 >> level); ++ty)
		{
			for (std::uint32_t tx = x0 >> level; tx <= (x1 >> level); ++tx)
			{
				const float value = fetch(level - 1, tx, ty);
				farthest = reverse ? std::min(farthest, value) : std::max(farthest, value);
			}
		}
		return reverse ? farthest > nearest : farthest < nearest;
	}
} // namespace Swim::Render::VisibilityMath
