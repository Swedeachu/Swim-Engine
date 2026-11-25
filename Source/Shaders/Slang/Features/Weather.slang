// Engine::Weather: rain streaks, snow flakes and rain splashes in the Forward+ transparent
// pass. Nothing is stored per particle: instance i is a hash of its world column cell and
// its index in it (see Weather.h), so the field is stable in the world as the camera moves.
// Instances [0, Grid.x) are falling particles, [Grid.x, Grid.x + Grid.w) splashes.

#include "Features/FeatureCommon.slang"
#include "ForwardPlus/ForwardFrame.slang"

struct WeatherConstants
{
	float4 CameraTime; // Camera xyz, time.
	float4 Fall;	   // Rain speed, snow speed, band height, radius.
	float4 Shape;	   // Drop length, drop width, flake size, radians per pixel.
	float4 Motion;	   // Wind velocity x, z; snow fraction; opacity.
	float4 Color;	   // Lit colour scale rgb; intensity.
	float4 Occlusion;  // Origin x, z; cell size; size (0: none).
	uint4 Grid;		   // Particles drawn, columns per side, per column, splashes drawn.
	float4 Splash;	   // Splash radius, size, cells per side, unused.
};

[[vk::push_constant]] ConstantBuffer<WeatherConstants> Constants;

[[vk::binding(0, 2)]] StructuredBuffer<float> OcclusionHeights;

static const float WeatherHidden = -1.0e8;

float4 WeatherRandom(int2 cell, uint index)
{
	uint h = FeatureHash(uint(cell.x) * 73856093u ^ uint(cell.y) * 19349663u ^ index * 83492791u);
	float4 r;
	for (uint i = 0u; i < 4u; ++i)
	{
		h = FeatureHash(h + i);
		r[i] = float(h & 0xFFFFFFu) / 16777216.0;
	}
	return r;
}

// The top of what stops precipitation over xz, or WeatherHidden without a field there.
float WeatherOcclusion(float2 xz)
{
	const float size = Constants.Occlusion.w;
	if (size <= 0.0)
	{
		return WeatherHidden;
	}
	const float2 cell = floor((xz - Constants.Occlusion.xy) / Constants.Occlusion.z);
	if (any(cell < 0.0) || any(cell >= size))
	{
		return WeatherHidden;
	}
	return OcclusionHeights[uint(cell.y) * uint(size) + uint(cell.x)];
}

struct VertexOutput
{
	float4 Position : SV_Position;
	float3 WorldPosition : WORLD_POSITION;
	float2 Uv : WEATHER_UV;
	float Alpha : WEATHER_ALPHA;
	nointerpolation uint Kind : WEATHER_KIND; // 0 drop, 1 flake, 2 splash.
};

static const float2 QuadCorners[6] = { float2(-1, -1), float2(1, -1), float2(1, 1), float2(-1, -1), float2(1, 1), float2(-1, 1) };

VertexOutput Hidden()
{
	VertexOutput output;
	output.Position = float4(0.0, 0.0, -1.0, 1.0); // Outside the depth range: clipped.
	output.WorldPosition = float3(0.0, 0.0, 0.0);
	output.Uv = float2(0.0, 0.0);
	output.Alpha = 0.0;
	output.Kind = 0u;
	return output;
}

// A quad centred on `center` spanning `axis` (half length) and `side` (half width).
VertexOutput Emit(float3 center, float3 axis, float3 side, float2 corner, float alpha, uint kind)
{
	VertexOutput output;
	const float3 world = center + axis * corner.y + side * corner.x;
	output.Position = ForwardProjectWorld(world, world).Position;
	output.WorldPosition = world;
	output.Uv = corner;
	output.Alpha = alpha;
	output.Kind = kind;
	return output;
}

