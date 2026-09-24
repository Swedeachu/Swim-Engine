// Post-processing records and helpers (items 73-74). Mirrors
// Engine/Systems/Renderer/PostProcess/PostProcessRecords.h and PostProcessReference.cpp
// line for line; ShaderCompiler.PostProcessLayout compares the reflected layouts.
#ifndef SWIM_POST_PROCESS_RECORDS_SLANG
#define SWIM_POST_PROCESS_RECORDS_SLANG

struct ExposureState
{
	float Ev100;
	float AverageLog2Luminance;
	float Exposure;
	uint Valid;
};

struct PostParams
{
	float4 WhiteBalance[3]; // Rows, linear Rec.709.
	float4 SlopeContrast;
	float4 OffsetSaturation;
	float4 PowerBloom; // w: bloom intensity / levels.
	uint ToneMapper;
	uint Encoding;
	uint Dither;
	uint BloomEnabled;
	float WhitePoint;
	float PaperWhiteNits;
	float PeakNits;
	uint GradingEnabled;
};

static const uint PostHistogramBins = 256;
static const uint PostOperatorClamp = 0;
static const uint PostOperatorReinhard = 1;
static const uint PostOperatorAces = 2;
static const uint PostOperatorPbrNeutral = 3;
static const uint PostEncodingSrgb = 0;
static const uint PostEncodingHdr10 = 1;
static const uint PostEncodingScRgb = 2;

float PostLuminance(float3 rgb)
{
	return rgb.x * 0.2126 + rgb.y * 0.7152 + rgb.z * 0.0722;
}

// Post::HistogramBin.
uint PostHistogramBin(float luminance, float minLog2, float inverseLog2Range)
{
	const float l = log2(luminance);
	if (!(l >= minLog2))
	{
		return 0;
	}
	const float t = saturate((l - minLog2) * inverseLog2Range);
	return min(uint(t * 254.0) + 1u, PostHistogramBins - 1u);
}

float PostBinLog2Luminance(uint bin, float minLog2, float log2Range)
{
	return minLog2 + (float(bin - 1u) + 0.5) / 254.0 * log2Range;
}

float PostExposureFromEv100(float ev100)
{
	return 1.0 / (1.2 * exp2(ev100));
}

// Post::BloomThreshold.
float3 PostBloomThreshold(float3 color, float threshold, float knee)
{
	const float brightness = max(color.x, max(color.y, color.z));
	float soft = clamp(brightness - threshold + knee, 0.0, 2.0 * knee);
	soft = soft * soft / (4.0 * knee + 1.0e-5);
	const float weight = max(soft, brightness - threshold) / max(brightness, 1.0e-5);
	return color * weight;
}

// Post::TentUpsample: 4x4 taps = bilinear at the texel's position convolved with [1 2 1] / 4.
float3 PostTentUpsample(Texture2D<float4> low, uint lowWidth, uint lowHeight, uint x, uint y, uint width, uint height)
{
	const float px = (float(x) + 0.5) * float(lowWidth) / float(width) - 0.5;
	const float py = (float(y) + 0.5) * float(lowHeight) / float(height) - 0.5;
	const float fx0 = floor(px);
	const float fy0 = floor(py);
	const float fx = px - fx0;
	const float fy = py - fy0;
	const float wx[4] = { (1.0 - fx) * 0.25, (2.0 - fx) * 0.25, (1.0 + fx) * 0.25, fx * 0.25 };
	const float wy[4] = { (1.0 - fy) * 0.25, (2.0 - fy) * 0.25, (1.0 + fy) * 0.25, fy * 0.25 };
	const int x0 = int(fx0) - 1;
	const int y0 = int(fy0) - 1;
	float3 sum = float3(0.0, 0.0, 0.0);
	for (int j = 0; j < 4; ++j)
	{
		float3 row = float3(0.0, 0.0, 0.0);
		const int ty = clamp(y0 + j, 0, int(lowHeight) - 1);
		for (int i = 0; i < 4; ++i)
		{
			const int tx = clamp(x0 + i, 0, int(lowWidth) - 1);
			row += low.Load(int3(tx, ty, 0)).xyz * wx[i];
		}
		sum += row * wy[j];
	}
	return sum;
}

// Post::Grade.
float3 PostGrade(PostParams p, float3 color)
{
	if (p.GradingEnabled == 0)
	{
		return color;
	}
	float3 c = float3(dot(p.WhiteBalance[0].xyz, color), dot(p.WhiteBalance[1].xyz, color), dot(p.WhiteBalance[2].xyz, color));
	c = 0.18 * pow(max(c, 0.0) / 0.18, p.SlopeContrast.w);
	c = pow(max(c * p.SlopeContrast.xyz + p.OffsetSaturation.xyz, 0.0), p.PowerBloom.xyz);
	const float l = PostLuminance(c);
	return max(l + (c - l) * p.OffsetSaturation.w, 0.0);
}

