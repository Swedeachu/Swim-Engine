#include "Engine/Systems/Animation/SkeletonInstance.h"
#include "Engine/Systems/Renderer/Skinning/SkinningReference.h"
#include "Tests/Fixtures/SkinningFixture.h"
#include "Tests/Framework/Test.h"

#include <cmath>
#include <random>
#include <stdexcept>

using namespace Swim;
using namespace Swim::Render;

namespace
{
	constexpr float Pi = 3.14159265358979f;

	SkinMatrix Affine(const Animation::JointPose& pose)
	{
		return Animation::ToAffineRows(Animation::ToMatrix(pose));
	}

	float Length(const std::array<float, 3>& v)
	{
		return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
	}

	bool Near(const std::array<float, 3>& a, const std::array<float, 3>& b, float tolerance = 1e-5f)
	{
		return std::abs(a[0] - b[0]) <= tolerance && std::abs(a[1] - b[1]) <= tolerance && std::abs(a[2] - b[2]) <= tolerance;
	}

	std::span<const GpuMorphDelta> DeltasOf(const SkinnedSource& source, std::size_t vertex)
	{
		const GpuSkinVertex& skin = source.SkinVertices[vertex];
		return std::span(source.MorphDeltas).subspan(skin.MorphFirst, skin.MorphCount);
	}
} // namespace

SWIM_TEST("Render.Skinning.Reference", "BindPoseIdentityRigidAndBlendedJoints")
{
	const Testing::SkinnedStrip strip;
	const SkinnedSource source = Skinning::BuildSource(strip.Vertices, strip.Influences, strip.Targets, Testing::SkinnedStrip::JointCount);
	const std::vector<SkinMatrix> identity(3, SkinMatrix{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 });
	const std::vector<float> noMorph{ 0.0f, 0.0f };
	for (std::size_t v = 0; v < strip.Vertices.size(); ++v)
	{
		const StandardVertex out = Skinning::SkinVertex(strip.Vertices[v], source.SkinVertices[v], DeltasOf(source, v), identity, noMorph);
		SWIM_CHECK(Near(out.Position, strip.Vertices[v].Position));
		SWIM_CHECK(Near(out.Normal, strip.Vertices[v].Normal));
		SWIM_CHECK_EQUAL(out.Tangent[3], strip.Vertices[v].Tangent[3]);
		SWIM_CHECK(out.TexCoord0 == strip.Vertices[v].TexCoord0);
	}

	// One rigid joint: the whole strip rotated 90 degrees about z and moved.
	Animation::JointPose rigid;
	rigid.Rotation = Animation::FromAxisAngle({ 0, 0, 1 }, Pi / 2.0f);
	rigid.Translation = { 3, 0, 0 };
	const std::vector<SkinMatrix> same(3, Affine(rigid));
	const std::size_t top = strip.Vertices.size() - 1;
	const StandardVertex moved = Skinning::SkinVertex(strip.Vertices[top], source.SkinVertices[top], DeltasOf(source, top), same, noMorph);
	const auto& p = strip.Vertices[top].Position;
	SWIM_CHECK(Near(moved.Position, { 3.0f - p[1], p[0], p[2] }));
	const auto& n = strip.Vertices[top].Normal;
	const float nLength = Length(n);
	SWIM_CHECK(Near(moved.Normal, { -n[1] / nLength, n[0] / nLength, n[2] / nLength }));
	SWIM_CHECK(Near({ moved.Tangent[0], moved.Tangent[1], moved.Tangent[2] }, { -1, 0, 0 }));

	// Halfway between two translated joints: the vertex moves half as far.
	const SkinInfluence half = Testing::SkinnedStrip::InfluenceAt(0.75f);
	SWIM_CHECK_NEAR(half.Weights[0] + half.Weights[1], 1.0f, 1e-6f);
	std::vector<SkinMatrix> split = identity;
	split[0][3] = 2.0f; // Root moves +2 in x, Mid stays.
	GpuSkinVertex skin;
	skin.Joints[0] = std::uint32_t(half.Joints[0]) | (std::uint32_t(half.Joints[1]) << 16);
	std::copy(half.Weights.begin(), half.Weights.end(), skin.Weights);
	StandardVertex vertex;
	vertex.Position = { 0.0f, 0.75f, 0.0f };
	SWIM_CHECK(Near(Skinning::SkinVertex(vertex, skin, {}, split, {}).Position, { 1.0f, 0.75f, 0.0f }));
}

