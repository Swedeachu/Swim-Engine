#include "Engine/Systems/Renderer/UiRendering/UiRenderSurfaces.h"

#include <bit>
#include <stdexcept>

namespace Swim::Render
{
	namespace
	{
		std::uint32_t FullChain(std::uint32_t width, std::uint32_t height)
		{
			return static_cast<std::uint32_t>(std::bit_width(std::max(width, height)));
		}
	} // namespace

	UiRenderSurfaces::UiRenderSurfaces(Rhi::Device& deviceInput, BindlessResourceTable& table, std::string name)
		: device(deviceInput), bindless(table), debugName(std::move(name))
	{
		Rhi::SamplerDesc samplerDesc{};
		samplerDesc.MinFilter = samplerDesc.MagFilter = Rhi::Filter::Linear;
		samplerDesc.MipFilter = Rhi::Filter::Linear;
		samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = Rhi::SamplerAddressMode::ClampToEdge;
		samplerDesc.DebugName = debugName;
		sampler = device.CreateSampler(samplerDesc);
		if (!sampler)
		{
			throw std::runtime_error(debugName + " sampler could not be created");
		}
		samplerHandle = bindless.RegisterSampler(*sampler);
	}

	UiRenderSurfaces::~UiRenderSurfaces()
	{
		for (auto& surface : surfaces)
		{
			if (surface.Texture)
			{
				bindless.Release(surface.Handle);
			}
		}
		bindless.Release(samplerHandle);
	}

	UiRenderSurfaces::Surface* UiRenderSurfaces::Find(UiRenderSurfaceHandle handle)
	{
		if (!handle || handle.Index >= surfaces.size())
		{
			return nullptr;
		}
		auto& surface = surfaces[handle.Index];
		return surface.Texture && surface.Generation == handle.Generation ? &surface : nullptr;
	}

	const UiRenderSurfaces::Surface* UiRenderSurfaces::Find(UiRenderSurfaceHandle handle) const
	{
		return const_cast<UiRenderSurfaces*>(this)->Find(handle);
	}

	UiRenderSurfaceHandle UiRenderSurfaces::Create(const UiRenderSurfaceDesc& desc)
	{
		if (desc.Width == 0 || desc.Height == 0 || desc.Width > 16384 || desc.Height > 16384 || desc.Format == Rhi::Format::Undefined ||
			Rhi::IsDepthFormat(desc.Format) || desc.MipLevels > FullChain(desc.Width, desc.Height))
		{
			throw std::invalid_argument(debugName + ": a surface needs 1 .. 16384 pixels, a color format and at most a full mip chain");
		}
		Surface surface;
		surface.Desc = desc;
		surface.Mips = desc.MipLevels == 0 ? FullChain(desc.Width, desc.Height) : desc.MipLevels;
		Rhi::TextureDesc textureDesc;
		textureDesc.Extent = { desc.Width, desc.Height, 1 };
		textureDesc.PixelFormat = desc.Format;
		textureDesc.Usage = Rhi::TextureUsage::ColorAttachment | Rhi::TextureUsage::Sampled |
			Rhi::TextureUsage::TransferSource; // Readable for diagnostics.
		textureDesc.MipLevels = surface.Mips;
		textureDesc.DebugName = desc.DebugName;
		surface.Texture = device.CreateTexture(textureDesc);
		if (!surface.Texture)
		{
			throw std::runtime_error(desc.DebugName + " could not be created");
		}
		Rhi::TextureViewDesc viewDesc;
		viewDesc.PixelFormat = desc.Format;
		viewDesc.MipLevelCount = surface.Mips;
		viewDesc.DebugName = desc.DebugName;
		surface.View = device.CreateTextureView(*surface.Texture, viewDesc);
		if (!surface.View)
		{
			throw std::runtime_error(desc.DebugName + " view could not be created");
		}
		const auto handle = bindless.TryRegisterTexture(*surface.View);
		if (!handle)
		{
			throw std::length_error(debugName + ": the bindless table is full");
		}
		surface.Handle = *handle;
		// Reuse a free slot (its generation moves on) or append one.
		for (std::uint32_t index = 0; index < surfaces.size(); ++index)
		{
			if (!surfaces[index].Texture)
			{
				surface.Generation = surfaces[index].Generation + 1;
				surfaces[index] = std::move(surface);
				return { index, surfaces[index].Generation };
			}
		}
		surface.Generation = 1;
		surfaces.push_back(std::move(surface));
		return { static_cast<std::uint32_t>(surfaces.size() - 1), 1 };
	}

	bool UiRenderSurfaces::IsValid(UiRenderSurfaceHandle handle) const
	{
		return Find(handle) != nullptr;
	}

