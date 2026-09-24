// Screen-space ambient occlusion and fog (critical-path item 76): mirrors
// ScreenSpaceRecords.h and every ScreenSpace:: function of ScreenSpaceReference.h.

struct ScreenSpaceParams
{
	float4 InverseProjection[4];
	float4 ViewRows[3];
	float4 InverseViewRows[3];
	float2 Jitter;
	uint Width;
	uint Height;
	float AoRadius;
	float AoFalloff;
	float AoPower;
	float AoMaxRadiusPixels;
	float AoRadiusToPixels;
	float AoBlurDepthTolerance;
	uint AoSliceCount;
	uint AoStepCount;
	uint NoiseFrame;
	uint AoEnabled;
	uint FogEnabled;
	uint Reserved0;
	float3 FogColor;
	float FogDensity;
	float3 FogSunColor;
	float FogHeightFalloff;
	float3 FogSunDirection;
	float FogBaseHeight;
	float FogAnisotropy;
	float FogStartDistance;
	float FogMaxDistance;
	float Reserved1;
};

static const float ScreenSpacePi = 3.14159265358979;

// The parameters one program uses, loaded once: GPU-assisted validation instruments
// every storage-buffer load.
struct ScreenSpaceView
{
	float4 InverseProjection[4];
	float2 Jitter;
	uint Width;
	uint Height;
};

ScreenSpaceView ScreenSpaceLoadView(StructuredBuffer<ScreenSpaceParams> params)
{
	ScreenSpaceView view;
	for (uint row = 0; row < 4; ++row)
	{
		view.InverseProjection[row] = params[0].InverseProjection[row];
	}
	view.Jitter = params[0].Jitter;
	view.Width = params[0].Width;
	view.Height = params[0].Height;
	return view;
}

// ScreenSpace::InterleavedGradientNoise.
float ScreenSpaceNoise(float x, float y, uint frame)
{
	const float shift = 5.588238 * float(frame % 64u);
	return frac(52.9829189 * frac(0.06711056 * (x + shift) + 0.00583715 * (y + shift)));
}

// ScreenSpace::ViewPosition; valid is false for the sky (depth <= 0).
float3 ScreenSpaceViewPosition(ScreenSpaceView view, float px, float py, float depth, out bool valid)
{
	valid = false;
	if (!(depth > 0.0))
	{
		return float3(0.0, 0.0, 0.0);
	}
	const float4 ndc = float4(2.0 * px / float(view.Width) - 1.0 - view.Jitter.x, 1.0 - 2.0 * py / float(view.Height) - view.Jitter.y, depth, 1.0);
	const float4 clip = float4(dot(view.InverseProjection[0], ndc), dot(view.InverseProjection[1], ndc), dot(view.InverseProjection[2], ndc),
		dot(view.InverseProjection[3], ndc));
	if (!(abs(clip.w) > 1.0e-20))
	{
		return float3(0.0, 0.0, 0.0);
	}
	valid = true;
	const float inverseW = 1.0 / clip.w;
	return clip.xyz * inverseW;
}

float3 ScreenSpaceLoadViewPosition(ScreenSpaceView view, Texture2D<float> depth, int x, int y, out bool valid)
{
	return ScreenSpaceViewPosition(view, float(x) + 0.5, float(y) + 0.5, depth.Load(int3(x, y, 0)), valid);
}
