#include "Engine/Assets/AnimationClipAsset.h"
#include "Engine/Assets/AssetDatabase.h"
#include "Engine/Assets/AssetSystem.h"
#include "Engine/Assets/MeshAsset.h"
#include "Engine/Assets/ModelAsset.h"
#include "Engine/Assets/SassetDecodedAsset.h"
#include "Engine/Assets/SassetFormat.h"
#include "Engine/Assets/SkeletonAsset.h"
#include "Tests/Fixtures/SkinnedGltfFixture.h"
#include "Tests/Framework/Test.h"
#include "Tools/AssetCompiler/GltfImporter.h"
#include "Tools/AssetCompiler/SassetWriter.h"
#include "Tools/AssetCompiler/StaticModelCompiler.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace
{
	using namespace Swim::Assets;
	using namespace Swim::AssetCompiler;

	struct PackedSkin
	{
		std::uint16_t Joints[4];
		float Weights[4];
	};

	static_assert(sizeof(PackedSkin) == 24);

	const CompiledSasset* FindAsset(const StaticModelCompileResult& result, SassetAssetType type, std::size_t ordinal = 0)
	{
		for (const CompiledSasset& asset : result.Assets)
		{
			if (asset.Type == type && ordinal-- == 0)
			{
				return &asset;
			}
		}
		return nullptr;
	}

	template <typename T> T Decode(const CompiledSasset& compiled)
	{
		SassetDecodeResult decoded = DecodeSasset(compiled.Bytes);
		SWIM_REQUIRE_MESSAGE(static_cast<bool>(decoded), decoded.Error.Message);
		SWIM_REQUIRE(std::holds_alternative<T>(decoded.Decoded.Asset));
		return std::get<T>(std::move(decoded.Decoded.Asset));
	}

	std::vector<std::byte> Build(SassetAssetType type, const std::string& path, std::vector<std::byte> payload)
	{
		AssetDatabase ids;
		SassetBuildInput input;
		input.Type = type;
		input.Id = ids.GetOrCreate(path);
		input.LogicalPath = path;
		input.CompilerProfileHash = ComputeContentHash("animation-tests");
		input.Payload = std::move(payload);
		SassetBuildResult built = BuildSasset(input);
		SWIM_REQUIRE_MESSAGE(static_cast<bool>(built), built.Error.Message);
		return built.Bytes;
	}

	StaticModelCompileResult CompileFixture()
	{
		const Swim::Testing::SkinnedGltfFixture fixture("swim-skinned-compile-test.gltf");
		GltfImportResult imported = GltfImporter{}.Import(fixture.Path());
		SWIM_REQUIRE_MESSAGE(static_cast<bool>(imported), imported.Error.Message);
		imported.Model.Nodes[4].Weights = { 0.25f }; // See SkinnedGltfFixture: fastgltf 0.9 cannot parse node weights.
		StaticModelCompileResult compiled = StaticModelCompiler{}.Compile(imported.Model, "Models/Skinned.gltf", {});
		SWIM_REQUIRE_MESSAGE(static_cast<bool>(compiled), compiled.Error.Message);
		return compiled;
	}
} // namespace

