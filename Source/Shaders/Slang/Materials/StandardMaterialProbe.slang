// Reflection probe for StandardMaterialParameters: the material layout tests
// derive a MaterialTemplate from this program's reflection. Not loaded at runtime.
#include "Materials/StandardMaterialParameters.slang"

[[vk::binding(0, 0)]] StructuredBuffer<StandardMaterialParameters> Materials;
[[vk::binding(1, 0)]] RWStructuredBuffer<float4> Output;

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
	const StandardMaterialParameters material = Materials[id.x];
	Output[id.x] = material.BaseColorFactor + float4(material.EmissiveFactor, material.MetallicFactor) +
		float4(material.RoughnessFactor, material.NormalScale, material.OcclusionStrength, material.AlphaCutoff) +
		float4(float(material.BaseColorTexture + material.MetallicRoughnessTexture + material.NormalTexture),
			float(material.OcclusionTexture + material.EmissiveTexture), float(material.MaterialSampler), float(material.Flags + material.Reserved));
}
