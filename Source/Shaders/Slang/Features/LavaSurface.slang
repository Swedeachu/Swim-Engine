// Molten surfaces (Engine::LavaSurface): vertex-pulled four-corner faces drawn in the
// opaque pass, the same flowing-liquid geometry as water.
//
// The melt is smooth and procedural: domain-warped value noise in world space (planar by
// the face's dominant axis, so faces join without seams), dragged along the face's flow
// with the two-phase flow-map trick. It glows bright yellow-orange from a blackbody ramp,
// with darker cooling streaks drifting on it, and its normal undulates with the noise
// (surface-gradient bump). The emission is HDR, so bloom gives the glow.
#include "Resources/DrawIndex.slang"
#include "ForwardPlus/ForwardFrame.slang"

struct LavaParams
{
	float4 Misc;	 // x: time; y: emission intensity; z: pattern scale (per metre); w: crust 0..1.
	float4 Tint;	 // rgb: tint; w: tint mix.
	float4 Crust;	 // rgb: crust colour; w: crust roughness.
	float4 Motion;	 // x: churn speed; y: pulse strength; z: fall speed; w: wave height (m).
};

struct LavaQuad
{
	float4 Corners[4]; // xyz; w: flow x, flow z, heat, falling.
};

[[vk::binding(0, 2)]] StructuredBuffer<LavaParams> Params;
[[vk::binding(1, 2)]] StructuredBuffer<LavaQuad> Quads;

struct VertexOutput
{
	float4 Position : SV_Position;
	float3 WorldPosition : WORLD_POSITION;
	nointerpolation float3 Normal : NORMAL;
	nointerpolation float4 Flow : FLOW; // xy: flow (m/s); z: heat; w: falling.
	float4 CurrentClip : CURRENT_CLIP;
	float4 PreviousClip : PREVIOUS_CLIP;
};

static const uint LavaQuadCorner[6] = { 0, 1, 2, 0, 2, 3 };

[shader("vertex")]
VertexOutput vertexMain(uint vertexIdInput : SWIM_VERTEX_INDEX SWIM_DRAW_BASE_PARAMETERS)
{
	const uint vertexId = DrawVertexIndex(vertexIdInput);
	const LavaQuad quad = Quads[vertexId / 6u];
	const float3 world = quad.Corners[LavaQuadCorner[vertexId % 6u]].xyz;
	const ForwardClip clip = ForwardProjectWorld(world, world);
	// The diagonals' cross product: the mean normal of a non-planar face.
	const float3 normal = cross(quad.Corners[2].xyz - quad.Corners[0].xyz, quad.Corners[3].xyz - quad.Corners[1].xyz);
	VertexOutput output;
	output.Position = clip.Position;
	output.WorldPosition = world;
	output.Normal = normal / max(length(normal), 1.0e-6);
	output.Flow = float4(quad.Corners[0].w, quad.Corners[1].w, quad.Corners[2].w, quad.Corners[3].w);
	output.CurrentClip = clip.Current;
	output.PreviousClip = clip.Previous;
	return output;
}

float2 LavaHash2(float2 p)
{
	const uint2 c = uint2(int2(p) + 65536);
	uint h = c.x * 1597334677u ^ c.y * 3812015801u;
	h = (h ^ (h >> 16u)) * 2246822519u;
	const uint h2 = (h ^ (h >> 13u)) * 3266489917u;
	return float2(float(h & 0xFFFFu), float(h2 >> 16u)) / 65535.0;
}

float LavaNoise(float2 p)
{
	const float2 i = floor(p);
	const float2 f = p - i;
	const float2 s = f * f * (3.0 - 2.0 * f);
	return lerp(lerp(LavaHash2(i).x, LavaHash2(i + float2(1.0, 0.0)).x, s.x), lerp(LavaHash2(i + float2(0.0, 1.0)).x,
		LavaHash2(i + float2(1.0, 1.0)).x, s.x), s.y);
}

float LavaFbm(float2 p)
{
	return LavaNoise(p) * 0.5 + LavaNoise(p * 2.03 + 7.3) * 0.3 + LavaNoise(p * 4.1 + 1.7) * 0.2;
}