	bool UiRenderSurfaces::Release(UiRenderSurfaceHandle handle, Rhi::TimelinePoint lastUse)
	{
		auto* surface = Find(handle);
		if (!surface)
		{
			return false;
		}
		bindless.Release(surface->Handle, lastUse);
		retired.push_back({ std::move(surface->Texture), std::move(surface->View), lastUse });
		surface->Pending.reset();
		surface->Recorded = false;
		return true;
	}

	std::size_t UiRenderSurfaces::Collect()
	{
		bindless.Collect();
		const auto before = retired.size();
		std::erase_if(retired,
			[](const Retired& entry)
			{
				return !entry.LastUse.Semaphore || entry.LastUse.Semaphore->GetCompletedValue() >= entry.LastUse.Value;
			});
		return before - retired.size();
	}

	std::size_t UiRenderSurfaces::Drain()
	{
		for (const auto& entry : retired)
		{
			if (entry.LastUse.Semaphore && !entry.LastUse.Semaphore->Wait(entry.LastUse.Value))
			{
				throw std::runtime_error(debugName + " timed out waiting for retiring surfaces");
			}
		}
		return Collect();
	}

	UiSurfaceFrame UiRenderSurfaces::Record(RenderGraph& graph, UiRenderSurfaceHandle handle, UiRenderer& renderer,
		const UiRenderProgram& program, Rhi::DescriptorTable& bindlessTable, const UiSurfaceContent& content)
	{
		auto* surface = Find(handle);
		if (!surface)
		{
			throw std::invalid_argument(debugName + ": invalid surface");
		}
		if (surface->Recorded)
		{
			throw std::logic_error(debugName + ": a surface was recorded twice in one frame");
		}
		if (!std::isfinite(content.DpiScale) || content.DpiScale <= 0.0f)
		{
			throw std::invalid_argument(debugName + " needs a positive DPI scale");
		}
		UiSurfaceFrame frame;
		frame.TextureIndex = bindless.GetIndex(surface->Handle);
		frame.SamplerIndex = bindless.GetIndex(samplerHandle);
		frame.MipLevels = surface->Mips;
		frame.Texture =
			graph.ImportTexture(*surface->Texture, surface->Initialized ? Rhi::ResourceState::ShaderRead : Rhi::ResourceState::Undefined);
		graph.Export(frame.Texture, Rhi::ResourceState::ShaderRead);
		surface->Recorded = true;
		const bool changed = content.Force || !surface->Initialized || content.PaintRevision != surface->Revision;
		if (!changed)
		{
			++pendingSkipped;
			return frame;
		}
		for (std::uint32_t mip = 0; mip < surface->Mips; ++mip)
		{
			UiRenderFrame draw;
			draw.Paint = content.Paint;
			draw.Target = frame.Texture;
			draw.TargetMip = mip;
			draw.DpiScale = content.DpiScale / float(1u << mip);
			draw.Composition = content.Composition;
			draw.Atlas = content.Atlas;
			draw.Images = content.Images;
			draw.Clear = true;
			draw.ClearColor = content.ClearColor;
			renderer.Record(graph, draw, program, bindlessTable);
		}
		surface->Pending = content.PaintRevision;
		frame.Drawn = true;
		++pendingDrawn;
		return frame;
	}

	void UiRenderSurfaces::CommitFrame()
	{
		for (auto& surface : surfaces)
		{
			if (surface.Pending)
			{
				surface.Revision = *surface.Pending;
				surface.Initialized = true;
				surface.Pending.reset();
			}
			surface.Recorded = false;
		}
		drawn += pendingDrawn;
		skipped += pendingSkipped;
		pendingDrawn = pendingSkipped = 0;
	}

	void UiRenderSurfaces::AbortFrame()
	{
		for (auto& surface : surfaces)
		{
			surface.Pending.reset();
			surface.Recorded = false;
		}
		pendingDrawn = pendingSkipped = 0;
	}

	UiRenderSurfacesStats UiRenderSurfaces::GetStats() const
	{
		UiRenderSurfacesStats stats;
		for (const auto& surface : surfaces)
		{
			stats.Surfaces += surface.Texture ? 1u : 0u;
		}
		stats.Retiring = static_cast<std::uint32_t>(retired.size());
		stats.DrawnSurfaces = drawn;
		stats.SkippedSurfaces = skipped;
		return stats;
	}

	std::vector<UI::UiPaintQuad> UiRenderSurfaces::PanelPaint(const UiSurfaceFrame& frame, UI::UiPoint canvasSize)
	{
		UI::UiPaintQuad quad;
		quad.Kind = UI::UiPaintKind::Image;
		quad.Bounds = { 0.0f, 0.0f, canvasSize.X, canvasSize.Y };
		quad.Clip = quad.Bounds;
		quad.Color = { 1.0f, 1.0f, 1.0f, 1.0f };
		quad.Uv = { 0.0f, 0.0f, 1.0f, 1.0f };
		quad.Texture = frame.TextureIndex;
		quad.Sampler = frame.SamplerIndex;
		return { quad };
	}
} // namespace Swim::Render
