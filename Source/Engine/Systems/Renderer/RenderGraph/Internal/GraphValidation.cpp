#include "Engine/Systems/Renderer/RenderGraph/Internal/GraphValidation.h"

namespace Swim::Render::Internal
{
	void ValidateName(std::string_view name)
	{
		if (name.empty() || name.find('\0') != std::string_view::npos)
		{
			throw std::invalid_argument("RenderGraph names must be nonempty without embedded NUL");
		}
	}

	const GraphResource& RequireResource(const GraphDefinition& graph, std::uint64_t owner, std::uint32_t index, GraphKind kind)
	{
		if (owner != graph.Id || index >= graph.Resources.size() || graph.Resources[index].Kind != kind)
		{
			throw std::invalid_argument("Invalid or foreign RenderGraph resource handle");
		}
		return graph.Resources[index];
	}

	void ValidateState(const GraphResource& r, Rhi::ResourceState state, bool allowUndefined)
	{
		using S = Rhi::ResourceState;
		if (state == S::Undefined)
		{
			if (allowUndefined)
			{
				return;
			}
			throw std::invalid_argument("RenderGraph Undefined is only an initial state");
		}

		if (r.Kind == GraphKind::Buffer)
		{
			using U = Rhi::BufferUsage;
			auto remaining = static_cast<std::uint32_t>(state);
			const auto check = [&](S flag, U usage)
			{
				if (!Rhi::HasAny(state, flag))
				{
					return;
				}
				if (Rhi::EnumAnd(r.Buffer.Usage, usage) != usage)
				{
					throw std::invalid_argument("RenderGraph buffer state requires matching usage: " + r.Name);
				}
				remaining &= ~static_cast<std::uint32_t>(flag);
			};

			check(S::Common, U::None);
			check(S::CopySource, U::TransferSource);
			check(S::CopyDestination, U::TransferDestination);
			check(S::VertexBuffer, U::Vertex);
			check(S::IndexBuffer, U::Index);
			check(S::UniformBuffer, U::Uniform);
			check(S::ShaderRead, U::Storage);
			check(S::ShaderWrite, U::Storage);
			check(S::IndirectArgument, U::Indirect);
			check(S::HostRead, U::None);
			check(S::HostWrite, U::None);

			if (remaining || (Rhi::HasAny(state, S::HostRead) && r.Buffer.Memory != Rhi::MemoryPreference::GpuToCpu) ||
				(Rhi::HasAny(state, S::HostWrite) && r.Buffer.Memory != Rhi::MemoryPreference::CpuToGpu))
			{
				throw std::invalid_argument("RenderGraph unsupported buffer state or host access: " + r.Name);
			}
			return;
		}

		using U = Rhi::TextureUsage;
		U usage = U::None;
		if (state == S::Common)
		{
		}
		else if (state == S::CopySource)
		{
			usage = U::TransferSource;
		}
		else if (state == S::CopyDestination)
		{
			usage = U::TransferDestination;
		}
		else if (state == S::ShaderRead)
		{
			usage = U::Sampled;
		}
		else if (state == S::ShaderWrite || state == (S::ShaderRead | S::ShaderWrite))
		{
			usage = U::Storage;
		}
		else if (state == S::ColorAttachment)
		{
			usage = U::ColorAttachment;
		}
		else if (state == S::DepthStencilRead || state == S::DepthStencilWrite || state == (S::DepthStencilRead | S::DepthStencilWrite))
		{
			usage = U::DepthStencilAttachment;
		}
		else if (state == (S::DepthStencilRead | S::ShaderRead))
		{
			usage = U::DepthStencilAttachment | U::Sampled;
		}
		else if (state != S::Present || !r.Imported)
		{
			throw std::invalid_argument("RenderGraph unsupported texture state: " + r.Name);
		}
		if (Rhi::EnumAnd(r.Texture.Usage, usage) != usage ||
			(Rhi::HasAny(state, S::ColorAttachment) && Rhi::IsDepthFormat(r.Texture.PixelFormat)) ||
			(Rhi::HasAny(state, S::DepthStencilRead | S::DepthStencilWrite) && !Rhi::IsDepthFormat(r.Texture.PixelFormat)))
		{
			throw std::invalid_argument("RenderGraph texture state requires matching usage/format: " + r.Name);
		}
	}

