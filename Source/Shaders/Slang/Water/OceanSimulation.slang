// FFT ocean (Engine::OceanSimulation), three programs from one source, run per cascade:
//  - OceanSpectrum (OCEAN_PASS=0): the Tessendorf spectrum h(k, t) from a JONSWAP spectrum
//    with directional spreading, evaluated each frame from a hashed Gaussian h0(k) (no
//    stored state), as eight real fields packed two per complex value into two textures;
//  - OceanFft (OCEAN_PASS=1): one inverse radix-2 Stockham FFT per row (or column) of N
//    complex pairs in group-shared memory;
//  - OceanAssemble (OCEAN_PASS=2): displacement, derivatives and whitecap foam (from the
//    Jacobian of the horizontal displacement, decaying from last frame's foam).
// Spectrum layout: texel n holds wave number index n < N/2 ? n : n - N (FFT order), so the
// inverse transform needs no sign correction.

#include "Features/FeatureCommon.slang"

#ifndef OCEAN_PASS
#define OCEAN_PASS 0
#endif

struct OceanConstants
{
	float4 Wind;	 // xy: wind direction (unit), z: wind speed (m/s), w: time (s, wrapped to the repeat period).
	float4 Spectrum; // x: amplitude, y: fetch (m), z: spreading exponent, w: base angular frequency (repeat).
	float4 Cascade;	 // x: tile size (m), y: k min, z: k max, w: small-wave damping length (m).
	float4 Foam;	 // x: choppiness, y: foam threshold (Jacobian), z: foam injection, w: foam kept from last frame (0..1).
	uint4 Size;		 // x: N, y: seed, z: FFT direction (0 rows, 1 columns), w: history valid.
};

[[vk::push_constant]] ConstantBuffer<OceanConstants> Constants;

static const float OceanGravity = 9.81;
static const float OceanPi = 3.14159265359;

float2 ComplexMul(float2 a, float2 b)
{
	return float2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x);
}

float2 ComplexI(float2 a)
{
	return float2(-a.y, a.x);
}

#if OCEAN_PASS == 0
[[vk::binding(0, 0)]] [[vk::image_format("rgba32f")]] RWTexture2D<float4> SpectrumA;
[[vk::binding(1, 0)]] [[vk::image_format("rgba32f")]] RWTexture2D<float4> SpectrumB;

// Two independent standard normal values per wave number (Box-Muller).
float2 OceanGaussian(int2 index, uint seed)
{
	const uint base = FeatureHash(uint(index.x & 0xFFFF) | (uint(index.y & 0xFFFF) << 16)) ^ FeatureHash(seed * 0x9E3779B9u);
	const float u1 = max(float(FeatureHash(base) >> 8) * (1.0 / 16777216.0), 1.0e-7);
	const float u2 = float(FeatureHash(base ^ 0x68E31DA4u) >> 8) * (1.0 / 16777216.0);
	const float r = sqrt(-2.0 * log(u1));
	return r * float2(cos(2.0 * OceanPi * u2), sin(2.0 * OceanPi * u2));
}

float OceanLogGamma(float x)
{
	// Stirling's series; x >= 1 here.
	const float inverse = 1.0 / x;
	return (x - 0.5) * log(x) - x + 0.91893853 + inverse / 12.0 - inverse * inverse * inverse / 360.0;
}

// JONSWAP energy at angular frequency omega (Hasselmann et al. 1973).
float OceanJonswap(float omega)
{
	const float wind = max(Constants.Wind.z, 0.1);
	const float fetch = max(Constants.Spectrum.y, 100.0);
	const float peak = 22.0 * pow(OceanGravity * OceanGravity / (wind * fetch), 1.0 / 3.0);
	const float alpha = 0.076 * pow(wind * wind / (fetch * OceanGravity), 0.22);
	const float sigma = omega <= peak ? 0.07 : 0.09;
	const float r = exp(-(omega - peak) * (omega - peak) / (2.0 * sigma * sigma * peak * peak));
	const float ratio = peak / omega;
	return alpha * OceanGravity * OceanGravity / pow(omega, 5.0) * exp(-1.25 * ratio * ratio * ratio * ratio) * pow(3.3, r);
}

// Directional spreading normalized over the half circle: cos^p of the angle to the wind.
float OceanSpreading(float cosine)
{
	const float p = Constants.Spectrum.z;
	const float norm = exp(OceanLogGamma(p * 0.5 + 1.0) - OceanLogGamma(p * 0.5 + 0.5)) / sqrt(OceanPi);
	return norm * pow(max(cosine, 0.0), p);
}

