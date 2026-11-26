// Procedural enchantment-style sheen (Engine::GlintSettings, Renderer/Features/Glint.h).
// Call GlintEvaluate (either mode) or GlintEmission (procedural only) from a surface's
// fragment stage and add the result to its emission:
//   surface.Emissive += GlintEvaluate(glint, glintTexture, repeatSampler, uv, ddx(uv), ddy(uv), normal, toCamera);
// uv is the surface's own texture coordinate (so the sheen follows the item), normal and
// toCamera are unit world vectors. Take the uv derivatives in uniform control flow.
#ifndef SWIM_GLINT_SLANG
#define SWIM_GLINT_SLANG

struct GlintParams
{
	float4 ColorA; // rgb; w: intensity.
	float4 ColorB; // rgb; w: sparkle.
	float4 Motion; // x: speed; y: band scale; z: band width; w: time (s).
	float4 Extra;  // x: rim; y: sparkle density; z: texture scale; w: texture speed.
	uint4 Source;  // x: mode (0 off, 1 procedural, 2 texture); y: bindless texture; z: bindless sampler.
};

static const uint GlintModeOff = 0u;
static const uint GlintModeProcedural = 1u;
static const uint GlintModeTexture = 2u;

float GlintHash(float2 cell, float salt)
{
	const uint2 c = uint2(int2(cell) + 32768);
	uint h = c.x * 1597334677u ^ c.y * 3812015801u ^ uint(salt * 977.0) * 2654435761u;
	h = (h ^ (h >> 16u)) * 2246822519u;
	h ^= h >> 13u;
	return float(h & 0xFFFFFFu) / 16777215.0;
}

// A soft band profile repeating every unit along x.
float GlintBand(float x, float width)
{
	const float d = abs(frac(x) - 0.5);
	return exp(-d * d / max(width * width, 1.0e-4));
}

float3 GlintEmission(GlintParams g, float2 uv, float3 normal, float3 toCamera)
{
	const float time = g.Motion.w;
	const float speed = g.Motion.x;
	const float scale = g.Motion.y;
	const float width = g.Motion.z;
	// Two sweeps: a broad one along one diagonal, a finer, slower one crossing it.
	const float a = GlintBand(dot(uv, float2(0.8, 0.6)) * scale - time * speed, width * 1.6);
	const float b = GlintBand(dot(uv, float2(-0.45, 0.9)) * scale * 1.7 + time * speed * 0.6, width);
	const float sweep = a * 0.75 + b * 0.45;
	// The colour drifts along the surface and with time.
	const float hue = 0.5 + 0.5 * sin(dot(uv, float2(3.1, 2.3)) + time * 0.9);
	const float3 tint = lerp(g.ColorA.rgb, g.ColorB.rgb, hue);
	// Rim sheen: edges facing away catch more light.
	const float facing = saturate(dot(normal, toCamera));
	const float rim = pow(1.0 - facing, 3.0) * g.Extra.x;
	// Sparkles: one candidate per cell, each twinkling on its own clock.
	const float density = max(g.Extra.y, 1.0);
	const float2 p = uv * density;
	const float2 cell = floor(p);
	const float2 spot = float2(GlintHash(cell, 1.0), GlintHash(cell, 2.0)) * 0.7 + 0.15;
	const float phase = GlintHash(cell, 3.0) * 6.2832;
	const float rate = 1.5 + GlintHash(cell, 4.0) * 3.0;
	const float twinkle = pow(saturate(sin(time * rate + phase)), 12.0);
	const float2 d = (p - cell - spot) * 2.0;
	// A small four-pointed star.
	const float star = exp(-dot(d, d) * 40.0) + 0.35 * exp(-abs(d.x) * 60.0 - d.y * d.y * 400.0) + 0.35 * exp(-abs(d.y) * 60.0 - d.x * d.x * 400.0);
	const float sparkle = star * twinkle * g.ColorB.w * (0.6 + 0.8 * sweep);
	return tint * (sweep * 0.8 + rim + 0.04) * g.ColorA.w + lerp(tint, float3(1.0, 1.0, 1.0), 0.4) * sparkle * g.ColorA.w * 2.0;
}

float2 GlintRotate(float2 v, float angle)
{
	const float c = cos(angle);
	const float s = sin(angle);
	return float2(c * v.x - s * v.y, s * v.x + c * v.y);
}

// The classic look: two layers of the glint texture at different angles scrolling at
// different rates, added. dx, dy: the uv derivatives.
float3 GlintTextureEmission(GlintParams g, Texture2D<float4> glint, SamplerState repeat, float2 uv, float2 dx, float2 dy)
{
	const float time = g.Motion.w * g.Extra.w;
	const float scale = g.Extra.z;
	const float a = -0.8727; // -50 degrees.
	const float b = 0.1745;	 // 10 degrees.
	const float2 uvA = GlintRotate(uv * scale, a) + float2(frac(time / 3.0), 0.0);
	const float2 uvB = GlintRotate(uv * scale, b) + float2(-frac(time / 4.873), 0.0);
	const float3 layerA = glint.SampleGrad(repeat, uvA, GlintRotate(dx * scale, a), GlintRotate(dy * scale, a)).rgb;
	const float3 layerB = glint.SampleGrad(repeat, uvB, GlintRotate(dx * scale, b), GlintRotate(dy * scale, b)).rgb;
	// The classic texture is bright: its two layers at full strength wash a surface out.
	return (layerA + layerB) * 0.5 * g.ColorA.w;
}

// Either mode by g.Source.x; glint/repeat: the texture and sampler g.Source.yz name
// (Textures[NonUniformResourceIndex(g.Source.y)] in a bindless shader), unused when procedural.
float3 GlintEvaluate(GlintParams g, Texture2D<float4> glint, SamplerState repeat, float2 uv, float2 dx, float2 dy, float3 normal, float3 toCamera)
{
	if (g.Source.x == GlintModeTexture)
	{
		return GlintTextureEmission(g, glint, repeat, uv, dx, dy);
	}
	if (g.Source.x == GlintModeProcedural)
	{
		return GlintEmission(g, uv, normal, toCamera);
	}
	return float3(0.0, 0.0, 0.0);
}

#endif
