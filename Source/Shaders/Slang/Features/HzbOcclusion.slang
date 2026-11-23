// Occlusion tests against the main view's hierarchical depth (RenderFeatureOcclusion), for
// render features that cull in RenderFeature::CullGeometry. The view is the occlusion's
// row-major, reverse-Z ViewProjection (clip = dot(rows[i], p)); the pyramid's mip i is depth
// level i + 1. Mirrors the engine's own late-phase test (GpuScene/GpuVisibility.slang).
#ifndef SWIM_HZB_OCCLUSION_SLANG
#define SWIM_HZB_OCCLUSION_SLANG

struct HzbView
{
	float4 Rows[4]; // ViewProjection, row-major.
	uint Width;		// Depth size (RenderFeatureOcclusion::Width / Height).
	uint Height;
	uint MipCount; // 0: no HZB this frame, nothing is occluded.
	uint Reserved;
};

// True when the world-space box is entirely behind the depth this frame's prepass wrote.
// Boxes crossing the near plane or leaving the screen are never occluded.
bool HzbOccluded(Texture2D<float> hzb, HzbView view, float3 boundsMin, float3 boundsMax)
{
	if (view.MipCount == 0 || view.Width == 0 || view.Height == 0)
	{
		return false;
	}
	const float width = float(view.Width);
	const float height = float(view.Height);
	float minX = 3.0e38;
	float minY = 3.0e38;
	float maxX = -3.0e38;
	float maxY = -3.0e38;
	float nearest = -3.0e38;
	for (uint i = 0; i < 8; ++i)
	{
		const float4 p = float4((i & 1u) != 0 ? boundsMax.x : boundsMin.x, (i & 2u) != 0 ? boundsMax.y : boundsMin.y,
			(i & 4u) != 0 ? boundsMax.z : boundsMin.z, 1.0);
		const float cx = dot(view.Rows[0], p);
		const float cy = dot(view.Rows[1], p);
		const float cz = dot(view.Rows[2], p);
		const float cw = dot(view.Rows[3], p);
		if (cw <= 1.0e-5)
		{
			return false;
		}
		const float z = cz / cw;
		if (z > 1.0)
		{
			return false;
		}
		const float sx = (cx / cw * 0.5 + 0.5) * width;
		const float sy = (0.5 - cy / cw * 0.5) * height;
		minX = min(minX, sx);
		maxX = max(maxX, sx);
		minY = min(minY, sy);
		maxY = max(maxY, sy);
		nearest = max(nearest, z);
	}
	if (maxX < 0.0 || maxY < 0.0 || minX >= width || minY >= height)
	{
		return false;
	}
	const uint x0 = uint(floor(max(minX, 0.0)));
	const uint y0 = uint(floor(max(minY, 0.0)));
	const uint x1 = uint(floor(min(maxX, width - 1.0)));
	const uint y1 = uint(floor(min(maxY, height - 1.0)));
	uint level = 1;
	while (level < view.MipCount && (((x1 >> level) - (x0 >> level)) > 1u || ((y1 >> level) - (y0 >> level)) > 1u))
	{
		++level;
	}
	float farthest = 3.0e38;
	for (uint ty = y0 >> level; ty <= (y1 >> level); ++ty)
	{
		for (uint tx = x0 >> level; tx <= (x1 >> level); ++tx)
		{
			farthest = min(farthest, hzb.Load(int3(int(tx), int(ty), int(level - 1))));
		}
	}
	return farthest > nearest;
}

bool HzbOccludedSphere(Texture2D<float> hzb, HzbView view, float3 center, float radius)
{
	return HzbOccluded(hzb, view, center - radius, center + radius);
}

#endif
