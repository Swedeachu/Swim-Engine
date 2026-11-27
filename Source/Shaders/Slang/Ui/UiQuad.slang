// UI quads (critical-path item 79): one instance per GpuUiQuad, six vertices each.
// The vertex stage clips the quad to its clip rectangle in canvas pixels, remaps the UV
// and maps the corners through ClipFromCanvas (screen overlays 1:1, world panels and
// billboards through the camera), so no fragment is discarded and one draw covers every
// clip. Fragments shade solids (with rounded corners and borders), MSDF glyphs and
// premultiplied images through the bindless table, then encode for the target (UiEncode)
// before premultiplied blending. World canvases take coverage ramps and MSDF ranges from
// screen-space derivatives. Bindings follow UiRenderBindings; Ui::ShadeQuad and Ui::Encode
// are the CPU definition.
#include "Resources/DrawIndex.slang"
#include "Features/Glint.slang"
#include "Ui/UiRecords.slang"

[[vk::binding(0, 0)]] StructuredBuffer<UiQuad> Quads;
[[vk::binding(0, 1)]] SamplerState Samplers[];
[[vk::binding(1, 1)]] Texture2D<float4> Textures[];

[[vk::push_constant]] ConstantBuffer<UiDrawConstants> Draw;

struct VertexOutput
{
	float4 Position : SV_Position;
	float2 Uv : TEXCOORD0;
	float2 Canvas : TEXCOORD1; // Canvas pixels (perspective-correct for world canvases).
	nointerpolation uint Quad : TEXCOORD2;
};

