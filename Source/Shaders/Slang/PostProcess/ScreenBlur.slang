// Screen blur (RenderSettings::Blur): a dual-filter (Kawase) chain over the finished,
// display-referred frame (RGBA8, sRGB encoded), before the UI. Three programs from one
// source (SCREEN_BLUR_PASS):
//  0 ScreenBlurDown       half-size downsample: the centre and four diagonal bilinear taps
//                         at Offset half-texels (the first level decodes sRGB to linear);
//  1 ScreenBlurUp         double-size upsample: four edge and four diagonal taps;
//  2 ScreenBlurComposite  mixes the blurred chain (linear, sampled bilinearly) back over the
//                         frame by Mix, everywhere or only inside the Regions rectangles.
// Intermediate levels are RGBA16F, so blurring happens in linear light.
#ifndef SCREEN_BLUR_PASS
#define SCREEN_BLUR_PASS 0
#endif

struct ScreenBlurConstants
{
	uint2 OutputSize;
	float2 SourceInvSize; // 1 / the source texture's size.
	float Offset;		  // Tap distance in half source texels (down) or half output texels (up).
	float Mix;			  // Composite: 0 = the frame .. 1 = fully blurred.
	uint Decode;		  // Down: the source is sRGB encoded (the frame).
	uint RegionCount;	  // Composite: 0 = everywhere, else only inside Regions[0 .. count).
	float4 Regions[4];	  // Pixel rectangles (x0, y0, x1, y1).
};

[[vk::push_constant]] ConstantBuffer<ScreenBlurConstants> Constants;

float3 SrgbToLinear(float3 c)
{
	c = saturate(c);
	return select(c <= 0.04045, c / 12.92, pow((c + 0.055) / 1.055, 2.4));
}

float3 LinearToSrgb(float3 c)
{
	c = saturate(c);
	return select(c <= 0.0031308, c * 12.92, 1.055 * pow(c, 1.0 / 2.4) - 0.055);
}

[[vk::binding(0, 0)]] Texture2D<float4> Source;
[[vk::binding(1, 0)]] SamplerState LinearClamp;

#if SCREEN_BLUR_PASS == 2
[[vk::binding(2, 0)]] Texture2D<float4> Blurred;
[[vk::binding(3, 0)]] [[vk::image_format("rgba8")]] RWTexture2D<float4> Output;
#else
[[vk::binding(2, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> Output;
#endif

float3 Tap(float2 uv)
{
	const float3 c = Source.SampleLevel(LinearClamp, uv, 0.0).rgb;
#if SCREEN_BLUR_PASS == 0
	return Constants.Decode != 0u ? SrgbToLinear(c) : c;
#else
	return c;
#endif
}

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	if (any(id.xy >= Constants.OutputSize))
	{
		return;
	}

	const float2 uv = (float2(id.xy) + 0.5) / float2(Constants.OutputSize);
#if SCREEN_BLUR_PASS == 0
	const float2 h = Constants.SourceInvSize * Constants.Offset;
	float3 sum = Tap(uv) * 4.0;
	sum += Tap(uv + float2(-h.x, -h.y));
	sum += Tap(uv + float2(h.x, -h.y));
	sum += Tap(uv + float2(-h.x, h.y));
	sum += Tap(uv + float2(h.x, h.y));
	Output[id.xy] = float4(sum / 8.0, 1.0);
#elif SCREEN_BLUR_PASS == 1
	const float2 h = 0.5 / float2(Constants.OutputSize) * Constants.Offset;
	float3 sum = Tap(uv + float2(-2.0 * h.x, 0.0));
	sum += Tap(uv + float2(2.0 * h.x, 0.0));
	sum += Tap(uv + float2(0.0, -2.0 * h.y));
	sum += Tap(uv + float2(0.0, 2.0 * h.y));
	sum += Tap(uv + float2(-h.x, -h.y)) * 2.0;
	sum += Tap(uv + float2(h.x, -h.y)) * 2.0;
	sum += Tap(uv + float2(-h.x, h.y)) * 2.0;
	sum += Tap(uv + float2(h.x, h.y)) * 2.0;
	Output[id.xy] = float4(sum / 12.0, 1.0);
#else
	const float4 frame = Source.Load(int3(id.xy, 0));
	float mask = Constants.RegionCount == 0u ? 1.0 : 0.0;
	const float2 pixel = float2(id.xy) + 0.5;

	for (uint i = 0; i < min(Constants.RegionCount, 4u); ++i)
	{
		const float4 r = Constants.Regions[i];
		if (all(pixel >= r.xy) && all(pixel < r.zw))
		{
			mask = 1.0;
		}
	}

	const float amount = Constants.Mix * mask;
	if (amount <= 0.0)
	{
		Output[id.xy] = frame;
		return;
	}

	const float3 blurred = LinearToSrgb(Blurred.SampleLevel(LinearClamp, uv, 0.0).rgb);
	Output[id.xy] = float4(lerp(frame.rgb, blurred, amount), frame.a);
#endif
}