// h0(k): the initial amplitude of one wave number, sqrt(P(k) dk^2 / 2) times a complex Gaussian.
float2 OceanH0(int2 index, float2 k, float kLength)
{
	if (kLength < Constants.Cascade.y || kLength > Constants.Cascade.z)
	{
		return float2(0.0, 0.0);
	}
	const float omega = sqrt(OceanGravity * kLength);
	const float dOmegaDk = OceanGravity / (2.0 * omega);
	const float cosine = dot(k / kLength, Constants.Wind.xy);
	const float damping = exp(-kLength * kLength * Constants.Cascade.w * Constants.Cascade.w);
	const float density = OceanJonswap(omega) * OceanSpreading(cosine) * dOmegaDk / kLength * damping;
	const float dk = 2.0 * OceanPi / Constants.Cascade.x;
	return OceanGaussian(index, Constants.Size.y) * sqrt(max(density, 0.0) * dk * dk * 0.5) * Constants.Spectrum.x;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	const uint n = Constants.Size.x;
	if (id.x >= n || id.y >= n)
	{
		return;
	}
	const int halfSize = int(n / 2u);
	const int2 index = int2(int(id.x) < halfSize ? int(id.x) : int(id.x) - int(n), int(id.y) < halfSize ? int(id.y) : int(id.y) - int(n));
	const float2 k = float2(index) * (2.0 * OceanPi / Constants.Cascade.x);
	const float kLength = max(sqrt(dot(k, k)), 1.0e-6);
	const float2 h0 = OceanH0(index, k, kLength);
	const float2 h0Minus = OceanH0(-index, -k, kLength);
	// Angular frequency quantized to the repeat period so time can wrap without a jump.
	const float base = Constants.Spectrum.w;
	const float omega = floor(sqrt(OceanGravity * kLength) / base) * base;
	const float phase = omega * Constants.Wind.w;
	const float2 rotation = float2(cos(phase), sin(phase));
	const float2 h = ComplexMul(h0, rotation) + ComplexMul(float2(h0Minus.x, -h0Minus.y), float2(rotation.x, -rotation.y));

	const float inverse = 1.0 / kLength;
	const float2 ih = ComplexI(h);
	const float2 dx = ih * (k.x * inverse);
	const float2 dz = ih * (k.y * inverse);
	const float2 dydx = ih * k.x;
	const float2 dydz = ih * k.y;
	const float2 dxdx = -h * (k.x * k.x * inverse);
	const float2 dzdz = -h * (k.y * k.y * inverse);
	const float2 dxdz = -h * (k.x * k.y * inverse);
	const bool zero = index.x == 0 && index.y == 0;
	// Two real fields u, v per complex value: IFFT(U + iV) = u + iv.
	SpectrumA[id.xy] = zero ? float4(0.0, 0.0, 0.0, 0.0) : float4(dx + ComplexI(h), dz + ComplexI(dxdz));
	SpectrumB[id.xy] = zero ? float4(0.0, 0.0, 0.0, 0.0) : float4(dydx + ComplexI(dydz), dxdx + ComplexI(dzdz));
}
#elif OCEAN_PASS == 1
[[vk::binding(0, 0)]] Texture2D<float4> Input;
[[vk::binding(1, 0)]] [[vk::image_format("rgba32f")]] RWTexture2D<float4> Output;

static const uint OceanMaxSize = 512;
groupshared float4 Lines[2][OceanMaxSize];

// One group per row; each thread runs butterflies i, i + 256, ... of every stage. Each
// float4 is two complex values transformed together.
[shader("compute")]
[numthreads(256, 1, 1)]
void computeMain(uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID)
{
	const uint n = Constants.Size.x;
	const uint row = group.y;
	const bool columns = Constants.Size.z != 0u;
	for (uint i = thread.x; i < n; i += 256u)
	{
		Lines[0][i] = Input.Load(int3(columns ? int2(row, i) : int2(i, row), 0));
	}
	GroupMemoryBarrierWithGroupSync();

	const uint halfSize = n / 2u;
	uint source = 0u;
	for (uint p = 1u; p < n; p <<= 1u)
	{
		for (uint i = thread.x; i < halfSize; i += 256u)
		{
			const uint k = i & (p - 1u);
			const float4 x0 = Lines[source][i];
			const float4 x1 = Lines[source][i + halfSize];
			const float angle = OceanPi * float(k) / float(p);
			const float2 w = float2(cos(angle), sin(angle));
			const float4 t = float4(ComplexMul(w, x1.xy), ComplexMul(w, x1.zw));
			const uint j = ((i - k) << 1u) + k;
			Lines[1u - source][j] = x0 + t;
			Lines[1u - source][j + p] = x0 - t;
		}
		GroupMemoryBarrierWithGroupSync();
		source = 1u - source;
	}

	for (uint i = thread.x; i < n; i += 256u)
	{
		Output[columns ? int2(row, i) : int2(i, row)] = Lines[source][i];
	}
}
#else
[[vk::binding(0, 0)]] Texture2D<float4> FieldsA;
[[vk::binding(1, 0)]] Texture2D<float4> FieldsB;
[[vk::binding(2, 0)]] Texture2D<float4> PreviousDisplacement;
[[vk::binding(3, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> Displacement;
[[vk::binding(4, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> Derivatives;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	const uint n = Constants.Size.x;
	if (id.x >= n || id.y >= n)
	{
		return;
	}
	const float4 a = FieldsA.Load(int3(id.xy, 0)); // dx, dy, dz, ddx/dz
	const float4 b = FieldsB.Load(int3(id.xy, 0)); // ddy/dx, ddy/dz, ddx/dx, ddz/dz
	const float chop = Constants.Foam.x;
	const float4 derivatives = float4(b.x, b.y, b.z * chop, b.w * chop);
	const float shear = a.w * chop;
	const float jacobian = (1.0 + derivatives.z) * (1.0 + derivatives.w) - shear * shear;
	const float previous = Constants.Size.w != 0u ? PreviousDisplacement.Load(int3(id.xy, 0)).w : 0.0;
	const float fresh = saturate((Constants.Foam.y - jacobian) * Constants.Foam.z);
	const float foam = saturate(max(previous * Constants.Foam.w, fresh));
	Displacement[id.xy] = float4(a.x * chop, a.y, a.z * chop, foam);
	Derivatives[id.xy] = derivatives;
}
#endif