SWIM_TEST("Render.Skinning.Reference", "MorphTargetsApplyInBindSpaceBeforeSkinning")
{
	const Testing::SkinnedStrip strip;
	const SkinnedSource source = Skinning::BuildSource(strip.Vertices, strip.Influences, strip.Targets, Testing::SkinnedStrip::JointCount);
	// Only non-zero deltas are stored: the bulge touches the middle rings, the normal target the top ring.
	std::size_t expected = 0;
	for (std::size_t v = 0; v < strip.Vertices.size(); ++v)
	{
		expected += (strip.Bulge[v] != std::array<float, 3>{ 0, 0, 0 }) + (strip.TopNormals[v] != std::array<float, 3>{ 0, 0, 0 });
	}
	SWIM_CHECK_EQUAL(source.MorphDeltas.size(), expected);
	SWIM_CHECK(source.MorphDeltas.size() < strip.Vertices.size() * 2);

	const std::vector<SkinMatrix> identity(3, SkinMatrix{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 });
	Animation::JointPose lifted;
	lifted.Translation = { 0, 1, 0 };
	const std::vector<SkinMatrix> up(3, Affine(lifted));
	const std::vector<float> weights{ 0.5f, 1.0f };
	const std::vector<float> previousWeights{ 0.0f, 0.0f };
	for (std::size_t v = 0; v < strip.Vertices.size(); ++v)
	{
		const auto& base = strip.Vertices[v].Position;
		const auto& bulge = strip.Bulge[v];
		const StandardVertex out = Skinning::SkinVertex(strip.Vertices[v], source.SkinVertices[v], DeltasOf(source, v), up, weights);
		SWIM_CHECK(Near(out.Position, { base[0] + 0.5f * bulge[0], base[1] + 1.0f, base[2] + 0.5f * bulge[2] }));
		const auto previous =
			Skinning::SkinPosition(strip.Vertices[v], source.SkinVertices[v], DeltasOf(source, v), identity, previousWeights);
		SWIM_CHECK(Near(previous, base));
		SWIM_CHECK_NEAR(Length(out.Normal), 1.0f, 1e-5f);
	}
}

SWIM_TEST("Render.Skinning.Reference", "NormalsFollowNonUniformScaleThroughTheCofactor")
{
	// A 45-degree surface: tangent (1, -1, 0), normal (1, 1, 0). Scaling x by 2 must keep
	// the transformed normal perpendicular to the transformed tangent.
	StandardVertex vertex;
	vertex.Normal = { 0.70710678f, 0.70710678f, 0.0f };
	vertex.Tangent = { 0.70710678f, -0.70710678f, 0.0f, 1.0f };
	GpuSkinVertex skin;
	const std::vector<SkinMatrix> stretch{ SkinMatrix{ 2, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 } };
	const StandardVertex out = Skinning::SkinVertex(vertex, skin, {}, stretch, {});
	const std::array<float, 3> tangent{ out.Tangent[0], out.Tangent[1], out.Tangent[2] };
	SWIM_CHECK_NEAR(out.Normal[0] * tangent[0] + out.Normal[1] * tangent[1] + out.Normal[2] * tangent[2], 0.0f, 1e-6f);
	SWIM_CHECK_NEAR(Length(out.Normal), 1.0f, 1e-6f);
	// The plain 3x3 would not: (2, 1)/|...| . (2, -1)/|...| != 0.
	SWIM_CHECK(out.Normal[1] > out.Normal[0]);
}

