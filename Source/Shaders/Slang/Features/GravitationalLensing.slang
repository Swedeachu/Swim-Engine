// Black holes (Engine::GravitationalLensing) on the linear HDR scene, before depth of
// field, the camera lens, exposure and bloom.
//
// Every pixel's light ray is traced backward through the hole's region (a sphere of Reach
// R_s) as a Schwarzschild null geodesic: in Cartesian coordinates around the mass the
// photon accelerates by
//     a = -1.5 R_s h^2 x / r^5,  h = |x x v|
// (the orbit equation u'' + u = 1.5 R_s u^2), integrated with midpoint steps that shrink
// near the hole. The ray either falls through the horizon (the shadow: its background is
// black) or escapes. An escaped ray's background is a warp of the rendered frame: the
// frame where the ray's bent direction projects (far away), averaged over the patch the
// pixel covers there, so the warped image is smooth: lensed sky and floor, Einstein rings
// and the far-side image. Surfaces in front of the region are left as they are.
// Along the way the ray crosses the gas (GravitationalLensing::GasDensity mirrors it):
//  - a thick, turbulent accretion torus in the hole's equatorial plane, rotating
//    differentially (Kepler: omega ~ r^-3/2) so its clumps shear into streams;
//  - Orbits (0..3) inclined gas rings around the hole like electron shells, each with
//    three bright clumps racing around it;
// emitting in rainbow hues that flow with the gas (Doppler-brightened on the approaching
// side) and absorbing a little (emission-absorption, front to back); gas behind the
// pixel's own surface is hidden. The deflection builds up only inside the region, so it
// fades to zero at its edge without a seam.
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

// --- The frame --------------------------------------------------------------------------

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

// Each thread's lensed frame position (x < 0: none), shared so the group can measure how
// fast it changes from pixel to pixel.
groupshared float2 SharedUv[8][8];