SWIM_TEST("AssetCompiler.GltfImporter", "ImportsSkinsInfluencesMorphTargetsAndAnimations")
{
	const Swim::Testing::SkinnedGltfFixture fixture("swim-skinned-import-test.gltf");
	const GltfImportResult result = GltfImporter{}.Import(fixture.Path());
	SWIM_REQUIRE_MESSAGE(static_cast<bool>(result), result.Error.Message);
	const IntermediateModel& model = result.Model;

	SWIM_REQUIRE_EQUAL(model.Skins.size(), std::size_t{ 1 });
	const SourceSkin& skin = model.Skins[0];
	SWIM_REQUIRE_EQUAL(skin.Joints.size(), std::size_t{ 3 });
	SWIM_CHECK_EQUAL(skin.Joints[0], 3u);
	SWIM_CHECK_EQUAL(skin.Joints[1], 2u);
	SWIM_CHECK_EQUAL(skin.Joints[2], 1u);
	SWIM_CHECK(skin.Skeleton == std::optional<std::uint32_t>(1));
	for (std::size_t joint = 0; joint < 3; ++joint)
	{
		SWIM_CHECK(skin.InverseBindMatrices[joint] == Swim::Testing::SkinnedGltfFixture::InverseBind(joint));
	}

	SWIM_REQUIRE_EQUAL(model.Meshes.size(), std::size_t{ 1 });
	const SourceMesh& mesh = model.Meshes[0];
	SWIM_REQUIRE_EQUAL(mesh.DefaultWeights.size(), std::size_t{ 1 });
	SWIM_CHECK_EQUAL(mesh.DefaultWeights[0], 0.5f);
	const SourcePrimitive& primitive = mesh.Primitives[0];
	SWIM_REQUIRE_EQUAL(primitive.Vertices.size(), std::size_t{ 4 });
	for (std::size_t v = 0; v < 4; ++v)
	{
		const SourceVertex& vertex = primitive.Vertices[v];
		SWIM_CHECK(vertex.HasSkin);
		SWIM_CHECK(vertex.Joints == Swim::Testing::SkinnedGltfFixture::Joints[v]);
		SWIM_CHECK(vertex.Weights == Swim::Testing::SkinnedGltfFixture::Weights[v]);
	}
	SWIM_REQUIRE_EQUAL(primitive.Targets.size(), std::size_t{ 1 });
	SWIM_REQUIRE_EQUAL(primitive.Targets[0].Position.size(), std::size_t{ 4 });
	SWIM_CHECK(primitive.Targets[0].Normal.empty());
	SWIM_CHECK(primitive.Targets[0].Position[3] == Swim::Testing::SkinnedGltfFixture::MorphDeltas[3]);

	SWIM_REQUIRE_EQUAL(model.Nodes.size(), std::size_t{ 5 });
	SWIM_CHECK(model.Nodes[4].SkinIndex == std::optional<std::uint32_t>(0));
	SWIM_CHECK(model.Nodes[4].Weights.empty());

	SWIM_REQUIRE_EQUAL(model.Animations.size(), std::size_t{ 1 });
	const SourceAnimation& animation = model.Animations[0];
	SWIM_CHECK_EQUAL(animation.Name, std::string("Wave"));
	SWIM_REQUIRE_EQUAL(animation.Channels.size(), std::size_t{ 4 });
	SWIM_CHECK(animation.Channels[0].Path == AnimationPath::Rotation);
	SWIM_CHECK_EQUAL(animation.Channels[0].Components, 4u);
	SWIM_CHECK_EQUAL(animation.Channels[0].Values.size(), std::size_t{ 8 });
	SWIM_CHECK(animation.Channels[1].Interpolation == AnimationInterpolation::Step);
	SWIM_CHECK(animation.Channels[1].Path == AnimationPath::Translation);
	SWIM_CHECK(animation.Channels[2].Path == AnimationPath::MorphWeights);
	SWIM_CHECK(animation.Channels[2].Interpolation == AnimationInterpolation::CubicSpline);
	SWIM_CHECK_EQUAL(animation.Channels[2].Components, 1u);
	SWIM_CHECK_EQUAL(animation.Channels[2].Values.size(), std::size_t{ 6 });
	SWIM_CHECK(animation.Channels[3].Path == AnimationPath::Scale);
	SWIM_CHECK_EQUAL(animation.Channels[3].Node, 0u);
}

