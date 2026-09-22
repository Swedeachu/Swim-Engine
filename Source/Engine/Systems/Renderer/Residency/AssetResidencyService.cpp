#include "Engine/Systems/Renderer/Residency/AssetResidencyService.h"
#include "Engine/Assets/SassetFormat.h"
#include "Engine/Systems/Renderer/Geometry/GeometryHeap.h"
#include "Engine/Systems/Renderer/RHI/RhiFormatInfo.h"
#include "Engine/Systems/Renderer/Residency/MeshGeometryPayload.h"
#include "Engine/Systems/Renderer/Residency/TextureResidency.h"
#include "Engine/Systems/Renderer/Resources/BindlessResourceTable.h"

#include <algorithm>

namespace Swim::Render
{
	namespace
	{
		using Assets::AssetErrorCode;
		using State = AssetResidencyState;

		// Dispatches to the request's typed AssetHandle.
		template <typename Fn> decltype(auto) WithHandle(const Internal::AssetResidencyRequest& request, Fn&& fn)
		{
			return request.Kind == Internal::ResidencyAssetKind::Mesh ? fn(request.MeshAsset) : fn(request.TextureAsset);
		}

		Assets::AssetId IdOf(const Internal::AssetResidencyRequest& request)
		{
			return WithHandle(request,
				[](auto handle)
				{
					return handle.GetId();
				});
		}

		std::uint64_t TextureUploadBytes(const Assets::TextureAsset& texture)
		{
			const auto selection = TextureResidency::SelectPayload(texture);
			if (!selection)
			{
				return 0;
			}
			std::uint64_t bytes = 0;
			for (const auto& mip : texture.Payloads[selection->Payload].Mips)
			{
				bytes += mip.SizeBytes;
			}
			return bytes;
		}
	} // namespace

	AssetResidencyService::AssetResidencyService(Assets::AssetSystem& assets, IO::AsyncIoService& io, Jobs::JobSystem* jobs,
		GeometryHeap& geometry, TextureResidency& textures, AssetResidencyDesc desc)
		: assets(assets), io(io), jobs(jobs), geometry(geometry), textures(textures), desc(std::move(desc))
	{
		if (!this->desc.MaxConcurrentReads || !this->desc.MaxConcurrentDecodes)
		{
			throw std::invalid_argument("AssetResidencyService needs at least one read and one decode slot");
		}
	}

	AssetResidencyService::~AssetResidencyService()
	{
		for (auto& [id, request] : requests)
		{
			if (request.State == State::Reading)
			{
				request.Read.RequestCancel();
			}
			if (request.State == State::Decoding && request.Decode && jobs)
			{
				abandoned.push_back(request.Decode);
			}
		}
		for (const auto& job : abandoned)
		{
			try
			{
				jobs->Wait(job);
			}
			catch (...)
			{
			}
		}
	}

	template <typename T> bool AssetResidencyService::RequestAsset(Kind kind, Assets::AssetHandle<T> handle)
	{
		if (!assets.IsCurrent(handle))
		{
			return false;
		}
		const auto id = handle.GetId();
		if (auto existing = requests.find(id); existing != requests.end())
		{
			if (existing->second.State != State::Failed)
			{
				return true;
			}
			requests.erase(existing); // Retry a failed request from the start.
		}

		Request request;
		request.Kind = kind;
		if constexpr (std::is_same_v<T, Assets::MeshAsset>)
		{
			request.MeshAsset = handle;
		}
		else
		{
			request.TextureAsset = handle;
		}
		request.Sequence = nextSequence++;
		if (assets.Resolve(handle))
		{
			request.State = State::WaitingForGpuUpload; // The CPU asset is already published.
		}
		else
		{
			request.State = State::Queued;
			assets.Queue(handle);
		}
		requests.emplace(id, std::move(request));
		return true;
	}

	bool AssetResidencyService::RequestMesh(Assets::AssetHandle<Assets::MeshAsset> mesh)
	{
		return RequestAsset(Kind::Mesh, mesh);
	}

