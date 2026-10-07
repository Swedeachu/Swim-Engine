// The temporal reflection filter (ReflectionSettings::Temporal), after the composite. The
// composite wrote the frame's colour and, per pixel, the reflection term that replaced the
// specular IBL with the weight it entered the colour with (ScreenSpaceComposite.slang). Here
// the term is blended with last frame's filtered term at the surface's reprojected position,
// so the half-resolution trace pattern and probe refreshes settle. The colour is corrected by
// the difference. Bindings follow ScreenSpaceReflectionTemporalBindings.
//
// Reflections move differently from the surface that shows them (a mirror's image slides as
// the camera moves; a reflected object moves on its own), so reprojecting by the surface's
// motion finds stale content wherever the reflection changed. Last frame's term is therefore
// clipped (in YCoCg, towards the neighbourhood mean) to the box this frame's 3x3
// neighbourhood spans within VarianceGamma standard deviations, and the further it had to
// be clipped, or the faster the surface moves on screen, the more this frame's term counts:
// a reflected object that moved or left leaves no trail and no double image.
#include "ScreenSpace/ScreenSpaceRecords.slang"

[[vk::binding(0, 0)]] Texture2D<float4> Color;
[[vk::binding(1, 0)]] Texture2D<float4> Term;	   // rgb: reflection term; a: its weight in Color.
[[vk::binding(2, 0)]] Texture2D<float2> Velocity;  // Current minus previous UV.
[[vk::binding(3, 0)]] Texture2D<float4> History;   // Last frame's filtered term (rgb).
[[vk::binding(4, 0)]] SamplerState LinearClamp;
[[vk::binding(5, 0)]] StructuredBuffer<ScreenSpaceParams> Params;
[[vk::binding(6, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> Output;
[[vk::binding(7, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> HistoryOut;

// ScreenSpace::ReflectionTemporal bit 1: last frame's history is valid.
static const uint ReflectionTemporalRead = 2u;
// ScreenSpace::ReflectionVarianceGamma: the clip box's half size in standard deviations.
static const float VarianceGamma = 1.0;
// ScreenSpace::ReflectionClipResponse: how fast this frame's term takes over as the history
// is clipped (relative to the term's brightness).
static const float ClipResponse = 4.0;
// ScreenSpace::ReflectionMotionResponse: screen pixels of surface motion per frame at which
// the history is dropped.
static const float MotionResponse = 12.0;

float3 ToYCoCg(float3 c)
{
	return float3(0.25 * c.r + 0.5 * c.g + 0.25 * c.b, 0.5 * c.r - 0.5 * c.b, -0.25 * c.r + 0.5 * c.g - 0.25 * c.b);
}

float3 FromYCoCg(float3 c)
{
	return float3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z);
}

// Clips `history` towards `centre` into the box [low, high] (the line segment's exit point).
float3 ClipToBox(float3 history, float3 centre, float3 low, float3 high)
{
	const float3 extent = max(0.5 * (high - low), float3(1.0e-5, 1.0e-5, 1.0e-5));
	const float3 middle = 0.5 * (high + low);
	const float3 offset = history - middle;
	const float3 units = abs(offset / extent);
	const float largest = max(units.x, max(units.y, units.z));
	return largest > 1.0 ? middle + offset / largest : history;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	const ScreenSpaceView view = ScreenSpaceLoadView(Params);
	if (id.x >= view.Width || id.y >= view.Height)
	{
		return;
	}
	const int3 at = int3(int(id.x), int(id.y), 0);
	const float4 color = Color.Load(at);
	const float4 term = Term.Load(at);
	if (!(term.a > 0.0))
	{
		Output[id.xy] = color;
		HistoryOut[id.xy] = float4(term.rgb, 0.0);
		return;
	}
	// The neighbourhood's statistics (only neighbours that carry a reflection term).
	const float3 centre = ToYCoCg(term.rgb);
	float3 sum = float3(0.0, 0.0, 0.0);
	float3 squares = float3(0.0, 0.0, 0.0);
	float3 low = centre;
	float3 high = centre;
	float count = 0.0;
	for (int y = -1; y <= 1; ++y)
	{
		for (int x = -1; x <= 1; ++x)
		{
			const int2 p = clamp(int2(at.xy) + int2(x, y), int2(0, 0), int2(int(view.Width), int(view.Height)) - 1);
			const float4 n = Term.Load(int3(p, 0));
			if (n.a > 0.0)
			{
				const float3 c = ToYCoCg(n.rgb);
				sum += c;
				squares += c * c;
				low = min(low, c);
				high = max(high, c);
				count += 1.0;
			}
		}
	}
	const float3 mean = sum / max(count, 1.0);
	const float3 deviation = sqrt(max(squares / max(count, 1.0) - mean * mean, float3(0.0, 0.0, 0.0)));
	// The variance box, never wider than the neighbourhood's own range.
	const float3 boxLow = max(low, mean - VarianceGamma * deviation);
	const float3 boxHigh = min(high, mean + VarianceGamma * deviation);
	float3 filtered = term.rgb;
	if ((Params[0].ReflectionTemporal & ReflectionTemporalRead) != 0u)
	{
		const float2 motion = Velocity.Load(at);
		const float2 uv = (float2(at.xy) + 0.5) / float2(float(view.Width), float(view.Height)) - motion;
		if (all(uv > 0.0) && all(uv < 1.0))
		{
			const float3 raw = ToYCoCg(max(History.SampleLevel(LinearClamp, uv, 0.0).rgb, float3(0.0, 0.0, 0.0)));
			const float3 clipped = ClipToBox(raw, mean, boxLow, boxHigh);
			// Clipped far (the reflection changed there) or moving fast: trust this frame.
			const float clipDistance = length(raw - clipped) / (max(centre.x, clipped.x) + 0.05);
			const float speed = length(motion * float2(float(view.Width), float(view.Height)));
			const float response = saturate(max(clipDistance * ClipResponse, speed / MotionResponse));
			const float blend = lerp(Params[0].ReflectionTemporalBlend, 1.0, response);
			filtered = max(FromYCoCg(lerp(clipped, centre, blend)), float3(0.0, 0.0, 0.0));
		}
	}
	Output[id.xy] = float4(max(color.rgb + term.a * (filtered - term.rgb), float3(0.0, 0.0, 0.0)), color.a);
	HistoryOut[id.xy] = float4(filtered, 1.0);
}
