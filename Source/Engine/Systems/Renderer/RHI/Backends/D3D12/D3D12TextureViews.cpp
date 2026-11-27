#include "Engine/Systems/Renderer/RHI/Backends/D3D12/D3D12Objects.h"

#include <algorithm>
#include <stdexcept>

namespace Swim::RhiD3D12
{

	namespace
	{

		bool IsArrayView(Rhi::TextureViewDimension dimension)
		{
			return dimension == Rhi::TextureViewDimension::Texture1DArray || dimension == Rhi::TextureViewDimension::Texture2DArray ||
				dimension == Rhi::TextureViewDimension::TextureCubeArray;
		}

		// The concrete format a view reads or writes: the view's own, else the texture's.
		Rhi::Format ViewFormat(const Rhi::TextureViewDesc& view, const Rhi::TextureDesc& texture)
		{
			return view.PixelFormat == Rhi::Format::Undefined ? texture.PixelFormat : view.PixelFormat;
		}

	} // namespace

	D3D12TextureView::D3D12TextureView(
		std::shared_ptr<D3D12DeviceState> stateValue, D3D12Texture& textureValue, const Rhi::TextureViewDesc& descValue)
		: state(std::move(stateValue)), texture(textureValue), desc(descValue)
	{
		desc.DebugName = {};
		const auto& textureDesc = texture.GetDesc();
		desc.PixelFormat = ViewFormat(desc, textureDesc);
		const auto range = GetRange();
		const bool color = Rhi::EnumAnd(textureDesc.Usage, Rhi::TextureUsage::ColorAttachment) != Rhi::TextureUsage::None;
		const bool depth = Rhi::EnumAnd(textureDesc.Usage, Rhi::TextureUsage::DepthStencilAttachment) != Rhi::TextureUsage::None;
		const bool array = IsArrayView(desc.Dimension) || range.ArrayLayerCount > 1 || range.BaseArrayLayer > 0 ||
			desc.Dimension == Rhi::TextureViewDimension::TextureCube;

		if (color)
		{
			D3D12_RENDER_TARGET_VIEW_DESC rtv{};
			rtv.Format = ToDxgiFormat(desc.PixelFormat);

			if (textureDesc.Dimension == Rhi::TextureDimension::Texture3D)
			{
				rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
				rtv.Texture3D = { range.BaseMipLevel, range.BaseArrayLayer, range.ArrayLayerCount };
			}
			else if (textureDesc.Samples != Rhi::SampleCount::X1)
			{
				rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
			}
			else if (array)
			{
				rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
				rtv.Texture2DArray = { range.BaseMipLevel, range.BaseArrayLayer, range.ArrayLayerCount, 0 };
			}
			else
			{
				rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
				rtv.Texture2D = { range.BaseMipLevel, 0 };
			}

			renderTarget = state->RenderTargets->Allocate();
			state->Device->CreateRenderTargetView(texture.GetResource(), &rtv, state->RenderTargets->Cpu(renderTarget));
		}

		if (depth)
		{
			D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
			dsv.Format = ToDxgiFormat(textureDesc.PixelFormat);

			if (textureDesc.Samples != Rhi::SampleCount::X1)
			{
				dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMS;
			}
			else if (array)
			{
				dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
				dsv.Texture2DArray = { range.BaseMipLevel, range.BaseArrayLayer, range.ArrayLayerCount };
			}
			else
			{
				dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
				dsv.Texture2D = { range.BaseMipLevel };
			}

			depthStencil = state->DepthStencils->Allocate(2);
			state->Device->CreateDepthStencilView(texture.GetResource(), &dsv, state->DepthStencils->Cpu(depthStencil));
			dsv.Flags = D3D12_DSV_FLAG_READ_ONLY_DEPTH |
				(Rhi::HasStencil(textureDesc.PixelFormat) ? D3D12_DSV_FLAG_READ_ONLY_STENCIL : D3D12_DSV_FLAG_NONE);
			state->Device->CreateDepthStencilView(texture.GetResource(), &dsv, state->DepthStencils->Cpu(depthStencil + 1));
		}
	}

	D3D12TextureView::~D3D12TextureView()
	{
		state->RenderTargets->Free(renderTarget);
		state->DepthStencils->Free(depthStencil, 2);
	}

	Rhi::TextureSubresourceRange D3D12TextureView::GetRange() const
	{
		const auto& textureDesc = texture.GetDesc();
		const auto layers = textureDesc.Dimension == Rhi::TextureDimension::Texture3D ? textureDesc.Extent.Depth : textureDesc.ArrayLayers;
		Rhi::TextureSubresourceRange range;
		range.BaseMipLevel = desc.BaseMipLevel;
		range.MipLevelCount = std::min(desc.MipLevelCount, textureDesc.MipLevels - std::min(desc.BaseMipLevel, textureDesc.MipLevels));
		range.BaseArrayLayer = desc.BaseArrayLayer;
		range.ArrayLayerCount = std::min(desc.ArrayLayerCount, layers - std::min(desc.BaseArrayLayer, layers));
		return range;
	}