	bool AssetResidencyService::RequestTexture(Assets::AssetHandle<Assets::TextureAsset> texture)
	{
		return RequestAsset(Kind::Texture, texture);
	}

	template <typename T> const AssetResidencyService::Request* AssetResidencyService::FindRequest(Assets::AssetHandle<T> handle) const
	{
		const auto found = requests.find(handle.GetId());
		if (found == requests.end())
		{
			return nullptr;
		}
		const bool matches = WithHandle(found->second,
			[&](auto stored)
			{
				if constexpr (std::is_same_v<decltype(stored), Assets::AssetHandle<T>>)
				{
					return stored == handle;
				}
				else
				{
					return false;
				}
			});
		return matches ? &found->second : nullptr;
	}

	void AssetResidencyService::ResetCpuState(const Request& request)
	{
		// Undo Queue/BeginLoading bookkeeping this service performed; a published
		// or failed CPU asset is left as it is.
		WithHandle(request,
			[&](auto handle)
			{
				const auto status = assets.GetStatus(handle);
				if (status.State == Assets::AssetLoadState::Queued || status.State == Assets::AssetLoadState::Loading)
				{
					assets.Unload(handle);
				}
			});
	}

	void AssetResidencyService::ReleaseRequest(Request& request, Rhi::TimelinePoint lastUse)
	{
		switch (request.State)
		{
		case State::Reading:
			request.Read.RequestCancel();
			ResetCpuState(request);
			break;
		case State::Decoding:
			if (request.Decode)
			{
				abandoned.push_back(request.Decode); // The slot keeps its bytes alive.
			}
			ResetCpuState(request);
			break;
		case State::Queued:
			ResetCpuState(request);
			break;
		case State::Uploading:
		case State::Resident:
			if (request.Kind == Kind::Mesh)
			{
				geometry.DestroyMesh(request.Mesh, lastUse);
			}
			else
			{
				// The element retires with the texture, so no submission indexes a destroyed view.
				if (desc.Bindless && request.Bindless)
				{
					desc.Bindless->Release(request.Bindless, lastUse);
					request.Bindless = {};
				}
				textures.DestroyTexture(request.Texture, lastUse);
			}
			break;
		default:
			break;
		}
	}

	template <typename T> bool AssetResidencyService::ReleaseAsset(Assets::AssetHandle<T> handle, Rhi::TimelinePoint lastUse)
	{
		const auto* found = FindRequest(handle);
		if (!found)
		{
			return false;
		}
		auto& request = requests.at(handle.GetId());
		ReleaseRequest(request, lastUse); // May throw for recorded uploads; the request stays.
		requests.erase(handle.GetId());
		return true;
	}

	bool AssetResidencyService::ReleaseMesh(Assets::AssetHandle<Assets::MeshAsset> mesh, Rhi::TimelinePoint lastUse)
	{
		return ReleaseAsset(mesh, lastUse);
	}

	bool AssetResidencyService::ReleaseTexture(Assets::AssetHandle<Assets::TextureAsset> texture, Rhi::TimelinePoint lastUse)
	{
		return ReleaseAsset(texture, lastUse);
	}

	void AssetResidencyService::ReleaseAll(Rhi::TimelinePoint lastUse)
	{
		for (auto it = requests.begin(); it != requests.end();)
		{
			ReleaseRequest(it->second, lastUse);
			it = requests.erase(it);
		}
	}

	void AssetResidencyService::Fail(Request& request, AssetErrorCode code, std::string message, bool failCpuAsset)
	{
		request.State = State::Failed;
		request.Error = { code, std::move(message) };
		request.Read = {};
		request.Decode = {};
		request.Slot.reset();
		if (failCpuAsset)
		{
			WithHandle(request,
				[&](auto handle)
				{
					assets.Fail(handle, request.Error);
				});
		}
	}

	void AssetResidencyService::Update()
	{
		totals.BytesStagedLastUpdate = 0;
		std::erase_if(abandoned,
			[](const Jobs::JobHandle& job)
			{
				return !job || job.IsComplete();
			});
		PollReads();
		PollDecodes();
		StartReads();
		StageUploads();
		ObserveUploads();
	}

