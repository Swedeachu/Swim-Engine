// Multisampled Forward+ resolve (RenderSettings::MsaaSamples). One fullscreen triangle reads
// the multisampled opaque targets and writes the single-sample ones every later pass uses:
//  - colour, indirect, reflectance and specular: the average of the samples, each weighted by
//    1 / (1 + luma) of its colour and the weighting undone afterwards, so a bright sample
//    (sky, emissive) does not swamp an edge after tone mapping. The four use the same weights,
//    so the parts of colour that ambient occlusion and reflections replace stay consistent;
//  - depth, normal, velocity and object id: the nearest sample's (reverse-Z: the largest
//    depth), a real surface for ambient occlusion, reflections, temporal reprojection and the
//    later passes' depth tests, never a blend of two surfaces;
//  - colour alpha is 1 (alpha-to-coverage draws leave their coverage in the samples); the
//    other averaged targets keep their averaged alpha (0 where only the sky was).
#include "Resources/DrawIndex.slang"

[[vk::binding(0, 0)]] Texture2DMS<float4> Color;
[[vk::binding(1, 0)]] Texture2DMS<float> ObjectId;
[[vk::binding(2, 0)]] Texture2DMS<float2> Velocity;
[[vk::binding(3, 0)]] Texture2DMS<float4> Normal;
[[vk::binding(4, 0)]] Texture2DMS<float4> Indirect;
[[vk::binding(5, 0)]] Texture2DMS<float4> Reflectance;
[[vk::binding(6, 0)]] Texture2DMS<float4> Specular;
[[vk::binding(7, 0)]] Texture2DMS<float> Depth;

// Uv is unused: an entry-point input of system values alone has no varying layout, which
// the runtime shader interface rejects.
struct VertexOutput
{
	float4 Position : SV_Position;
	float2 Uv : TEXCOORD0;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexIdInput : SWIM_VERTEX_INDEX SWIM_DRAW_BASE_PARAMETERS)
{
	const uint vertexId = DrawVertexIndex(vertexIdInput);
	static const float2 positions[3] = { float2(-1, -1), float2(3, -1), float2(-1, 3) };
	VertexOutput output;
	output.Position = float4(positions[vertexId], 0.0, 1.0);
	output.Uv = positions[vertexId] * 0.5 + 0.5;
	return output;
}

struct ResolveOutput
{
	float4 Color : SV_Target0;
	float ObjectId : SV_Target1;
	float2 Velocity : SV_Target2;
	float4 Normal : SV_Target3;
	float4 Indirect : SV_Target4;
	float4 Reflectance : SV_Target5;
	float4 Specular : SV_Target6;
	float Depth : SV_Depth;
};

float ResolveWeight(float3 color)
{
	const float luma = dot(max(color, 0.0), float3(0.2126, 0.7152, 0.0722));
	return 1.0 / (1.0 + luma);
}

[shader("fragment")]
ResolveOutput fragmentMain(VertexOutput input)
{
	const int2 pixel = int2(input.Position.xy + input.Uv * 0.0);
	uint width = 0;
	uint height = 0;
	uint samples = 1;
	Color.GetDimensions(width, height, samples);
	float3 color = 0.0;
	float4 indirect = 0.0;
	float4 reflectance = 0.0;
	float4 specular = 0.0;
	float totalWeight = 0.0;
	float nearest = -1.0;
	int nearestSample = 0;

	for (uint s = 0; s < samples; ++s)
	{
		const int index = int(s);
		const float3 sampleColor = Color.Load(pixel, index).rgb;
		const float weight = ResolveWeight(sampleColor);
		color += sampleColor * weight;
		indirect += Indirect.Load(pixel, index) * weight;
		reflectance += Reflectance.Load(pixel, index) * weight;
		specular += Specular.Load(pixel, index) * weight;
		totalWeight += weight;
		const float depth = Depth.Load(pixel, index);
		if (depth > nearest)
		{
			nearest = depth;
			nearestSample = index;
		}
	}

	const float inverse = totalWeight > 0.0 ? 1.0 / totalWeight : 0.0;
	ResolveOutput output;
	output.Color = float4(color * inverse, 1.0);
	output.Indirect = indirect * inverse;
	output.Reflectance = reflectance * inverse;
	output.Specular = specular * inverse;
	output.ObjectId = ObjectId.Load(pixel, nearestSample);
	output.Velocity = Velocity.Load(pixel, nearestSample);
	output.Normal = Normal.Load(pixel, nearestSample);
	output.Depth = max(nearest, 0.0);
	return output;
}
