// The wind field of Engine::WindSettings (Renderer/Features/Wind.h), bit-for-bit the CPU
// model, plus vertex sway for flexible geometry (grass, leaves, banners, cables).
#ifndef SWIM_WIND_SLANG
#define SWIM_WIND_SLANG

// Engine::GpuWind: Flow = (direction x, direction z, speed, gustiness),
// Gust = (gust scale, turbulence, time, 0).
struct WindParams
{
	float4 Flow;
	float4 Gust;
};

static const float WindTwoPi = 6.28318530718;

float WindGust(WindParams wind, float2 xz, float time)
{
	const float2 d = wind.Flow.xy;
	const float scale = wind.Gust.x;
	const float along = dot(xz, d) / scale;
	const float across = (xz.y * d.x - xz.x * d.y) / scale;
	const float travel = time * max(wind.Flow.z, 0.5) / scale;
	const float wave = sin((along - travel) * WindTwoPi) * 0.6 + sin((along * 2.3 + across * 0.7 - travel * 1.9) * WindTwoPi) * 0.4;
	return 0.5 + 0.5 * wave;
}

float3 WindVelocity(WindParams wind, float3 position, float time)
{
	const float2 d = wind.Flow.xy;
	const float speed = wind.Flow.z;
	const float gust = WindGust(wind, position.xz, time);
	const float flow = speed * (1.0 + wind.Flow.w * (gust * 2.0 - 1.0));
	const float eddy = sin(time * 1.7 + (position.x - position.z) * 0.31) * sin(time * 0.6 + position.z * 0.17);
	const float cross = eddy * wind.Gust.y * speed * 0.35;
	return float3(d.x * flow - d.y * cross, 0.0, d.y * flow + d.x * cross);
}

float WindHash(float3 anchor)
{
	const int3 cell = int3(floor(anchor * 4.0));
	uint h = uint(cell.x) * 73856093u ^ uint(cell.y) * 19349663u ^ uint(cell.z) * 83492791u;
	h = (h ^ (h >> 16u)) * 2246822519u;
	h ^= h >> 13u;
	return float(h & 0xFFFFu) / 65535.0;
}

// The displacement (metres) of a flexible vertex at `time`. anchor identifies the plant or
// object (its phase and the wind it feels); weight is 0 at the rooted end and 1 at the free
// end; flexibility is the lean in metres per m/s of wind at weight 1. The vertex leans
// downwind with the gusts, flutters around that lean and dips so its length stays about
// the same. Evaluate it at the previous frame's time as well for motion vectors.
float3 WindSway(WindParams wind, float3 anchor, float weight, float flexibility, float time)
{
	if (flexibility <= 0.0 || weight <= 0.0)
	{
		return float3(0.0, 0.0, 0.0);
	}

	const float3 velocity = WindVelocity(wind, anchor, time);
	const float phase = WindHash(anchor) * WindTwoPi;
	const float rate = 1.3 + 0.12 * wind.Flow.z;
	const float flutter = sin(time * rate * WindTwoPi * 0.5 + phase) * 0.7 + sin(time * rate * 2.7 + phase * 1.7) * 0.3;
	const float2 across = float2(-wind.Flow.y, wind.Flow.x);
	const float calm = 0.25 + 0.1 * wind.Flow.z; // Even still air keeps a faint movement.
	float2 offset = velocity.xz * (0.75 + 0.25 * flutter) + across * flutter * calm;
	offset *= flexibility * weight * weight;
	const float lean = length(offset);
	const float limit = 0.6 * weight;
	offset *= lean > limit ? limit / lean : 1.0;
	return float3(offset.x, -0.5 * dot(offset, offset), offset.y);
}

#endif
