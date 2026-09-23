// Standard-material draw smoke (items 59-60): GPU-driven draws of GPU Scene rows
// whose MaterialSet indexes the GpuMaterialTable buffer. Textures and samplers
// come from the bindless arrays in space 1; shading is StandardPbrShade with one
// directional light and a constant ambient term. Normal maps use a tangent frame
// derived from screen-space derivatives (no tangent stream).
#include "GpuScene/GpuSceneRecords.slang"
#include "GpuScene/VisibilityRecords.slang"
#include "Materials/StandardPbr.slang"

struct ShadingView
{
	float4 ViewProjection[4]; // Row-major rows.
	float4 ViewDirection;	  // xyz: toward the camera.
	float4 LightDirection;	  // xyz: toward the light.
	float4 LightRadiance;
	float4 Ambient;
	uint MaterialCount;
	uint Reserved0;
	uint Reserved1;
	uint Reserved2;
};

[[vk::binding(0, 0)]] StructuredBuffer<GpuInstanceRecord> Instances;
[[vk::binding(1, 0)]] StructuredBuffer<GpuTransformRecord> Transforms;
[[vk::binding(2, 0)]] StructuredBuffer<GpuDrawRecord> DrawRecords;
[[vk::binding(3, 0)]] StructuredBuffer<float> Positions;
[[vk::binding(4, 0)]] StructuredBuffer<ShadingView> Views;
[[vk::binding(5, 0)]] StructuredBuffer<StandardMaterialParameters> Materials;
[[vk::binding(0, 1)]] SamplerState Samplers[];
[[vk::binding(1, 1)]] Texture2D<float4> Textures[];

struct VertexOutput
{
	float4 Position : SV_Position;
	float3 WorldPosition : WORLD_POSITION;
	float3 Normal : NORMAL;
	float2 Uv : TEXCOORD0;
	nointerpolation uint Material : MATERIAL;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexId : SV_VulkanVertexID, uint drawSlot : SV_VulkanInstanceID)
{
	const GpuDrawRecord record = DrawRecords[drawSlot];
	const GpuInstanceRecord instance = Instances[record.InstanceRow];
	const float3 local = float3(Positions[vertexId * 3], Positions[vertexId * 3 + 1], Positions[vertexId * 3 + 2]);
	const GpuTransformRecord transform = Transforms[instance.TransformIndex];
	const float3 world = GpuSceneTransformPoint(transform.Current, local);
	const ShadingView view = Views[0];
	const float4 p = float4(world, 1.0);
	VertexOutput output;
	output.Position = float4(dot(view.ViewProjection[0], p), dot(view.ViewProjection[1], p), dot(view.ViewProjection[2], p),
		dot(view.ViewProjection[3], p));
	output.WorldPosition = world;
	// Flat quads in local XY facing +Z; uniform scale keeps the normal transform simple.
	output.Normal = normalize(float3(transform.Current[0].z, transform.Current[1].z, transform.Current[2].z));
	output.Uv = local.xy * 0.5 + 0.5;
	output.Material = instance.MaterialSet < view.MaterialCount ? instance.MaterialSet : 0u;
	return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input, bool frontFacing : SV_IsFrontFace) : SV_Target0
{
	const ShadingView view = Views[0];
	const StandardMaterialParameters material = Materials[input.Material];
	const SamplerState materialSampler = Samplers[NonUniformResourceIndex(material.MaterialSampler)];
	StandardPbrTexels texels;
	texels.BaseColor = Textures[NonUniformResourceIndex(material.BaseColorTexture)].Sample(materialSampler, input.Uv);
	texels.MetallicRoughness = Textures[NonUniformResourceIndex(material.MetallicRoughnessTexture)].Sample(materialSampler, input.Uv).rgb;
	texels.HasNormal = material.NormalTexture != 0;
	texels.TangentNormal = Textures[NonUniformResourceIndex(material.NormalTexture)].Sample(materialSampler, input.Uv).rgb * 2.0 - 1.0;
	texels.Occlusion = Textures[NonUniformResourceIndex(material.OcclusionTexture)].Sample(materialSampler, input.Uv).r;
	texels.Emissive = Textures[NonUniformResourceIndex(material.EmissiveTexture)].Sample(materialSampler, input.Uv).rgb;

	// Tangent frame from screen-space derivatives: invert the 2x2 Jacobian of uv so
	// T = dp/du and B = dp/dv exactly (orientation-safe with the flipped viewport),
	// then orthogonalize against the normal.
	const float3 normal = normalize(input.Normal);
	const float3 dp1 = ddx(input.WorldPosition);
	const float3 dp2 = ddy(input.WorldPosition);
	const float2 duv1 = ddx(input.Uv);
	const float2 duv2 = ddy(input.Uv);
	const float determinant = duv1.x * duv2.y - duv1.y * duv2.x;
	const float inverse = abs(determinant) > 1.0e-20 ? 1.0 / determinant : 0.0;
	const float3 dpdu = (dp1 * duv2.y - dp2 * duv1.y) * inverse;
	const float3 dpdv = (dp2 * duv1.x - dp1 * duv2.x) * inverse;
	const float3 tangent = normalize(dpdu - normal * dot(normal, dpdu) + float3(1.0e-20, 0.0, 0.0));
	const float3 bitangent = normalize(dpdv - normal * dot(normal, dpdv) + float3(0.0, 1.0e-20, 0.0));

	StandardPbrLighting lighting;
	lighting.View = view.ViewDirection.xyz;
	lighting.LightDirection = view.LightDirection.xyz;
	lighting.LightRadiance = view.LightRadiance.rgb;
	lighting.Ambient = view.Ambient.rgb;
	const StandardPbrResult result = StandardPbrShade(material, texels, normal, tangent, bitangent, frontFacing, lighting);
	if (result.Discard)
	{
		discard;
	}
	return result.Color;
}