// x: heat (about 0.4..1.3); y: height for the bump (0..1).
float2 LavaPattern(float2 uv, float time, LavaParams p)
{
	const float t = time * p.Motion.x;
	// Domain warp: slow swirls folding the melt.
	const float2 warp = float2(LavaFbm(uv * 0.8 + float2(t * 0.05, 0.0)), LavaFbm(uv * 0.8 + float2(5.2, -t * 0.04))) - 0.5;
	const float2 q = uv + warp * 1.2;
	const float n = LavaFbm(q * 1.3 + t * 0.03);
	// Cooling streaks: thin darker skins stretched along the flow.
	const float skin = smoothstep(0.58, 0.78, LavaFbm(q * float2(2.6, 1.4) + float2(0.0, t * 0.08)));
	const float pulse = 1.0 + p.Motion.y * sin(time * 1.3 + n * 7.0);
	const float heat = (0.8 + 0.45 * n - p.Misc.w * skin * 1.6) * pulse;
	return float2(heat, n * 0.7 + skin * 0.3);
}

// Heat to radiance: deep red through saturated orange to small yellow-orange cores. Green
// stays near a fifth of red over most of the melt, and the bulk lands just above the neutral
// tonemapper's compression knee at intensity 0.8 (so it does not desaturate to pale yellow);
// only the hottest veins go brighter and yellower.
float3 LavaEmission(float heat)
{
	const float t = saturate(heat);
	float3 c = float3(0.3, 0.012, 0.002) * smoothstep(0.0, 0.3, t);
	c = lerp(c, float3(1.0, 0.11, 0.008), smoothstep(0.25, 0.6, t));
	c = lerp(c, float3(1.0, 0.2, 0.014), smoothstep(0.55, 0.9, t));
	const float core = smoothstep(0.95, 1.3, heat);
	c = lerp(c, float3(1.0, 0.38, 0.035), core);
	return c * (0.2 + t * 0.8) * (1.0 + 0.4 * core);
}

[shader("fragment")]
ForwardOpaqueTargets fragmentMain(VertexOutput input)
{
	const LavaParams p = Params[0];
	const ForwardView view = Views[0];
	const float time = p.Misc.x;
	float3 normal = input.Normal;
	normal = dot(normal, view.CameraPosition - input.WorldPosition) < 0.0 ? -normal : normal;
	const float3 a = abs(normal);
	const bool top = a.y >= a.x && a.y >= a.z && input.Flow.w < 0.5;
	float2 uv;
	float2 flow;
	if (top)
	{
		uv = input.WorldPosition.xz;
		flow = input.Flow.xy;
	}
	else
	{
		uv = float2(a.x > a.z ? input.WorldPosition.z : input.WorldPosition.x, input.WorldPosition.y);
		flow = float2(0.0, -max(p.Motion.z, length(input.Flow.xy)));
	}
	uv *= p.Misc.z;
	flow *= p.Misc.z;

	// Two-phase flow: two copies dragged along the flow, each reset while the other shows.
	const float phase0 = frac(time * 0.15);
	const float phase1 = frac(time * 0.15 + 0.5);
	const float2 h0 = LavaPattern(uv - flow * phase0 * 6.6, time, p);
	const float2 h1 = LavaPattern(uv - flow * phase1 * 6.6 + 0.37, time, p);
	const float blend = abs(1.0 - 2.0 * phase0);
	const float2 pattern = lerp(h1, h0, blend);
	const float heat = pattern.x * input.Flow.z;

	// Surface-gradient bump from the pattern's height.
	const float h = pattern.y * p.Motion.w;
	const float3 dPdx = ddx(input.WorldPosition);
	const float3 dPdy = ddy(input.WorldPosition);
	const float3 r1 = cross(dPdy, normal);
	const float3 r2 = cross(normal, dPdx);
	const float det = dot(dPdx, r1);
	const float3 gradient = sign(det) * (ddx(h) * r1 + ddy(h) * r2);
	const float3 bumped = abs(det) * normal - gradient;
	normal = dot(bumped, bumped) > 1.0e-12 ? normalize(bumped) : normal;

	const float3 molten = LavaEmission(heat);
	const float3 tinted = p.Tint.rgb * dot(molten, float3(0.2126, 0.7152, 0.0722)) * 1.5;
	const float hot = saturate(heat * 1.3);

	StandardPbrSurface surface;
	surface.Normal = normal;
	surface.BaseColor = p.Crust.rgb * (1.0 - hot);
	surface.Metallic = 0.0;
	surface.PerceptualRoughness = lerp(p.Crust.w, 0.75, hot); // Broad, dim sun glints, not white streaks.
	surface.Occlusion = 1.0;
	surface.Emissive = lerp(molten, tinted, p.Tint.w) * p.Misc.y;
	surface.Alpha = 1.0;
	surface.Discard = false;
	surface.Thin = false;

	const ForwardLighting lit = ForwardLightSurface(view, Grid[0], surface, input.WorldPosition, input.Position.xy);
	return ForwardOpaqueWrite(lit, surface, ForwardNoObject, input.CurrentClip, input.PreviousClip);
}
