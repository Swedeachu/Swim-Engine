// UI renderer (critical-path item 79): mirrors UiRenderRecords.h and every Ui::
// function of UiRenderReference.h.
#ifndef SWIM_UI_RECORDS_SLANG
#define SWIM_UI_RECORDS_SLANG

struct UiQuad
{
	float4 Rect;		// x0, y0, x1, y1 in framebuffer pixels (unclipped).
	float4 Clip;		// x0, y0, x1, y1.
	float4 Uv;			// u0, v0 at (x0, y0); u1, v1 at (x1, y1).
	float4 Color;		// Premultiplied linear RGBA.
	float4 BorderColor; // Premultiplied linear RGBA.
	float Radius;
	float Border;
	float PixelRange;
	uint Kind;
	uint Texture;
	uint Sampler;
	uint Reserved0;
	uint Reserved1;
};

struct UiDrawConstants
{
	float2 TargetSize;
	uint Encoding;
	float WhiteScale;
};

static const uint UiQuadSolid = 0u;
static const uint UiQuadGlyph = 1u;
static const uint UiQuadImage = 2u;

static const uint UiEncodingSrgb = 0u;
static const uint UiEncodingLinear = 1u;
static const uint UiEncodingHdr10 = 2u;
static const uint UiEncodingScRgb = 3u;

// Ui::Median.
float UiMedian(float3 v)
{
	return max(min(v.r, v.g), min(max(v.r, v.g), v.b));
}

// Ui::RoundedBoxDistance: negative inside.
float UiRoundedBoxDistance(float2 p, float2 center, float2 halfSize, float radius)
{
	const float2 q = abs(p - center) - (halfSize - radius);
	return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
}

// Ui::SrgbOetf.
float UiSrgbOetf(float x)
{
	return x <= 0.0031308 ? 12.92 * x : 1.055 * pow(x, 1.0 / 2.4) - 0.055;
}

// Ui::PqOetf: nits -> [0, 1].
float UiPqOetf(float nits)
{
	const float m1 = 0.1593017578125;
	const float m2 = 78.84375;
	const float c1 = 0.8359375;
	const float c2 = 18.8515625;
	const float c3 = 18.6875;
	const float y = pow(saturate(nits / 10000.0), m1);
	return pow((c1 + c2 * y) / (1.0 + c3 * y), m2);
}

// Ui::Rec709ToRec2020.
float3 UiRec709ToRec2020(float3 c)
{
	return float3(0.6274040 * c.r + 0.3292820 * c.g + 0.0433136 * c.b, 0.0690970 * c.r + 0.9195400 * c.g + 0.0113612 * c.b,
		0.0163916 * c.r + 0.0880132 * c.g + 0.8955950 * c.b);
}

// Ui::Encode: the stored value of a premultiplied linear UI color.
float4 UiEncode(float4 color, uint encoding, float whiteScale)
{
	if (encoding == UiEncodingLinear || encoding == UiEncodingScRgb)
	{
		return float4(color.rgb * whiteScale, color.a);
	}
	if (color.a <= 0.0)
	{
		return float4(0.0, 0.0, 0.0, 0.0);
	}
	const float3 straight = color.rgb / color.a;
	float3 encoded;
	if (encoding == UiEncodingSrgb)
	{
		encoded = float3(UiSrgbOetf(saturate(straight.r)), UiSrgbOetf(saturate(straight.g)), UiSrgbOetf(saturate(straight.b)));
	}
	else
	{
		const float3 nits = UiRec709ToRec2020(max(straight, 0.0)) * whiteScale;
		encoded = float3(UiPqOetf(nits.r), UiPqOetf(nits.g), UiPqOetf(nits.b));
	}
	return float4(encoded * color.a, color.a);
}

// Ui::ShadeQuad for solid quads (the fragment is inside the clipped rectangle).
float4 UiShadeSolid(UiQuad q, float2 p)
{
	if (q.Radius <= 0.0 && q.Border <= 0.0)
	{
		return q.Color;
	}
	const float2 center = (q.Rect.xy + q.Rect.zw) * 0.5;
	const float2 halfSize = max(q.Rect.zw - q.Rect.xy, 1e-6) * 0.5;
	const float radius = min(q.Radius, min(halfSize.x, halfSize.y));
	const float outer = saturate(0.5 - UiRoundedBoxDistance(p, center, halfSize, radius));
	if (q.Border <= 0.0)
	{
		return q.Color * outer;
	}
	const float2 innerHalf = max(halfSize - q.Border, 0.0);
	float fill = 0.0;
	if (innerHalf.x > 0.0 && innerHalf.y > 0.0)
	{
		fill = min(outer, saturate(0.5 - UiRoundedBoxDistance(p, center, innerHalf, max(radius - q.Border, 0.0))));
	}
	return q.BorderColor * (outer - fill) + q.Color * fill;
}

#endif
