// Black holes (Engine::GravitationalLensing) on the linear HDR scene, before depth of
// field, the camera lens, exposure and bloom.
//
// Every pixel's light ray is traced backward through the hole's region (a sphere of Reach
// R_s) as a Schwarzschild null geodesic: in Cartesian coordinates around the mass the
// photon accelerates by
//     a = -1.5 R_s h^2 x / r^5,  h = |x x v|
// (the orbit equation u'' + u = 1.5 R_s u^2), integrated with midpoint steps that shrink
// near the hole. The ray either falls through the horizon (the shadow: its background is
// black) or passes behind a surface of the rendered frame (tested against the depth buffer
// along the whole curved path, and on along a straight line once it leaves the region):
// lensed sky, floor and objects, Einstein rings, the far-side image, without cut-offs.
// Along the way it crosses the gas (GravitationalLensing::GasDensity mirrors it):
//  - a thick, turbulent accretion torus in the hole's equatorial plane, rotating
//    differentially (Kepler: omega ~ r^-3/2) so its clumps shear into streams;
//  - Orbits (0..3) inclined gas rings around the hole like electron shells, each with
//    three bright clumps racing around it;
// emitting in rainbow hues that flow with the gas (Doppler-brightened on the approaching
// side) and absorbing a little (emission-absorption, front to back). The deflection
// builds up only inside the region, so it fades to zero at its edge without a seam.
#include "Features/FeatureCommon.slang"

static const uint MaxLenses = 4;	   // GravitationalLensing::MaxLenses.
static const uint MaxSteps = 240;	   // GravitationalLensing::MaxSteps.
static const float GasStep = 0.4;	   // Largest step inside the gas (R_s): GravitationalLensing::GasStep.
static const float GasExtinction = 0.45; // Per R_s at density 1.

struct LensRecord
{
	float4 PositionRadius; // xyz: world position; w: Schwarzschild radius R_s.
	float4 Params;		   // x: strength (scales the bending); y: reach (R_s); z: gas radius (R_s); w: gas density.
	float4 Gas;			   // x: gas brightness; y: gas speed (rad/s at 3 R_s); z: orbit rings (0..3); w: unused.
	float4 Normal;		   // xyz: the disk's axis (unit); w: unused.
};

struct LensingParams
{
	float4 Forward; // xyz; w: tan(half horizontal fov).
	float4 Right;	// xyz; w: tan(half vertical fov).
	float4 Up;		// xyz; w: near plane (depth = near / view z).
	float4 Camera;	// xyz; w: lens count.
	float4 Misc;	// x: time (s).
	uint4 Size;		// xy: size.
	LensRecord Lenses[MaxLenses];
};