SWIM_TEST("AssetCompiler.StaticModelCompiler", "SkeletonsAreParentsFirstAndSkinJointsAreRemapped")
{
	const StaticModelCompileResult compiled = CompileFixture();
	SWIM_CHECK_EQUAL(compiled.Stats.Skeletons, std::size_t{ 1 });
	SWIM_CHECK_EQUAL(compiled.Stats.SkinnedMeshes, std::size_t{ 1 });

	const CompiledSasset* skeletonFile = FindAsset(compiled, SassetAssetType::Skeleton);
	SWIM_REQUIRE(skeletonFile != nullptr);
	const SkeletonAsset skeleton = Decode<SkeletonAsset>(*skeletonFile);
	SWIM_REQUIRE_EQUAL(skeleton.Joints.size(), std::size_t{ 3 });
	SWIM_CHECK_EQUAL(skeleton.Joints[0].Name, std::string("Hip"));
	SWIM_CHECK_EQUAL(skeleton.Joints[1].Name, std::string("Spine"));
	SWIM_CHECK_EQUAL(skeleton.Joints[2].Name, std::string("Head"));
	SWIM_CHECK_EQUAL(skeleton.Joints[0].Parent, SkeletonJoint::InvalidJoint);
	SWIM_CHECK_EQUAL(skeleton.Joints[1].Parent, 0u);
	SWIM_CHECK_EQUAL(skeleton.Joints[2].Parent, 1u);
	SWIM_CHECK_EQUAL(skeleton.Joints[2].SourceNode, 3u);
	// Skin joint 2 was Hip, 1 Spine, 0 Head.
	SWIM_CHECK(skeleton.Joints[0].InverseBind == Swim::Testing::SkinnedGltfFixture::InverseBind(2));
	SWIM_CHECK(skeleton.Joints[2].InverseBind == Swim::Testing::SkinnedGltfFixture::InverseBind(0));
	// The Armature above the root joint becomes the root transform.
	const float rootZ = skeleton.RootTransform[14];
	SWIM_CHECK_EQUAL(rootZ, 1.0f);
	const float spineY = skeleton.Joints[1].RestTransform.Translation[1];
	SWIM_CHECK_EQUAL(spineY, 1.0f);

	const CompiledSasset* meshFile = FindAsset(compiled, SassetAssetType::Mesh);
	SWIM_REQUIRE(meshFile != nullptr);
	const MeshAsset mesh = Decode<MeshAsset>(*meshFile);
	SWIM_REQUIRE_EQUAL(mesh.VertexStreams.size(), std::size_t{ 2 });
	SWIM_CHECK_EQUAL(mesh.VertexStreams[1].StrideBytes, 24u);
	SWIM_CHECK_EQUAL(mesh.VertexStreams[1].DataOffsetBytes, std::uint64_t{ 4 * 48 });
	SWIM_REQUIRE_EQUAL(mesh.VertexBytes.size(), std::size_t{ 4 * 48 + 4 * 24 });

	PackedSkin skin[4];
	std::memcpy(skin, mesh.VertexBytes.data() + 4 * 48, sizeof(skin));
	// Vertex 0: (0.2 Hip, 0.6 Spine) -> sorted, normalized, remapped: Spine 0.75, Hip 0.25.
	SWIM_CHECK_EQUAL(skin[0].Joints[0], std::uint16_t{ 1 });
	SWIM_CHECK_EQUAL(skin[0].Joints[1], std::uint16_t{ 0 });
	SWIM_CHECK_NEAR(skin[0].Weights[0], 0.75f, 1e-6f);
	SWIM_CHECK_NEAR(skin[0].Weights[1], 0.25f, 1e-6f);
	SWIM_CHECK_EQUAL(skin[0].Weights[2], 0.0f);
	SWIM_CHECK_EQUAL(skin[0].Joints[2], std::uint16_t{ 1 }); // Unused slots repeat the strongest joint.
	// Vertex 2: (0.1 Head, 0.1 Spine, 0.2 Hip) -> Hip 0.5, Head 0.25, Spine 0.25.
	SWIM_CHECK_EQUAL(skin[2].Joints[0], std::uint16_t{ 0 });
	SWIM_CHECK_EQUAL(skin[2].Joints[1], std::uint16_t{ 2 });
	SWIM_CHECK_EQUAL(skin[2].Joints[2], std::uint16_t{ 1 });
	SWIM_CHECK_NEAR(skin[2].Weights[0], 0.5f, 1e-6f);
	// Vertex 3 had no weight: bound fully to skin joint 0 (Head).
	SWIM_CHECK_EQUAL(skin[3].Joints[0], std::uint16_t{ 2 });
	SWIM_CHECK_EQUAL(skin[3].Weights[0], 1.0f);
	for (const PackedSkin& influences : skin)
	{
		const float sum = influences.Weights[0] + influences.Weights[1] + influences.Weights[2] + influences.Weights[3];
		SWIM_CHECK_NEAR(sum, 1.0f, 1e-6f);
		SWIM_CHECK(influences.Weights[0] >= influences.Weights[1] && influences.Weights[1] >= influences.Weights[2]);
	}

	SWIM_REQUIRE_EQUAL(mesh.MorphTargets.size(), std::size_t{ 1 });
	SWIM_REQUIRE_EQUAL(mesh.MorphTargets[0].PositionDeltas.size(), std::size_t{ 12 });
	SWIM_CHECK(mesh.MorphTargets[0].NormalDeltas.empty());
	const float delta = mesh.MorphTargets[0].PositionDeltas[3 * 3 + 2];
	SWIM_CHECK_EQUAL(delta, 1.0f);
	SWIM_REQUIRE_EQUAL(mesh.DefaultMorphWeights.size(), std::size_t{ 1 });
	SWIM_CHECK_EQUAL(mesh.DefaultMorphWeights[0], 0.5f);
}

