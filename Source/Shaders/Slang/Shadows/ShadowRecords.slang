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
	float Reserved0;
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
	// Member loads only: GPU-assisted validation instruments each storage-buffer access.
	const float3 fromLight = position - record.LightPosition;
	const float distance = views[viewIndex].Perspective != 0 ? sqrt(dot(fromLight, fromLight)) : 1.0;
	const float texel = views[viewIndex].TexelWorldSize * distance;
	const float nDotL = clamp(dot(normal, toLight), 0.0, 1.0);
	// Wider PCF kernels compare texels farther from the receiver: scale the offset.
	const float offset = texel * (record.NormalBias + record.SlopeBias * (1.0 - nDotL)) * float(max(record.PcfRadius, 1u));
	const float3 shifted = position + normal * offset;
	// The offset can cross into another cube face (all faces share TexelWorldSize):
	// select again with the shifted point.
	const uint shiftedIndex = ShadowSelectView(record, shifted, cameraViewDepth);
	if (shiftedIndex == ShadowNoView || shiftedIndex >= viewCount)
	{
		return 1.0;
	}
	const float4 rows[4] = views[shiftedIndex].ViewProjection;
	const float4 rect = views[shiftedIndex].AtlasRect;
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
	const int cx = clamp(int(floor(pixelX)), x0, x1);
	const int cy = clamp(int(floor(pixelY)), y0, y1);
	const int radius = int(record.PcfRadius);
	float lit = 0.0;
	for (int dy = -radius; dy <= radius; ++dy)
	{
		for (int dx = -radius; dx <= radius; ++dx)
		{
			const int tx = clamp(cx + dx, x0, x1);
			const int ty = clamp(cy + dy, y0, y1);
			lit += ndc.z + record.DepthBias >= atlas.Load(int3(tx, ty, 0)) ? 1.0 : 0.0;
		}
	}
	const float taps = float((2 * radius + 1) * (2 * radius + 1));
	return lit / taps;
}

#endif
