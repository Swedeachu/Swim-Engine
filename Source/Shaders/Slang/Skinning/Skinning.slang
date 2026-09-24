// GPU skinning (critical-path item 78), program SwimSkinning. One thread per
// output vertex, one group row per instance of the dispatch (push constant: the
// first row). Each vertex: sparse morph deltas in bind space (current and previous
// weights), then linear blend skinning by up to four joints with this frame's and
// the previous frame's palettes. Writes the full StandardVertex at OutputVertex and
// the previous position PreviousOffset vertices further in the same GeometryHeap
// page, which Clustered Forward+ reads for motion vectors. Mirrors
// Skinning::SkinVertex / SkinPosition; bindings follow SkinningBindings.
#include "Skinning/SkinningRecords.slang"

struct SkinningConstants
{
	uint FirstRow;
};

[[vk::push_constant]] ConstantBuffer<SkinningConstants> Constants;
[[vk::binding(0, 0)]] StructuredBuffer<SkinDispatch> Dispatches;
[[vk::binding(1, 0)]] StructuredBuffer<float> SourceVertices;
[[vk::binding(2, 0)]] StructuredBuffer<SkinVertex> SkinVertices;
[[vk::binding(3, 0)]] StructuredBuffer<MorphDelta> MorphDeltas;
[[vk::binding(4, 0)]] StructuredBuffer<float4> Palettes;
[[vk::binding(5, 0)]] StructuredBuffer<float> MorphWeights;
[[vk::binding(6, 0)]] RWStructuredBuffer<float> Output;

float3 SkinNormalized(float3 v, float3 fallback)
{
	const float lengthSquared = dot(v, v);
	return lengthSquared > 1e-30 ? v / sqrt(lengthSquared) : fallback;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 id : SV_DispatchThreadID, uint3 group : SV_GroupID)
{
	const SkinDispatch d = Dispatches[Constants.FirstRow + group.y];
	const uint v = id.x;
	if (v >= d.VertexCount)
	{
		return;
	}
	const uint vertex = d.SourceVertex + v;
	float s[SkinVertexFloats];
	for (uint i = 0; i < SkinVertexFloats; ++i)
	{
		s[i] = SourceVertices[vertex * SkinVertexFloats + i];
	}
	const SkinVertex skin = SkinVertices[vertex];
	const float3 sourceNormal = float3(s[3], s[4], s[5]);
	const float3 sourceTangent = float3(s[6], s[7], s[8]);
	float3 position = float3(s[0], s[1], s[2]);
	float3 previousPosition = position;
	float3 normal = sourceNormal;
	float3 tangent = sourceTangent;
	for (uint k = 0; k < skin.MorphCount; ++k)
	{
		const MorphDelta delta = MorphDeltas[d.SourceMorph + skin.MorphFirst + k];
		const float w = MorphWeights[d.MorphWeights + delta.Target];
		const float wp = MorphWeights[d.MorphWeights + d.MorphTargetCount + delta.Target];
		const float3 dp = float3(delta.Position[0], delta.Position[1], delta.Position[2]);
		position += w * dp;
		previousPosition += wp * dp;
		normal += w * float3(delta.Normal[0], delta.Normal[1], delta.Normal[2]);
		tangent += w * float3(delta.Tangent[0], delta.Tangent[1], delta.Tangent[2]);
	}

	// Blended rows of the current (0..2) and previous (3..5) palettes, loaded in one loop.
	float4 rows[6] = { float4(0, 0, 0, 0), float4(0, 0, 0, 0), float4(0, 0, 0, 0), float4(0, 0, 0, 0), float4(0, 0, 0, 0), float4(0, 0, 0, 0) };
	for (uint slot = 0; slot < 4; ++slot)
	{
		const float w = skin.Weights[slot];
		if (w == 0.0)
		{
			continue;
		}
		const uint joint = SkinJoint(skin, slot);
		for (uint r = 0; r < 6; ++r)
		{
			const uint matrix = d.Palette + joint + (r < 3 ? 0u : d.JointCount);
			rows[r] += w * Palettes[matrix * 3u + (r < 3 ? r : r - 3u)];
		}
	}
	const float4 p1 = float4(position, 1.0);
	const float4 q1 = float4(previousPosition, 1.0);
	const float3 skinned = float3(dot(rows[0], p1), dot(rows[1], p1), dot(rows[2], p1));
	const float3 previous = float3(dot(rows[3], q1), dot(rows[4], q1), dot(rows[5], q1));
	const float3 r0 = rows[0].xyz, r1 = rows[1].xyz, r2 = rows[2].xyz;
	const float3 cofactor = float3(dot(cross(r1, r2), normal), dot(cross(r2, r0), normal), dot(cross(r0, r1), normal));
	const float3 n = SkinNormalized(cofactor, sourceNormal);
	const float3 t = SkinNormalized(float3(dot(r0, tangent), dot(r1, tangent), dot(r2, tangent)), sourceTangent);

	float o[SkinVertexFloats + 3] = { skinned.x, skinned.y, skinned.z, n.x, n.y, n.z, t.x, t.y, t.z, s[9], s[10], s[11], previous.x, previous.y,
		previous.z };
	const uint out = (d.OutputVertex + v) * SkinVertexFloats;
	const uint previousOut = (d.OutputVertex + d.PreviousOffset + v) * SkinVertexFloats;
	for (uint i = 0; i < SkinVertexFloats + 3; ++i)
	{
		Output[i < SkinVertexFloats ? out + i : previousOut + (i - SkinVertexFloats)] = o[i];
	}
}
