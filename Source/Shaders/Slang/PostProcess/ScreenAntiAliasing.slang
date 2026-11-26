// Non-temporal anti-aliasing and sharpening on the display-referred frame (RGBA8, sRGB
// encoded), five programs from one source (SCREEN_AA_PASS):
//  0 ScreenAaFxaa     single-pass FXAA (Lottes 2009, the 3.11 quality search);
//  1 ScreenAaEdges    luma edges with local contrast adaptation (x: left edge, y: top edge);
//  2 ScreenAaWeights  morphological (MLAA/SMAA 1x style) coverage: each edge run is searched
//                     up to 32 pixels both ways, its ends classified by the crossing edges and
//                     the area under the reconstructed silhouette stored signed per pixel;
//  3 ScreenAaBlend    blends every pixel with the neighbours the weights point at;
//  4 ScreenSharpen    contrast-adaptive sharpening (after the AMD FidelityFX CAS idea).
#include "Features/FeatureCommon.slang"
#include "Tweak.slang"

#ifndef SCREEN_AA_PASS
#define SCREEN_AA_PASS 0
#endif

struct ScreenAaConstants
{
	uint2 Size;
	float2 InvSize;
	[Tweak(0.0, 0.5)] float Threshold; // Edge luma threshold (FXAA, edges).
	[Tweak(0.0, 1.0)] float Strength; // Sharpening strength 0..1.
	uint2 Reserved;
};

[[vk::push_constant]] ConstantBuffer<ScreenAaConstants> Constants;

static const int SearchSteps = 32;

float Luma(float3 color)
{
	return dot(color, float3(0.299, 0.587, 0.114));
}

int2 ClampPixel(int2 pixel)
{
	return clamp(pixel, int2(0, 0), int2(Constants.Size) - 1);
}

#if SCREEN_AA_PASS == 0
[[vk::binding(0, 0)]] Texture2D<float4> Color;
[[vk::binding(1, 0)]] SamplerState LinearClamp;
[[vk::binding(2, 0)]] [[vk::image_format("rgba8")]] RWTexture2D<float4> Output;

float LumaAt(float2 uv)
{
	return Luma(Color.SampleLevel(LinearClamp, uv, 0.0).rgb);
}

