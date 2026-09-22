#include "Tests/Fixtures/ResidencyServiceFixture.h"

#include <cstring>

using namespace Swim;
using namespace Swim::Render;
using State = AssetResidencyState;

namespace
{
	Assets::MeshAsset MakeMesh(std::uint8_t seed, std::size_t vertices = 3)
	{
		Assets::MeshAsset mesh;
		mesh.VertexStreams = { { 12, 0, vertices * 12 } };
		mesh.VertexAttributes = { { Assets::VertexSemantic::Position, Assets::VertexElementFormat::Float32x3, 0, 0 } };
		mesh.VertexBytes.resize(vertices * 12);
		for (std::size_t i = 0; i < mesh.VertexBytes.size(); ++i)
		{
			mesh.VertexBytes[i] = static_cast<std::byte>(seed + i);
		}
		mesh.IndexFormat = Assets::IndexElementFormat::UInt32;
		mesh.IndexBytes.resize(12);
		mesh.Primitives = { { 0, 3, 0, 0, {} } };
		return mesh;
	}

	Assets::TextureAsset MakeTexture(Assets::TexturePayloadFormat format = Assets::TexturePayloadFormat::RGBA8UNorm)
	{
		Assets::TextureAsset texture;
		texture.Width = 2;
		texture.Height = 2;
		Assets::TexturePayloadVariant payload;
		payload.Format = format;
		payload.Mips.push_back({ 2, 2, 1, 0, 16, 16 });
		payload.Bytes.resize(16, std::byte{ 0x5A });
		texture.Payloads.push_back(std::move(payload));
		return texture;
	}

	template <typename T> Assets::AssetHandle<T> PublishCpu(Assets::AssetSystem& assets, const char* path, T asset)
	{
		auto handle = assets.Declare<T>(path);
		assets.BeginLoading(handle);
		SWIM_REQUIRE(assets.Publish(handle, std::move(asset)));
		return handle;
	}
} // namespace

SWIM_TEST("Render.AssetResidency", "ResidentCpuAssetsStageUploadAndBecomeGpuResident")
{
	Testing::ResidencyServiceFixture fixture("Resident");
	auto& service = fixture.Service();
	const auto meshAsset = MakeMesh(10);
	const auto mesh = PublishCpu(fixture.assets, "Models/Tri.mesh", meshAsset);
	const auto texture = PublishCpu(fixture.assets, "Textures/Albedo.texture", MakeTexture());

	SWIM_CHECK(service.RequestMesh(mesh));
	SWIM_CHECK(service.RequestMesh(mesh)); // Idempotent.
	SWIM_CHECK(service.RequestTexture(texture));
	SWIM_CHECK(service.GetState(mesh) == State::WaitingForGpuUpload);

	service.Update();
	SWIM_CHECK(service.GetState(mesh) == State::Uploading);
	SWIM_CHECK(service.GetState(texture) == State::Uploading);
	SWIM_CHECK_EQUAL(service.GetStats().BytesStagedLastUpdate, 36u + 12u + 16u);
	// GPU residency owns copies now; CPU validity is released by default.
	SWIM_CHECK(fixture.assets.Resolve(mesh) == nullptr);
	SWIM_CHECK(fixture.assets.IsCurrent(mesh));

	const auto resources = fixture.UploadFrame();
	SWIM_CHECK_EQUAL(resources.Geometry.RecordedMeshes, 1u);
	SWIM_CHECK_EQUAL(resources.Textures.Uploads.size(), 1u);
	SWIM_CHECK(service.GetState(mesh) == State::Resident);
	SWIM_CHECK(service.GetState(texture) == State::Resident);

	const auto gpuMesh = service.GetGpuMesh(mesh);
	const auto* row = fixture.geometry->GetMetadata(gpuMesh);
	SWIM_REQUIRE(row != nullptr);
	SWIM_CHECK_EQUAL(row->VertexCount, 3u);
	const auto& page = static_cast<Testing::MockMappedBuffer*>(fixture.geometry->GetPage(row->VertexPage))->Bytes;
	SWIM_CHECK(std::memcmp(page.data() + row->VertexOffset * 12, meshAsset.VertexBytes.data(), 36) == 0);
	SWIM_CHECK(fixture.textures->GetState(service.GetGpuTexture(texture)) == GpuUploadState::Resident);
	SWIM_CHECK_EQUAL(service.GetStats().Resident, 2u);

	// Releasing retires the GPU data after the supplied last use.
	Testing::MockTimeline frame;
	SWIM_CHECK(service.ReleaseMesh(mesh, { &frame, 2 }));
	SWIM_CHECK(service.GetState(mesh) == State::Unloaded);
	SWIM_CHECK(!fixture.geometry->IsValid(gpuMesh));
	SWIM_CHECK_EQUAL(fixture.geometry->GetStats().RetiringMeshes, 1u);
	frame.Complete(2);
	fixture.geometry->Collect();
	SWIM_CHECK_EQUAL(fixture.geometry->GetStats().RetiringMeshes, 0u);
	SWIM_CHECK(!service.ReleaseMesh(mesh));
	service.ReleaseAll({});
	SWIM_CHECK_EQUAL(fixture.textures->GetStats().RetiringTextures, 1u);
}