[[vk::binding(0, 0)]] StructuredBuffer<LensingParams> Params;
[[vk::binding(1, 0)]] Texture2D<float4> Color;
[[vk::binding(2, 0)]] Texture2D<float> Depth;
[[vk::binding(3, 0)]] SamplerState LinearClamp;
[[vk::binding(4, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> Output;

float ViewZ(float depth, float near)
{
	return depth > 0.0 ? near / depth : 1.0e9;
}

// --- The gas (GravitationalLensing::GasDensity) ---------------------------------------

float3 RotateAbout(float3 p, float3 axis, float angle)
{
	const float c = cos(angle);
	const float s = sin(angle);
	return p * c + cross(axis, p) * s + axis * dot(axis, p) * (1.0 - c);
}

float Lattice(int3 c)
{
	return FeatureHash01(uint(c.x) * 73856093u ^ uint(c.y) * 19349663u ^ uint(c.z) * 83492791u);
}

float ValueNoise(float3 p)
{
	const float3 cell = floor(p);
	const float3 f = p - cell;
	const float3 s = f * f * (3.0 - 2.0 * f);
	const int3 c = int3(cell);
	const float x00 = lerp(Lattice(c), Lattice(c + int3(1, 0, 0)), s.x);
	const float x10 = lerp(Lattice(c + int3(0, 1, 0)), Lattice(c + int3(1, 1, 0)), s.x);
	const float x01 = lerp(Lattice(c + int3(0, 0, 1)), Lattice(c + int3(1, 0, 1)), s.x);
	const float x11 = lerp(Lattice(c + int3(0, 1, 1)), Lattice(c + int3(1, 1, 1)), s.x);
	return lerp(lerp(x00, x10, s.y), lerp(x01, x11, s.y), s.z);
}

float Fbm(float3 p)
{
	return 0.67 * ValueNoise(p) + 0.33 * ValueNoise(p * 2.03 + 11.7);
}

float3 Rainbow(float hue)
{
	const float3 c = saturate(abs(frac(hue + float3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0);
	return lerp(float3(1.0, 1.0, 1.0), c, 0.75);
}

struct GasSample
{
	float Density;
	float3 Emission; // Per unit density.
};

// What the gas needs of one lens that does not change along a ray: the disk's frame and
// each ring's axis, frame and rotation this frame (computed once per pixel, not per step).
struct GasFrame
{
	float3 Axis;
	float3 E1;
	float3 E2;
	float3 RingAxis[3];
	float3 RingF1[3];
	float3 RingF2[3];
	float2 RingRotation[3]; // cos, sin of the ring's angle this frame.
	float RingSign[3];		// Direction of the ring's orbit.
	uint Rings;
};

GasFrame MakeGasFrame(float3 axis, float time, LensRecord lens)
{
	GasFrame frame;
	frame.Axis = axis;
	frame.E1 = normalize(abs(axis.y) < 0.9 ? cross(axis, float3(0.0, 1.0, 0.0)) : cross(axis, float3(1.0, 0.0, 0.0)));
	frame.E2 = cross(axis, frame.E1);
	frame.Rings = min(uint(lens.Gas.z), 3u);
	const float speed = lens.Gas.y;
	for (uint k = 0; k < 3u; ++k)
	{
		const float azimuth = float(k) * 2.0943951;
		const float3 ringAxis = normalize(axis * 0.42 + (frame.E1 * cos(azimuth) + frame.E2 * sin(azimuth)) * 0.91);
		const float radius = 4.2 + 1.4 * float(k);
		const float omega = 2.2 * speed * pow(radius / 3.0, -1.5) * (k == 1u ? -1.0 : 1.0);
		const float3 f1 = normalize(abs(ringAxis.y) < 0.9 ? cross(ringAxis, float3(0.0, 1.0, 0.0)) : cross(ringAxis, float3(1.0, 0.0, 0.0)));
		frame.RingAxis[k] = ringAxis;
		frame.RingF1[k] = f1;
		frame.RingF2[k] = cross(ringAxis, f1);
		frame.RingRotation[k] = float2(cos(-omega * time), sin(-omega * time));
		frame.RingSign[k] = sign(omega);
	}
	return frame;
}

float3 RotateBy(float3 p, float3 axis, float2 rotation)
{
	return p * rotation.x + cross(axis, p) * rotation.y + axis * dot(axis, p) * (1.0 - rotation.x);
}

// p: position relative to the hole in R_s; ray: the photon's direction (for the Doppler
// brightening).
GasSample Gas(float3 p, GasFrame frame, float3 ray, float time, LensRecord lens)
{
	GasSample result;
	result.Density = 0.0;
	result.Emission = float3(0.0, 0.0, 0.0);
	const float outer = lens.Params.z;
	const float r = length(p);
	if (r < 1.2 || r > outer)
	{
		return result;
	}
	const float speed = lens.Gas.y;
	const float3 axis = frame.Axis;

	// The torus: differential rotation shears the turbulence into orbital streams.
	float3 color = float3(0.0, 0.0, 0.0);
	float density = 0.0;
	{
		const float z = dot(p, axis);
		const float rho = length(p - axis * z);
		const float height = 0.3 + 0.12 * rho;
		const float radial = smoothstep(2.2, 3.4, rho) * (1.0 - smoothstep(outer * 0.55, outer * 0.95, rho));
		const float envelope = radial * exp(-z * z / (height * height));
		// The rotation, noise and colour only where the torus has gas at all.
		if (envelope > 1.0e-3)
		{
			const float omega = speed * pow(max(rho, 1.5) / 3.0, -1.5);
			const float3 q = RotateAbout(p, axis, -omega * time);
			const float turbulence = saturate(Fbm(q * 0.85 + 7.1) * 1.7 - 0.35);
			const float d = envelope * turbulence;
			const float angle = atan2(dot(q, frame.E2), dot(q, frame.E1));
			const float3 orbit = normalize(cross(axis, p) + 1.0e-5);
			const float beaming = pow(saturate(1.0 + 0.55 * dot(orbit, -ray)), 2.0);
			const float heat = pow(3.0 / max(rho, 2.5), 1.3);
			color += Rainbow(angle / 6.2831853 + rho * 0.07 + time * 0.04) * (d * heat * beaming);
			density += d;
		}
	}

	// Electron-shell rings: inclined orbits with clumps racing around them.
	for (uint k = 0; k < frame.Rings; ++k)
	{
		const float3 ringAxis = frame.RingAxis[k];
		const float radius = 4.2 + 1.4 * float(k);
		const float z = dot(p, ringAxis);
		const float rho = length(p - ringAxis * z);
		const float tube = 0.35 + 0.1 * float(k);
		const float falloff = exp(-((rho - radius) * (rho - radius) + z * z) / (tube * tube));
		if (falloff < 1.0e-3)
		{
			continue;
		}
		const float3 q = RotateBy(p, ringAxis, frame.RingRotation[k]);
		const float angle = atan2(dot(q, frame.RingF2[k]), dot(q, frame.RingF1[k]));
		const float clumps = pow(0.5 + 0.5 * cos(3.0 * angle), 10.0);
		const float stream = saturate(Fbm(q * 1.5 + float(k) * 13.0) * 1.5 - 0.3);
		const float d = falloff * (0.25 + 0.6 * stream + 1.6 * clumps);
		const float3 orbit = normalize(cross(ringAxis, p) + 1.0e-5) * frame.RingSign[k];
		const float beaming = pow(saturate(1.0 + 0.55 * dot(orbit, -ray)), 2.0);
		color += Rainbow(angle / 6.2831853 + float(k) * 0.33 + time * 0.12) * (d * (0.8 + 1.5 * clumps) * beaming);
		density += d;
	}
	result.Density = density * lens.Params.w;
	result.Emission = density > 1.0e-5 ? color / density * lens.Gas.x : float3(0.0, 0.0, 0.0);
	return result;
}

// --- The geodesic -----------------------------------------------------------------------

float3 Acceleration(float3 x, float3 v, float rs)
{
	const float3 h = cross(x, v);
	const float r2 = dot(x, x);
	return -1.5 * rs * dot(h, h) * x / (r2 * r2 * sqrt(r2));
}

// --- Screen-space hits ----------------------------------------------------------------

// Projects a point (relative to the camera) into the frame. False behind the near plane.
bool ProjectPoint(LensingParams p, float3 rel, out float2 uv, out float z)
{
	z = dot(rel, p.Forward.xyz);
	uv = float2(0.0, 0.0);
	if (z <= p.Up.w)
	{
		return false;
	}
	const float2 ndc = float2(dot(rel, p.Right.xyz) / (z * p.Forward.w), dot(rel, p.Up.xyz) / (z * p.Right.w));
	uv = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
	return true;
}

// Tests a point on the bent ray against the depth buffer: a hit when it lies just behind
// the surface drawn at its pixel (within a thickness that grows with distance and with how
// far the ray moved in depth since the last test, so fast steps cannot tunnel through).
// Only depth is read per step; the colour is fetched once, where the trace ends. Records
// where the ray last passed over geometry (for misses into the void).
bool SceneHit(LensingParams p, float3 rel, float stepDz, inout float2 hitUv, inout float2 lastGeometryUv, inout bool haveGeometry)
{
	float2 uv;
	float z;
	if (!ProjectPoint(p, rel, uv, z) || any(uv < 0.0) || any(uv > 1.0))
	{
		return false;
	}
	const uint2 pixel = min(uint2(uv * float2(p.Size.xy)), p.Size.xy - 1u);
	const float sceneZ = ViewZ(Depth.Load(int3(pixel, 0)), p.Up.w);
	if (sceneZ >= 1.0e8)
	{
		return false; // Sky: nothing to hit.
	}
	const float gap = z - sceneZ;
	if (gap < 0.0)
	{
		lastGeometryUv = uv;
		haveGeometry = true;
		return false;
	}
	const float thickness = max(max(0.25, 0.04 * sceneZ), abs(stepDz) * 1.5);
	if (gap > thickness)
	{
		return false;
	}
	hitUv = uv;
	return true;
}

float3 FrameColor(float2 uv)
{
	return Color.SampleLevel(LinearClamp, uv, 0.0).rgb;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	const LensingParams p = Params[0];
	if (id.x >= p.Size.x || id.y >= p.Size.y)
	{
		return;
	}
	const float2 size = float2(p.Size.xy);
	const float2 uv = (float2(id.xy) + 0.5) / size;
	const float3 ray = FeatureViewRay(uv, p.Forward, p.Right, p.Up);
	const float near = p.Up.w;
	const float pixelZ = ViewZ(Depth.Load(int3(id.xy, 0)), near);
	// How far along the (unbent) ray the pixel's own surface is.
	const float sceneDistance = pixelZ >= 1.0e8 ? 1.0e9 : pixelZ / max(dot(ray, p.Forward.xyz), 1.0e-3);
	const float3 original = Color.Load(int3(id.xy, 0)).rgb;
	float3 color = original;
	const float time = p.Misc.x;

	const uint count = min(uint(p.Camera.w), MaxLenses);
	for (uint i = 0; i < count; ++i)
	{
		const LensRecord lens = p.Lenses[i];
		const float rs = lens.PositionRadius.w;
		if (rs <= 0.0)
		{
			continue;
		}
		const float bendRs = rs * lens.Params.x;
		const float region = max(lens.Params.y, 4.0) * rs;
		const float gasOuter = lens.Params.z * rs;
		const float3 axis = normalize(lens.Normal.xyz);

		// Enter the region (or start at the camera inside it).
		float3 x = p.Camera.xyz - lens.PositionRadius.xyz;
		float3 v = ray;
		float travelled = 0.0;
		const float bHalf = dot(x, v);
		const float c = dot(x, x) - region * region;
		if (c > 0.0)
		{
			const float disc = bHalf * bHalf - c;
			if (disc <= 0.0 || bHalf >= 0.0)
			{
				continue; // The ray misses the region.
			}
			travelled = -bHalf - sqrt(disc);
			if (travelled >= sceneDistance)
			{
				continue; // The pixel's surface is in front of the region.
			}
			x += v * travelled;
		}

		const GasFrame gasFrame = MakeGasFrame(axis, time, lens); // Only for rays that enter the region.
		float3 light = float3(0.0, 0.0, 0.0);
		float transmittance = 1.0;
		// The bent ray is traced against the depth buffer along its whole curved path (the
		// frame is the only record of the scene): the first surface it passes behind is what
		// the pixel sees. So a surface inside the region is seen through the bending around
		// it, never cut off at the depth its unbent pixel happened to have.
		float2 hitUv = uv;
		float2 lastGeometryUv = uv;
		bool haveGeometry = false;
		const float3 camToHole = lens.PositionRadius.xyz - p.Camera.xyz;
		uint outcome = 0u; // 0: undecided (orbiting: treated as captured), 1: captured, 2: escaped, 3: hit the scene.
		float lastZ = dot(camToHole + x, p.Forward.xyz);
		for (uint step = 0; step < MaxSteps; ++step)
		{
			const float r = length(x);
			if (r < rs)
			{
				outcome = 1u;
				break;
			}
			if (r > region * 1.0001 && dot(x, v) > 0.0)
			{
				outcome = 2u;
				break;
			}
			float ds = max(0.14 * (r - 0.5 * rs), 0.02 * rs);
			const bool inGas = r < gasOuter + GasStep * rs && lens.Params.w > 0.0;
			if (inGas)
			{
				ds = min(ds, GasStep * rs);
			}
			// Midpoint (RK2) step of the geodesic; the gas is sampled at the midpoint.
			const float3 a1 = Acceleration(x, v, bendRs);
			const float3 xm = x + v * (0.5 * ds);
			const float3 vm = normalize(v + a1 * (0.5 * ds));
			const float3 a2 = Acceleration(xm, vm, bendRs);
			const float3 next = x + vm * ds;
			const float nextZ = dot(camToHole + next, p.Forward.xyz);
			if (SceneHit(p, camToHole + next, nextZ - lastZ, hitUv, lastGeometryUv, haveGeometry))
			{
				outcome = 3u; // The gas behind the surface is hidden: stop before it.
				break;
			}
			if (inGas)
			{
				const GasSample gas = Gas(xm / rs, gasFrame, vm, time, lens);
				if (gas.Density > 0.0)
				{
					const float dt = ds / rs;
					const float absorbed = exp(-GasExtinction * gas.Density * dt);
					// Emission integrated over the step with its own absorption.
					light += transmittance * gas.Emission * (1.0 - absorbed) / GasExtinction;
					transmittance *= absorbed;
				}
			}
			x = next;
			v = normalize(v + a2 * ds);
			lastZ = nextZ;
			if (transmittance < 0.005)
			{
				break;
			}
		}

		float3 background = float3(0.0, 0.0, 0.0);
		if (outcome == 3u)
		{
			background = FrameColor(hitUv);
		}
		else if (outcome == 2u)
		{
			// Out of the region the ray is straight: march on (in steps that grow with
			// distance) until it passes behind a surface or leaves the frame.
			bool hit = false;
			float3 rel = camToHole + x;
			float t = max(0.25 * rs, 0.02 * length(rel));
			for (uint k = 0; k < 64u; ++k)
			{
				const float3 nextRel = rel + v * t;
				const float nextZ = dot(nextRel, p.Forward.xyz);
				float2 uv;
				float z;
				if (!ProjectPoint(p, nextRel, uv, z) || any(uv < -0.02) || any(uv > 1.02))
				{
					break;
				}
				if (SceneHit(p, nextRel, nextZ - lastZ, hitUv, lastGeometryUv, haveGeometry))
				{
					hit = true;
					break;
				}
				rel = nextRel;
				lastZ = nextZ;
				t *= 1.12;
			}
			if (hit)
			{
				background = FrameColor(hitUv);
			}
			else
			{
				// No surface on screen: the direction's own point far away (sky, or distant
				// geometry the march ran out of steps before), fading to the pixel itself where
				// it leaves the frame.
				float2 uv;
				float z;
				if (ProjectPoint(p, camToHole + x + v * 1.0e4, uv, z))
				{
					const float2 outside = max(max(-uv, uv - 1.0), 0.0);
					const float onScreen = 1.0 - saturate(max(outside.x, outside.y) * 8.0);
					background = lerp(original, Color.SampleLevel(LinearClamp, saturate(uv), 0.0).rgb, onScreen);
					// A downward ray that found no floor went past the ground's edge into the
					// void: it keeps the last ground it crossed rather than showing sky there.
					const uint2 pixel = min(uint2(saturate(uv) * size), p.Size.xy - 1u);
					if (v.y < 0.0 && haveGeometry && Depth.Load(int3(pixel, 0)) <= 0.0)
					{
						background = FrameColor(lastGeometryUv);
					}
				}
				else
				{
					background = haveGeometry ? FrameColor(lastGeometryUv) : original; // Bent behind the camera.
				}
			}
		}
		color = background * transmittance + light;
	}
	Output[id.xy] = float4(color, 1.0);
}