float LumaOffset(int2 pixel, int2 offset)
{
	return Luma(Color.Load(int3(ClampPixel(pixel + offset), 0)).rgb);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	if (any(id.xy >= Constants.Size))
	{
		return;
	}

	const int2 pixel = int2(id.xy);
	const float4 center = Color.Load(int3(pixel, 0));
	const float lumaC = Luma(center.rgb);
	const float lumaN = LumaOffset(pixel, int2(0, -1));
	const float lumaS = LumaOffset(pixel, int2(0, 1));
	const float lumaW = LumaOffset(pixel, int2(-1, 0));
	const float lumaE = LumaOffset(pixel, int2(1, 0));
	const float lumaMin = min(lumaC, min(min(lumaN, lumaS), min(lumaW, lumaE)));
	const float lumaMax = max(lumaC, max(max(lumaN, lumaS), max(lumaW, lumaE)));
	const float range = lumaMax - lumaMin;

	// A high floor: texel detail inside block faces and glyph strokes is not an edge.
	if (range < max(0.0833, lumaMax * Constants.Threshold))
	{
		Output[id.xy] = center;
		return;
	}

	const float lumaNW = LumaOffset(pixel, int2(-1, -1));
	const float lumaNE = LumaOffset(pixel, int2(1, -1));
	const float lumaSW = LumaOffset(pixel, int2(-1, 1));
	const float lumaSE = LumaOffset(pixel, int2(1, 1));
	const float lumaNS = lumaN + lumaS;
	const float lumaWE = lumaW + lumaE;
	const float edgeH = abs(-2.0 * lumaW + lumaNW + lumaSW) + abs(-2.0 * lumaC + lumaNS) * 2.0 + abs(-2.0 * lumaE + lumaNE + lumaSE);
	const float edgeV = abs(-2.0 * lumaN + lumaNW + lumaNE) + abs(-2.0 * lumaC + lumaWE) * 2.0 + abs(-2.0 * lumaS + lumaSW + lumaSE);
	const bool horizontal = edgeH >= edgeV;

	const float luma1 = horizontal ? lumaN : lumaW;
	const float luma2 = horizontal ? lumaS : lumaE;
	const float gradient1 = luma1 - lumaC;
	const float gradient2 = luma2 - lumaC;
	const bool steepest1 = abs(gradient1) >= abs(gradient2);
	const float gradientScaled = 0.25 * max(abs(gradient1), abs(gradient2));
	float stepLength = horizontal ? Constants.InvSize.y : Constants.InvSize.x;
	float lumaLocal;

	if (steepest1)
	{
		stepLength = -stepLength;
		lumaLocal = 0.5 * (luma1 + lumaC);
	}
	else
	{
		lumaLocal = 0.5 * (luma2 + lumaC);
	}

	const float2 uv = (float2(pixel) + 0.5) * Constants.InvSize;
	float2 edgeUv = uv;

	if (horizontal)
	{
		edgeUv.y += stepLength * 0.5;
	}
	else
	{
		edgeUv.x += stepLength * 0.5;
	}

	const float2 offset = horizontal ? float2(Constants.InvSize.x, 0.0) : float2(0.0, Constants.InvSize.y);
	float2 uv1 = edgeUv - offset;
	float2 uv2 = edgeUv + offset;
	float end1 = LumaAt(uv1) - lumaLocal;
	float end2 = LumaAt(uv2) - lumaLocal;
	bool reached1 = abs(end1) >= gradientScaled;
	bool reached2 = abs(end2) >= gradientScaled;
	static const float Quality[12] = { 1.0, 1.0, 1.0, 1.0, 1.5, 2.0, 2.0, 2.0, 2.0, 4.0, 8.0, 8.0 };

	for (int i = 0; i < 12 && !(reached1 && reached2); ++i)
	{
		if (!reached1)
		{
			uv1 -= offset * Quality[i];
			end1 = LumaAt(uv1) - lumaLocal;
			reached1 = abs(end1) >= gradientScaled;
		}

		if (!reached2)
		{
			uv2 += offset * Quality[i];
			end2 = LumaAt(uv2) - lumaLocal;
			reached2 = abs(end2) >= gradientScaled;
		}
	}

	const float distance1 = horizontal ? uv.x - uv1.x : uv.y - uv1.y;
	const float distance2 = horizontal ? uv2.x - uv.x : uv2.y - uv.y;
	const bool closer1 = distance1 < distance2;
	const float closest = min(distance1, distance2);
	const float edgeLength = distance1 + distance2;
	const bool centerSmaller = lumaC < lumaLocal;
	const bool correctVariation = ((closer1 ? end1 : end2) < 0.0) != centerSmaller;
	float pixelOffset = correctVariation ? -closest / edgeLength + 0.5 : 0.0;

	// Subpixel aliasing (single-pixel features).
	const float average = (2.0 * (lumaNS + lumaWE) + (lumaNW + lumaNE + lumaSW + lumaSE)) / 12.0;
	const float subpixel = saturate(abs(average - lumaC) / range);
	const float subpixelBlend = (-2.0 * subpixel + 3.0) * subpixel * subpixel;
	// Light subpixel smoothing (FXAA's 0.75 blurs fine texture and text).
	pixelOffset = max(pixelOffset, subpixelBlend * subpixelBlend * 0.2);

	float2 finalUv = uv;

	if (horizontal)
	{
		finalUv.y += pixelOffset * stepLength;
	}
	else
	{
		finalUv.x += pixelOffset * stepLength;
	}

	Output[id.xy] = float4(Color.SampleLevel(LinearClamp, finalUv, 0.0).rgb, center.a);
}

#elif SCREEN_AA_PASS == 1
[[vk::binding(0, 0)]] Texture2D<float4> Color;
[[vk::binding(1, 0)]] [[vk::image_format("rgba8")]] RWTexture2D<float4> Edges;

