// UI quads (critical-path item 79): one instance per GpuUiQuad, six vertices each.
// The vertex stage clips the quad to its clip rectangle and remaps the UV, so no
// fragment is discarded and one draw covers every clip. Fragments shade solids (with
// rounded corners and borders), MSDF glyphs and premultiplied images through the
// bindless table, then encode for the target (UiEncode) before premultiplied blending.
// Bindings follow UiRenderBindings; Ui::ShadeQuad and Ui::Encode are the CPU definition.
#include "Ui/UiRecords.slang"

[[vk::binding(0, 0)]] StructuredBuffer<UiQuad> Quads;
[[vk::binding(0, 1)]] SamplerState Samplers[];
[[vk::binding(1, 1)]] Texture2D<float4> Textures[];

[[vk::push_constant]] ConstantBuffer<UiDrawConstants> Draw;

struct VertexOutput
{
	float4 Position : SV_Position;
	float2 Uv : TEXCOORD0;
	nointerpolation uint Quad : TEXCOORD1;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
	const float4 rect = Quads[instanceId].Rect;
	const float4 clip = Quads[instanceId].Clip;
	const float4 uv = Quads[instanceId].Uv;
	const float2 low = max(rect.xy, clip.xy);
	const float2 high = max(min(rect.zw, clip.zw), low); // Empty clips collapse to a point.
	// Corners {0, 1, 2, 2, 1, 3} (one nibble per vertex): bit 0 selects x, bit 1 selects y.
	const uint corner = (0x312210u >> (vertexId * 4u)) & 0xFu;
	const float2 t = float2(float(corner & 1u), float(corner >> 1u));
	const float2 p = lerp(low, high, t);
	const float2 s = (p - rect.xy) / max(rect.zw - rect.xy, 1e-6);
	VertexOutput output;
	// RHI clip space is +Y-up (the Vulkan backend flips the viewport); UI pixels are +Y-down.
	const float2 ndc = p / Draw.TargetSize * 2.0 - 1.0;
	output.Position = float4(ndc.x, -ndc.y, 0.0, 1.0);
	output.Uv = lerp(uv.xy, uv.zw, s);
	output.Quad = instanceId;
	return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target0
{
	const UiQuad q = Quads[input.Quad];
	const float2 p = input.Position.xy;
	float4 color;
	if (q.Kind == UiQuadGlyph)
	{
		const float3 msd =
			Textures[NonUniformResourceIndex(q.Texture)].Sample(Samplers[NonUniformResourceIndex(q.Sampler)], input.Uv).rgb;
		color = q.Color * saturate(q.PixelRange * (UiMedian(msd) - 0.5) + 0.5);
	}
	else if (q.Kind == UiQuadImage)
	{
		color = q.Color * Textures[NonUniformResourceIndex(q.Texture)].Sample(Samplers[NonUniformResourceIndex(q.Sampler)], input.Uv);
	}
	else
	{
		color = UiShadeSolid(q, p);
	}
	return UiEncode(color, Draw.Encoding, Draw.WhiteScale);
}
