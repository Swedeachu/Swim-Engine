#include "Engine/Systems/Renderer/RHI/Backends/D3D12/D3D12CommandList.h"

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace Swim::RhiD3D12
{

	namespace
	{

		using State = Rhi::ResourceState;

		struct BarrierScope
		{
			D3D12_BARRIER_SYNC Sync = D3D12_BARRIER_SYNC_NONE;
			D3D12_BARRIER_ACCESS Access = D3D12_BARRIER_ACCESS_COMMON; // Zero: bits are ORed in.
			D3D12_BARRIER_LAYOUT Layout = D3D12_BARRIER_LAYOUT_UNDEFINED;
		};

		bool Storage(State state)
		{
			return Rhi::HasAny(state, State::ShaderWrite);
		}

		// Enhanced barriers: the RHI state as sync scope, access and texture layout.
		BarrierScope ToAccess(State state, bool texture)
		{
			BarrierScope result;
			const auto add = [&](State flag, D3D12_BARRIER_SYNC sync, D3D12_BARRIER_ACCESS access)
			{
				if (Rhi::HasAny(state, flag))
				{
					result.Sync |= sync;
					result.Access |= access;
				}
			};

			if (state == State::Undefined)
			{
				return { D3D12_BARRIER_SYNC_NONE, D3D12_BARRIER_ACCESS_NO_ACCESS, D3D12_BARRIER_LAYOUT_UNDEFINED };
			}

			if (state == State::Common)
			{
				return { D3D12_BARRIER_SYNC_ALL, D3D12_BARRIER_ACCESS_COMMON, D3D12_BARRIER_LAYOUT_COMMON };
			}

			if (state == State::Present)
			{
				return { D3D12_BARRIER_SYNC_NONE, D3D12_BARRIER_ACCESS_NO_ACCESS, D3D12_BARRIER_LAYOUT_PRESENT };
			}

			add(State::CopySource, D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_ACCESS_COPY_SOURCE);
			add(State::CopyDestination, D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_ACCESS_COPY_DEST);
			add(State::VertexBuffer, D3D12_BARRIER_SYNC_VERTEX_SHADING, D3D12_BARRIER_ACCESS_VERTEX_BUFFER);
			add(State::IndexBuffer, D3D12_BARRIER_SYNC_INDEX_INPUT, D3D12_BARRIER_ACCESS_INDEX_BUFFER);
			add(State::UniformBuffer, D3D12_BARRIER_SYNC_ALL_SHADING, D3D12_BARRIER_ACCESS_CONSTANT_BUFFER);
			add(State::IndirectArgument, D3D12_BARRIER_SYNC_EXECUTE_INDIRECT, D3D12_BARRIER_ACCESS_INDIRECT_ARGUMENT);
			add(State::ColorAttachment, D3D12_BARRIER_SYNC_RENDER_TARGET, D3D12_BARRIER_ACCESS_RENDER_TARGET);
			add(State::DepthStencilRead, D3D12_BARRIER_SYNC_DEPTH_STENCIL, D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ);
			add(State::DepthStencilWrite, D3D12_BARRIER_SYNC_DEPTH_STENCIL, D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE);
			add(State::AccelerationStructureRead, D3D12_BARRIER_SYNC_RAYTRACING,
				D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_READ);
			add(State::AccelerationStructureWrite, D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE,
				D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_WRITE);

			if (Storage(state))
			{
				add(State::ShaderWrite, D3D12_BARRIER_SYNC_ALL_SHADING, D3D12_BARRIER_ACCESS_UNORDERED_ACCESS);
			}
			else
			{
				add(State::ShaderRead, D3D12_BARRIER_SYNC_ALL_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
			}

			// NO_ACCESS excludes every other bit, so it marks only states without GPU
			// access.
			if (result.Access == D3D12_BARRIER_ACCESS_COMMON)
			{
				result.Access = D3D12_BARRIER_ACCESS_NO_ACCESS;
			}

			if (texture)
			{
				result.Layout = Storage(state)					   ? D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS
					: Rhi::HasAny(state, State::DepthStencilWrite) ? D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE
					: Rhi::HasAny(state, State::DepthStencilRead)  ? D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_READ
					: Rhi::HasAny(state, State::ColorAttachment)   ? D3D12_BARRIER_LAYOUT_RENDER_TARGET
					: Rhi::HasAny(state, State::CopyDestination)   ? D3D12_BARRIER_LAYOUT_COPY_DEST
					: Rhi::HasAny(state, State::CopySource)		   ? D3D12_BARRIER_LAYOUT_COPY_SOURCE
					: Rhi::HasAny(state, State::ShaderRead)		   ? D3D12_BARRIER_LAYOUT_SHADER_RESOURCE
																   : D3D12_BARRIER_LAYOUT_COMMON;
			}

			return result;
		}

		// Legacy barriers: the RHI state as resource states. Compute lists cannot name
		// the pixel-shader state, so their reads are non-pixel only.
		D3D12_RESOURCE_STATES ToLegacy(State state, bool compute)
		{
			if (state == State::Undefined || state == State::Common || state == State::Present)
			{
				return D3D12_RESOURCE_STATE_COMMON;
			}

			D3D12_RESOURCE_STATES result = D3D12_RESOURCE_STATE_COMMON;
			const auto add = [&](State flag, D3D12_RESOURCE_STATES native)
			{
				if (Rhi::HasAny(state, flag))
				{
					result |= native;
				}
			};

			add(State::CopySource, D3D12_RESOURCE_STATE_COPY_SOURCE);
			add(State::CopyDestination, D3D12_RESOURCE_STATE_COPY_DEST);
			add(State::VertexBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
			add(State::UniformBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
			add(State::IndexBuffer, D3D12_RESOURCE_STATE_INDEX_BUFFER);
			add(State::IndirectArgument, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
			add(State::ColorAttachment, D3D12_RESOURCE_STATE_RENDER_TARGET);
			add(State::DepthStencilRead, D3D12_RESOURCE_STATE_DEPTH_READ);
			add(State::DepthStencilWrite, D3D12_RESOURCE_STATE_DEPTH_WRITE);

			if (Storage(state))
			{
				return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
			}

			if (Rhi::HasAny(state, State::ShaderRead))
			{
				result |= compute ? D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
								  : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			}

			// A write state cannot be combined with anything else.
			if (result & D3D12_RESOURCE_STATE_DEPTH_WRITE)
			{
				return D3D12_RESOURCE_STATE_DEPTH_WRITE;
			}

			return result;
		}

		D3D12_BARRIER_SUBRESOURCE_RANGE
		ToRange(const D3D12Texture& texture, const Rhi::TextureSubresourceRange& range)
		{
			const auto& desc = texture.GetDesc();
			const auto layers = desc.Dimension == Rhi::TextureDimension::Texture3D ? 1u : desc.ArrayLayers;
			D3D12_BARRIER_SUBRESOURCE_RANGE result{};
			result.IndexOrFirstMipLevel = range.BaseMipLevel;
			result.NumMipLevels = std::min(range.MipLevelCount, desc.MipLevels - range.BaseMipLevel);
			result.FirstArraySlice = std::min(range.BaseArrayLayer, layers);
			result.NumArraySlices = std::min(range.ArrayLayerCount, layers - result.FirstArraySlice);
			result.FirstPlane = 0;
			result.NumPlanes = texture.GetPlaneCount();
			return result;
		}

	} // namespace

	void D3D12CommandList::RecordEnhanced(std::span<const BufferTransition> buffers, std::span<const TextureTransition> textures)
	{
		auto& bufferBarriers = enhancedBuffers;
		auto& textureBarriers = enhancedTextures;
		bufferBarriers.clear();
		textureBarriers.clear();
		// Transitions of textures kept in the COMMON layout change no layout: their sync and
		// access go into one global barrier ("Global Barriers have no effect on texture
		// layout", Enhanced Barriers spec), with no per-subresource work for the runtime or a
		// debug layer. Only a discard (an undefined before state) stays a texture barrier.
		D3D12_GLOBAL_BARRIER global{ D3D12_BARRIER_SYNC_NONE, D3D12_BARRIER_SYNC_NONE, D3D12_BARRIER_ACCESS_NO_ACCESS,
			D3D12_BARRIER_ACCESS_NO_ACCESS };
		const auto merge = [](D3D12_BARRIER_ACCESS& into, D3D12_BARRIER_ACCESS access)
		{
			// NO_ACCESS excludes every other bit.
			if (access == D3D12_BARRIER_ACCESS_NO_ACCESS)
			{
				return;
			}

			into = into == D3D12_BARRIER_ACCESS_NO_ACCESS ? access : into | access;
		};

		for (const auto& transition : buffers)
		{
			if (transition.Buffer->IsHostHeap())
			{
				continue; // Upload/readback memory: the fence orders host access.
			}

			const auto before = ToAccess(transition.Before, false);
			const auto after = ToAccess(transition.After, false);
			D3D12_BUFFER_BARRIER barrier{};
			barrier.SyncBefore = before.Sync;
			barrier.SyncAfter = after.Sync;
			barrier.AccessBefore = before.Access;
			barrier.AccessAfter = after.Access;
			barrier.pResource = transition.Buffer->GetResource();
			barrier.Offset = 0;
			barrier.Size = UINT64_MAX;
			bufferBarriers.push_back(barrier);
		}

		for (const auto& transition : textures)
		{
			auto before = ToAccess(transition.Before, true);
			auto after = ToAccess(transition.After, true);

			if (transition.Texture->KeepsCommonLayout())
			{
				after.Layout = D3D12_BARRIER_LAYOUT_COMMON;

				if (before.Layout != D3D12_BARRIER_LAYOUT_UNDEFINED)
				{
					global.SyncBefore |= before.Sync;
					global.SyncAfter |= after.Sync;
					merge(global.AccessBefore, before.Access);
					merge(global.AccessAfter, after.Access);
					continue;
				}
			}

			D3D12_TEXTURE_BARRIER barrier{};
			barrier.SyncBefore = before.Sync;
			barrier.SyncAfter = after.Sync;
			barrier.AccessBefore = before.Access;
			barrier.AccessAfter = after.Access;
			barrier.LayoutBefore = before.Layout;
			barrier.LayoutAfter = after.Layout;
			barrier.pResource = transition.Texture->GetResource();
			barrier.Subresources = ToRange(*transition.Texture, transition.Range);
			textureBarriers.push_back(barrier);
		}

		D3D12_BARRIER_GROUP groups[3]{};
		UINT count = 0;

		if (global.SyncBefore != D3D12_BARRIER_SYNC_NONE || global.SyncAfter != D3D12_BARRIER_SYNC_NONE)
		{
			groups[count].Type = D3D12_BARRIER_TYPE_GLOBAL;
			groups[count].NumBarriers = 1;
			groups[count++].pGlobalBarriers = &global;
		}

		if (!bufferBarriers.empty())
		{
			groups[count].Type = D3D12_BARRIER_TYPE_BUFFER;
			groups[count].NumBarriers = static_cast<UINT32>(bufferBarriers.size());
			groups[count++].pBufferBarriers = bufferBarriers.data();
		}

		if (!textureBarriers.empty())
		{
			groups[count].Type = D3D12_BARRIER_TYPE_TEXTURE;
			groups[count].NumBarriers = static_cast<UINT32>(textureBarriers.size());
			groups[count++].pTextureBarriers = textureBarriers.data();
		}

		if (count != 0)
		{
			list7->Barrier(count, groups);
		}
	}

	void D3D12CommandList::RecordLegacy(std::span<const BufferTransition> buffers, std::span<const TextureTransition> textures)
	{
		const bool compute = type != Rhi::QueueType::Graphics;
		auto& barriers = legacyBarriers;
		barriers.clear();
		const auto push = [&](ID3D12Resource* resource, UINT subresource, D3D12_RESOURCE_STATES& tracked, State before, State after)
		{
			// The caller's before state, or the tracked one when it does not know
			// (Undefined).
			const auto from = before == State::Undefined ? tracked : ToLegacy(before, compute);
			const auto to = ToLegacy(after, compute);

			if (from == to)
			{
				if (Storage(before) && Storage(after))
				{
					D3D12_RESOURCE_BARRIER uav{ D3D12_RESOURCE_BARRIER_TYPE_UAV };
					uav.UAV.pResource = resource;
					barriers.push_back(uav);
				}
			}
			else
			{
				D3D12_RESOURCE_BARRIER barrier{ D3D12_RESOURCE_BARRIER_TYPE_TRANSITION };
				barrier.Transition = { resource, subresource, from, to };
				barriers.push_back(barrier);
			}

			tracked = to;
		};

		for (const auto& transition : buffers)
		{
			if (!transition.Buffer->IsHostHeap())
			{
				push(transition.Buffer->GetResource(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, transition.Buffer->TrackedState,
					transition.Before, transition.After);
			}
		}

		for (const auto& transition : textures)
		{
			auto& texture = *transition.Texture;
			const auto range = ToRange(texture, transition.Range);

			for (std::uint32_t plane = 0; plane < range.NumPlanes; ++plane)
			{
				for (std::uint32_t layer = range.FirstArraySlice; layer < range.FirstArraySlice + range.NumArraySlices; ++layer)
				{
					for (std::uint32_t mip = range.IndexOrFirstMipLevel; mip < range.IndexOrFirstMipLevel + range.NumMipLevels; ++mip)
					{
						const auto index = texture.Subresource(mip, layer, plane);
						push(texture.GetResource(), index, texture.TrackedStates[index], transition.Before, transition.After);
					}
				}
			}
		}

		if (!barriers.empty())
		{
			list->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
		}
	}

	void D3D12CommandList::RecordBarriers(std::span<const BufferTransition> buffers, std::span<const TextureTransition> textures)
	{
		if (rendering)
		{
			throw std::logic_error("D3D12: transitions are recorded outside rendering");
		}

		FlushCopies();

		if (list7)
		{
			RecordEnhanced(buffers, textures);
		}
		else
		{
			RecordLegacy(buffers, textures);
		}
	}

	void D3D12CommandList::Transition(Rhi::Buffer& buffer, Rhi::ResourceState before, Rhi::ResourceState after)
	{
		const BufferTransition transition{ &static_cast<D3D12Buffer&>(buffer), before, after };
		RecordBarriers({ &transition, 1 }, {});
	}

	void D3D12CommandList::Transition(
		Rhi::Texture& texture, Rhi::ResourceState before, Rhi::ResourceState after, const Rhi::TextureSubresourceRange& range)
	{
		const TextureTransition transition{ &static_cast<D3D12Texture&>(texture), before, after, range };
		RecordBarriers({}, { &transition, 1 });
		transition.Texture->LastState = after;
	}

	void D3D12CommandList::Transitions(std::span<const BufferBarrier> buffers, std::span<const TextureBarrier> textures)
	{
		auto& bufferTransitions = transitionBuffers;
		auto& textureTransitions = transitionTextures;
		bufferTransitions.clear();
		textureTransitions.clear();

		for (const auto& barrier : buffers)
		{
			bufferTransitions.push_back({ &static_cast<D3D12Buffer&>(*barrier.Resource), barrier.Before, barrier.After });
		}

		for (const auto& barrier : textures)
		{
			textureTransitions.push_back({ &static_cast<D3D12Texture&>(*barrier.Resource), barrier.Before, barrier.After, barrier.Range });
			textureTransitions.back().Texture->LastState = barrier.After;
		}

		RecordBarriers(bufferTransitions, textureTransitions);
	}

} // namespace Swim::RhiD3D12
