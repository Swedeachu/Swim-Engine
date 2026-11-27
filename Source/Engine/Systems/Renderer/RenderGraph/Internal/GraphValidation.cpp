#include "Engine/Systems/Renderer/RenderGraph/Internal/GraphValidation.h"
#include <bit>

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

	namespace
	{

		using S = Rhi::ResourceState;

		struct BufferStateUsage
		{
			S State;
			Rhi::BufferUsage Usage;
		};

		// Each buffer state flag and the usage it needs.
		const BufferStateUsage BufferStates[] = {
			{ S::Common, Rhi::BufferUsage::None },
			{ S::CopySource, Rhi::BufferUsage::TransferSource },
			{ S::CopyDestination, Rhi::BufferUsage::TransferDestination },
			{ S::VertexBuffer, Rhi::BufferUsage::Vertex },
			{ S::IndexBuffer, Rhi::BufferUsage::Index },
			{ S::UniformBuffer, Rhi::BufferUsage::Uniform },
			{ S::ShaderRead, Rhi::BufferUsage::Storage },
			{ S::ShaderWrite, Rhi::BufferUsage::Storage },
			{ S::IndirectArgument, Rhi::BufferUsage::Indirect },
			{ S::HostRead, Rhi::BufferUsage::None },
			{ S::HostWrite, Rhi::BufferUsage::None },
		};

		struct TextureStateUsage
		{
			S State;
			Rhi::TextureUsage Usage;
		};

		// The texture states a graph accepts (an exact match) and the usage each needs.
		const TextureStateUsage TextureStates[] = {
			{ S::Common, Rhi::TextureUsage::None },
			{ S::CopySource, Rhi::TextureUsage::TransferSource },
			{ S::CopyDestination, Rhi::TextureUsage::TransferDestination },
			{ S::ShaderRead, Rhi::TextureUsage::Sampled },
			{ S::ShaderWrite, Rhi::TextureUsage::Storage },
			{ S::ShaderRead | S::ShaderWrite, Rhi::TextureUsage::Storage },
			{ S::ColorAttachment, Rhi::TextureUsage::ColorAttachment },
			{ S::DepthStencilRead, Rhi::TextureUsage::DepthStencilAttachment },
			{ S::DepthStencilWrite, Rhi::TextureUsage::DepthStencilAttachment },
			{ S::DepthStencilRead | S::DepthStencilWrite, Rhi::TextureUsage::DepthStencilAttachment },
			{ S::DepthStencilRead | S::ShaderRead, Rhi::TextureUsage::DepthStencilAttachment | Rhi::TextureUsage::Sampled },
		};

		void ValidateBufferState(const GraphResource& r, S state)
		{
			auto remaining = static_cast<std::uint32_t>(state);

			for (const auto& entry : BufferStates)
			{
				if (!Rhi::HasAny(state, entry.State))
				{
					continue;
				}

				if (Rhi::EnumAnd(r.Buffer.Usage, entry.Usage) != entry.Usage)
				{
					throw std::invalid_argument("RenderGraph buffer state requires matching usage: " + r.Name);
				}

				remaining &= ~static_cast<std::uint32_t>(entry.State);
			}

			if (remaining || (Rhi::HasAny(state, S::HostRead) && r.Buffer.Memory != Rhi::MemoryPreference::GpuToCpu) ||
				(Rhi::HasAny(state, S::HostWrite) && r.Buffer.Memory != Rhi::MemoryPreference::CpuToGpu))
			{
				throw std::invalid_argument("RenderGraph unsupported buffer state or host access: " + r.Name);
			}
		}

		Rhi::TextureUsage RequiredTextureUsage(const GraphResource& r, S state)
		{
			for (const auto& entry : TextureStates)
			{
				if (entry.State == state)
				{
					return entry.Usage;
				}
			}

			if (state != S::Present || !r.Imported)
			{
				throw std::invalid_argument("RenderGraph unsupported texture state: " + r.Name);
			}

			return Rhi::TextureUsage::None;
		}

	} // namespace

	void ValidateState(const GraphResource& r, Rhi::ResourceState state, bool allowUndefined)
	{
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
			ValidateBufferState(r, state);
			return;
		}

		const auto usage = RequiredTextureUsage(r, state);

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
		result.reserve(std::size_t(range.ArrayLayerCount) * range.MipLevelCount);
		ForEachCell(r, range,
			[&](std::uint32_t cell)
			{
				result.push_back(cell);
			});
		return result;
	}

	std::uint64_t PooledBufferSize(std::uint64_t size)
	{
		if (size <= 256)
		{
			return 256;
		}

		const auto step = std::bit_floor(size - 1) / 4;
		return (size + step - 1) / step * step;
	}

	bool Compatible(const GraphResource& a, const GraphResource& b)
	{
		if (a.Kind != b.Kind)
		{
			return false;
		}

		if (a.Kind == GraphKind::Buffer)
		{
			return PooledBufferSize(a.Buffer.Size) == PooledBufferSize(b.Buffer.Size) && a.Buffer.Usage == b.Buffer.Usage &&
				a.Buffer.Memory == b.Buffer.Memory && a.Buffer.PersistentMap == b.Buffer.PersistentMap;
		}

		const auto& x = a.Texture;
		const auto& y = b.Texture;
		return x.Dimension == y.Dimension && x.Extent.Width == y.Extent.Width && x.Extent.Height == y.Extent.Height &&
			x.Extent.Depth == y.Extent.Depth && x.PixelFormat == y.PixelFormat && x.Usage == y.Usage && x.MipLevels == y.MipLevels &&
			x.ArrayLayers == y.ArrayLayers && x.Samples == y.Samples;
	}

} // namespace Swim::Render::Internal
