#include "Engine/Systems/Renderer/UiRendering/UiAtlasTextures.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphTransfers.h"

#include <stdexcept>

namespace Swim::Render
{
	namespace
	{
		// RGB8 atlas rows -> RGBA8 (alpha 1), tightly packed.
		std::vector<std::byte> ExpandRows(const Text::AtlasPageView& page, std::uint32_t y, std::uint32_t rows)
		{
			std::vector<std::byte> bytes(std::size_t(page.Size) * rows * 4);
			const std::size_t first = std::size_t(y) * page.Size;
			for (std::size_t texel = 0; texel < std::size_t(page.Size) * rows; ++texel)
			{
				const std::size_t source = (first + texel) * 3;
				bytes[texel * 4 + 0] = std::byte(page.Pixels[source + 0]);
				bytes[texel * 4 + 1] = std::byte(page.Pixels[source + 1]);
				bytes[texel * 4 + 2] = std::byte(page.Pixels[source + 2]);
				bytes[texel * 4 + 3] = std::byte(0xFF);
			}
			return bytes;
		}
	} // namespace

	UiAtlasTextures::UiAtlasTextures(Rhi::Device& deviceInput, BindlessResourceTable& table, UiAtlasTexturesDesc descInput)
		: device(deviceInput), bindless(table), desc(std::move(descInput))
	{
		if (desc.MaxPages == 0 || desc.MaxPages > 4096)
		{
			throw std::invalid_argument(desc.DebugName + " needs 1 .. 4096 pages");
		}
		Rhi::SamplerDesc samplerDesc{};
		samplerDesc.MinFilter = samplerDesc.MagFilter = Rhi::Filter::Linear;
		samplerDesc.MipFilter = Rhi::Filter::Nearest;
		samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = Rhi::SamplerAddressMode::ClampToEdge;
		samplerDesc.MaxLod = 0.0f;
		samplerDesc.DebugName = desc.DebugName;
		sampler = device.CreateSampler(samplerDesc);
		if (!sampler)
		{
			throw std::runtime_error(desc.DebugName + " sampler could not be created");
		}
		samplerHandle = bindless.RegisterSampler(*sampler);
	}

	UiAtlasTextures::~UiAtlasTextures()
	{
		for (auto& page : pages)
		{
			bindless.Release(page.Handle);
		}
		bindless.Release(samplerHandle);
	}

	UiAtlasTextures::Page UiAtlasTextures::CreatePage(std::uint32_t index, std::uint32_t size)
	{
		Page page;
		const std::string name = desc.DebugName + " page " + std::to_string(index);
		Rhi::TextureDesc textureDesc;
		textureDesc.Extent = { size, size, 1 };
		textureDesc.PixelFormat = Rhi::Format::RGBA8Unorm;
		textureDesc.Usage = Rhi::TextureUsage::Sampled | Rhi::TextureUsage::TransferDestination;
		textureDesc.DebugName = name;
		page.Texture = device.CreateTexture(textureDesc);
		if (!page.Texture)
		{
			throw std::runtime_error(name + " could not be created");
		}
		Rhi::TextureViewDesc viewDesc;
		viewDesc.PixelFormat = Rhi::Format::RGBA8Unorm;
		viewDesc.DebugName = name;
		page.View = device.CreateTextureView(*page.Texture, viewDesc);
		if (!page.View)
		{
			throw std::runtime_error(name + " view could not be created");
		}
		const auto handle = bindless.TryRegisterTexture(*page.View);
		if (!handle)
		{
			throw std::length_error(desc.DebugName + ": the bindless table is full");
		}
		page.Handle = *handle;
		return page;
	}