	void AssetResidencyService::StartReads()
	{
		std::uint32_t reading = 0;
		std::vector<Request*> queued;
		for (auto& [id, request] : requests)
		{
			reading += request.State == State::Reading;
			if (request.State == State::Queued)
			{
				queued.push_back(&request);
			}
		}
		std::sort(queued.begin(), queued.end(),
			[](const Request* a, const Request* b)
			{
				return a->Sequence < b->Sequence;
			});
		for (auto* request : queued)
		{
			if (reading >= desc.MaxConcurrentReads)
			{
				break;
			}
			const auto path = desc.ResolveCookedPath ? desc.ResolveCookedPath(IdOf(*request)) : std::filesystem::path{};
			if (path.empty())
			{
				Fail(*request, AssetErrorCode::NotFound, "no cooked .sasset path is known for this asset");
				continue;
			}
			try
			{
				request->Read = io.ReadFileAsync(path);
			}
			catch (const std::exception& error)
			{
				Fail(*request, AssetErrorCode::Io, error.what());
				continue;
			}
			request->State = State::Reading;
			WithHandle(*request,
				[&](auto handle)
				{
					assets.BeginLoading(handle);
				});
			++reading;
		}
	}

	void AssetResidencyService::PollReads()
	{
		std::uint32_t decoding = 0;
		for (auto& [id, request] : requests)
		{
			decoding += request.State == State::Decoding;
		}
		std::vector<Request*> completed;
		for (auto& [id, request] : requests)
		{
			if (request.State == State::Reading && request.Read.IsComplete())
			{
				completed.push_back(&request);
			}
		}
		std::sort(completed.begin(), completed.end(),
			[](const Request* a, const Request* b)
			{
				return a->Sequence < b->Sequence;
			});
		for (auto* request : completed)
		{
			const auto status = request->Read.GetStatus();
			if (status != IO::IoStatus::Succeeded)
			{
				const auto code = status == IO::IoStatus::Cancelled ? AssetErrorCode::Cancelled : AssetErrorCode::Io;
				Fail(*request, code, request->Read.GetErrorMessage());
				continue;
			}
			if (decoding >= desc.MaxConcurrentDecodes)
			{
				continue; // Stays Reading (complete) until a decode slot frees up.
			}
			totals.BytesRead += request->Read.GetResult().GetSingleBuffer().size();
			request->Slot = std::make_shared<Internal::AssetDecodeSlot>();
			request->State = State::Decoding;
			++decoding;
			if (jobs)
			{
				// The job owns shared references to the read bytes and the result slot,
				// so an abandoned decode never touches this service.
				request->Decode = jobs->Schedule(
					[read = request->Read, slot = request->Slot](std::uint32_t)
					{
						slot->Result = Assets::DecodeSasset(read.GetResult().GetSingleBuffer());
					});
			}
			else
			{
				request->Slot->Result = Assets::DecodeSasset(request->Read.GetResult().GetSingleBuffer());
			}
		}
	}

	void AssetResidencyService::PollDecodes()
	{
		for (auto& [id, request] : requests)
		{
			if (request.State != State::Decoding || (request.Decode && !request.Decode.IsComplete()))
			{
				continue;
			}
			auto result = std::move(request.Slot->Result);
			request.Slot.reset();
			request.Decode = {};
			request.Read = {}; // Release the file bytes.
			Publish(request, std::move(result));
		}
	}

	void AssetResidencyService::Publish(Request& request, Assets::SassetDecodeResult result)
	{
		if (!result)
		{
			const auto code = result.Error.Code == Assets::SassetErrorCode::UnsupportedVersion ? AssetErrorCode::UnsupportedVersion
																							   : AssetErrorCode::InvalidData;
			Fail(request, code, result.Error.Message);
			return;
		}
		const auto& metadata = result.Decoded.Metadata;
		const auto expected = request.Kind == Kind::Mesh ? Assets::SassetAssetType::Mesh : Assets::SassetAssetType::Texture;
		if (metadata.Id != IdOf(request) || metadata.Type != expected)
		{
			Fail(request, AssetErrorCode::InvalidData, "cooked object does not contain the requested asset id and type");
			return;
		}
		const auto published = Assets::PublishSasset(assets, std::move(result.Decoded));
		if (!published)
		{
			Fail(request, AssetErrorCode::InvalidData, published.Error.Message);
			return;
		}
		request.State = State::WaitingForGpuUpload;
	}

