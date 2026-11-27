// Block-style fire (Engine::BlockFire): vertex-pulled flame cards textured with a flipbook
// from a texture array, swaying in the wind like foliage, emissive (bloom does the glow).
// One source, three programs:
//  - BlockFire (opaque pass): the cards, alpha-cut texture, lit only by their own emission;
//  - BlockFireProcedural (BLOCK_FIRE_PROCEDURAL): the same cards with a noise flame instead
//    of a texture;
//  - BlockFireGlow (BLOCK_FIRE_GLOW, transparent pass): per fire a soft glow halo, rising
//    embers, quick sparks and light smoke: stateless GPU particles whose positions are
//    functions of time and the fire's seed.
#include "Resources/DrawIndex.slang"
#include "ForwardPlus/ForwardFrame.slang"
#include "Features/Wind.slang"

struct BlockFireParams
{
	float4 Misc;   // x: time; y: previous frame's time; z: emission; w: glow strength.
	WindParams Wind;
	float4 Motion; // x: sway (m per m/s); y: flicker 0..1; z: smoke opacity; w: unused.
	uint4 Counts;  // x: fires; y: texture array layers.
};

struct FireRecord
{
	float4 PositionSeed; // xyz: cell min corner (block) or base centre (small, entity); w: seed.
	float4 Size;		 // x: width; y: height; z: intensity; w: frames per second.
	float4 Tint;		 // rgb: emission tint; w: unused.
	uint4 Info;			 // x: first layer; y: frames | rows << 16; z: shape | attachment << 8; w: embers | sparks << 8 | smoke << 16.
};

static const uint ShapeBlock = 0u;
static const uint ShapeSmall = 1u;
static const uint ShapeEntity = 2u;

static const uint AttachFloor = 0u;
static const uint AttachCeiling = 1u;
static const uint AttachNorth = 2u; // The wall at the cell's min z.
static const uint AttachSouth = 3u; // Max z.
static const uint AttachWest = 4u;	// Min x.
static const uint AttachEast = 5u;	// Max x.

static const uint CardsPerFire = 4u;
static const uint SlotsPerFire = 12u; // Glow program: 1 halo, 6 embers, 2 sparks, 3 smoke.

[[vk::binding(0, 2)]] StructuredBuffer<BlockFireParams> Params;
[[vk::binding(1, 2)]] StructuredBuffer<FireRecord> Fires;
#if !defined(BLOCK_FIRE_GLOW) && !defined(BLOCK_FIRE_PROCEDURAL)
[[vk::binding(2, 2)]] Texture2DArray<float4> FireTexture;
[[vk::binding(3, 2)]] SamplerState FireSampler;
#endif

float FireHash(uint a, uint b)
{
	uint h = a * 747796405u + b * 2891336453u + 12345u;
	h = ((h >> ((h >> 28u) + 4u)) ^ h) * 277803737u;
	return float((h >> 22u) ^ h) / 4294967295.0;
}

// Engine::BlockFire::Flicker: the same curve drives the light.
float FireFlicker(float time, float seed, float amount)
{
	const float wave = 0.12 * sin(time * 9.3 + seed * 7.0) + 0.06 * sin(time * 23.1 + seed * 3.0 + 1.7) + 0.04 * sin(time * 41.0 + seed);
	return 1.0 + (wave - 0.02) * amount * 4.0;
}

float3 HiddenPosition()
{
	return float3(0.0, -1.0e6, 0.0);
}

// Map a wall card's local (x across the wall, z out of it) into the cell.
float2 WallToCell(uint attach, float2 local)
{
	if (attach == AttachSouth)
	{
		return float2(1.0 - local.x, 1.0 - local.y);
	}
	if (attach == AttachWest)
	{
		return float2(local.y, local.x);
	}
	if (attach == AttachEast)
	{
		return float2(1.0 - local.y, local.x);
	}
	return local;
}

struct Card
{
	float3 A;  // Bottom-left.
	float3 B;  // Bottom-right.
	float3 Up; // Bottom to top (leaning cards lean).
	bool Visible;
};

