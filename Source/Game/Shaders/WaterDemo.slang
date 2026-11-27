// Game::WaterDemo: one quad of water in the transparent pass, shaded by Water/WaterShading.slang.
#include "Resources/DrawIndex.slang"
#include "ForwardPlus/ForwardFrame.slang"
#include "Water/WaterShading.slang"

struct WaterDemoConstants
{
	float4 Center;	 // xyz, w: time.
	float4 Size;	 // xy: half size, z: wave scale, w: murk.
	float4 Viewport; // xy: pixels.
};

[[vk::push_constant]] ConstantBuffer<WaterDemoConstants> Constants;
[[vk::binding(0, 2)]] Texture2D<float4> SceneColor;
[[vk::binding(1, 2)]] Texture2D<float> SceneDepth;
[[vk::binding(2, 2)]] SamplerState SceneSampler;

struct VertexOutput
{
	float4 Position : SV_Position;
	float3 WorldPosition : WORLD_POSITION;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexIdInput : SWIM_VERTEX_INDEX SWIM_DRAW_BASE_PARAMETERS)
{
	const uint vertexId = DrawVertexIndex(vertexIdInput);
	const float2 quad[6] = { float2(-1, -1), float2(1, -1), float2(1, 1), float2(-1, -1), float2(1, 1), float2(-1, 1) };
	const float2 corner = quad[vertexId] * Constants.Size.xy;
	const float3 world = Constants.Center.xyz + float3(corner.x, 0.0, corner.y);
	VertexOutput output;
	output.Position = ForwardProjectWorld(world, world).Position;
	output.WorldPosition = world;
	return output;
}

// Slopes of a sum of travelling waves (directions spread around the wind, steeper and
// shorter toward the detail).
float2 WaterDemoSlope(float2 p, float time)
{
	float2 slope = float2(0.0, 0.0);
	float amplitude = 0.05;
	float frequency = 0.6;
	float angle = 0.3;
	for (uint i = 0u; i < 8u; ++i)
	{
		const float2 d = float2(cos(angle), sin(angle));
		const float speed = sqrt(9.81 / frequency);
		const float phase = dot(d, p) * frequency + time * speed * frequency;
		slope += d * cos(phase) * amplitude * frequency;
		amplitude *= 0.72;
		frequency *= 1.55;
		angle += 1.7;
	}
	return slope;
}

[shader("fragment")]
ForwardTransparentTargets fragmentMain(VertexOutput input)
{
	const float time = Constants.Center.w;
	const float2 slope = WaterDemoSlope(input.WorldPosition.xz, time) * Constants.Size.z;
	WaterSurface surface;
	surface.WorldPosition = input.WorldPosition;
	surface.Normal = normalize(float3(-slope.x, 1.0, -slope.y));
	surface.Up = float3(0.0, 1.0, 0.0);
	surface.Position = input.Position;
	surface.Time = time;
	surface.Foam = 0.0;
	WaterOptics optics = WaterDefaultOptics();
	optics.Scattering *= Constants.Size.w;
	WaterScene scene;
	scene.Color = SceneColor;
	scene.Depth = SceneDepth;
	scene.Sampler = SceneSampler;
	scene.Size = Constants.Viewport.xy;
	ForwardLighting lit;
	lit.Radiance = WaterShade(surface, optics, scene);
	lit.Indirect = float3(0.0, 0.0, 0.0);
	lit.Reflectance = float3(0.0, 0.0, 0.0);
	lit.Specular = float3(0.0, 0.0, 0.0);
	return ForwardTransparentWrite(lit, 1.0);
}