	bool AssetResidencyService::StageOne(Request& request, std::uint64_t& bytes)
	{
		const auto label = assets.GetDatabase().FindPath(IdOf(request)).value_or(std::string{});
		try
		{
			if (request.Kind == Kind::Mesh)
			{
				const auto* mesh = assets.Resolve(request.MeshAsset);
				if (!mesh)
				{
					Fail(request, AssetErrorCode::Internal, "mesh asset is no longer resident before GPU staging", false);
					return false;
				}
				const auto payload = BuildMeshGeometryPayload(*mesh);
				request.Mesh = geometry.CreateMesh(payload.Describe(label));
				bytes = payload.GetUploadBytes();
			}
			else
			{
				const auto* texture = assets.Resolve(request.TextureAsset);
				if (!texture)
				{
					Fail(request, AssetErrorCode::Internal, "texture asset is no longer resident before GPU staging", false);
					return false;
				}
				bytes = TextureUploadBytes(*texture);
				request.Texture = textures.CreateTexture(*texture, label);
			}
		}
		catch (const std::length_error&)
		{
			return false; // Capacity backpressure: retry on a later Update.
		}
		catch (const std::invalid_argument& error)
		{
			Fail(request, AssetErrorCode::InvalidData, error.what(), false); // The CPU asset stays valid.
			return false;
		}
		catch (const std::exception& error)
		{
			Fail(request, AssetErrorCode::Internal, error.what(), false);
			return false;
		}

		request.State = State::Uploading;
		if (!desc.RetainCpuAssets)
		{
			// GPU residency owns a copy now; CPU validity is independent of it.
			WithHandle(request,
				[&](auto handle)
				{
					assets.Unload(handle);
				});
		}
		return true;
	}

	void AssetResidencyService::StageUploads()
	{
		std::vector<Request*> waiting;
		for (auto& [id, request] : requests)
		{
			if (request.State == State::WaitingForGpuUpload)
			{
				waiting.push_back(&request);
			}
		}
		std::sort(waiting.begin(), waiting.end(),
			[](const Request* a, const Request* b)
			{
				return a->Sequence < b->Sequence;
			});
		std::uint64_t staged = 0;
		for (auto* request : waiting)
		{
			if (staged && staged >= desc.UploadBudgetBytes)
			{
				break;
			}
			std::uint64_t bytes = 0;
			if (StageOne(*request, bytes))
			{
				staged += std::max<std::uint64_t>(bytes, 1);
			}
			else if (request->State == State::WaitingForGpuUpload)
			{
				break; // Capacity backpressure keeps FIFO order.
			}
		}
		totals.BytesStagedLastUpdate = staged;
		totals.BytesStaged += staged;
	}

	void AssetResidencyService::ObserveUploads()
	{
		geometry.Collect();
		textures.Collect();
		for (auto& [id, request] : requests)
		{
			if (request.State == State::Resident && request.Kind == Kind::Texture && !request.Bindless && !request.BindlessRejected)
			{
				RegisterBindless(request); // Retry after the table was full.
				continue;
			}
			if (request.State != State::Uploading)
			{
				continue;
			}
			const auto state = request.Kind == Kind::Mesh ? geometry.GetResidency(request.Mesh) : textures.GetState(request.Texture);
			if (state == GpuUploadState::Resident)
			{
				request.State = State::Resident;
				if (request.Kind == Kind::Texture)
				{
					RegisterBindless(request);
				}
			}
			else if (state == GpuUploadState::Invalid)
			{
				Fail(request, AssetErrorCode::Internal, "GPU residency was destroyed outside AssetResidencyService", false);
			}
		}
	}

