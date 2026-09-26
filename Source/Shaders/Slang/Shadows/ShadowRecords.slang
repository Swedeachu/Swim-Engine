// Shadow records and sampling (Phase 16, items 70-72). Mirrors
// Engine/Systems/Renderer/Shadows/ShadowRecords.h and ShadowMath.cpp line for line
// (SelectShadowView, ProjectToShadowView, ShadowFactor); ShaderCompiler.ShadowLayout
// compares the reflected layouts.
#ifndef SWIM_SHADOW_RECORDS_SLANG
#define SWIM_SHADOW_RECORDS_SLANG

static const uint ShadowKindNone = 0;
static const uint ShadowKindDirectional = 1;
static const uint ShadowKindSpot = 2;
static const uint ShadowKindPoint = 3;
static const uint ShadowMaxCascades = 4;
static const uint ShadowNoView = 0xffffffffu;

struct GpuShadowRecord
{
	uint Kind;
	uint FirstView;
	uint ViewCount;
	uint PcfRadius;
	float NormalBias;
	float SlopeBias;
	float DepthBias;
	float CascadeBlend;
	float4 CascadeFar;
	float3 LightPosition;
	float Reserved1;
};

struct GpuShadowView
{
	float4 ViewProjection[4]; // Row-major rows, reverse-Z.
	float4 AtlasRect;		  // Pixels: x, y, size, size.
	float TexelWorldSize;
	uint Perspective;
	float Reserved0;
	float Reserved1;
};

// Shadows::PointShadowFace: major axis, ties to X then Y.
uint ShadowPointFace(float3 d)
{
	const float3 a = abs(d);
	if (a.x >= a.y && a.x >= a.z)
	{
		return d.x >= 0.0 ? 0u : 1u;
	}
	if (a.y >= a.z)
	{
		return d.y >= 0.0 ? 2u : 3u;
	}
	return d.z >= 0.0 ? 4u : 5u;
}

// Shadows::SelectShadowView; ShadowNoView when unshadowed at this point.
uint ShadowSelectView(GpuShadowRecord record, float3 position, float cameraViewDepth)
{
	if (record.Kind == ShadowKindDirectional)
	{
		const uint count = min(record.ViewCount, ShadowMaxCascades);
		for (uint i = 0; i < count; ++i)
		{
			if (cameraViewDepth < record.CascadeFar[i])
			{
				return record.FirstView + i;
			}
		}
		return ShadowNoView;
	}
	if (record.Kind == ShadowKindSpot)
	{
		return record.ViewCount > 0 ? record.FirstView : ShadowNoView;
	}
	if (record.Kind == ShadowKindPoint)
	{
		return record.ViewCount >= 6 ? record.FirstView + ShadowPointFace(position - record.LightPosition) : ShadowNoView;
	}
	return ShadowNoView;
}

// Shadows::SampleShadowView: the filtered comparison of one view (1 = lit).
float ShadowSampleView(Texture2D<float> atlas, StructuredBuffer<GpuShadowView> views, uint viewIndex, GpuShadowRecord record,
	float3 shifted)
{
	const float4 rows[4] = views[viewIndex].ViewProjection;
	const float4 rect = views[viewIndex].AtlasRect;
	const float4 p = float4(shifted, 1.0);
	const float4 clip = float4(dot(rows[0], p), dot(rows[1], p), dot(rows[2], p), dot(rows[3], p));
	if (!(clip.w > 1.0e-6))
	{
		return 1.0;
	}
	const float3 ndc = clip.xyz / clip.w;
	if (!(abs(ndc.x) <= 1.0) || !(abs(ndc.y) <= 1.0) || !(ndc.z >= 0.0) || !(ndc.z <= 1.0))
	{
		return 1.0;
	}
	const float pixelX = rect.x + (ndc.x * 0.5 + 0.5) * rect.z;
	const float pixelY = rect.y + (0.5 - ndc.y * 0.5) * rect.w;
	const int x0 = int(rect.x);
	const int y0 = int(rect.y);
	const int x1 = x0 + int(rect.z) - 1;
	const int y1 = y0 + int(rect.w) - 1;
	const float receiver = ndc.z + record.DepthBias;
	const int radius = int(record.PcfRadius);
	if (radius == 0)
	{
		const int tx = clamp(int(floor(pixelX)), x0, x1);
		const int ty = clamp(int(floor(pixelY)), y0, y1);
		return receiver >= atlas.Load(int3(tx, ty, 0)) ? 1.0 : 0.0;
	}
	// A (2R + 1)-texel box slid continuously across the texel grid: the (2R + 2)^2 texels
	// it touches, the outer ring weighted by the covered fraction (bilinear PCF).
	const float ux = pixelX - 0.5;
	const float uy = pixelY - 0.5;
	const float bx = floor(ux);
	const float by = floor(uy);
	const float fx = ux - bx;
	const float fy = uy - by;
	float lit = 0.0;
	int hits = 0;
	for (int dy = -radius; dy <= radius + 1; ++dy)
	{
		const float wy = dy == -radius ? 1.0 - fy : (dy == radius + 1 ? fy : 1.0);
		const int ty = clamp(int(by) + dy, y0, y1);
		for (int dx = -radius; dx <= radius + 1; ++dx)
		{
			const float wx = dx == -radius ? 1.0 - fx : (dx == radius + 1 ? fx : 1.0);
			const int tx = clamp(int(bx) + dx, x0, x1);
			if (receiver >= atlas.Load(int3(tx, ty, 0)))
			{
				lit += wx * wy;
				hits += 1;
			}
		}
	}
	// Exactly 0 or 1 when every tap agrees (the weights need not sum to exactly 1 in float).
	const int taps = (2 * radius + 2) * (2 * radius + 2);
	if (hits == 0 || hits == taps)
	{
		return hits == 0 ? 0.0 : 1.0;
	}
	const float width = float(2 * radius + 1);
	return saturate(lit / (width * width));
}

