[[vk::binding(0)]] Sampler2D msdfAtlas;
uniform float2 atlasSize;
uniform float4 fillColor;
uniform float4 strokeColor;
uniform float strokeWidthPx;
uniform float msdfPixelRange;

struct FragmentInput
{
	float4 Position : SV_Position;
	[[vk::location(0)]] float2 TexCoord : TEXCOORD0;
};

float Median3(float a, float b, float c)
{
	return max(min(a, b), min(max(a, b), c));
}

float ScreenPixelRange(float2 uv)
{
	float2 duvDx = ddx(uv) * atlasSize;
	float2 duvDy = ddy(uv) * atlasSize;
	float scale = max(length(duvDx), length(duvDy));
	return max(scale, 1.0f);
}

[shader("fragment")]
float4 main(FragmentInput input) : SV_Target
{
	float2 uv = float2(input.TexCoord.x, 1.0f - input.TexCoord.y);
	float3 msdf = msdfAtlas.Sample(uv).rgb;
	float signedDistance = Median3(msdf.r, msdf.g, msdf.b) - 0.5f;
	float distanceAtlasPixels = signedDistance * msdfPixelRange;
	float screenRange = ScreenPixelRange(uv);
	float fillAlpha = saturate(distanceAtlasPixels / screenRange + 0.5f) * fillColor.a;

	float strokeAlpha = 0.0f;
	if (strokeWidthPx > 0.0f)
	{
		float edgeDistancePixels = abs(distanceAtlasPixels) / screenRange;
		float strokeBand = 1.0f - smoothstep(
			strokeWidthPx - 0.75f,
			strokeWidthPx + 0.75f,
			edgeDistancePixels);
		float supportEdge = (0.5f * msdfPixelRange) / screenRange;
		float supportSoft = max(1.0f / screenRange, 1.0e-4f);
		float supportMask = 1.0f - smoothstep(
			supportEdge - supportSoft,
			supportEdge,
			edgeDistancePixels);
		strokeAlpha = strokeBand * supportMask * strokeColor.a;
	}

	float alpha = saturate(fillAlpha + strokeAlpha);
	if (alpha < 0.002f)
	{
		discard;
	}

	float3 color = fillColor.rgb * fillAlpha + strokeColor.rgb * strokeAlpha;
	return float4(color, alpha);
}
