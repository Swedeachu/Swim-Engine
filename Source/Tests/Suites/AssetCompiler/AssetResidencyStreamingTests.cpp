#include "Engine/Assets/SassetDecodedAsset.h"
#include "Tests/Fixtures/ResidencyServiceFixture.h"
#include "Tools/AssetCompiler/SassetWriter.h"

#include <cstring>

using namespace Swim;
using namespace Swim::Render;
using State = AssetResidencyState;

namespace
{
	Assets::MeshAsset MakeMesh(std::uint8_t seed)
	{
		Assets::MeshAsset mesh;
		mesh.VertexStreams = { { 12, 0, 36 }, { 8, 36, 24 } };
		mesh.VertexAttributes = { { Assets::VertexSemantic::Position, Assets::VertexElementFormat::Float32x3, 0, 0 },
			{ Assets::VertexSemantic::TexCoord0, Assets::VertexElementFormat::Float32x2, 1, 0 } };
		mesh.VertexBytes.resize(60);
		for (std::size_t i = 0; i < mesh.VertexBytes.size(); ++i)
		{
			mesh.VertexBytes[i] = static_cast<std::byte>(seed * 7 + i);
		}
		mesh.IndexFormat = Assets::IndexElementFormat::UInt16;
		mesh.IndexBytes.resize(6, std::byte{ 2 });
		mesh.Primitives = { { 0, 3, 0, 1, {} } };
		mesh.Lods = { { 0, 1, 1.0f } };
		return mesh;
	}

	Assets::TextureAsset MakeTexture()
	{
		Assets::TextureAsset texture;
		texture.Width = 2;
		texture.Height = 2;
		Assets::TexturePayloadVariant payload;
		payload.Format = Assets::TexturePayloadFormat::RGBA8UNorm;
		payload.Mips = { { 2, 2, 1, 0, 16, 16 }, { 1, 1, 1, 16, 4, 4 } };
		payload.Bytes.resize(20);
		for (std::size_t i = 0; i < payload.Bytes.size(); ++i)
		{
			payload.Bytes[i] = static_cast<std::byte>(200 - i);
		}
		texture.Payloads.push_back(std::move(payload));
		return texture;
	}

	template <typename T>
	std::vector<std::byte> Cook(Assets::SassetAssetType type, Assets::AssetId id, const std::string& path, const T& asset)
	{
		AssetCompiler::SassetBuildInput input;
		input.Type = type;
		input.Id = id;
		input.LogicalPath = path;
		input.CompilerProfileHash = Assets::ComputeContentHash("residency-tests");
		input.Payload = AssetCompiler::SerializeAssetPayload(asset);
		const auto built = AssetCompiler::BuildSasset(input);
		SWIM_REQUIRE_MESSAGE(static_cast<bool>(built), built.Error.Message);
		return built.Bytes;
	}
} // namespace

SWIM_TEST("AssetCompiler.SassetDecode", "DecodesOffThreadAndPublishesOnTheOwnerThread")
{
	Assets::AssetDatabase ids;
	const auto id = ids.GetOrCreate("Models/Decode.mesh");
	const auto mesh = MakeMesh(4);
	auto bytes = Cook(Assets::SassetAssetType::Mesh, id, "Models/Decode.mesh", mesh);

	auto decoded = Assets::DecodeSasset(bytes);
	SWIM_REQUIRE_MESSAGE(static_cast<bool>(decoded), decoded.Error.Message);
	SWIM_CHECK(!decoded.Decoded.RequiresOwnerThreadDecode());
	SWIM_REQUIRE(std::holds_alternative<Assets::MeshAsset>(decoded.Decoded.Asset));
	SWIM_CHECK(std::get<Assets::MeshAsset>(decoded.Decoded.Asset).VertexBytes == mesh.VertexBytes);

	Assets::AssetSystem assets;
	SWIM_REQUIRE(assets.Initialize());
	const auto published = Assets::PublishSasset(assets, std::move(decoded.Decoded));
	SWIM_REQUIRE_MESSAGE(static_cast<bool>(published), published.Error.Message);
	const auto handle = assets.Find<Assets::MeshAsset>("Models/Decode.mesh");
	SWIM_REQUIRE(assets.Resolve(handle) != nullptr);
	SWIM_CHECK(assets.Resolve(handle)->Primitives[0].MaterialSlot == 1u);
	SWIM_CHECK(assets.GetStatus(handle).Hash == Assets::ComputeContentHash(AssetCompiler::SerializeAssetPayload(mesh)));

	bytes[bytes.size() - 1] ^= std::byte{ 0xFF };
	SWIM_CHECK(!Assets::DecodeSasset(bytes));

	// Handle-resolving types stay on the owner-thread LoadSasset path.
	Assets::ModelAsset model;
	const auto modelBytes = Cook(Assets::SassetAssetType::Model, ids.GetOrCreate("Models/Decode.model"), "Models/Decode.model", model);
	auto modelDecode = Assets::DecodeSasset(modelBytes);
	SWIM_REQUIRE(static_cast<bool>(modelDecode));
	SWIM_CHECK(modelDecode.Decoded.RequiresOwnerThreadDecode());
	SWIM_CHECK(!Assets::PublishSasset(assets, std::move(modelDecode.Decoded)));
	assets.Shutdown();
}