SWIM_TEST("AssetCompiler.StaticModelCompiler", "AnimationsBecomeNameBoundClipsReferencedByTheModel")
{
	const StaticModelCompileResult compiled = CompileFixture();
	SWIM_CHECK_EQUAL(compiled.Stats.Animations, std::size_t{ 1 });
	const CompiledSasset* clipFile = FindAsset(compiled, SassetAssetType::AnimationClip);
	SWIM_REQUIRE(clipFile != nullptr);
	const AnimationClipAsset clip = Decode<AnimationClipAsset>(*clipFile);
	SWIM_CHECK_EQUAL(clip.Name, std::string("Wave"));
	SWIM_CHECK_EQUAL(clip.Duration, 1.0f);
	SWIM_REQUIRE_EQUAL(clip.Tracks.size(), std::size_t{ 4 });
	SWIM_CHECK_EQUAL(clip.Tracks[0].Target, std::string("Spine"));
	SWIM_CHECK_EQUAL(clip.Tracks[1].Target, std::string("Hip"));
	SWIM_CHECK_EQUAL(clip.Tracks[2].Target, std::string("Body"));
	SWIM_CHECK(clip.Tracks[2].Path == AnimationPath::MorphWeights);
	SWIM_CHECK_EQUAL(clip.Tracks[3].Target, std::string("Armature"));

	// Load the whole graph through the runtime: the model resolves its skin,
	// skeleton list, animations and morph weights as typed handles.
	AssetSystem assets;
	SWIM_REQUIRE(assets.Initialize());
	for (const CompiledSasset& file : compiled.Assets)
	{
		const SassetLoadResult loaded = LoadSasset(assets, file.Bytes);
		SWIM_REQUIRE_MESSAGE(static_cast<bool>(loaded), loaded.Error.Message);
	}
	const ModelAsset* model = assets.Resolve(assets.Find<ModelAsset>(compiled.RootLogicalPath));
	SWIM_REQUIRE(model != nullptr);
	SWIM_REQUIRE_EQUAL(model->Skeletons.size(), std::size_t{ 1 });
	SWIM_REQUIRE_EQUAL(model->Animations.size(), std::size_t{ 1 });
	SWIM_REQUIRE_EQUAL(model->Nodes.size(), std::size_t{ 5 });
	SWIM_CHECK(model->Nodes[4].Skin == model->Skeletons[0]);
	SWIM_CHECK(!model->Nodes[1].Skin.IsValid());
	SWIM_REQUIRE_EQUAL(model->Nodes[4].MorphWeights.size(), std::size_t{ 1 });
	SWIM_CHECK_EQUAL(model->Nodes[4].MorphWeights[0], 0.25f);
	SWIM_CHECK(assets.Resolve(model->Skeletons[0]) != nullptr);
	const AnimationClipAsset* resolvedClip = assets.Resolve(model->Animations[0]);
	SWIM_REQUIRE(resolvedClip != nullptr);
	SWIM_CHECK_EQUAL(resolvedClip->Tracks.size(), std::size_t{ 4 });
	assets.Shutdown();
}

