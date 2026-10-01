[[vk::binding(0, 0)]]
SamplerState cubeSampler : register(s0, space0);

[[vk::binding(1, 0)]]
TextureCube skybox : register(t1, space0);

struct FSInput
{
	float4 position : SV_Position;
	float3 texDir : TEXCOORD0;
};

float4 main(FSInput input) : SV_Target
{
	return skybox.Sample(cubeSampler, normalize(input.texDir));
}