[shader("vertex")]
VertexOutput vertexMain(uint vertexId : SV_VulkanVertexID, uint instance : SV_VulkanInstanceID)
{
	const float3 camera = Constants.CameraTime.xyz;
	const float time = Constants.CameraTime.w;
	const float2 corner = QuadCorners[vertexId % 6u];
	const float pixelAngle = Constants.Shape.w;
	const float intensity = Constants.Color.w;

	if (instance >= Constants.Grid.x)
	{
		// A splash: a short-lived crown on the occluding surface.
		const uint index = instance - Constants.Grid.x;
		const uint side = uint(Constants.Splash.z);
		const uint total = side * side;
		const uint column = (index * 7919u) % total;
		const uint layer = index / total;
		const float cellSize = Constants.Splash.x * 2.0 / float(side);
		const int2 cell = int2(floor(camera.xz / cellSize)) + int2(int(column % side), int(column / side)) - int(side / 2u);
		const float4 r = WeatherRandom(cell, layer + 977u);
		const float period = 0.45 + 0.5 * r.z;
		const float age = frac(time / period + r.w) / 0.3;
		const float2 xz = (float2(cell) + r.xy) * cellSize;
		const float surface = WeatherOcclusion(xz);
		const float distance = length(xz - camera.xz);

		if (age >= 1.0 || surface <= WeatherHidden || abs(surface - camera.y) > Constants.Fall.z * 0.5 || distance > Constants.Splash.x)
		{
			return Hidden();
		}

		const float3 toCamera = normalize(camera - float3(xz.x, surface, xz.y));
		const float3 right = normalize(cross(float3(0.0, 1.0, 0.0), toCamera));
		const float size = Constants.Splash.y * (0.5 + age);
		const float width = max(size, distance * pixelAngle);
		const float fade = (1.0 - age) * saturate((Constants.Splash.x - distance) / (Constants.Splash.x * 0.3)) * size / width;
		const float3 center = float3(xz.x, surface + size * 0.5, xz.y);
		return Emit(center, float3(0.0, size * 0.5, 0.0), right * width, corner, fade * (1.0 - Constants.Motion.z) * 0.6, 2u);
	}

	const uint side = Constants.Grid.y;
	const uint total = side * side;
	const uint layer = instance / total;
	const uint column = ((instance % total) * 7919u) % total;
	const float radius = Constants.Fall.w;
	const float cellSize = radius * 2.0 / float(side);
	const int2 cell = int2(floor(camera.xz / cellSize)) + int2(int(column % side), int(column / side)) - int(side / 2u);
	const float4 r = WeatherRandom(cell, layer);
	const float4 r2 = WeatherRandom(cell, layer + 4099u);
	const bool snow = r2.x < Constants.Motion.z;
	const float speed = snow ? Constants.Fall.y * (0.75 + 0.5 * r2.y) : Constants.Fall.x * (0.9 + 0.2 * r2.y);
	const float height = Constants.Fall.z;
	const float top = camera.y + height * 0.5;

	// Falling through the band at its own phase; the wind carries it from the band's top.
	const float fall = frac((r.z * height - time * speed - (top - height)) / height);
	float3 position;
	position.xz = (float2(cell) + r.xy) * cellSize;
	position.y = top - height + fall * height;
	const float2 wind = Constants.Motion.xy;
	const float fallen = (top - position.y) / speed;
	position.xz += wind * (fallen - height * 0.5 / speed);

	if (snow)
	{
		position.x += sin(time * (0.8 + 0.6 * r2.z) + r.x * 6.2832) * 0.35;
		position.z += cos(time * (0.6 + 0.5 * r2.w) + r.y * 6.2832) * 0.35;
	}

	const float horizontal = length(position.xz - camera.xz);
	const float distance = length(position - camera);

	if (position.y < WeatherOcclusion(position.xz) || horizontal > radius || distance < 0.25)
	{
		return Hidden();
	}

	const float edge = saturate((radius - horizontal) / (radius * 0.35)) * saturate(fall * 6.0) * saturate((1.0 - fall) * 6.0) *
					   saturate((distance - 0.25) * 1.5);
	const float3 toCamera = (camera - position) / distance;

	if (snow)
	{
		const float3 right = normalize(cross(float3(0.0, 1.0, 0.0), toCamera));
		const float3 up = cross(toCamera, right);
		const float size = Constants.Shape.z * (0.6 + 0.8 * r2.w);
		const float width = max(size, distance * pixelAngle * 1.2);
		const float alpha = edge * min(1.0, Constants.Motion.w * 2.2) * (size * size) / (width * width);
		return Emit(position, up * width, right * width, corner, alpha, 1u);
	}

	const float3 velocity = float3(wind.x, -speed, wind.y);
	const float3 axis = normalize(velocity);
	const float3 across = normalize(cross(axis, toCamera));
	const float streak = Constants.Shape.x * speed / Constants.Fall.x;
	const float width = max(Constants.Shape.y, distance * pixelAngle * 1.2);
	const float alpha = edge * Constants.Motion.w * Constants.Shape.y / width;
	return Emit(position, axis * (streak * 0.5), across * (width * 0.5), corner, alpha, 0u);
}

[shader("fragment")]
ForwardTransparentTargets fragmentMain(VertexOutput input)
{
	float coverage;

	if (input.Kind == 0u)
	{
		// A streak: soft across, tapering toward its tail.
		coverage = (1.0 - input.Uv.x * input.Uv.x) * saturate(1.0 - abs(input.Uv.y)) * saturate(input.Uv.y * 0.5 + 0.8);
	}
	else if (input.Kind == 1u)
	{
		coverage = pow(saturate(1.0 - dot(input.Uv, input.Uv)), 1.5);
	}
	else
	{
		// A crown: a ring opening upward.
		const float2 p = float2(input.Uv.x, input.Uv.y * 0.5 + 0.5);
		const float r = length(float2(p.x, p.y * 1.6));
		coverage = saturate(1.0 - abs(r - 0.7) * 4.0) * saturate(1.0 - p.y);
	}

	const float alpha = saturate(coverage * input.Alpha);

	if (alpha <= 0.002)
	{
		discard;
	}

	StandardPbrSurface surface;
	const float3 toCamera = normalize(Views[0].CameraPosition - input.WorldPosition);
	surface.Normal = input.Kind == 2u ? float3(0.0, 1.0, 0.0) : toCamera;
	surface.BaseColor = input.Kind == 1u ? float3(0.9, 0.92, 0.95) : float3(0.8, 0.84, 0.9);
	surface.Metallic = 0.0;
	surface.PerceptualRoughness = input.Kind == 1u ? 0.9 : 0.35;
	surface.Occlusion = 1.0;
	surface.Emissive = float3(0.0, 0.0, 0.0);
	surface.Alpha = alpha;
	surface.Discard = false;
	surface.Thin = false;
	ForwardLighting lit = ForwardLightSurface(surface, input.WorldPosition, input.Position.xy);
	lit.Radiance *= Constants.Color.rgb;
	return ForwardTransparentWrite(lit, alpha);
}