SWIM_TEST("AssetCompiler.AssetResidency", "StreamsCookedMeshesAndTexturesThroughJobsIntoGpuResidency")
{
	Testing::ResidencyServiceFixture fixture("Streaming");
	AssetResidencyDesc desc;
	desc.MaxConcurrentReads = 2;
	desc.MaxConcurrentDecodes = 1;
	auto& service = fixture.Service(std::move(desc));

	std::vector<Assets::AssetHandle<Assets::MeshAsset>> meshes;
	std::vector<Assets::MeshAsset> sources;
	for (std::uint8_t i = 0; i < 4; ++i)
	{
		const std::string path = "Models/Stream" + std::to_string(i) + ".mesh";
		auto handle = fixture.assets.Declare<Assets::MeshAsset>(path);
		sources.push_back(MakeMesh(i));
		fixture.WriteObject(handle.GetId(), "mesh" + std::to_string(i) + ".sasset",
			Cook(Assets::SassetAssetType::Mesh, handle.GetId(), path, sources.back()));
		meshes.push_back(handle);
		SWIM_CHECK(service.RequestMesh(handle));
	}
	const auto texture = fixture.assets.Declare<Assets::TextureAsset>("Textures/Stream.texture");
	const auto textureAsset = MakeTexture();
	fixture.WriteObject(texture.GetId(), "texture.sasset",
		Cook(Assets::SassetAssetType::Texture, texture.GetId(), "Textures/Stream.texture", textureAsset));
	service.RequestTexture(texture);

	service.Update();
	SWIM_CHECK(service.GetStats().Reading <= 2u); // Bounded concurrent reads.
	SWIM_REQUIRE(fixture.UpdateUntil(
		[&]
		{
			const auto stats = service.GetStats();
			return stats.Uploading == 5u;
		}));
	SWIM_CHECK(service.GetStats().BytesRead > 0u);
	// Decoded CPU assets were published, then released once staged on the GPU side.
	SWIM_CHECK(fixture.assets.IsCurrent(meshes[0]));
	SWIM_CHECK(fixture.assets.Resolve(meshes[0]) == nullptr);

	fixture.UploadFrame();
	SWIM_CHECK_EQUAL(service.GetStats().Resident, 5u);
	for (std::size_t i = 0; i < meshes.size(); ++i)
	{
		const auto* row = fixture.geometry->GetMetadata(service.GetGpuMesh(meshes[i]));
		SWIM_REQUIRE(row != nullptr);
		SWIM_CHECK_EQUAL(row->VertexStride, 20u);
		SWIM_CHECK_EQUAL(row->IndexBytes, 2u);
		SWIM_CHECK_EQUAL(fixture.geometry->GetSubmeshes(service.GetGpuMesh(meshes[i]))[0].MaterialSlot, 1u);
		// The interleaved first vertex: position then uv.
		const auto& page = static_cast<Testing::MockMappedBuffer*>(fixture.geometry->GetPage(row->VertexPage))->Bytes;
		const auto base = std::size_t(row->VertexOffset) * 20;
		SWIM_CHECK(std::memcmp(page.data() + base, sources[i].VertexBytes.data(), 12) == 0);
		SWIM_CHECK(std::memcmp(page.data() + base + 12, sources[i].VertexBytes.data() + 36, 8) == 0);
	}
	auto* gpuTexture = static_cast<Testing::MockTexture*>(fixture.textures->GetTexture(service.GetGpuTexture(texture)));
	SWIM_REQUIRE(gpuTexture != nullptr);
	SWIM_CHECK(std::memcmp(gpuTexture->Bytes({ 1, 0 }).data(), textureAsset.Payloads[0].Bytes.data() + 16, 4) == 0);

	// Re-requesting after release reads the object again (the CPU copy was dropped).
	SWIM_CHECK(service.ReleaseMesh(meshes[0]));
	SWIM_CHECK(service.RequestMesh(meshes[0]));
	SWIM_CHECK(service.GetState(meshes[0]) == State::Queued);
	SWIM_REQUIRE(fixture.UpdateUntil(
		[&]
		{
			return service.GetState(meshes[0]) == State::Uploading;
		}));
}

SWIM_TEST("AssetCompiler.AssetResidency", "RejectsObjectsWithTheWrongIdentityOrTypeAndCancelsReleasedWork")
{
	Testing::ResidencyServiceFixture fixture("Mismatch");
	auto& service = fixture.Service();
	const auto mesh = fixture.assets.Declare<Assets::MeshAsset>("Models/Wrong.mesh");
	const auto other = fixture.assets.GetDatabase().GetOrCreate("Textures/Other.texture");
	// The mesh's path resolves to a texture object with a different id.
	fixture.WriteObject(
		mesh.GetId(), "wrong.sasset", Cook(Assets::SassetAssetType::Texture, other, "Textures/Other.texture", MakeTexture()));
	service.RequestMesh(mesh);
	SWIM_REQUIRE(fixture.UpdateUntil(
		[&]
		{
			return service.GetState(mesh) == State::Failed;
		}));
	SWIM_CHECK(service.GetError(mesh.GetId()).Code == Assets::AssetErrorCode::InvalidData);

	// Released while reading/decoding: nothing is published or staged afterwards.
	const auto late = fixture.assets.Declare<Assets::MeshAsset>("Models/Late.mesh");
	fixture.WriteObject(late.GetId(), "late.sasset", Cook(Assets::SassetAssetType::Mesh, late.GetId(), "Models/Late.mesh", MakeMesh(1)));
	service.RequestMesh(late);
	service.Update();
	SWIM_CHECK(service.GetState(late) == State::Reading);
	SWIM_CHECK(service.ReleaseMesh(late));
	for (int i = 0; i < 20; ++i)
	{
		fixture.io.PumpCompletions();
		service.Update();
	}
	SWIM_CHECK(service.GetState(late) == State::Unloaded);
	SWIM_CHECK(fixture.assets.Resolve(late) == nullptr);
	SWIM_CHECK_EQUAL(fixture.geometry->GetStats().PendingMeshes, 0u);
}