	void ValidateAccess(const GraphResource& r, GraphAccess access, Rhi::ResourceState state, Rhi::QueueType type)
	{
		using S = Rhi::ResourceState;
		ValidateState(r, state);

		// ShaderRead|ShaderWrite is also the RHI's read-only storage-image layout.
		const bool storageRead = r.Kind == GraphKind::Texture && state == (S::ShaderRead | S::ShaderWrite);
		const bool writes = Rhi::HasAny(state, S::CopyDestination | S::ShaderWrite | S::ColorAttachment | S::DepthStencilWrite | S::Common);
		if ((access == GraphAccess::Read && writes && !storageRead) || (access != GraphAccess::Read && !writes) || state == S::Present ||
			Rhi::HasAny(state, S::HostRead | S::HostWrite))
		{
			throw std::invalid_argument("RenderGraph access/state mismatch: " + r.Name);
		}
		// ReadWrite + CopyDestination is a partial copy that preserves the bytes or
		// texels it does not overwrite; it therefore requires initialized contents.
		if (access == GraphAccess::ReadWrite && r.Kind == GraphKind::Buffer && state != S::CopyDestination &&
			!Rhi::HasAny(state, S::ShaderRead | S::Common))
		{
			throw std::invalid_argument("RenderGraph read/write buffers need a state with shader read access");
		}
		if (type == Rhi::QueueType::Transfer && state != S::CopySource && state != S::CopyDestination)
		{
			throw std::invalid_argument("RenderGraph transfer passes require copy states");
		}
		if (type == Rhi::QueueType::Compute &&
			Rhi::HasAny(state, S::ColorAttachment | S::DepthStencilRead | S::DepthStencilWrite | S::VertexBuffer | S::IndexBuffer))
		{
			throw std::invalid_argument("RenderGraph compute pass declares a graphics-only state");
		}
	}

	Rhi::TextureSubresourceRange NormalizeRange(const GraphResource& r, Rhi::TextureSubresourceRange range)
	{
		if (r.Kind == GraphKind::Buffer)
		{
			return { 0, 1, 0, 1 };
		}
		const auto& d = r.Texture;
		if (range.BaseMipLevel >= d.MipLevels || range.BaseArrayLayer >= d.ArrayLayers)
		{
			throw std::invalid_argument("RenderGraph subresource base is out of range");
		}
		if (range.MipLevelCount == UINT32_MAX)
		{
			range.MipLevelCount = d.MipLevels - range.BaseMipLevel;
		}
		if (range.ArrayLayerCount == UINT32_MAX)
		{
			range.ArrayLayerCount = d.ArrayLayers - range.BaseArrayLayer;
		}
		if (!range.MipLevelCount || !range.ArrayLayerCount || range.MipLevelCount > d.MipLevels - range.BaseMipLevel ||
			range.ArrayLayerCount > d.ArrayLayers - range.BaseArrayLayer)
		{
			throw std::invalid_argument("RenderGraph subresource count is out of range");
		}
		return range;
	}

	std::uint32_t CellCount(const GraphResource& r)
	{
		if (r.Kind == GraphKind::Buffer)
		{
			return 1;
		}
		const auto count = std::uint64_t(r.Texture.MipLevels) * r.Texture.ArrayLayers;
		if (!count || count > UINT32_MAX)
		{
			throw std::invalid_argument("RenderGraph invalid subresource count");
		}
		return static_cast<std::uint32_t>(count);
	}

	std::vector<std::uint32_t> Cells(const GraphResource& r, const Rhi::TextureSubresourceRange& range)
	{
		if (r.Kind == GraphKind::Buffer)
		{
			return { 0 };
		}
		std::vector<std::uint32_t> result;
		for (std::uint32_t layer = range.BaseArrayLayer; layer < range.BaseArrayLayer + range.ArrayLayerCount; ++layer)
		{
			for (std::uint32_t mip = range.BaseMipLevel; mip < range.BaseMipLevel + range.MipLevelCount; ++mip)
			{
				result.push_back(layer * r.Texture.MipLevels + mip);
			}
		}
		return result;
	}

	bool Compatible(const GraphResource& a, const GraphResource& b)
	{
		if (a.Kind != b.Kind)
		{
			return false;
		}
		if (a.Kind == GraphKind::Buffer)
		{
			return a.Buffer.Size == b.Buffer.Size && a.Buffer.Usage == b.Buffer.Usage && a.Buffer.Memory == b.Buffer.Memory &&
				a.Buffer.PersistentMap == b.Buffer.PersistentMap;
		}
		const auto& x = a.Texture;
		const auto& y = b.Texture;
		return x.Dimension == y.Dimension && x.Extent.Width == y.Extent.Width && x.Extent.Height == y.Extent.Height &&
			x.Extent.Depth == y.Extent.Depth && x.PixelFormat == y.PixelFormat && x.Usage == y.Usage && x.MipLevels == y.MipLevels &&
			x.ArrayLayers == y.ArrayLayers && x.Samples == y.Samples;
	}
} // namespace Swim::Render::Internal