	D3D12_CPU_DESCRIPTOR_HANDLE D3D12TextureView::GetRenderTarget() const
	{
		if (renderTarget == D3D12DescriptorHeap::Invalid)
		{
			throw std::logic_error("D3D12: the view's texture has no ColorAttachment usage");
		}

		return state->RenderTargets->Cpu(renderTarget);
	}

	D3D12_CPU_DESCRIPTOR_HANDLE D3D12TextureView::GetDepthStencil(bool readOnly) const
	{
		if (depthStencil == D3D12DescriptorHeap::Invalid)
		{
			throw std::logic_error("D3D12: the view's texture has no DepthStencilAttachment usage");
		}

		return state->DepthStencils->Cpu(depthStencil + (readOnly ? 1u : 0u));
	}

	void D3D12TextureView::WriteShaderResource(D3D12_CPU_DESCRIPTOR_HANDLE destination) const
	{
		const auto& textureDesc = texture.GetDesc();
		const auto range = GetRange();
		D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
		srv.Format = Rhi::IsDepthFormat(desc.PixelFormat) ? ToDepthReadFormat(desc.PixelFormat) : ToDxgiFormat(desc.PixelFormat);
		srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		const auto mips = range.MipLevelCount;
		// Single-layer and cube views have no first-slice field: a view of a later layer (or of
		// any layer of an array texture) is written as the array dimension starting there, which
		// the non-array shader declaration reads as its first layer.
		const bool sliced = range.BaseArrayLayer != 0 || textureDesc.ArrayLayers > 1;

		switch (desc.Dimension)
		{
		case Rhi::TextureViewDimension::Texture1D:
			if (sliced)
			{
				srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
				srv.Texture1DArray = { range.BaseMipLevel, mips, range.BaseArrayLayer, 1, 0.0f };
				break;
			}

			srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
			srv.Texture1D = { range.BaseMipLevel, mips, 0.0f };
			break;
		case Rhi::TextureViewDimension::Texture1DArray:
			srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
			srv.Texture1DArray = { range.BaseMipLevel, mips, range.BaseArrayLayer, range.ArrayLayerCount, 0.0f };
			break;
		case Rhi::TextureViewDimension::Texture2DArray:
			srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
			srv.Texture2DArray = { range.BaseMipLevel, mips, range.BaseArrayLayer, range.ArrayLayerCount, 0, 0.0f };
			break;
		case Rhi::TextureViewDimension::Texture3D:
			srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
			srv.Texture3D = { range.BaseMipLevel, mips, 0.0f };
			break;
		case Rhi::TextureViewDimension::TextureCube:
			if (range.BaseArrayLayer != 0)
			{
				srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
				srv.TextureCubeArray = { range.BaseMipLevel, mips, range.BaseArrayLayer, 1, 0.0f };
				break;
			}

			srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
			srv.TextureCube = { range.BaseMipLevel, mips, 0.0f };
			break;
		case Rhi::TextureViewDimension::TextureCubeArray:
			srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
			srv.TextureCubeArray = { range.BaseMipLevel, mips, range.BaseArrayLayer, range.ArrayLayerCount / 6, 0.0f };
			break;
		default:
			if (textureDesc.Samples != Rhi::SampleCount::X1)
			{
				srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
			}
			else if (sliced)
			{
				srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
				srv.Texture2DArray = { range.BaseMipLevel, mips, range.BaseArrayLayer, 1, 0, 0.0f };
			}
			else
			{
				srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
				srv.Texture2D = { range.BaseMipLevel, mips, 0, 0.0f };
			}

			break;
		}

		state->Device->CreateShaderResourceView(texture.GetResource(), &srv, destination);
	}

	void D3D12TextureView::WriteUnorderedAccess(D3D12_CPU_DESCRIPTOR_HANDLE destination) const
	{
		const auto range = GetRange();
		D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
		uav.Format = ToDxgiFormat(desc.PixelFormat);

		if (texture.GetDesc().ArrayLayers > 1 || range.BaseArrayLayer != 0)
		{
			uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
			uav.Texture2DArray = { range.BaseMipLevel, range.BaseArrayLayer, 1, 0 };
		}
		else
		{
			uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
			uav.Texture2D = { range.BaseMipLevel, 0 };
		}

		state->Device->CreateUnorderedAccessView(texture.GetResource(), nullptr, &uav, destination);
	}

} // namespace Swim::RhiD3D12