SWIM_TEST("AssetCompiler.SassetFormat", "StaticPayloadsKeepVersionOneAndAnimatedOnesUseVersionTwo")
{
	MeshAsset mesh;
	mesh.VertexStreams.push_back({ 12, 0, 12 });
	mesh.VertexAttributes.push_back({ VertexSemantic::Position, VertexElementFormat::Float32x3, 0, 0 });
	mesh.VertexBytes.resize(12);
	const auto versionOf = [](const std::vector<std::byte>& payload)
	{
		std::uint32_t version = 0;
		std::memcpy(&version, payload.data(), sizeof(version));
		return version;
	};
	SWIM_CHECK_EQUAL(versionOf(SerializeAssetPayload(mesh)), SassetPayloadVersion);
	mesh.MorphTargets.push_back({ "smile", { 0, 0, 1 }, {}, {} });
	mesh.DefaultMorphWeights = { 0.5f };
	SWIM_CHECK_EQUAL(versionOf(SerializeAssetPayload(mesh)), SassetMeshPayloadVersion);

	const auto bytes = Build(SassetAssetType::Mesh, "Meshes/Morph", SerializeAssetPayload(mesh));
	SassetDecodeResult decoded = DecodeSasset(bytes);
	SWIM_REQUIRE_MESSAGE(static_cast<bool>(decoded), decoded.Error.Message);
	const MeshAsset& roundTrip = std::get<MeshAsset>(decoded.Decoded.Asset);
	SWIM_REQUIRE_EQUAL(roundTrip.MorphTargets.size(), std::size_t{ 1 });
	SWIM_CHECK_EQUAL(roundTrip.MorphTargets[0].Name, std::string("smile"));
	SWIM_CHECK(roundTrip.MorphTargets[0].PositionDeltas == std::vector<float>({ 0, 0, 1 }));

	// A delta array that does not cover the vertices is rejected.
	mesh.MorphTargets[0].PositionDeltas = { 0, 0, 1, 0, 0, 1 };
	SWIM_CHECK(!DecodeSasset(Build(SassetAssetType::Mesh, "Meshes/BadMorph", SerializeAssetPayload(mesh))));

	ModelAsset model;
	model.Nodes.resize(1);
	SWIM_CHECK_EQUAL(versionOf(SerializeAssetPayload(model)), SassetPayloadVersion);
	model.Nodes[0].MorphWeights = { 1.0f };
	SWIM_CHECK_EQUAL(versionOf(SerializeAssetPayload(model)), SassetModelPayloadVersion);
}

SWIM_TEST("AssetCompiler.SassetFormat", "MalformedSkeletonsAndClipsAreRejected")
{
	SkeletonAsset skeleton;
	skeleton.Joints.resize(2);
	skeleton.Joints[0].Name = "Root";
	skeleton.Joints[1].Name = "Child";
	skeleton.Joints[1].Parent = 0;
	SWIM_CHECK(static_cast<bool>(DecodeSasset(Build(SassetAssetType::Skeleton, "Skeletons/Good", SerializeAssetPayload(skeleton)))));
	skeleton.Joints[0].Parent = 1; // Child listed after its parent's parent: not parents first.
	SWIM_CHECK(!DecodeSasset(Build(SassetAssetType::Skeleton, "Skeletons/Cycle", SerializeAssetPayload(skeleton))));

	AnimationClipAsset clip;
	clip.Duration = 1.0f;
	AnimationTrack track;
	track.Target = "Root";
	track.Path = AnimationPath::Rotation;
	track.Components = 4;
	track.Times = { 0.0f, 1.0f };
	track.Values = { 0, 0, 0, 1, 0, 0, 0, 1 };
	clip.Tracks.push_back(track);
	clip.Events = { { 0.25f, "step" }, { 0.75f, "step" } };
	const auto good = DecodeSasset(Build(SassetAssetType::AnimationClip, "Clips/Good", SerializeAssetPayload(clip)));
	SWIM_REQUIRE(static_cast<bool>(good));
	SWIM_CHECK_EQUAL(std::get<AnimationClipAsset>(good.Decoded.Asset).Events.size(), std::size_t{ 2 });

	AnimationClipAsset bad = clip;
	bad.Tracks[0].Times = { 1.0f, 1.0f };
	SWIM_CHECK(!DecodeSasset(Build(SassetAssetType::AnimationClip, "Clips/Times", SerializeAssetPayload(bad))));
	bad = clip;
	bad.Tracks[0].Components = 3;
	SWIM_CHECK(!DecodeSasset(Build(SassetAssetType::AnimationClip, "Clips/Components", SerializeAssetPayload(bad))));
	bad = clip;
	bad.Tracks[0].Interpolation = AnimationInterpolation::CubicSpline; // Needs three values per key.
	SWIM_CHECK(!DecodeSasset(Build(SassetAssetType::AnimationClip, "Clips/Cubic", SerializeAssetPayload(bad))));
	bad = clip;
	bad.Events = { { 0.75f, "b" }, { 0.25f, "a" } };
	SWIM_CHECK(!DecodeSasset(Build(SassetAssetType::AnimationClip, "Clips/Events", SerializeAssetPayload(bad))));
}