float LumaPixel(int2 pixel)
{
	return Luma(Color.Load(int3(ClampPixel(pixel), 0)).rgb);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	if (any(id.xy >= Constants.Size))
	{
		return;
	}

	const int2 p = int2(id.xy);
	const float l = LumaPixel(p);
	const float left = abs(l - LumaPixel(p + int2(-1, 0)));
	const float top = abs(l - LumaPixel(p + int2(0, -1)));
	const float right = abs(l - LumaPixel(p + int2(1, 0)));
	const float bottom = abs(l - LumaPixel(p + int2(0, 1)));
	const float leftLeft = abs(LumaPixel(p + int2(-1, 0)) - LumaPixel(p + int2(-2, 0)));
	const float topTop = abs(LumaPixel(p + int2(0, -1)) - LumaPixel(p + int2(0, -2)));
	// Local contrast adaptation: an edge much weaker than its neighbours is dropped.
	const float strongest = max(max(left, top), max(max(right, bottom), max(leftLeft, topTop)));
	float2 edges = step(Constants.Threshold, float2(left, top));
	edges *= step(0.5 * strongest, float2(left, top));
	edges.x = p.x == 0 ? 0.0 : edges.x;
	edges.y = p.y == 0 ? 0.0 : edges.y;
	Edges[id.xy] = float4(edges, 0.0, 0.0);
}