	void AssetResidencyService::RegisterBindless(Request& request)
	{
		if (!desc.Bindless)
		{
			return;
		}
		// A rejected view leaves the texture Resident (still usable through
		// GetGpuTexture) on the fallback element, with the reason in GetError.
		auto* view = textures.GetView(request.Texture);
		try
		{
			if (!view)
			{
				throw std::logic_error("resident texture has no view");
			}
			if (auto handle = desc.Bindless->TryRegisterTexture(*view))
			{
				request.Bindless = *handle;
			}
		}
		catch (const std::exception& error)
		{
			request.BindlessRejected = true;
			request.Error = { AssetErrorCode::Internal, std::string("texture could not become bindless: ") + error.what() };
		}
	}

	GpuResidencyGraphResources AssetResidencyService::Import(RenderGraph& graph)
	{
		GpuResidencyGraphResources resources;
		resources.Geometry = geometry.Import(graph);
		try
		{
			resources.Textures = textures.Import(graph);
		}
		catch (...)
		{
			geometry.AbortUploads();
			throw;
		}
		return resources;
	}

	void AssetResidencyService::CommitUploads(Rhi::TimelinePoint completion)
	{
		geometry.CommitUploads(completion);
		textures.CommitUploads(completion);
	}

	void AssetResidencyService::AbortUploads()
	{
		geometry.AbortUploads();
		textures.AbortUploads();
	}

	AssetResidencyState AssetResidencyService::GetState(Assets::AssetHandle<Assets::MeshAsset> mesh) const
	{
		const auto* request = FindRequest(mesh);
		return request ? request->State : State::Unloaded;
	}

	AssetResidencyState AssetResidencyService::GetState(Assets::AssetHandle<Assets::TextureAsset> texture) const
	{
		const auto* request = FindRequest(texture);
		return request ? request->State : State::Unloaded;
	}

	GpuMeshHandle AssetResidencyService::GetGpuMesh(Assets::AssetHandle<Assets::MeshAsset> mesh) const
	{
		const auto* request = FindRequest(mesh);
		return request ? request->Mesh : GpuMeshHandle{};
	}

	GpuTextureHandle AssetResidencyService::GetGpuTexture(Assets::AssetHandle<Assets::TextureAsset> texture) const
	{
		const auto* request = FindRequest(texture);
		return request ? request->Texture : GpuTextureHandle{};
	}

	std::uint32_t AssetResidencyService::GetBindlessIndex(Assets::AssetHandle<Assets::TextureAsset> texture) const
	{
		const auto* request = FindRequest(texture);
		return request && desc.Bindless ? desc.Bindless->GetIndex(request->Bindless) : BindlessResourceTable::FallbackIndex;
	}

	Assets::AssetError AssetResidencyService::GetError(Assets::AssetId id) const
	{
		const auto found = requests.find(id);
		return found == requests.end() ? Assets::AssetError{} : found->second.Error;
	}

	AssetResidencyStats AssetResidencyService::GetStats() const
	{
		auto stats = totals;
		stats.AbandonedDecodes = static_cast<std::uint32_t>(abandoned.size());
		for (const auto& [id, request] : requests)
		{
			switch (request.State)
			{
			case State::Queued:
				++stats.Queued;
				break;
			case State::Reading:
				++stats.Reading;
				break;
			case State::Decoding:
				++stats.Decoding;
				break;
			case State::WaitingForGpuUpload:
				++stats.WaitingForGpuUpload;
				break;
			case State::Uploading:
				++stats.Uploading;
				break;
			case State::Resident:
				++stats.Resident;
				if (request.Kind == Kind::Texture && desc.Bindless)
				{
					if (request.Bindless)
					{
						++stats.BindlessTextures;
					}
					else if (!request.BindlessRejected)
					{
						++stats.BindlessPending;
					}
				}
				break;
			case State::Failed:
				++stats.Failed;
				break;
			default:
				break;
			}
		}
		return stats;
	}
} // namespace Swim::Render
