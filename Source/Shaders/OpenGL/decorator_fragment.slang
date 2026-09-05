uniform float4 fillColor;
uniform float4 strokeColor;
uniform float2 strokeWidth;
uniform float2 cornerRadius;
uniform int enableFill;
uniform int enableStroke;
uniform int roundCorners;
uniform int useTexture;
uniform float2 resolution;
uniform float2 quadSize;
uniform int isWorldSpace;
[[vk::binding(0)]] Sampler2D albedoTex;

struct FragmentInput
{
	float4 Position : SV_Position;
	[[vk::location(0)]] noperspective float2 TexCoord : TEXCOORD0;
	[[vk::location(1)]] float3 Color : COLOR0;
};

float RoundedRectSDF(float2 pos, float2 halfSize, float radius)
{
	float2 d = abs(pos) - halfSize + radius;
	return length(max(d, float2(0.0f))) + min(max(d.x, d.y), 0.0f) - radius;
}

float BoxSDF(float2 pos, float2 halfSize)
{
	float2 d = abs(pos) - halfSize;
	return length(max(d, float2(0.0f))) + min(max(d.x, d.y), 0.0f);
}

[shader("fragment")]
float4 main(FragmentInput input) : SV_Target
{
	float2 halfSize = quadSize * 0.5f;
	float2 posPx = (input.TexCoord - float2(0.5f)) * quadSize;

	float radius = roundCorners == 1
		? min(max(cornerRadius.x, cornerRadius.y), min(halfSize.x, halfSize.y))
		: 0.0f;

	float2 innerHalf = halfSize;
	float innerRadius = radius;
	if (enableStroke == 1)
	{
		innerHalf = max(halfSize - strokeWidth, float2(0.0f));
		innerRadius = max(radius - max(strokeWidth.x, strokeWidth.y), 0.0f);
	}

	float distOuter = radius > 0.0f
		? RoundedRectSDF(posPx, halfSize, radius)
		: BoxSDF(posPx, halfSize);
	float distInner = radius > 0.0f
		? RoundedRectSDF(posPx, innerHalf, innerRadius)
		: BoxSDF(posPx, innerHalf);

	const float edgeAA = 1.5f;
	float aaWidthOuter = max(fwidth(distOuter) * edgeAA, 1.0e-3f);
	float aaWidthInner = max(fwidth(distInner) * edgeAA, 1.0e-3f);
	float outerAlpha = 1.0f - smoothstep(0.0f, aaWidthOuter, distOuter);
	float innerAlpha = 1.0f - smoothstep(0.0f, aaWidthInner, distInner);

	if (outerAlpha < 0.2f)
	{
		outerAlpha = 0.0f;
	}
	if (innerAlpha < 0.2f)
	{
		innerAlpha = 0.0f;
	}

	float strokeAlpha = enableStroke == 1 ? saturate(outerAlpha - innerAlpha) : 0.0f;
	float fillAlpha = enableFill == 1 ? innerAlpha : 0.0f;
	float4 textureSample = albedoTex.Sample(input.TexCoord);
	bool useVertexColor = all(fillColor.rgb < float3(0.0f));

	float4 fillSource = float4(1.0f, 1.0f, 1.0f, 1.0f);
	if (useTexture == 1)
	{
		fillSource = textureSample;
	}
	else if (useVertexColor)
	{
		fillSource = float4(input.Color, 1.0f);
	}
	else
	{
		fillSource = fillColor;
	}

	float combinedAlpha = saturate(fillAlpha + strokeAlpha);
	float3 combinedColor = combinedAlpha > 0.0f
		? (fillSource.rgb * fillAlpha + strokeColor.rgb * strokeAlpha) / combinedAlpha
		: float3(0.0f);

	return float4(combinedColor, combinedAlpha);
}
