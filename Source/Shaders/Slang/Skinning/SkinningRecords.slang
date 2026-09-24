// GPU skinning (critical-path item 78): mirrors Renderer/Skinning/SkinningRecords.h
// (std430; the ShaderCompiler.SkinningLayout test compares every offset).
#ifndef SWIM_SKINNING_RECORDS_SLANG
#define SWIM_SKINNING_RECORDS_SLANG

struct SkinVertex
{
	uint Joints[2]; // Four 16-bit joints, low half first.
	float Weights[4];
	uint MorphFirst; // Relative to the dispatch's SourceMorph.
	uint MorphCount;
};

struct MorphDelta
{
	uint Target;
	float Position[3];
	float Normal[3];
	float Tangent[3];
};

struct SkinDispatch
{
	uint SourceVertex;
	uint VertexCount;
	uint OutputVertex;
	uint PreviousOffset;
	uint Palette; // Matrices: JointCount current, then JointCount previous.
	uint JointCount;
	uint MorphWeights; // MorphTargetCount current, then as many previous.
	uint MorphTargetCount;
	uint SourceMorph;
	uint Reserved0;
	uint Reserved1;
	uint Reserved2;
};

static const uint SkinVertexFloats = 12; // StandardVertex.

uint SkinJoint(SkinVertex skin, uint slot)
{
	return (skin.Joints[slot >> 1] >> ((slot & 1u) * 16u)) & 0xffffu;
}

#endif