#elif SCREEN_AA_PASS == 2
[[vk::binding(0, 0)]] Texture2D<float4> Edges;
[[vk::binding(1, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> Weights;

bool EdgeAt(int2 pixel, int axis)
{
	if (any(pixel < 0) || any(pixel >= int2(Constants.Size)))
	{
		return false;
	}

	return Edges.Load(int3(pixel, 0))[axis] > 0.5;
}

// Integral of h * max(1 - x / m, 0) over [a, b].
float Wedge(float h, float m, float a, float b)
{
	const float a1 = min(a, m);
	const float b1 = min(b, m);
	return h * ((b1 - a1) - (b1 * b1 - a1 * a1) / (2.0 * m));
}

// Signed coverage of pixel i in a run of n edge pixels whose ends cross at heights
// hl and hr (+-0.5: the silhouette leaves the run on the positive or negative side).
float RunArea(int i, int n, float hl, float hr)
{
	const float m = 0.5 * float(n);
	return Wedge(hl, m, float(i), float(i + 1)) + Wedge(hr, m, float(n - 1 - i), float(n - i));
}

float Crossing(bool positive, bool negative)
{
	return positive == negative ? 0.0 : (positive ? 0.5 : -0.5);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	if (any(id.xy >= Constants.Size))
	{
		return;
	}

	const int2 p = int2(id.xy);
	float2 area = 0.0;
	const float2 edges = Edges.Load(int3(p, 0)).xy;

	// Top edge (between p and the pixel above): a horizontal run.
	if (edges.y > 0.5)
	{
		int left = 0;
		int right = 0;

		while (left < SearchSteps && EdgeAt(p - int2(left + 1, 0), 1))
		{
			++left;
		}

		while (right < SearchSteps && EdgeAt(p + int2(right + 1, 0), 1))
		{
			++right;
		}

		const int2 first = p - int2(left, 0);
		const int2 last = p + int2(right, 0);
		// Positive: the crossing edge is in this row (the silhouette dips into it).
		const float hl = Crossing(EdgeAt(first, 0), EdgeAt(first + int2(0, -1), 0));
		const float hr = Crossing(EdgeAt(last + int2(1, 0), 0), EdgeAt(last + int2(1, -1), 0));
		area.x = RunArea(left, left + right + 1, hl, hr);
	}

	// Left edge (between p and the pixel to its left): a vertical run.
	if (edges.x > 0.5)
	{
		int up = 0;
		int down = 0;

		while (up < SearchSteps && EdgeAt(p - int2(0, up + 1), 0))
		{
			++up;
		}

		while (down < SearchSteps && EdgeAt(p + int2(0, down + 1), 0))
		{
			++down;
		}

		const int2 first = p - int2(0, up);
		const int2 last = p + int2(0, down);
		const float hu = Crossing(EdgeAt(first, 1), EdgeAt(first + int2(-1, 0), 1));
		const float hd = Crossing(EdgeAt(last + int2(0, 1), 1), EdgeAt(last + int2(-1, 1), 1));
		area.y = RunArea(up, up + down + 1, hu, hd);
	}

	Weights[id.xy] = float4(area, 0.0, 0.0);
}

#elif SCREEN_AA_PASS == 3
[[vk::binding(0, 0)]] Texture2D<float4> Color;
[[vk::binding(1, 0)]] Texture2D<float4> Weights;
[[vk::binding(2, 0)]] [[vk::image_format("rgba8")]] RWTexture2D<float4> Output;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	if (any(id.xy >= Constants.Size))
	{
		return;
	}

	const int2 p = int2(id.xy);
	const float2 here = Weights.Load(int3(p, 0)).xy;
	const float below = Weights.Load(int3(ClampPixel(p + int2(0, 1)), 0)).x;
	const float right = Weights.Load(int3(ClampPixel(p + int2(1, 0)), 0)).y;
	// Positive weights pull this pixel toward the neighbour across its own edge; negative
	// weights on the neighbour's edge pull it toward that neighbour.
	float4 w = float4(max(here.x, 0.0), max(-below, 0.0), max(here.y, 0.0), max(-right, 0.0));
	const float total = w.x + w.y + w.z + w.w;
	const float4 center = Color.Load(int3(p, 0));

	if (total <= 1.0e-5)
	{
		Output[id.xy] = center;
		return;
	}

	w /= max(total, 1.0);
	float3 color = center.rgb * (1.0 - (w.x + w.y + w.z + w.w));
	color += Color.Load(int3(ClampPixel(p + int2(0, -1)), 0)).rgb * w.x;
	color += Color.Load(int3(ClampPixel(p + int2(0, 1)), 0)).rgb * w.y;
	color += Color.Load(int3(ClampPixel(p + int2(-1, 0)), 0)).rgb * w.z;
	color += Color.Load(int3(ClampPixel(p + int2(1, 0)), 0)).rgb * w.w;
	Output[id.xy] = float4(color, center.a);
}

#else
[[vk::binding(0, 0)]] Texture2D<float4> Color;
[[vk::binding(1, 0)]] [[vk::image_format("rgba8")]] RWTexture2D<float4> Output;

float3 Tap(int2 pixel)
{
	return Color.Load(int3(ClampPixel(pixel), 0)).rgb;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	if (any(id.xy >= Constants.Size))
	{
		return;
	}

	const int2 p = int2(id.xy);
	const float4 center = Color.Load(int3(p, 0));
	const float3 a = Tap(p + int2(-1, -1));
	const float3 b = Tap(p + int2(0, -1));
	const float3 c = Tap(p + int2(1, -1));
	const float3 d = Tap(p + int2(-1, 0));
	const float3 e = center.rgb;
	const float3 f = Tap(p + int2(1, 0));
	const float3 g = Tap(p + int2(-1, 1));
	const float3 h = Tap(p + int2(0, 1));
	const float3 i = Tap(p + int2(1, 1));
	float3 lo = min(min(min(d, e), min(f, b)), h);
	float3 hi = max(max(max(d, e), max(f, b)), h);
	lo += min(lo, min(min(a, c), min(g, i)));
	hi += max(hi, max(max(a, c), max(g, i)));
	// Less sharpening where the neighbourhood is already near black or white (no ringing).
	const float3 amplitude = sqrt(saturate(min(lo, 2.0 - hi) / max(hi, 1.0e-4)));
	const float peak = -1.0 / lerp(8.0, 5.0, saturate(Constants.Strength));
	const float3 weight = amplitude * peak;
	const float3 color = (b * weight + d * weight + f * weight + h * weight + e) / (1.0 + 4.0 * weight);
	Output[id.xy] = float4(saturate(color), center.a);
}
#endif
