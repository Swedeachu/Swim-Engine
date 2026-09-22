#include "Engine/Systems/Renderer/RenderGraph/RenderGraphExecutor.h"
#include "Engine/Systems/Renderer/RenderGraph/Internal/GraphExecutionState.h"
#include "Engine/Systems/Renderer/RenderGraph/Internal/GraphValidation.h"
#include <algorithm>
#include <bit>

namespace Swim::Render
{
	namespace
	{
		constexpr auto UploadUsages = Rhi::BufferUsage::TransferSource | Rhi::BufferUsage::Vertex | Rhi::BufferUsage::Index |
			Rhi::BufferUsage::Uniform | Rhi::BufferUsage::Storage | Rhi::BufferUsage::Indirect;
		constexpr std::uint64_t MinimumStagingCapacity = 64 * 1024;

		std::uint64_t CheckedAdd(std::uint64_t a, std::uint64_t b)
		{
			if (a > UINT64_MAX - b)
			{
				throw std::overflow_error("RenderGraph staging size overflow");
			}
			return a + b;
		}

		// Smallest power of two holding `bound`, never below the configured reservation.
		std::uint64_t StagingCapacity(std::uint64_t bound, std::uint64_t reservation)
		{
			const auto wanted = std::max({ bound, reservation, MinimumStagingCapacity });
			if (wanted > (UINT64_MAX >> 1) + 1)
			{
				throw std::overflow_error("RenderGraph staging capacity overflow");
			}
			return std::bit_ceil(wanted);
		}

		std::unique_ptr<Rhi::UploadArena> CreateUpload(Rhi::Device& device, std::uint64_t capacity)
		{
			auto arena = Rhi::UploadArena::Create(device, { capacity, UploadUsages, "RenderGraph upload staging" });
			if (!arena)
			{
				throw std::runtime_error("RenderGraph upload staging allocation failed");
			}
			return arena;
		}

		std::unique_ptr<Rhi::ReadbackArena> CreateReadback(Rhi::Device& device, std::uint64_t capacity)
		{
			auto arena = Rhi::ReadbackArena::Create(device, { capacity, "RenderGraph readback staging" });
			if (!arena)
			{
				throw std::runtime_error("RenderGraph readback staging allocation failed");
			}
			return arena;
		}
	} // namespace

	void RenderGraphExecutor::CreateInitialStaging()
	{
		if (state->Desc.UploadCapacity)
		{
			state->Upload = CreateUpload(state->Device, state->Desc.UploadCapacity);
		}
		if (state->Desc.ReadbackCapacity)
		{
			state->Readback = CreateReadback(state->Device, state->Desc.ReadbackCapacity);
		}
	}