Card FireCard(FireRecord fire, uint index)
{
	Card card;
	card.Visible = true;
	const float3 p = fire.PositionSeed.xyz;
	const float width = fire.Size.x;
	const float height = fire.Size.y;
	const uint shape = fire.Info.z & 0xFFu;
	const uint attach = (fire.Info.z >> 8u) & 0xFFu;
	if (shape == ShapeSmall || shape == ShapeEntity)
	{
		const uint cards = shape == ShapeSmall ? 2u : 4u;
		card.Visible = index < cards;
		const float angle = (float(index) / float(cards)) * 3.14159265 + 0.785398 + fire.PositionSeed.w;
		const float2 across = float2(cos(angle), sin(angle)) * width * 0.5;
		card.A = p - float3(across.x, 0.0, across.y);
		card.B = p + float3(across.x, 0.0, across.y);
		card.Up = float3(0.0, height, 0.0);
		return card;
	}
	if (attach == AttachFloor || attach == AttachCeiling)
	{
		const float base = attach == AttachCeiling ? 0.3 : 0.0;
		const float h = height * (attach == AttachCeiling ? 0.7 : 1.0);
		const float3 o = p + float3(0.0, base, 0.0);
		if (index == 0u)
		{
			card.A = o;
			card.B = o + float3(1.0, 0.0, 1.0);
			card.Up = float3(0.0, h, 0.0);
		}
		else if (index == 1u)
		{
			card.A = o + float3(1.0, 0.0, 0.0);
			card.B = o + float3(0.0, 0.0, 1.0);
			card.Up = float3(0.0, h, 0.0);
		}
		else if (index == 2u)
		{
			card.A = o + float3(0.0, 0.0, 0.08);
			card.B = o + float3(1.0, 0.0, 0.08);
			card.Up = float3(0.0, h, 0.2);
		}
		else
		{
			card.A = o + float3(1.0, 0.0, 0.92);
			card.B = o + float3(0.0, 0.0, 0.92);
			card.Up = float3(0.0, h, -0.2);
		}
		return card;
	}
	// Walls: two cards climbing the wall, leaning into it at the top.
	card.Visible = index < 2u;
	const float offset = index == 0u ? 0.07 : 0.24;
	const float lean = index == 0u ? 0.06 : 0.18;
	const float2 a = WallToCell(attach, float2(0.0, offset));
	const float2 b = WallToCell(attach, float2(1.0, offset));
	const float2 top = WallToCell(attach, float2(0.0, offset - lean)) - a;
	card.A = p + float3(a.x, 0.0, a.y);
	card.B = p + float3(b.x, 0.0, b.y);
	card.Up = float3(top.x, height, top.y);
	return card;
}

float3 Sway(BlockFireParams params, FireRecord fire, float3 anchor, float weight, float time)
{
	const float seed = fire.PositionSeed.w;
	// Wind lean and gust flutter as foliage does, plus the flame's own lick.
	float3 offset = WindSway(params.Wind, anchor, weight, params.Motion.x, time);
	offset.xz += float2(sin(time * 6.1 + seed * 11.0), cos(time * 5.3 + seed * 7.0)) * 0.035 * weight * fire.Size.y;
	return offset;
}

#if !defined(BLOCK_FIRE_GLOW)

struct VertexOutput
{
	float4 Position : SV_Position;
	float3 WorldPosition : WORLD_POSITION;
	float2 Uv : TEXCOORD0; // u across the card, v up (0 at the bottom).
	nointerpolation float3 Normal : NORMAL;
	nointerpolation uint Fire : FIRE;
	float4 CurrentClip : CURRENT_CLIP;
	float4 PreviousClip : PREVIOUS_CLIP;
};

static const uint CardCorner[6] = { 0, 1, 2, 0, 2, 3 };

