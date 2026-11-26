// Temporal anti-aliasing (critical-path item 75): mirrors TemporalRecords.h and every
// Temporal:: function of TemporalReference.h.

struct TemporalResolveConstants
{
	uint Width;
	uint Height;
	float Feedback;
	float ClipGamma;
	uint HistoryValid;
	float ReactiveFeedback;
	float ReactiveClipGamma;
	uint Reserved0;
};

// Temporal::Reactive: the composite marks reactive pixels in the colour's alpha as 2 + reactivity
// (ScreenSpaceComposite.slang); any other alpha (1 from Forward+, blended particles) is 0.
float TemporalReactive(float alpha)
{
	return alpha >= 1.5 ? saturate(alpha - 2.0) : 0.0;
}

// Temporal::Luminance (Rec. 709).
float TemporalLuminance(float3 rgb)
{
	return 0.2126 * rgb.r + 0.7152 * rgb.g + 0.0722 * rgb.b;
}

// Temporal::RgbToYCoCg / YCoCgToRgb.
float3 TemporalRgbToYCoCg(float3 rgb)
{
	return float3(0.25 * rgb.r + 0.5 * rgb.g + 0.25 * rgb.b, 0.5 * rgb.r - 0.5 * rgb.b, -0.25 * rgb.r + 0.5 * rgb.g - 0.25 * rgb.b);
}

float3 TemporalYCoCgToRgb(float3 ycocg)
{
	return float3(ycocg.x + ycocg.y - ycocg.z, ycocg.x + ycocg.z, ycocg.x - ycocg.y - ycocg.z);
}

// Temporal::Sanitize: non-finite channels become 0, negative ones 0.
float3 TemporalSanitize(float4 color)
{
	float3 result;
	for (uint c = 0; c < 3; ++c)
	{
		result[c] = isfinite(color[c]) ? max(color[c], 0.0) : 0.0;
	}
	return result;
}

// Temporal::ClipToBox: toward the box centre until inside.
float3 TemporalClipToBox(float3 value, float3 minimum, float3 maximum)
{
	const float3 center = 0.5 * (maximum + minimum);
	const float3 extent = max(0.5 * (maximum - minimum), float3(1.0e-6, 1.0e-6, 1.0e-6));
	const float3 offset = value - center;
	const float3 units = abs(offset) / extent;
	const float scale = max(units.x, max(units.y, units.z));
	return scale <= 1.0 ? value : center + offset / scale;
}

// Temporal::SampleBilinear: clamp-to-edge, from Loads.
// Temporal::SampleCatmullRom: clamp-to-edge Catmull-Rom from 4 x 4 Loads.
float4 TemporalSampleCatmullRom(Texture2D<float4> image, uint width, uint height, float2 uv)
{
	const float px = uv.x * float(width) - 0.5;
	const float py = uv.y * float(height) - 0.5;
	const float fx0 = floor(px);
	const float fy0 = floor(py);
	const float tx = px - fx0;
	const float ty = py - fy0;
	const float4 wx = float4(0.5 * (-tx * tx * tx + 2.0 * tx * tx - tx), 0.5 * (3.0 * tx * tx * tx - 5.0 * tx * tx + 2.0),
		0.5 * (-3.0 * tx * tx * tx + 4.0 * tx * tx + tx), 0.5 * (tx * tx * tx - tx * tx));
	const float4 wy = float4(0.5 * (-ty * ty * ty + 2.0 * ty * ty - ty), 0.5 * (3.0 * ty * ty * ty - 5.0 * ty * ty + 2.0),
		0.5 * (-3.0 * ty * ty * ty + 4.0 * ty * ty + ty), 0.5 * (ty * ty * ty - ty * ty));
	float4 result = float4(0.0, 0.0, 0.0, 0.0);
	for (int j = 0; j < 4; ++j)
	{
		const int y = int(clamp(fy0 - 1.0 + float(j), 0.0, float(height - 1)));
		for (int i = 0; i < 4; ++i)
		{
			const int x = int(clamp(fx0 - 1.0 + float(i), 0.0, float(width - 1)));
			result += image.Load(int3(x, y, 0)) * (wx[i] * wy[j]);
		}
	}
	return result;
}

float4 TemporalSampleBilinear(Texture2D<float4> image, uint width, uint height, float2 uv)
{
	const float px = uv.x * float(width) - 0.5;
	const float py = uv.y * float(height) - 0.5;
	const float fx0 = floor(px);
	const float fy0 = floor(py);
	const float fx = px - fx0;
	const float fy = py - fy0;
	const int x0 = int(clamp(fx0, 0.0, float(width - 1)));
	const int x1 = int(clamp(fx0 + 1.0, 0.0, float(width - 1)));
	const int y0 = int(clamp(fy0, 0.0, float(height - 1)));
	const int y1 = int(clamp(fy0 + 1.0, 0.0, float(height - 1)));
	const float4 a = image.Load(int3(x0, y0, 0));
	const float4 b = image.Load(int3(x1, y0, 0));
	const float4 c = image.Load(int3(x0, y1, 0));
	const float4 d = image.Load(int3(x1, y1, 0));
	const float4 top = a + (b - a) * fx;
	const float4 bottom = c + (d - c) * fx;
	return top + (bottom - top) * fy;
}