	void RenderGraphExecutor::StageBuffers(const CompiledRenderGraph& graph)
	{
		auto& s = *state;
		const auto& resources = graph.definition->Resources;
		s.Ranges.assign(resources.size(), {});
		s.ReadbackSlices.assign(resources.size(), {});

		// The caller has waited for the previous submission, so both batches are complete.
		if (s.Upload)
		{
			s.Upload->Reset();
		}
		if (s.Readback && !s.Readback->TryReset())
		{
			throw std::logic_error("RenderGraph readback staging is still in flight");
		}

		// Conservative padding bound: every Vulkan-class offset limit is <= 256 bytes.
		const auto& caps = s.Device.GetAdapterInfo().Capabilities;
		const std::uint64_t padding =
			std::max({ std::uint64_t(256), caps.MinUniformBufferOffsetAlignment, caps.MinStorageBufferOffsetAlignment });
		std::vector<std::uint32_t> uploads;
		std::vector<std::uint32_t> readbacks;
		std::uint64_t uploadBound = 0;
		std::uint64_t readbackBound = 0;
		for (std::uint32_t r = 0; r < resources.size(); ++r)
		{
			const auto& resource = resources[r];
			if (resource.Staging == Internal::GraphStaging::None || graph.lifetimes[r].First == GraphResourceLifetime::Unused)
			{
				continue;
			}
			const auto bytes = CheckedAdd(resource.Buffer.Size, std::max(resource.Alignment, padding));
			if (resource.Staging == Internal::GraphStaging::Upload)
			{
				uploads.push_back(r);
				uploadBound = CheckedAdd(uploadBound, bytes);
			}
			else
			{
				readbacks.push_back(r);
				readbackBound = CheckedAdd(readbackBound, bytes);
			}
		}

		std::vector<std::span<std::byte>> uploadBytes;
		const auto allocateUploads = [&]
		{
			uploadBytes.clear();
			for (auto r : uploads)
			{
				auto slice = s.Upload->Allocate(resources[r].Buffer.Size, resources[r].Alignment);
				if (!slice)
				{
					s.Upload->Reset();
					return false;
				}
				s.Ranges[r] = { slice->Resource, slice->Offset, resources[r].Buffer.Size };
				uploadBytes.push_back(slice->Bytes);
			}
			return true;
		};
		if (!uploads.empty() && (!s.Upload || !allocateUploads()))
		{
			// Grow only here: the previous batch is complete and no slice survives.
			s.Upload.reset();
			s.Upload = CreateUpload(s.Device, StagingCapacity(uploadBound, s.Desc.UploadCapacity));
			if (!allocateUploads())
			{
				throw std::logic_error("RenderGraph upload staging bound was insufficient");
			}
		}

		const auto allocateReadbacks = [&]
		{
			for (auto r : readbacks)
			{
				auto slice = s.Readback->Allocate(resources[r].Buffer.Size, resources[r].Alignment);
				if (!slice)
				{
					if (!s.Readback->TryReset())
					{
						throw std::logic_error("RenderGraph readback staging reset failed");
					}
					return false;
				}
				s.Ranges[r] = { &slice->GetBuffer(), slice->GetOffset(), slice->GetSize() };
				s.ReadbackSlices[r] = *slice;
			}
			return true;
		};
		if (!readbacks.empty() && (!s.Readback || !allocateReadbacks()))
		{
			s.Readback.reset();
			s.Readback = CreateReadback(s.Device, StagingCapacity(readbackBound, s.Desc.ReadbackCapacity));
			if (!allocateReadbacks())
			{
				throw std::logic_error("RenderGraph readback staging bound was insufficient");
			}
		}

		for (std::uint32_t r = 0; r < resources.size(); ++r)
		{
			if (s.Ranges[r].Buffer)
			{
				s.Resources[r] = s.Ranges[r].Buffer;
			}
		}

		// Writers run last, so a throwing writer leaves nothing half-bound beyond
		// staging bytes that the next execution resets.
		for (std::size_t i = 0; i < uploads.size(); ++i)
		{
			resources[uploads[i]].Writer(uploadBytes[i]);
		}
	}

	const Rhi::ReadbackSlice& RenderGraphExecutor::GetReadbackSlice(GraphBuffer resource) const
	{
		if (!state->HasResult || state->Recording)
		{
			throw std::logic_error("RenderGraph has no successful execution result");
		}
		const auto& r = Internal::RequireResource(*state->Graph.definition, resource.Graph, resource.Index, GraphKind::Buffer);
		if (r.Staging != Internal::GraphStaging::Readback)
		{
			throw std::invalid_argument("RenderGraph resource is not a readback buffer: " + r.Name);
		}
		return state->ReadbackSlices[resource.Index];
	}

	Rhi::ReadbackStatus RenderGraphExecutor::TryGetReadback(GraphBuffer resource, std::span<const std::byte>& data)
	{
		data = {};
		const auto& slice = GetReadbackSlice(resource);
		return state->Readback->TryGetData(slice, data);
	}

	Rhi::ReadbackStatus RenderGraphExecutor::TryReadback(GraphBuffer resource, std::span<std::byte> destination)
	{
		const auto& slice = GetReadbackSlice(resource);
		return state->Readback->TryRead(slice, destination);
	}

	std::uint64_t RenderGraphExecutor::GetUploadCapacity() const
	{
		return state->Upload ? state->Upload->GetCapacity() : 0;
	}

	std::uint64_t RenderGraphExecutor::GetReadbackCapacity() const
	{
		return state->Readback ? state->Readback->GetCapacity() : 0;
	}
} // namespace Swim::Render