[shader("vertex")]
VertexOutput vertexMain(uint vertexIdInput : SWIM_VERTEX_INDEX SWIM_DRAW_BASE_PARAMETERS)
{
	const uint vertexId = DrawVertexIndex(vertexIdInput);
	const BlockFireParams params = Params[0];
	const uint fireIndex = vertexId / (CardsPerFire * 6u);
	const FireRecord fire = Fires[fireIndex];
	const Card card = FireCard(fire, (vertexId / 6u) % CardsPerFire);
	const uint corner = CardCorner[vertexId % 6u];
	const float u = corner == 1u || corner == 2u ? 1.0 : 0.0;
	const float v = corner >= 2u ? 1.0 : 0.0;
	float3 world = card.A + (card.B - card.A) * u + card.Up * v;
	float3 previous = world;
	const float3 anchor = card.A;
	world += Sway(params, fire, anchor, v, params.Misc.x);
	previous += Sway(params, fire, anchor, v, params.Misc.y);
	if (!card.Visible)
	{
		world = HiddenPosition();
		previous = world;
	}
	const ForwardClip clip = ForwardProjectWorld(world, previous);
	VertexOutput output;
	output.Position = clip.Position;
	output.WorldPosition = world;
	output.Uv = float2(u, v);
	output.Normal = normalize(cross(card.B - card.A, card.Up));
	output.Fire = fireIndex;
	output.CurrentClip = clip.Current;
	output.PreviousClip = clip.Previous;
	return output;
}

float FireNoise(float2 p)
{
	const float2 i = floor(p);
	const float2 f = p - i;
	const float2 s = f * f * (3.0 - 2.0 * f);
	const uint2 c = uint2(int2(i) + 4096);
	return lerp(lerp(FireHash(c.x, c.y), FireHash(c.x + 1u, c.y), s.x), lerp(FireHash(c.x, c.y + 1u), FireHash(c.x + 1u, c.y + 1u), s.x), s.y);
}

[shader("fragment")]
ForwardOpaqueTargets fragmentMain(VertexOutput input)
{
	const BlockFireParams params = Params[0];
	const FireRecord fire = Fires[input.Fire];
	const float time = params.Misc.x;
	const float seed = fire.PositionSeed.w;
	float4 texel;
#if defined(BLOCK_FIRE_PROCEDURAL)
	{
		// Pixel-snapped noise flame in the 16-texel blocky style.
		const float2 pixel = (floor(input.Uv * 16.0) + 0.5) / 16.0;
		const float n = FireNoise(float2(pixel.x * 5.0 + seed * 13.0, pixel.y * 4.0 - time * 3.0)) * 0.6 +
			FireNoise(float2(pixel.x * 11.0, pixel.y * 9.0 - time * 5.0)) * 0.4;
		const float edge = 1.0 - abs(pixel.x * 2.0 - 1.0);
		const float body = n * (1.2 - pixel.y) * (0.4 + edge);
		const float heat = saturate(body * 1.4 - 0.15);
		texel.rgb = lerp(float3(0.8, 0.12, 0.02), lerp(float3(1.0, 0.55, 0.08), float3(1.0, 0.92, 0.6), saturate(heat * 2.0 - 1.0)), saturate(heat * 2.0));
		texel.a = body > 0.35 ? 1.0 : 0.0;
	}
#else
	{
		const uint frames = max(fire.Info.y & 0xFFFFu, 1u);
		const uint rows = max(fire.Info.y >> 16u, 1u);
		const uint frame = uint(floor(time * fire.Size.w + seed * 17.0)) % frames;
		// Frames past the array (a flipbook that is really rows in one layer, or a wrong
		// count) fall back to the first layer rather than reading the placeholder.
		const uint layers = max(params.Counts.y, 1u);
		const uint first = min(fire.Info.x, layers - 1u);
		uint layer = first + (rows > 1u ? frame / rows : frame);
		layer = layer < layers ? layer : first;
		const uint row = rows > 1u ? frame % rows : 0u;
		const float2 uv = float2(input.Uv.x, (float(row) + 1.0 - input.Uv.y) / float(rows));
		texel = FireTexture.Sample(FireSampler, float3(uv, float(layer)));
	}
#endif
	if (texel.a < 0.5)
	{
		discard;
	}
	const float flicker = FireFlicker(time, seed, params.Motion.y);
	// Hot texels burn hotter: the bright core of the texture blooms most.
	const float luminance = dot(texel.rgb, float3(0.2126, 0.7152, 0.0722));
	const float3 emission = texel.rgb * (0.6 + luminance * 1.6) * params.Misc.z * fire.Size.z * flicker * fire.Tint.rgb;

	float3 normal = input.Normal;
	normal = dot(normal, Views[0].CameraPosition - input.WorldPosition) < 0.0 ? -normal : normal;
	StandardPbrSurface surface;
	surface.Normal = normal;
	surface.BaseColor = 0.0;
	surface.Metallic = 0.0;
	surface.PerceptualRoughness = 1.0;
	surface.Occlusion = 1.0;
	surface.Emissive = emission;
	surface.Alpha = 1.0;
	surface.Discard = false;
	surface.Thin = true;
	// Flame is pure emission (black, fully rough): the cluster light loop and shadow lookups
	// would add next to nothing, and crossed flame quads overdraw, so they are skipped.
	ForwardLighting lit;
	lit.Radiance = emission;
	lit.Indirect = float3(0.0, 0.0, 0.0);
	lit.Reflectance = float3(0.0, 0.0, 0.0);
	lit.Specular = float3(0.0, 0.0, 0.0);
	return ForwardOpaqueWrite(lit, surface, ForwardNoObject, input.CurrentClip, input.PreviousClip);
}

