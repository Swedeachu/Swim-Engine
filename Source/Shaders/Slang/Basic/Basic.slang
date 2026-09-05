// First Slang-native raster sample for the modern renderer path.
//
// It intentionally uses ParameterBlock<> instead of hard-coded Vulkan binding
// numbers so reflection becomes the source of truth for future RHI layouts.

struct FrameParameters
{
	float4x4 View;
	float4x4 Projection;
};

struct ObjectParameters
{
	float4x4 Model;
	float HasTexture;
	float3 Padding;
};

struct MaterialParameters
{
	Texture2D<float4> AlbedoTexture;
	SamplerState AlbedoSampler;
};

ParameterBlock<FrameParameters> Frame;
ParameterBlock<ObjectParameters> Object;
ParameterBlock<MaterialParameters> Material;

struct VertexInput
{
	float3 Position : POSITION;
	float3 Color : COLOR;
	float2 TexCoord : TEXCOORD0;
};

struct VertexOutput
{
	float4 Position : SV_Position;
	float3 Color : COLOR;
	float2 TexCoord : TEXCOORD0;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
	VertexOutput output;
	float4 worldPosition = mul(Object.Model, float4(input.Position, 1.0f));
	float4 viewPosition = mul(Frame.View, worldPosition);
	output.Position = mul(Frame.Projection, viewPosition);
	output.Color = input.Color;
	output.TexCoord = input.TexCoord;
	return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
	if (Object.HasTexture > 0.5f)
	{
		return Material.AlbedoTexture.Sample(Material.AlbedoSampler, input.TexCoord);
	}
	return float4(input.Color, 1.0f);
}