float3 PostToneMapReinhard(float3 color, float whitePoint)
{
	const float3 x = max(color, 0.0);
	return saturate(x * (1.0 + x / (whitePoint * whitePoint)) / (1.0 + x));
}

float3 PostToneMapAces(float3 color)
{
	const float3 c = max(color, 0.0);
	float3 v = float3(dot(float3(0.59719, 0.35458, 0.04823), c), dot(float3(0.07600, 0.90834, 0.01566), c),
		dot(float3(0.02840, 0.13383, 0.83777), c));
	const float3 a = v * (v + 0.0245786) - 0.000090537;
	const float3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
	v = a / b;
	return saturate(float3(dot(float3(1.60475, -0.53108, -0.07367), v), dot(float3(-0.10208, 1.10813, -0.00605), v),
		dot(float3(-0.00327, -0.07276, 1.07602), v)));
}

float3 PostToneMapPbrNeutral(float3 color)
{
	const float startCompression = 0.8 - 0.04;
	const float desaturation = 0.15;
	float3 c = max(color, 0.0);
	const float x = min(c.x, min(c.y, c.z));
	const float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
	c -= offset;
	const float peak = max(c.x, max(c.y, c.z));
	if (peak < startCompression)
	{
		return c;
	}
	const float d = 1.0 - startCompression;
	const float newPeak = 1.0 - d * d / (peak + d - startCompression);
	c *= newPeak / peak;
	const float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
	return c * (1.0 - g) + newPeak * g;
}

float3 PostToneMap(uint op, float3 color, float whitePoint)
{
	if (op == PostOperatorReinhard)
	{
		return PostToneMapReinhard(color, whitePoint);
	}
	if (op == PostOperatorAces)
	{
		return PostToneMapAces(color);
	}
	if (op == PostOperatorPbrNeutral)
	{
		return PostToneMapPbrNeutral(color);
	}
	return saturate(color);
}

float PostSrgbOetf(float linear)
{
	return linear <= 0.0031308 ? 12.92 * linear : 1.055 * pow(linear, 1.0 / 2.4) - 0.055;
}

float PostPqOetf(float nits)
{
	const float m1 = 0.1593017578125;
	const float m2 = 78.84375;
	const float c1 = 0.8359375;
	const float c2 = 18.8515625;
	const float c3 = 18.6875;
	const float y = pow(clamp(nits / 10000.0, 0.0, 1.0), m1);
	return pow((c1 + c2 * y) / (1.0 + c3 * y), m2);
}

float3 PostRec709ToRec2020(float3 c)
{
	return float3(dot(float3(0.6274040, 0.3292820, 0.0433136), c), dot(float3(0.0690970, 0.9195400, 0.0113612), c),
		dot(float3(0.0163916, 0.0880132, 0.8955950), c));
}

float PostInterleavedGradientNoise(uint x, uint y)
{
	const float inner = 0.06711056 * float(x) + 0.00583715 * float(y);
	const float v = 52.9829189 * (inner - floor(inner));
	return v - floor(v);
}

// Post::CompositeTexel.
float4 PostCompositeTexel(PostParams p, float exposure, float4 source, float3 bloom, uint x, uint y)
{
	float3 c = source.xyz * exposure;
	if (p.BloomEnabled != 0)
	{
		c += bloom * p.PowerBloom.w;
	}
	c = PostGrade(p, c);
	if (p.Encoding == PostEncodingSrgb)
	{
		const float3 t = saturate(PostToneMap(p.ToneMapper, c, p.WhitePoint));
		const float noise = p.Dither != 0 ? (PostInterleavedGradientNoise(x, y) - 0.5) / 255.0 : 0.0;
		return float4(saturate(PostSrgbOetf(t.x) + noise), saturate(PostSrgbOetf(t.y) + noise), saturate(PostSrgbOetf(t.z) + noise),
			saturate(source.w));
	}
	const float headroom = p.PeakNits / p.PaperWhiteNits;
	const float3 t = PostToneMap(p.ToneMapper, c / headroom, p.WhitePoint);
	const float3 nits = t * headroom * p.PaperWhiteNits;
	if (p.Encoding == PostEncodingHdr10)
	{
		const float3 wide = PostRec709ToRec2020(nits);
		return float4(PostPqOetf(wide.x), PostPqOetf(wide.y), PostPqOetf(wide.z), source.w);
	}
	return float4(nits / 80.0, source.w);
}

#endif
