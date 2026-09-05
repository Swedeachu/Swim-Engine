#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanResourceState.h"

#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanFormatUtils.h"

#include <stdexcept>

namespace Swim::RhiVulkan
{

	VulkanResourceState GetBufferState(const Rhi::BufferDesc& desc, Rhi::ResourceState state)
	{
		VulkanResourceState result{};
		std::uint32_t remaining = static_cast<std::uint32_t>(state);
		const auto add = [&](Rhi::ResourceState flag, Rhi::BufferUsage usage, VkPipelineStageFlags2 stages, VkAccessFlags2 access)
		{
			if (Rhi::HasAny(state, flag))
			{
				if (usage != Rhi::BufferUsage::None && !HasBufferUsage(desc.Usage, usage))
				{
					throw std::invalid_argument("Vulkan buffer state requires matching creation usage");
				}
				remaining &= ~static_cast<std::uint32_t>(flag);
				result.Stages |= stages;
				result.Access |= access;
			}
		};
		add(Rhi::ResourceState::Common, Rhi::BufferUsage::None, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
		add(Rhi::ResourceState::CopySource, Rhi::BufferUsage::TransferSource, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
		add(Rhi::ResourceState::CopyDestination, Rhi::BufferUsage::TransferDestination, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
		add(Rhi::ResourceState::VertexBuffer, Rhi::BufferUsage::Vertex, VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT, VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT);
		add(Rhi::ResourceState::IndexBuffer, Rhi::BufferUsage::Index, VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT, VK_ACCESS_2_INDEX_READ_BIT);
		add(Rhi::ResourceState::UniformBuffer, Rhi::BufferUsage::Uniform, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
		add(Rhi::ResourceState::ShaderRead, Rhi::BufferUsage::Storage, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
		add(Rhi::ResourceState::ShaderWrite, Rhi::BufferUsage::Storage, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
		add(Rhi::ResourceState::IndirectArgument, Rhi::BufferUsage::Indirect, VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
		add(Rhi::ResourceState::HostRead, Rhi::BufferUsage::None, VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
		add(Rhi::ResourceState::HostWrite, Rhi::BufferUsage::None, VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_WRITE_BIT);
		if (remaining != 0 ||
			(Rhi::HasAny(state, Rhi::ResourceState::HostRead) && desc.Memory != Rhi::MemoryPreference::GpuToCpu) ||
			(Rhi::HasAny(state, Rhi::ResourceState::HostWrite) && desc.Memory != Rhi::MemoryPreference::CpuToGpu))
		{
			throw std::invalid_argument("Unsupported Vulkan buffer state or CPU access policy");
		}
		return result;
	}

	VulkanResourceState GetTextureState(const Rhi::TextureDesc& desc, Rhi::ResourceState state)
	{
		Rhi::TextureUsage usage = Rhi::TextureUsage::None;
		VulkanResourceState result{};
		switch (static_cast<std::uint32_t>(state))
		{
		case static_cast<std::uint32_t>(Rhi::ResourceState::Undefined):
			return result;
		case static_cast<std::uint32_t>(Rhi::ResourceState::Common):
			return { VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL };
		case static_cast<std::uint32_t>(Rhi::ResourceState::CopySource):
			usage = Rhi::TextureUsage::TransferSource;
			result = { VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL };
			break;
		case static_cast<std::uint32_t>(Rhi::ResourceState::CopyDestination):
			usage = Rhi::TextureUsage::TransferDestination;
			result = { VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL };
			break;
		case static_cast<std::uint32_t>(Rhi::ResourceState::ShaderRead):
			usage = Rhi::TextureUsage::Sampled;
			result = { VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
				Rhi::IsDepthFormat(desc.PixelFormat) ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
			break;
		case static_cast<std::uint32_t>(Rhi::ResourceState::ShaderWrite):
		case static_cast<std::uint32_t>(Rhi::ResourceState::ShaderRead | Rhi::ResourceState::ShaderWrite):
			usage = Rhi::TextureUsage::Storage;
			result = { VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL };
			break;
		case static_cast<std::uint32_t>(Rhi::ResourceState::ColorAttachment):
			usage = Rhi::TextureUsage::ColorAttachment;
			result = { VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
			break;
		case static_cast<std::uint32_t>(Rhi::ResourceState::DepthStencilRead):
		case static_cast<std::uint32_t>(Rhi::ResourceState::DepthStencilRead | Rhi::ResourceState::ShaderRead):
			usage = Rhi::TextureUsage::DepthStencilAttachment;
			result = { VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
				VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL };
			if (Rhi::HasAny(state, Rhi::ResourceState::ShaderRead))
			{
				usage = usage | Rhi::TextureUsage::Sampled;
				result.Stages |= VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
				result.Access |= VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
			}
			break;
		case static_cast<std::uint32_t>(Rhi::ResourceState::DepthStencilWrite):
		case static_cast<std::uint32_t>(Rhi::ResourceState::DepthStencilRead | Rhi::ResourceState::DepthStencilWrite):
			usage = Rhi::TextureUsage::DepthStencilAttachment;
			result = { VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
				VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
			break;
		case static_cast<std::uint32_t>(Rhi::ResourceState::Present):
			return { VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR };
		default:
			throw std::invalid_argument("Unsupported or incompatible Vulkan texture state combination");
		}
		if ((Rhi::EnumAnd(desc.Usage, usage) != usage) ||
			(Rhi::HasAny(state, Rhi::ResourceState::ColorAttachment) && Rhi::IsDepthFormat(desc.PixelFormat)) ||
			(Rhi::HasAny(state, Rhi::ResourceState::DepthStencilRead | Rhi::ResourceState::DepthStencilWrite) && !Rhi::IsDepthFormat(desc.PixelFormat)))
		{
			throw std::invalid_argument("Vulkan texture state requires matching creation usage and format");
		}
		return result;
	}

} // namespace Swim::RhiVulkan