// The instance index includes the first instance (DrawIndex.slang), so it is the quad index
// of every blend run.
[shader("vertex")]
VertexOutput vertexMain(uint vertexIdInput : SWIM_VERTEX_INDEX, uint instanceIdInput : SWIM_INSTANCE_INDEX SWIM_DRAW_BASE_PARAMETERS)
{
	const uint vertexId = DrawVertexIndex(vertexIdInput);
	const uint instanceId = DrawInstanceIndex(instanceIdInput);
	const float4 rect = Quads[instanceId].Rect;
	const float4 clip = Quads[instanceId].Clip;
	const float4 uv = Quads[instanceId].Uv;
	// Sheared glyphs (Ui::ShadeQuad): clipped vertically here, horizontally per fragment.
	const bool sheared = Quads[instanceId].Kind != UiQuadSolid && Quads[instanceId].Kind != UiQuadGradient && Quads[instanceId].Radius != 0.0;
	float2 low = max(rect.xy, clip.xy);
	float2 high = max(min(rect.zw, clip.zw), low); // Empty clips collapse to a point.
	if (sheared)
	{
		low.x = rect.x;
		high.x = max(rect.z, rect.x);
	}
	// Corners {0, 1, 2, 2, 1, 3} (one nibble per vertex): bit 0 selects x, bit 1 selects y.
	const uint corner = (0x312210u >> (vertexId * 4u)) & 0xFu;
	const float2 t = float2(float(corner & 1u), float(corner >> 1u));
	const float2 p = lerp(low, high, t);
	const float2 s = (p - rect.xy) / max(rect.zw - rect.xy, 1e-6);
	float2 drawn = p;
	if (sheared)
	{
		drawn.x += Quads[instanceId].Radius * (Quads[instanceId].Border - p.y);
	}
	const float4 v = float4(drawn, 0.0, 1.0);
	VertexOutput output;
	output.Position = float4(dot(Draw.ClipFromCanvas[0], v), dot(Draw.ClipFromCanvas[1], v), dot(Draw.ClipFromCanvas[2], v),
		dot(Draw.ClipFromCanvas[3], v));
	output.Uv = lerp(uv.xy, uv.zw, s);
	output.Canvas = drawn;
	output.Quad = instanceId;
	return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target0
{
	const UiQuad q = Quads[input.Quad];
	const float2 p = input.Canvas;
	// Derivatives in uniform control flow, before any per-quad branch.
	const float2 canvasWidth = fwidth(p);
	const float2 uvWidth = max(fwidth(input.Uv), float2(1e-9, 1e-9));
	const bool world = (Draw.Flags & UiDrawWorld) != 0u;
	float4 color;
	if (q.Kind == UiQuadGlyph)
	{
		const float3 msd =
			Textures[NonUniformResourceIndex(q.Texture)].Sample(Samplers[NonUniformResourceIndex(q.Sampler)], input.Uv).rgb;
		const float range = world ? max(0.5 * (q.UnitRange / uvWidth.x + q.UnitRange / uvWidth.y), 1.0) : q.PixelRange;
		color = q.Color * saturate(range * (UiMedian(msd) - 0.5) + 0.5);
	}
	else if (q.Kind == UiQuadColorGlyph)
	{
		const float4 texel =
			Textures[NonUniformResourceIndex(q.Texture)].Sample(Samplers[NonUniformResourceIndex(q.Sampler)], input.Uv);
		const float3 linear = float3(UiSrgbEotf(texel.r), UiSrgbEotf(texel.g), UiSrgbEotf(texel.b));
		color = q.Color * float4(linear * texel.a, texel.a);
	}
	else if (q.Kind == UiQuadImage)
	{
		color = q.Color * Textures[NonUniformResourceIndex(q.Texture)].Sample(Samplers[NonUniformResourceIndex(q.Sampler)], input.Uv);
	}
	else if (q.Kind == UiQuadImageGlint)
	{
		color = q.Color * Textures[NonUniformResourceIndex(q.Texture)].Sample(Samplers[NonUniformResourceIndex(q.Sampler)], input.Uv);
		// The sheen over the icon's opaque texels, added (premultiplied, alpha unchanged).
		GlintParams g;
		g.ColorA = float4(q.BorderColor.rgb, q.BorderColor.a);
		g.ColorB = float4(lerp(q.BorderColor.rgb, float3(0.3, 0.6, 1.0), 0.6), 1.0);
		g.Motion = float4(0.35, 1.6, 0.12, Draw.Glint.x);
		g.Extra = float4(0.0, 5.0, 1.0, 1.0);
		g.Source = uint4(uint(q.PixelRange + 0.5), uint(q.UnitRange + 0.5), uint(Draw.Glint.y + 0.5), 0u);
		const float2 cellUv = (input.Uv - q.Uv.xy) / max(abs(q.Uv.zw - q.Uv.xy), float2(1e-6, 1e-6));
		const float2 cellScale = 1.0 / max(abs(q.Uv.zw - q.Uv.xy), float2(1e-6, 1e-6));
		const float3 glint = GlintEvaluate(g, Textures[NonUniformResourceIndex(g.Source.y)], Samplers[NonUniformResourceIndex(g.Source.z)],
			cellUv, uvWidth * cellScale, float2(0.0, 0.0), float3(0.0, 0.0, 1.0), float3(0.0, 0.0, 1.0));
		color.rgb += glint * color.a;
	}
	else
	{
		UiQuad shaded = q;
		if (q.Kind == UiQuadGradient)
		{
			const float2 size = max(q.Rect.zw - q.Rect.xy, float2(1e-6, 1e-6));
			const float t = saturate(q.PixelRange > 0.5 ? (p.y - q.Rect.y) / size.y : (p.x - q.Rect.x) / size.x);
			shaded.Color = lerp(q.Color, q.Uv, t);
		}
		color = UiShadeSolid(shaded, p, world ? max(0.5 * (canvasWidth.x + canvasWidth.y), 1e-6) : 1.0);
	}
	if (q.Kind != UiQuadSolid && q.Kind != UiQuadGradient && q.Radius != 0.0 && !(p.x >= q.Clip.x && p.x < q.Clip.z))
	{
		color = float4(0.0, 0.0, 0.0, 0.0);
	}
	return UiEncode(color * Draw.Opacity, Draw.Encoding, Draw.WhiteScale);
}