#else // BLOCK_FIRE_GLOW

struct VertexOutput
{
	float4 Position : SV_Position;
	float2 Local : TEXCOORD0; // -1..1 across the billboard.
	nointerpolation float4 Color : COLOR0; // Premultiplied; alpha 0 adds.
	nointerpolation uint Kind : KIND;	   // 0: halo; 1: ember/spark; 2: smoke.
};

static const float2 BillboardCorner[6] = { float2(-1, -1), float2(1, -1), float2(1, 1), float2(-1, -1), float2(1, 1), float2(-1, 1) };

float3 FireCentre(FireRecord fire)
{
	const uint shape = fire.Info.z & 0xFFu;
	const uint attach = (fire.Info.z >> 8u) & 0xFFu;
	if (shape != ShapeBlock)
	{
		return fire.PositionSeed.xyz + float3(0.0, fire.Size.y * 0.45, 0.0);
	}
	float2 xz = float2(0.5, 0.5);
	if (attach >= AttachNorth)
	{
		xz = WallToCell(attach, float2(0.5, 0.2));
	}
	const float y = attach == AttachCeiling ? 0.65 : 0.45;
	return fire.PositionSeed.xyz + float3(xz.x, fire.Size.y * y, xz.y);
}

[shader("vertex")]
VertexOutput vertexMain(uint vertexIdInput : SWIM_VERTEX_INDEX SWIM_DRAW_BASE_PARAMETERS)
{
	const uint vertexId = DrawVertexIndex(vertexIdInput);
	const BlockFireParams params = Params[0];
	const uint fireIndex = vertexId / (SlotsPerFire * 6u);
	const uint slot = (vertexId / 6u) % SlotsPerFire;
	const FireRecord fire = Fires[fireIndex];
	const float time = params.Misc.x;
	const float seed = fire.PositionSeed.w;
	const uint seedBits = uint(seed * 65536.0);
	const float flicker = FireFlicker(time, seed, params.Motion.y);
	const float3 centre = FireCentre(fire);
	const float scale = max(fire.Size.x, fire.Size.y);
	const uint embers = fire.Info.w & 0xFFu;
	const uint sparks = (fire.Info.w >> 8u) & 0xFFu;
	const uint smokes = (fire.Info.w >> 16u) & 0xFFu;
	const float3 warm = float3(1.0, 0.5, 0.16) * fire.Tint.rgb;

	float3 position = centre;
	float2 size = 0.0;
	float4 color = 0.0;
	uint kind = 0u;
	bool visible = true;
	if (slot == 0u)
	{
		// The halo: wide and faint, brighter for small flames (torches, candles).
		const bool small = (fire.Info.z & 0xFFu) == ShapeSmall;
		size = scale * (small ? 2.4 : 1.6);
		color = float4(warm * params.Misc.w * fire.Size.z * flicker * (small ? 0.9 : 0.35), 0.0);
	}
	else
	{
		const uint particle = slot - 1u;
		const float r0 = FireHash(seedBits, particle * 3u);
		const float r1 = FireHash(seedBits, particle * 3u + 1u);
		const float r2 = FireHash(seedBits, particle * 3u + 2u);
		const float3 wind = WindVelocity(params.Wind, centre, time);
		if (particle < 6u)
		{
			// Embers: drift up and swirl, cooling from yellow to red.
			visible = particle < embers;
			const float life = frac(time * (0.3 + 0.35 * r2) + r0);
			const float swirl = time * (1.4 + r1) + r0 * 6.2832;
			position = centre + float3((r0 - 0.5) * fire.Size.x * 0.8 + sin(swirl) * 0.12 * life, fire.Size.y * (0.2 + life * 1.8),
								   (r1 - 0.5) * fire.Size.x * 0.8 + cos(swirl * 0.8) * 0.12 * life) + wind * life * 0.35;
			size = float2(0.035, 0.035) * (1.0 - life * 0.5);
			const float fade = (1.0 - life) * smoothstep(0.0, 0.08, life);
			color = float4(lerp(float3(6.0, 3.0, 0.8), float3(2.5, 0.5, 0.08), life) * fade * fire.Size.z, 0.0);
			kind = 1u;
		}
		else if (particle < 8u)
		{
			// Sparks: short fast pops, stretched upward.
			visible = particle - 6u < sparks;
			const float cycle = 1.7 + r2 * 1.3;
			const float life = frac(time / cycle + r0) * cycle / 0.45;
			visible = visible && life < 1.0;
			const float2 spread = float2(r1 - 0.5, r2 - 0.5) * 1.6;
			position = centre + float3(spread.x * life * 0.6, fire.Size.y * (0.1 + life * 1.1 - life * life * 0.5), spread.y * life * 0.6);
			size = float2(0.02, 0.07);
			color = float4(float3(9.0, 5.0, 1.6) * (1.0 - life) * fire.Size.z, 0.0);
			kind = 1u;
		}
		else
		{
			// Light smoke: soft puffs rising above, growing and thinning, carried by the wind.
			visible = particle - 8u < smokes;
			const float life = frac(time * (0.16 + 0.08 * r2) + r0);
			position = centre + float3((r0 - 0.5) * 0.3, fire.Size.y * (0.7 + life * 2.6), (r1 - 0.5) * 0.3) + wind * life * 1.2;
			size = scale * (0.35 + life * 0.9);
			const float alpha = params.Motion.z * sin(life * 3.14159) * (0.7 + 0.3 * r2);
			const float grey = 0.32;
			color = float4(grey * alpha, grey * alpha, grey * alpha, alpha);
			kind = 2u;
		}
	}

	const float2 corner = BillboardCorner[vertexId % 6u];
	const float3 toCamera = normalize(Views[0].CameraPosition - position);
	const float3 right = normalize(cross(float3(0.0, 1.0, 0.0), toCamera) + float3(1.0e-4, 0.0, 0.0));
	const float3 up = cross(toCamera, right);
	float3 world = position + right * corner.x * size.x + up * corner.y * size.y;
	if (!visible)
	{
		world = HiddenPosition();
	}
	const ForwardClip clip = ForwardProjectWorld(world, world);
	VertexOutput output;
	output.Position = clip.Position;
	output.Local = corner;
	output.Color = color;
	output.Kind = kind;
	return output;
}

[shader("fragment")]
ForwardTransparentTargets fragmentMain(VertexOutput input)
{
	const float r2 = dot(input.Local, input.Local);
	float falloff;
	if (input.Kind == 0u)
	{
		falloff = exp(-r2 * 3.5) * saturate(1.0 - r2);
	}
	else if (input.Kind == 1u)
	{
		falloff = exp(-r2 * 4.0) + exp(-r2 * 16.0);
	}
	else
	{
		falloff = exp(-r2 * 2.5) * saturate(1.0 - r2);
	}
	const float4 color = input.Color * falloff;
	ForwardTransparentTargets output;
	output.Color = color;
	output.Indirect = float4(0.0, 0.0, 0.0, color.a);
	output.Reflectance = output.Indirect;
	output.Specular = output.Indirect;
	return output;
}

#endif