SWIM_TEST("Render.Skinning.Reference", "SourcesAreValidated")
{
	const Testing::SkinnedStrip strip;
	SWIM_CHECK_THROWS(Skinning::BuildSource(strip.Vertices, std::span(strip.Influences).first(3), {}, 3), std::invalid_argument);
	SWIM_CHECK_THROWS(Skinning::BuildSource(strip.Vertices, strip.Influences, {}, 2), std::invalid_argument); // Tip is joint 2.
	SWIM_CHECK_THROWS(Skinning::BuildSource(strip.Vertices, strip.Influences, {}, 0), std::invalid_argument);
	auto negative = strip.Influences;
	negative[0].Weights[1] = -0.5f;
	SWIM_CHECK_THROWS(Skinning::BuildSource(strip.Vertices, negative, {}, 3), std::invalid_argument);
	std::vector<std::array<float, 3>> shortDeltas(3);
	const SkinnedMorphTarget broken{ shortDeltas, {}, {} };
	SWIM_CHECK_THROWS(Skinning::BuildSource(strip.Vertices, strip.Influences, std::span(&broken, 1), 3), std::invalid_argument);
	// Zero-weight slots may name any joint; they are stored as joint 0 and skipped.
	auto ignored = strip.Influences;
	ignored[0].Joints[3] = 60000;
	const SkinnedSource packed = Skinning::BuildSource(strip.Vertices, ignored, {}, 3);
	SWIM_CHECK_EQUAL(Skinning::Joint(packed.SkinVertices[0], 3), std::uint16_t{ 0 });
}

SWIM_TEST("Render.Skinning.Reference", "BoundsContainEverySkinnedVertexOfRandomPoses")
{
	const Testing::SkinnedStrip strip;
	const SkinnedSource source = Skinning::BuildSource(strip.Vertices, strip.Influences, strip.Targets, Testing::SkinnedStrip::JointCount);
	const auto skeleton = Testing::MakeChainSkeleton();
	Animation::SkeletonInstance instance(skeleton);
	std::mt19937 random(78);
	std::uniform_real_distribution<float> angle(-Pi, Pi), weight(-0.5f, 1.5f), scale(0.5f, 2.0f);
	for (int trial = 0; trial < 50; ++trial)
	{
		Animation::AnimationPose pose = Animation::MakeRestPose(*skeleton);
		for (auto& joint : pose.Joints)
		{
			joint.Rotation = Animation::FromAxisAngle({ angle(random), angle(random), angle(random) }, angle(random));
			joint.Scale = { scale(random), scale(random), scale(random) };
		}
		instance.Update(pose);
		const std::vector<float> weights{ weight(random), weight(random) };
		const RenderBounds bounds = Skinning::ComputeBounds(source.Bounds, instance.GetSkinningMatrices(), weights);
		for (std::size_t v = 0; v < strip.Vertices.size(); ++v)
		{
			const auto p = Skinning::SkinPosition(
				strip.Vertices[v], source.SkinVertices[v], DeltasOf(source, v), instance.GetSkinningMatrices(), weights);
			for (int axis = 0; axis < 3; ++axis)
			{
				SWIM_CHECK(std::abs(p[axis] - bounds.Center[axis]) <= bounds.Extents[axis] + 1e-4f);
			}
		}
	}
	// At rest the bounds are the mesh's own box.
	instance.Update(Animation::MakeRestPose(*skeleton));
	const RenderBounds rest = Skinning::ComputeBounds(source.Bounds, instance.GetSkinningMatrices(), std::vector<float>{ 0.0f, 0.0f });
	SWIM_CHECK(Near(rest.Center, { 0.0f, 1.0f, 0.0f }));
	SWIM_CHECK(Near(rest.Extents, { 0.25f, 1.0f, 0.25f }));
}
