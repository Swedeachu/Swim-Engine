// The temporal reflection filter (ReflectionSettings::Temporal), after the composite. The
// composite wrote the frame's colour and, per pixel, the reflection term that replaced the
// specular IBL with the weight it entered the colour with (ScreenSpaceComposite.slang). Here
// the term is blended with last frame's filtered term at the surface's reprojected position,
// with last frame's clamped to the range of this frame's 3x3 neighbourhood (as TAA does), so
// the half-resolution trace pattern, the jittered rays and probe refreshes settle while a
// reflected object that moves is followed without trails. The colour is corrected by the
// difference. Bindings follow ScreenSpaceReflectionTemporalBindings.
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
	// The neighbourhood's range (only neighbours that carry a reflection term).
	float3 low = term.rgb;
	float3 high = term.rgb;
	for (int y = -1; y <= 1; ++y)
	{
		for (int x = -1; x <= 1; ++x)
		{
			const int2 p = clamp(int2(at.xy) + int2(x, y), int2(0, 0), int2(int(view.Width), int(view.Height)) - 1);
			const float4 n = Term.Load(int3(p, 0));
			if (n.a > 0.0)
			{
				low = min(low, n.rgb);
				high = max(high, n.rgb);
			}
		}
	}
	float3 filtered = term.rgb;
	if ((Params[0].ReflectionTemporal & ReflectionTemporalRead) != 0u)
	{
		const float2 uv = (float2(at.xy) + 0.5) / float2(float(view.Width), float(view.Height)) - Velocity.Load(at);
		if (all(uv > 0.0) && all(uv < 1.0))
		{
			const float3 history = clamp(History.SampleLevel(LinearClamp, uv, 0.0).rgb, low, high);
			filtered = lerp(history, term.rgb, Params[0].ReflectionTemporalBlend);
		}
	}
	Output[id.xy] = float4(max(color.rgb + term.a * (filtered - term.rgb), float3(0.0, 0.0, 0.0)), color.a);
	HistoryOut[id.xy] = float4(filtered, 1.0);
}