SWIM_TEST("Render.AssetResidency", "UploadBudgetAdmitsWaitingAssetsInRequestOrder")
{
	Testing::ResidencyServiceFixture fixture("Budget");
	AssetResidencyDesc desc;
	desc.UploadBudgetBytes = 100; // Each mesh stages 48 + 12 = 60 bytes.
	desc.RetainCpuAssets = true;
	auto& service = fixture.Service(std::move(desc));
	std::vector<Assets::AssetHandle<Assets::MeshAsset>> meshes;
	for (int i = 0; i < 4; ++i)
	{
		const std::string path = "Models/Budget" + std::to_string(i) + ".mesh";
		meshes.push_back(PublishCpu(fixture.assets, path.c_str(), MakeMesh(std::uint8_t(i), 4)));
		service.RequestMesh(meshes.back());
	}
	service.Update();
	SWIM_CHECK(service.GetState(meshes[0]) == State::Uploading);
	SWIM_CHECK(service.GetState(meshes[1]) == State::Uploading); // 60 < 100 admits a second.
	SWIM_CHECK(service.GetState(meshes[2]) == State::WaitingForGpuUpload);
	SWIM_CHECK_EQUAL(service.GetStats().BytesStagedLastUpdate, 120u);
	SWIM_CHECK(fixture.assets.Resolve(meshes[0]) != nullptr); // Retained on request.
	service.Update();
	SWIM_CHECK(service.GetState(meshes[3]) == State::Uploading);
	SWIM_CHECK_EQUAL(service.GetStats().BytesStaged, 240u);

	// A single asset larger than the budget still advances on its own.
	AssetResidencyDesc tiny;
	tiny.UploadBudgetBytes = 1;
	auto& strict = fixture.Service(std::move(tiny));
	const auto big = PublishCpu(fixture.assets, "Models/Big.mesh", MakeMesh(9, 30));
	strict.RequestMesh(big);
	strict.Update();
	SWIM_CHECK(strict.GetState(big) == State::Uploading);
}