// The frame averaged over the patch one output pixel covers in it (axes a and b, in frame
// pixels). Where the lens magnifies, the patch is still at least a pixel and a half across,
// so the frame's pixel steps (the jagged floor edge, stretched into bands) blend into a
// smooth gradient instead of being enlarged; where it shrinks the image, the average keeps
// the squeezed pattern from aliasing.
float3 FootprintColor(float2 uv, float2 a, float2 b, float2 size)
{
	const float MinAxis = 1.5;
	const float MaxAxis = 24.0;
	const float la = length(a);
	const float lb = length(b);
	a = la > 1.0e-4 ? a * (clamp(la, MinAxis, MaxAxis) / la) : float2(MinAxis, 0.0);
	b = lb > 1.0e-4 ? b * (clamp(lb, MinAxis, MaxAxis) / lb) : float2(0.0, MinAxis);
	const uint taps = max(length(a), length(b)) > 6.0 ? 4u : 3u;
	float3 sum = float3(0.0, 0.0, 0.0);
	for (uint j = 0; j < taps; ++j)
	{
		for (uint i = 0; i < taps; ++i)
		{
			const float2 o = (float2(i, j) + 0.5) / float(taps) - 0.5;
			sum += Color.SampleLevel(LinearClamp, saturate(uv + (a * o.x + b * o.y) / size), 0.0).rgb;
		}
	}
	return sum / float(taps * taps);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 dispatchId : SV_DispatchThreadID, uint3 local : SV_GroupThreadID)
{
	const LensingParams p = Params[0];
	// Every thread takes part in the group's exchange; those outside the frame trace nothing.
	const bool inside = dispatchId.x < p.Size.x && dispatchId.y < p.Size.y;
	const uint2 id = min(dispatchId.xy, p.Size.xy - 1u);
	const float2 size = float2(p.Size.xy);
	const float2 uv = (float2(id.xy) + 0.5) / size;
	const float3 ray = FeatureViewRay(uv, p.Forward, p.Right, p.Up);
	const float near = p.Up.w;
	const float pixelZ = ViewZ(Depth.Load(int3(id.xy, 0)), near);
	// How far along the (unbent) ray the pixel's own surface is; the sky is far away.
	const bool sky = pixelZ >= 1.0e8;
	const float sceneDistance = sky ? 1.0e4 : pixelZ / max(dot(ray, p.Forward.xyz), 1.0e-3);
	const float3 original = Color.Load(int3(id.xy, 0)).rgb;
	float3 color = original;
	const float time = p.Misc.x;
	// The last lens's background, when it is a lookup of the frame (sampled after the exchange).
	float2 frameUv = float2(-1.0, -1.0);
	float frameTransmittance = 1.0;
	float3 frameLight = float3(0.0, 0.0, 0.0);

	const uint count = inside ? min(uint(p.Camera.w), MaxLenses) : 0u;
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
		bool captured = true; // Orbiting until the step budget runs out counts as captured.
		for (uint step = 0; step < MaxSteps; ++step)
		{
			const float r = length(x);
			if (r < rs)
			{
				break;
			}
			if (r > region * 1.0001 && dot(x, v) > 0.0)
			{
				captured = false;
				break;
			}
			// Small steps: a coarser integration left the bent direction off by a step-dependent
			// amount, which showed as concentric bands and jagged steps in the warped image.
			float ds = max(0.07 * (r - 0.5 * rs), 0.015 * rs);
			// Gas in front of the pixel's own surface only (a nearer surface hides the rest).
			const bool inGas = r < gasOuter + GasStep * rs && lens.Params.w > 0.0 && travelled < sceneDistance;
			if (inGas)
			{
				ds = min(ds, GasStep * rs);
			}
			// Midpoint (RK2) step of the geodesic; the gas is sampled at the midpoint.
			const float3 a1 = Acceleration(x, v, bendRs);
			const float3 xm = x + v * (0.5 * ds);
			const float3 vm = normalize(v + a1 * (0.5 * ds));
			const float3 a2 = Acceleration(xm, vm, bendRs);
			if (inGas)
			{
				const GasSample gas = Gas(xm / rs, gasFrame, vm, time, lens);
				if (gas.Density > 0.0)
				{
					const float dt = min(ds, sceneDistance - travelled) / rs;
					const float absorbed = exp(-GasExtinction * gas.Density * dt);
					// Emission integrated over the step with its own absorption.
					light += transmittance * gas.Emission * (1.0 - absorbed) / GasExtinction;
					transmittance *= absorbed;
				}
			}
			x += vm * ds;
			v = normalize(v + a2 * ds);
			travelled += ds;
			if (transmittance < 0.005)
			{
				break;
			}
		}

		float3 background = float3(0.0, 0.0, 0.0);
		frameUv = float2(-1.0, -1.0);
		if (!captured)
		{
			// The escaped ray's direction, seen far away: the frame where that direction
			// projects is what it sees. (Going on for the pixel's own distance instead cut the
			// image straight across where the pixels' depth jumps, at the horizon.)
			const float3 rel = lens.PositionRadius.xyz - p.Camera.xyz + x + v * 1.0e4;
			float2 source;
			float z;
			if (ProjectPoint(p, rel, source, z))
			{
				const float2 outside = max(max(-source, source - 1.0), 0.0);
				const float onScreen = 1.0 - saturate(max(outside.x, outside.y) * 8.0);
				background = lerp(original, Color.SampleLevel(LinearClamp, saturate(source), 0.0).rgb, onScreen);
				if (onScreen >= 1.0)
				{
					frameUv = source; // Sampled over its footprint after the exchange below.
				}
			}
			else
			{
				background = original; // Bent behind the camera.
			}
		}
		color = background * transmittance + light;
		frameTransmittance = transmittance;
		frameLight = light;
	}

	SharedUv[local.y][local.x] = frameUv;
	GroupMemoryBarrierWithGroupSync();
	if (!inside || frameUv.x < 0.0)
	{
		if (inside)
		{
			Output[id.xy] = float4(color, 1.0);
		}
		return;
	}
	// How far the lensed position moves between neighbouring pixels (within the group; not
	// across the shadow or a jump between images): the footprint to average over.
	const uint nx = local.x < 7u ? local.x + 1u : local.x - 1u;
	const uint ny = local.y < 7u ? local.y + 1u : local.y - 1u;
	const float2 ux = SharedUv[local.y][nx];
	const float2 uy = SharedUv[ny][local.x];
	float2 a = ux.x >= 0.0 ? (ux - frameUv) * size : float2(0.0, 0.0);
	float2 b = uy.x >= 0.0 ? (uy - frameUv) * size : float2(0.0, 0.0);
	a = length(a) < 48.0 ? a : float2(0.0, 0.0);
	b = length(b) < 48.0 ? b : float2(0.0, 0.0);
	color = FootprintColor(frameUv, a, b, size) * frameTransmittance + frameLight;
	Output[id.xy] = float4(color, 1.0);
}