	UiAtlasFrame UiAtlasTextures::Update(RenderGraph& graph, const Text::GlyphAtlas& atlas)
	{
		if (pending)
		{
			throw std::logic_error(desc.DebugName + " has a frame awaiting CommitFrame/AbortFrame");
		}
		if (attached && attached != &atlas)
		{
			throw std::logic_error(desc.DebugName + " is attached to another glyph atlas; Release it first");
		}
		const std::uint32_t count = atlas.GetPageCount();
		if (count > desc.MaxPages)
		{
			throw std::length_error(desc.DebugName + " exceeds its page budget");
		}
		attached = &atlas;
		UiAtlasFrame frame;
		frame.SamplerIndex = bindless.GetIndex(samplerHandle);
		frame.PageSize = atlas.GetDesc().PageSize;
		while (pages.size() < count)
		{
			pages.push_back(CreatePage(static_cast<std::uint32_t>(pages.size()), frame.PageSize));
		}
		pendingBytes = 0;
		for (std::uint32_t index = 0; index < count; ++index)
		{
			auto& page = pages[index];
			const auto view = atlas.GetPage(index);
			const auto texture =
				graph.ImportTexture(*page.Texture, page.Initialized ? Rhi::ResourceState::ShaderRead : Rhi::ResourceState::Undefined);
			graph.Export(texture, Rhi::ResourceState::ShaderRead);
			frame.Pages.push_back(texture);
			frame.TextureIndices.push_back(bindless.GetIndex(page.Handle));
			std::uint32_t y = 0;
			std::uint32_t rows = 0;
			if (!page.Initialized)
			{
				rows = view.Size; // An initializing whole-page write.
			}
			else if (view.Revision > page.Revision)
			{
				const auto changed = atlas.GetChangedRows(index, page.Revision);
				y = changed.Y;
				rows = changed.Height;
			}
			if (rows > 0)
			{
				const auto bytes = ExpandRows(view, y, rows);
				Rhi::BufferTextureCopyRegion region;
				region.TextureOffset = { 0, std::int32_t(y), 0 };
				region.Extent = { view.Size, rows, 1 };
				AddTextureUpload(graph, desc.DebugName + " upload " + std::to_string(index), bytes, texture, region);
				frame.UploadedPages++;
				frame.UploadedRows += rows;
				frame.UploadedBytes += bytes.size();
			}
			page.PendingRevision = view.Revision;
			page.Pending = true;
		}
		pendingBytes = frame.UploadedBytes;
		pending = true;
		return frame;
	}

	void UiAtlasTextures::CommitFrame()
	{
		for (auto& page : pages)
		{
			if (page.Pending)
			{
				page.Revision = page.PendingRevision;
				page.Initialized = true;
				page.Pending = false;
			}
		}
		uploadedBytes += pendingBytes;
		pendingBytes = 0;
		pending = false;
	}

	void UiAtlasTextures::AbortFrame()
	{
		for (auto& page : pages)
		{
			page.Pending = false;
		}
		pendingBytes = 0;
		pending = false;
	}

	void UiAtlasTextures::Release(Rhi::TimelinePoint lastUse)
	{
		if (pending)
		{
			throw std::logic_error(desc.DebugName + " cannot release while a frame is pending");
		}
		for (auto& page : pages)
		{
			bindless.Release(page.Handle, lastUse);
			retired.push_back({ std::move(page.Texture), std::move(page.View), lastUse });
		}
		pages.clear();
		attached = nullptr;
	}

	std::size_t UiAtlasTextures::Collect()
	{
		// Rewrite completed bindless elements to the fallback before their views go away.
		bindless.Collect();
		const auto before = retired.size();
		std::erase_if(retired,
			[](const Retired& entry)
			{
				return !entry.LastUse.Semaphore || entry.LastUse.Semaphore->GetCompletedValue() >= entry.LastUse.Value;
			});
		return before - retired.size();
	}

	std::size_t UiAtlasTextures::Drain()
	{
		for (const auto& entry : retired)
		{
			if (entry.LastUse.Semaphore && !entry.LastUse.Semaphore->Wait(entry.LastUse.Value))
			{
				throw std::runtime_error(desc.DebugName + " timed out waiting for retiring pages");
			}
		}
		return Collect();
	}

	std::uint32_t UiAtlasTextures::GetSamplerIndex() const
	{
		return bindless.GetIndex(samplerHandle);
	}

	UiAtlasTexturesStats UiAtlasTextures::GetStats() const
	{
		return { static_cast<std::uint32_t>(pages.size()), static_cast<std::uint32_t>(retired.size()), uploadedBytes };
	}
} // namespace Swim::Render
