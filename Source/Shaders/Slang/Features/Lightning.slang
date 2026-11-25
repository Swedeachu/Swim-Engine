// Lightning bolts (Engine::Lightning) in the Forward+ transparent pass: each segment is a
// camera-facing ribbon pulled from Segments, drawn additively (premultiplied colour with
// alpha 0) as a hot core inside a wide glow, so bloom carries the flash.
#include "ForwardPlus/ForwardFrame.slang"

struct LightningConstants
{
	float4 Color; // rgb: core radiance; w: glow width (ribbon width / core width).
	float4 Glow;  // x: glow intensity, y: core sharpness.
};

[[vk::push_constant]] ConstantBuffer<LightningConstants> Constants;

// Two float4 per segment: start xyz + core width (m), end xyz + intensity.
[[vk::binding(0, 2)]] StructuredBuffer<float4> Segments;

struct VertexOutput
{
	float4 Position : SV_Position;
	float Across : RIBBON_ACROSS; // -1..1 over the ribbon (glow) width.
	nointerpolation float Intensity : RIBBON_INTENSITY;
};

static const uint RibbonCorner[6] = { 0, 1, 2, 0, 2, 3 };

[shader("vertex")]
VertexOutput vertexMain(uint vertexId : SV_VulkanVertexID)
{
	const uint segment = vertexId / 6u;
	const uint corner = RibbonCorner[vertexId % 6u];
	const float4 a = Segments[segment * 2u];
	const float4 b = Segments[segment * 2u + 1u];
	const float3 p = (corner == 1u || corner == 2u) ? b.xyz : a.xyz;
	const float3 along = b.xyz - a.xyz;
	float3 side = cross(along, p - Views[0].CameraPosition);
	side = dot(side, side) > 1.0e-12 ? normalize(side) : float3(1.0, 0.0, 0.0);
	const float across = corner >= 2u ? 1.0 : -1.0;
	const float3 world = p + side * (across * a.w * Constants.Color.w);

	VertexOutput output;
	output.Position = ForwardProjectWorld(world, world).Position;
	output.Across = across;
	output.Intensity = b.w;
	return output;
}

[shader("fragment")]
ForwardTransparentTargets fragmentMain(VertexOutput input)
{
	const float x = input.Across;
	const float core = exp(-x * x * Constants.Color.w * Constants.Color.w * Constants.Glow.y);
	const float glow = exp(-x * x * 3.0) * Constants.Glow.x;
	ForwardTransparentTargets output;
	output.Color = float4(Constants.Color.rgb * (input.Intensity * (core + glow)), 0.0);
	output.Indirect = float4(0.0, 0.0, 0.0, 0.0);
	output.Reflectance = output.Indirect;
	output.Specular = output.Indirect;
	return output;
}