// Shadows::ShadowFactor: 1 = lit.
float ShadowFactor(Texture2D<float> atlas, StructuredBuffer<GpuShadowRecord> records, StructuredBuffer<GpuShadowView> views,
	uint shadowIndex, float3 position, float3 normal, float3 toLight, float cameraViewDepth)
{
	uint recordCount;
	uint recordStride;
	records.GetDimensions(recordCount, recordStride);
	if (shadowIndex >= recordCount)
	{
		return 1.0;
	}
	const GpuShadowRecord record = records[shadowIndex];
	const uint viewIndex = ShadowSelectView(record, position, cameraViewDepth);
	uint viewCount;
	uint viewStride;
	views.GetDimensions(viewCount, viewStride);
	if (viewIndex == ShadowNoView || viewIndex >= viewCount)
	{
		return 1.0;
	}
	const float nDotL = clamp(dot(normal, toLight), 0.0, 1.0);
	// Wider PCF kernels compare texels farther from the receiver: scale the offset.
	const float bias = (record.NormalBias + record.SlopeBias * (1.0 - nDotL)) * float(record.PcfRadius + 1u);
	const bool directional = record.Kind == ShadowKindDirectional;
	uint first = viewIndex;
	uint second = ShadowNoView;
	float t = 0.0;
	float3 shifted = position;
	if (directional)
	{
		// Blend into the next cascade (or fade out after the last one) over the far
		// CascadeBlend fraction of this cascade's depth range.
		const uint cascade = viewIndex - record.FirstView;
		const float farDepth = record.CascadeFar[cascade];
		const float nearDepth = cascade == 0u ? 0.0 : record.CascadeFar[cascade - 1u];
		const float band = record.CascadeBlend * (farDepth - nearDepth);
		t = band > 0.0 ? saturate((cameraViewDepth - (farDepth - band)) / band) : 0.0;
		if (cascade + 1u < min(record.ViewCount, ShadowMaxCascades) && viewIndex + 1u < viewCount)
		{
			second = viewIndex + 1u;
		}
	}
	else
	{
		// Spot and point views are perspective: texels grow with the distance to the light.
		const float3 fromLight = position - record.LightPosition;
		const float distance = sqrt(dot(fromLight, fromLight));
		shifted = position + normal * (views[viewIndex].TexelWorldSize * distance * bias);
		// The offset can cross into another cube face (all faces share TexelWorldSize):
		// select again with the shifted point.
		first = ShadowSelectView(record, shifted, cameraViewDepth);
		if (first == ShadowNoView || first >= viewCount)
		{
			return 1.0;
		}
	}
	// One sampling site for both cascades (a loop, not two inlined copies, keeps the
	// storage-buffer access count under the GPU-AV instrumentation budget).
	float lit[2] = { 1.0, 1.0 };
	[loop]
	for (uint k = 0; k < 2u; ++k)
	{
		const uint v = k == 0u ? first : second;
		if (v == ShadowNoView)
		{
			break;
		}
		const float3 at = directional ? position + normal * (views[v].TexelWorldSize * bias) : shifted;
		lit[k] = ShadowSampleView(atlas, views, v, record, at);
		if (!(t > 0.0))
		{
			break;
		}
	}
	return lerp(lit[0], lit[1], t);
}

#endif