SWIM_TEST("Render.AssetResidency", "ReportsMissingUnreadableAndInvalidObjects")
{
	Testing::ResidencyServiceFixture fixture("Failures", false);
	auto& service = fixture.Service();

	const auto unknown = fixture.assets.Declare<Assets::MeshAsset>("Models/Unknown.mesh");
	SWIM_CHECK(service.RequestMesh(unknown));
	SWIM_CHECK(service.GetState(unknown) == State::Queued);
	SWIM_CHECK(fixture.assets.GetStatus(unknown).State == Assets::AssetLoadState::Queued);
	service.Update();
	SWIM_CHECK(service.GetState(unknown) == State::Failed);
	SWIM_CHECK(service.GetError(unknown.GetId()).Code == Assets::AssetErrorCode::NotFound);
	SWIM_CHECK(fixture.assets.GetStatus(unknown).State == Assets::AssetLoadState::Failed);

	const auto missing = fixture.assets.Declare<Assets::MeshAsset>("Models/Missing.mesh");
	fixture.MapObject(missing.GetId(), fixture.root / "does-not-exist.sasset");
	service.RequestMesh(missing);
	SWIM_REQUIRE(fixture.UpdateUntil(
		[&]
		{
			return service.GetState(missing) == State::Failed;
		}));
	SWIM_CHECK(service.GetError(missing.GetId()).Code == Assets::AssetErrorCode::Io);

	const auto garbage = fixture.assets.Declare<Assets::TextureAsset>("Textures/Garbage.texture");
	fixture.WriteObject(garbage.GetId(), "garbage.sasset", std::vector<std::byte>(256, std::byte{ 0x7F }));
	service.RequestTexture(garbage);
	SWIM_REQUIRE(fixture.UpdateUntil(
		[&]
		{
			return service.GetState(garbage) == State::Failed;
		}));
	SWIM_CHECK(service.GetError(garbage.GetId()).Code == Assets::AssetErrorCode::InvalidData);
	SWIM_CHECK_EQUAL(service.GetStats().BytesRead, 256u);
	SWIM_CHECK_EQUAL(service.GetStats().Failed, 3u);

	// A failed request can be retried once its object exists.
	fixture.MapObject(unknown.GetId(), fixture.root / "still-missing.sasset");
	SWIM_CHECK(service.RequestMesh(unknown));
	SWIM_CHECK(service.GetState(unknown) == State::Queued);
	SWIM_CHECK(service.ReleaseMesh(unknown));
	SWIM_CHECK(fixture.assets.GetStatus(unknown).State == Assets::AssetLoadState::Unloaded);

	// Stale handles are rejected.
	fixture.assets.Forget(unknown);
	SWIM_CHECK(!service.RequestMesh(unknown));
}

SWIM_TEST("Render.AssetResidency", "UnsupportedGpuPayloadsFailWithoutInvalidatingTheCpuAsset")
{
	Testing::ResidencyServiceFixture fixture("Unsupported", false);
	auto& service = fixture.Service();
	const auto compressed = PublishCpu(fixture.assets, "Textures/Bc7.texture", MakeTexture(Assets::TexturePayloadFormat::BC7UNorm));
	service.RequestTexture(compressed);
	service.Update();
	SWIM_CHECK(service.GetState(compressed) == State::Failed);
	SWIM_CHECK(service.GetError(compressed.GetId()).Code == Assets::AssetErrorCode::InvalidData);
	SWIM_CHECK(fixture.assets.Resolve(compressed) != nullptr);

	// Capacity exhaustion is backpressure, not failure.
	auto& limited = fixture.Service();
	std::vector<Assets::AssetHandle<Assets::TextureAsset>> textures;
	for (int i = 0; i < 17; ++i) // The fixture's TextureResidency holds 16.
	{
		const std::string path = "Textures/Many" + std::to_string(i) + ".texture";
		textures.push_back(PublishCpu(fixture.assets, path.c_str(), MakeTexture()));
		limited.RequestTexture(textures.back());
	}
	limited.Update();
	SWIM_CHECK(limited.GetState(textures[15]) == State::Uploading);
	SWIM_CHECK(limited.GetState(textures[16]) == State::WaitingForGpuUpload);
	fixture.UploadFrame();
	SWIM_CHECK(limited.ReleaseTexture(textures[0]));
	fixture.textures->Collect();
	limited.Update();
	SWIM_CHECK(limited.GetState(textures[16]) == State::Uploading);
}

SWIM_TEST("Render.AssetResidency", "RecordedUploadsMustBeCommittedOrAbortedBeforeRelease")
{
	Testing::ResidencyServiceFixture fixture("Recorded", false);
	auto& service = fixture.Service();
	const auto mesh = PublishCpu(fixture.assets, "Models/Recorded.mesh", MakeMesh(3));
	service.RequestMesh(mesh);
	service.Update();
	RenderGraph graph;
	service.Import(graph);
	SWIM_CHECK_THROWS(service.ReleaseMesh(mesh), std::logic_error);
	SWIM_CHECK(service.GetState(mesh) == State::Uploading); // Request survives the failed release.
	service.AbortUploads();
	SWIM_CHECK(fixture.geometry->GetResidency(service.GetGpuMesh(mesh)) == GpuUploadState::PendingUpload);
	fixture.UploadFrame();
	SWIM_CHECK(service.GetState(mesh) == State::Resident);
}
