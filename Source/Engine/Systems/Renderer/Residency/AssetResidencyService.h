#pragma once
#include "Engine/Assets/AssetSystem.h"
#include "Engine/Assets/MeshAsset.h"
#include "Engine/Assets/TextureAsset.h"
#include "Engine/Systems/Renderer/Residency/AssetResidencyDesc.h"
#include "Engine/Systems/Renderer/Residency/AssetResidencyStats.h"
#include "Engine/Systems/Renderer/Residency/GpuResidencyGraphResources.h"
#include "Engine/Systems/Renderer/Residency/Internal/AssetResidencyRequest.h"

#include <unordered_map>

namespace Swim::Render
{
	class GeometryHeap;
	class RenderGraph;
	class TextureResidency;

	// Connects compiled MeshAsset/TextureAsset identities to GPU residency
	// without hidden uploads or blocking IO (critical-path item 44):
	//
	//   Request*   Queued (or straight to WaitingForGpuUpload when the CPU asset
	//              is already resident in the AssetSystem)
	//   Update     starts async .sasset reads (bounded), hands completed reads to
	//              decode jobs (hash validation + payload decode off the owner
	//              thread), publishes decoded assets on the owner thread, stages
	//              waiting assets into GeometryHeap/TextureResidency within the
	//              per-update byte budget and observes upload completion
	//   Import     records the heap/texture uploads into a graph; CommitUploads
	//              after the graph's successful Execute, AbortUploads otherwise
	//   Release*   cancels outstanding work or retires GPU data after lastUse
	//
	// Owner thread only (the AssetSystem owner). IO completion is polled, so no
	// callback can outlive this object. The AssetSystem, IO service, optional job
	// system, GeometryHeap and TextureResidency must outlive the service. GPU
	// residency created here is released only through Release*/ReleaseAll; the
	// destructor cancels CPU work but leaves GPU data to its owners' Drain.
	class AssetResidencyService
	{
	  public:
		// Without a job system, decoding runs inline during Update.
		AssetResidencyService(Assets::AssetSystem& assets, IO::AsyncIoService& io, Jobs::JobSystem* jobs, GeometryHeap& geometry,
			TextureResidency& textures, AssetResidencyDesc desc = {});
		~AssetResidencyService();
		AssetResidencyService(const AssetResidencyService&) = delete;
		AssetResidencyService& operator=(const AssetResidencyService&) = delete;

		// Idempotent while a request exists; a Failed request is retried. Returns
		// false for handles that are not current in the AssetSystem.
		bool RequestMesh(Assets::AssetHandle<Assets::MeshAsset> mesh);
		bool RequestTexture(Assets::AssetHandle<Assets::TextureAsset> texture);

		// lastUse must cover every submission that may read the GPU data. Uploads
		// recorded by a pending Import must be committed or aborted first.
		bool ReleaseMesh(Assets::AssetHandle<Assets::MeshAsset> mesh, Rhi::TimelinePoint lastUse = {});
		bool ReleaseTexture(Assets::AssetHandle<Assets::TextureAsset> texture, Rhi::TimelinePoint lastUse = {});
		void ReleaseAll(Rhi::TimelinePoint lastUse);

		void Update();

		GpuResidencyGraphResources Import(RenderGraph& graph);
		void CommitUploads(Rhi::TimelinePoint completion);
		void AbortUploads();

		AssetResidencyState GetState(Assets::AssetHandle<Assets::MeshAsset> mesh) const;
		AssetResidencyState GetState(Assets::AssetHandle<Assets::TextureAsset> texture) const;
		// Valid from Uploading onward.
		GpuMeshHandle GetGpuMesh(Assets::AssetHandle<Assets::MeshAsset> mesh) const;
		GpuTextureHandle GetGpuTexture(Assets::AssetHandle<Assets::TextureAsset> texture) const;
		// The failure recorded for a Failed request (Code None otherwise).
		Assets::AssetError GetError(Assets::AssetId id) const;
		AssetResidencyStats GetStats() const;

	  private:
		using Request = Internal::AssetResidencyRequest;
		using Kind = Internal::ResidencyAssetKind;

		template <typename T> bool RequestAsset(Kind kind, Assets::AssetHandle<T> handle);
		template <typename T> bool ReleaseAsset(Assets::AssetHandle<T> handle, Rhi::TimelinePoint lastUse);
		template <typename T> const Request* FindRequest(Assets::AssetHandle<T> handle) const;
		void ReleaseRequest(Request& request, Rhi::TimelinePoint lastUse);
		void StartReads();
		void PollReads();
		void PollDecodes();
		void StageUploads();
		void ObserveUploads();
		void Publish(Request& request, Assets::SassetDecodeResult result);
		void Fail(Request& request, Assets::AssetErrorCode code, std::string message, bool failCpuAsset = true);
		bool StageOne(Request& request, std::uint64_t& bytes);
		void ResetCpuState(const Request& request);

		Assets::AssetSystem& assets;
		IO::AsyncIoService& io;
		Jobs::JobSystem* jobs;
		GeometryHeap& geometry;
		TextureResidency& textures;
		AssetResidencyDesc desc;
		std::unordered_map<Assets::AssetId, Request> requests;
		std::vector<Jobs::JobHandle> abandoned; // Decode jobs of released requests.
		std::uint64_t nextSequence = 0;
		AssetResidencyStats totals;
	};
} // namespace Swim::Render
