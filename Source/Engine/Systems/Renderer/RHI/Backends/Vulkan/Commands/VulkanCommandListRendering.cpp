#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Commands/VulkanCommandList.h"

#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanFormatUtils.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanResourceAccess.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanTransferUtils.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Resources/VulkanTextureView.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace Swim::RhiVulkan
{

	namespace
	{

		VkAttachmentLoadOp ToVkLoadOp(Rhi::LoadOp op)
		{
			switch (op)
			{
			case Rhi::LoadOp::Load: return VK_ATTACHMENT_LOAD_OP_LOAD;
			case Rhi::LoadOp::Clear: return VK_ATTACHMENT_LOAD_OP_CLEAR;
			case Rhi::LoadOp::Discard: return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			default: throw std::invalid_argument("Unknown RHI attachment load operation");
			}
		}

		VkAttachmentStoreOp ToVkStoreOp(Rhi::StoreOp op)
		{
			switch (op)
			{
			case Rhi::StoreOp::Store: return VK_ATTACHMENT_STORE_OP_STORE;
			case Rhi::StoreOp::Discard: return VK_ATTACHMENT_STORE_OP_DONT_CARE;
			default: throw std::invalid_argument("Unknown RHI attachment store operation");
			}
		}

	} // namespace

	void VulkanCommandList::BeginRendering(const Rhi::RenderingDesc& desc)
	{
		RequireRecording(true);
		RequireGraphicsQueue();
		const auto& limits = GetState()->Device.physical_device.properties.limits;
		if (desc.RenderArea.Width == 0 || desc.RenderArea.Height == 0 ||
			desc.RenderArea.Width > limits.maxFramebufferWidth || desc.RenderArea.Height > limits.maxFramebufferHeight ||
			desc.ColorAttachments.size() > limits.maxColorAttachments ||
			(desc.ColorAttachments.empty() && desc.DepthStencilAttachment == nullptr))
		{
			throw std::invalid_argument("Vulkan rendering requires attachments and a nonempty supported render area");
		}
		std::uint32_t samples = 0;
		const auto validateView = [&](Rhi::TextureView* view, bool depth)
		{
			if (view == nullptr)
			{
				throw std::invalid_argument("Vulkan rendering attachment view is null");
			}
			RequireResource<VulkanTextureView>(*view, GetState());
			const auto& viewDesc = view->GetDesc();
			const auto& textureDesc = view->GetTexture().GetDesc();
			const auto usage = depth ? Rhi::TextureUsage::DepthStencilAttachment : Rhi::TextureUsage::ColorAttachment;
			if (!HasTextureUsage(textureDesc.Usage, usage) || Rhi::IsDepthFormat(viewDesc.PixelFormat) != depth ||
				viewDesc.MipLevelCount != 1 || viewDesc.BaseMipLevel >= 32 ||
				(viewDesc.Dimension != Rhi::TextureViewDimension::Texture2D && viewDesc.Dimension != Rhi::TextureViewDimension::Texture2DArray) ||
				desc.RenderArea.Width > std::max(1u, textureDesc.Extent.Width >> viewDesc.BaseMipLevel) ||
				desc.RenderArea.Height > std::max(1u, textureDesc.Extent.Height >> viewDesc.BaseMipLevel))
			{
				throw std::invalid_argument("Vulkan attachment usage, format, view or extent is incompatible with rendering");
			}
			const auto attachmentSamples = static_cast<std::uint32_t>(textureDesc.Samples);
			if (samples != 0 && samples != attachmentSamples)
			{
				throw std::invalid_argument("Vulkan rendering attachments must have matching sample counts");
			}
			samples = attachmentSamples;
		};

		std::vector<VkRenderingAttachmentInfo> colors;
		colors.reserve(desc.ColorAttachments.size());
		for (const auto& attachment : desc.ColorAttachments)
		{
			validateView(attachment.View, false);
			if (attachment.Load == Rhi::LoadOp::Clear && IsIntegerColorFormat(attachment.View->GetDesc().PixelFormat))
			{
				throw std::invalid_argument("RHI ClearColor currently supports floating-point and normalized color attachments");
			}
			VkRenderingAttachmentInfo color{};
			color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
			color.imageView = FromNativeHandle<VkImageView>(attachment.View->GetNativeHandle());
			color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			color.loadOp = ToVkLoadOp(attachment.Load);
			color.storeOp = ToVkStoreOp(attachment.Store);
			std::copy(attachment.Clear.Value.begin(), attachment.Clear.Value.end(), color.clearValue.color.float32);
			colors.push_back(color);
		}

		VkRenderingAttachmentInfo depth{};
		bool hasStencil = false;
		if (desc.DepthStencilAttachment != nullptr)
		{
			const auto& attachment = *desc.DepthStencilAttachment;
			validateView(attachment.View, true);
			if (attachment.DepthLoad == Rhi::LoadOp::Clear &&
				(!std::isfinite(attachment.ClearDepth) || attachment.ClearDepth < 0.0f || attachment.ClearDepth > 1.0f))
			{
				throw std::invalid_argument("Vulkan depth clear must be in the zero-to-one depth range");
			}
			depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
			depth.imageView = FromNativeHandle<VkImageView>(attachment.View->GetNativeHandle());
			depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			depth.loadOp = ToVkLoadOp(attachment.DepthLoad);
			depth.storeOp = ToVkStoreOp(attachment.DepthStore);
			depth.clearValue.depthStencil = { attachment.ClearDepth, attachment.ClearStencil };
			hasStencil = Rhi::HasStencil(attachment.View->GetDesc().PixelFormat);
		}

		VkRenderingInfo renderingInfo{};
		renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
		renderingInfo.renderArea.extent = { desc.RenderArea.Width, desc.RenderArea.Height };
		renderingInfo.layerCount = 1;
		renderingInfo.colorAttachmentCount = static_cast<std::uint32_t>(colors.size());
		renderingInfo.pColorAttachments = colors.data();
		renderingInfo.pDepthAttachment = desc.DepthStencilAttachment != nullptr ? &depth : nullptr;
		renderingInfo.pStencilAttachment = hasStencil ? &depth : nullptr;
		GetState()->Dispatch.vkCmdBeginRendering(commandBuffer, &renderingInfo);
		rendering = true;
	}

	void VulkanCommandList::EndRendering()
	{
		RequireRecording();
		if (!rendering)
		{
			throw std::logic_error("Vulkan EndRendering requires an active rendering scope");
		}
		GetState()->Dispatch.vkCmdEndRendering(commandBuffer);
		rendering = false;
	}

	void VulkanCommandList::SetViewport(const Rhi::Viewport& viewport)
	{
		RequireRecording();
		RequireGraphicsQueue();
		const auto& limits = GetState()->Device.physical_device.properties.limits;
		const float right = viewport.X + viewport.Width;
		const float bottom = viewport.Y + viewport.Height;
		if (!std::isfinite(viewport.X) || !std::isfinite(viewport.Y) || !std::isfinite(right) || !std::isfinite(bottom) ||
			!std::isfinite(viewport.Width) || !std::isfinite(viewport.Height) || viewport.Width <= 0.0f || viewport.Height <= 0.0f ||
			viewport.Width > limits.maxViewportDimensions[0] || viewport.Height > limits.maxViewportDimensions[1] ||
			viewport.X < limits.viewportBoundsRange[0] || viewport.Y < limits.viewportBoundsRange[0] ||
			right > limits.viewportBoundsRange[1] || bottom > limits.viewportBoundsRange[1] ||
			!std::isfinite(viewport.MinDepth) || !std::isfinite(viewport.MaxDepth) ||
			viewport.MinDepth < 0.0f || viewport.MinDepth > 1.0f || viewport.MaxDepth < 0.0f || viewport.MaxDepth > 1.0f)
		{
			throw std::invalid_argument("Vulkan viewport exceeds supported bounds or depth range");
		}
		// RHI uses +Y-up NDC. Adapt at the viewport, never in camera projection.
		const VkViewport native{ viewport.X, bottom, viewport.Width, -viewport.Height, viewport.MinDepth, viewport.MaxDepth };
		GetState()->Dispatch.vkCmdSetViewport(commandBuffer, 0, 1, &native);
	}

	void VulkanCommandList::SetScissor(const Rhi::ScissorRect& scissor)
	{
		RequireRecording();
		RequireGraphicsQueue();
		constexpr auto maximum = static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max());
		if (scissor.X < 0 || scissor.Y < 0 || scissor.Width > maximum - static_cast<std::uint32_t>(scissor.X) ||
			scissor.Height > maximum - static_cast<std::uint32_t>(scissor.Y))
		{
			throw std::invalid_argument("Vulkan scissor must have nonnegative offsets and representable bounds");
		}
		const VkRect2D native{ { scissor.X, scissor.Y }, { scissor.Width, scissor.Height } };
		GetState()->Dispatch.vkCmdSetScissor(commandBuffer, 0, 1, &native);
	}

} // namespace Swim::RhiVulkan
