struct PushConstants
{
	float4x4 view;
	float4x4 proj;
	float3x3 rotation;
};

[[vk::push_constant]]
PushConstants pc;

struct VSInput
{
	float3 position : POSITION;
};

struct VSOutput
{
	float4 position : SV_Position;
	float3 texDir : TEXCOORD0;
};

VSOutput main(VSInput input)
{
	VSOutput output;

	// Apply rotation to skybox direction vector
	float3 rotatedDir = mul(pc.rotation, input.position);
	output.texDir = rotatedDir;

	// Project into clip space (w trick keeps box infinitely far)
	float4 clipPos = mul(pc.proj, mul(pc.view, float4(input.position, 1.0f)));
	output.position = clipPos.xyww;

	return output;
}
