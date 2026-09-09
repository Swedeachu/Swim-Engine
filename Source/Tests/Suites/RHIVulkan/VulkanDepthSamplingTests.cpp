#include "Tests/Fixtures/VulkanStorageTextureCapture.h"
#include "Tests/Framework/Test.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanDevice.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Descriptors/VulkanDescriptorImages.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanResourceState.h"

using namespace Swim;

namespace
{
	VkFormatFeatureFlags2 depthFeatures = 0;

	void EnableDepth(Testing::VulkanDescriptorCapture& capture)
	{
		depthFeatures = VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_DEPTH_COMPARISON_BIT;
		capture.State->Instance->Dispatch.vkGetPhysicalDeviceFormatProperties2 = +[](VkPhysicalDevice, VkFormat, VkFormatProperties2* properties)
		{
			static_cast<VkFormatProperties3*>(properties->pNext)->optimalTilingFeatures = depthFeatures;
		};
	}

	Rhi::TextureDesc DepthDesc(Rhi::Format format = Rhi::Format::D24UnormS8Uint)
	{
		Rhi::TextureDesc desc{};
		desc.PixelFormat = format;
		desc.Usage = Rhi::TextureUsage::DepthStencilAttachment | Rhi::TextureUsage::Sampled;
		desc.Extent = { 8, 8, 1 };
		desc.MipLevels = 2;
		desc.ArrayLayers = 2;
		return desc;
	}
}

SWIM_TEST("RHI.Vulkan.DepthSampling", "DepthAspectViewsDescriptorsAndAttachmentTransitionsAgree")
{
	Testing::VulkanDescriptorCapture capture;
	EnableDepth(capture);
	static VkImageAspectFlags nativeAspect = 0;
	capture.State->Dispatch.vkCreateImageView = +[](VkDevice, const VkImageViewCreateInfo* info,
		const VkAllocationCallbacks*, VkImageView* view) -> VkResult
	{
		nativeAspect = info->subresourceRange.aspectMask;
		*view = RhiVulkan::FromNativeHandle<VkImageView>(1);
		return VK_SUCCESS;
	};
	capture.State->Dispatch.vkDestroyImageView = +[](VkDevice, VkImageView, const VkAllocationCallbacks*) {};
	RhiVulkan::VulkanDevice device(capture.State, {}, nullptr, nullptr, nullptr);
	capture.Commands->Begin();
	for (const auto format : { Rhi::Format::D16Unorm, Rhi::Format::D32Float, Rhi::Format::D24UnormS8Uint, Rhi::Format::D32FloatS8Uint })
	{
		RhiVulkan::VulkanTexture texture(capture.State, RhiVulkan::FromNativeHandle<VkImage>(1), DepthDesc(format));
		Rhi::TextureViewDesc desc{};
		desc.BaseMipLevel = desc.BaseArrayLayer = 1;
		desc.Aspect = Rhi::TextureAspect::Depth;
		auto view = device.CreateTextureView(texture, desc);
		SWIM_REQUIRE(view);
		SWIM_CHECK_EQUAL(nativeAspect, static_cast<VkImageAspectFlags>(VK_IMAGE_ASPECT_DEPTH_BIT));
		Rhi::DescriptorWrite write{};
		write.TextureResource = view.get();
		Rhi::DescriptorBindingDesc binding{};
		const auto image = RhiVulkan::BuildVulkanImageDescriptor(capture.State, binding, write);
		SWIM_CHECK_EQUAL(image.imageLayout, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
		binding.SampledClass = Rhi::SampledTextureClass::Uint;
		SWIM_CHECK_THROWS(RhiVulkan::BuildVulkanImageDescriptor(capture.State, binding, write), std::invalid_argument);
		desc.Aspect = Rhi::TextureAspect::Color;
		SWIM_CHECK(!device.CreateTextureView(texture, desc));
		desc.Aspect = Rhi::TextureAspect::Automatic;
		auto attachment = device.CreateTextureView(texture, desc);
		SWIM_REQUIRE(attachment);
		capture.Commands->Transition(texture, Rhi::ResourceState::Undefined, Rhi::ResourceState::DepthStencilWrite, { 1, 1, 1, 1 });
		Rhi::DepthStencilAttachmentDesc depth{ attachment.get(), Rhi::LoadOp::Clear, Rhi::StoreOp::Store, 0.5f, 0 };
		capture.Commands->BeginRendering({ {}, &depth, { 4, 4 } });
		capture.Commands->EndRendering();
		capture.Commands->Transition(texture, Rhi::ResourceState::DepthStencilWrite, Rhi::ResourceState::ShaderRead, { 1, 1, 1, 1 });
		SWIM_CHECK_EQUAL(capture.Images.back().newLayout, image.imageLayout);
		SWIM_CHECK_EQUAL(capture.Images.back().subresourceRange.aspectMask, nativeAspect);
		if (Rhi::HasStencil(format))
		{
			depth.View = view.get();
			SWIM_CHECK_THROWS(capture.Commands->BeginRendering({ {}, &depth, { 4, 4 } }), std::invalid_argument);
			write.TextureResource = attachment.get();
			binding.SampledClass = Rhi::SampledTextureClass::Float;
			SWIM_CHECK_THROWS(RhiVulkan::BuildVulkanImageDescriptor(capture.State, binding, write), std::invalid_argument);
		}
	}
	capture.Commands->End();
}

SWIM_TEST("RHI.Vulkan.DepthSampling", "ComparisonSamplersMapAllOperatorsAndInvalidValuesReject")
{
	Testing::VulkanDescriptorCapture capture;
	const std::array operations{ Rhi::CompareOp::Never, Rhi::CompareOp::Less, Rhi::CompareOp::Equal, Rhi::CompareOp::LessEqual,
		Rhi::CompareOp::Greater, Rhi::CompareOp::NotEqual, Rhi::CompareOp::GreaterEqual, Rhi::CompareOp::Always };
	for (std::uint32_t index = 0; index < operations.size(); ++index)
	{
		Rhi::SamplerDesc desc{};
		desc.EnableComparison = true;
		desc.Comparison = operations[index];
		desc.MinFilter = desc.MagFilter = desc.MipFilter = Rhi::Filter::Nearest;
		auto sampler = RhiVulkan::VulkanSampler::Create(capture.State, desc);
		SWIM_REQUIRE(sampler);
		SWIM_CHECK_EQUAL(capture.SamplerInfo.compareEnable, VK_TRUE);
		SWIM_CHECK_EQUAL(capture.SamplerInfo.compareOp, static_cast<VkCompareOp>(index));
	}
	Rhi::SamplerDesc invalid{};
	invalid.EnableComparison = true;
	invalid.Comparison = static_cast<Rhi::CompareOp>(255);
	SWIM_CHECK(!RhiVulkan::VulkanSampler::Create(capture.State, invalid));
	SWIM_CHECK_EQUAL(capture.SamplersDestroyed, capture.SamplersCreated);
}

SWIM_TEST("RHI.Vulkan.DepthSampling", "MissingComparisonFeatureRejectsAllocationAndDescriptorBatch")
{
	Testing::VulkanStorageTextureCapture capture;
	EnableDepth(capture);
	SWIM_CHECK(RhiVulkan::ValidateVulkanSampledDepthTexture(*capture.State, DepthDesc()));
	capture.ImageResult = VK_ERROR_FORMAT_NOT_SUPPORTED;
	SWIM_CHECK(!RhiVulkan::ValidateVulkanSampledDepthTexture(*capture.State, DepthDesc()));
	capture.ImageResult = VK_SUCCESS;
	capture.ImageProperties.maxMipLevels = 1;
	SWIM_CHECK(!RhiVulkan::ValidateVulkanSampledDepthTexture(*capture.State, DepthDesc()));
	Rhi::DescriptorSchemaDesc schema{ 0, { { 0, Rhi::DescriptorType::SampledTexture, 2, Rhi::ShaderStageMask::Compute } } };
	auto program = capture.MakeComputeProgram({ { &schema, 1 }, {} });
	auto layout = RhiVulkan::VulkanPipelineLayout::Create(capture.State, { program.get(), {} });
	SWIM_REQUIRE(layout);
	auto table = RhiVulkan::VulkanDescriptorTable::Create(capture.State, { layout.get(), 0, 0, {} });
	SWIM_REQUIRE(table);
	RhiVulkan::VulkanTexture texture(capture.State, VK_NULL_HANDLE, DepthDesc());
	Rhi::TextureViewDesc viewDesc{};
	viewDesc.PixelFormat = Rhi::Format::D24UnormS8Uint;
	viewDesc.Aspect = Rhi::TextureAspect::Depth;
	RhiVulkan::VulkanTextureView depth(capture.State, texture, RhiVulkan::FromNativeHandle<VkImageView>(1), viewDesc);
	viewDesc.Aspect = Rhi::TextureAspect::Stencil;
	RhiVulkan::VulkanTextureView stencil(capture.State, texture, RhiVulkan::FromNativeHandle<VkImageView>(2), viewDesc);
	std::array<Rhi::DescriptorWrite, 2> writes{};
	writes[0].TextureResource = &depth;
	writes[1].TextureResource = &stencil;
	writes[1].ArrayIndex = 1;
	SWIM_CHECK_THROWS(table->Write(writes), std::invalid_argument);
	SWIM_CHECK_EQUAL(capture.Updates, 0u);
	SWIM_CHECK(!table->IsComplete());
	depthFeatures = VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT;
	SWIM_CHECK(!RhiVulkan::ValidateVulkanSampledDepthTexture(*capture.State, DepthDesc()));
	writes[1].TextureResource = &depth;
	SWIM_CHECK_THROWS(table->Write(writes), std::invalid_argument);
	SWIM_CHECK_EQUAL(capture.Updates, 0u);
	EnableDepth(capture);
	table->Write(writes);
	SWIM_CHECK(table->IsComplete());
	SWIM_CHECK_EQUAL(capture.ImagesWritten[0].imageLayout, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
}

SWIM_TEST("RHI.Vulkan.DepthSampling", "SampledOnlyDepthUsesShaderReadLayoutWithoutAttachmentUsage")
{
	Testing::VulkanDescriptorCapture capture;
	EnableDepth(capture);
	auto desc = DepthDesc(Rhi::Format::D32Float);
	desc.Usage = Rhi::TextureUsage::Sampled;
	RhiVulkan::VulkanTexture texture(capture.State, VK_NULL_HANDLE, desc);
	Rhi::TextureViewDesc viewDesc{};
	viewDesc.PixelFormat = desc.PixelFormat;
	RhiVulkan::VulkanTextureView view(capture.State, texture, RhiVulkan::FromNativeHandle<VkImageView>(1), viewDesc);
	Rhi::DescriptorWrite write{};
	write.TextureResource = &view;
	const auto image = RhiVulkan::BuildVulkanImageDescriptor(capture.State, {}, write);
	SWIM_CHECK_EQUAL(image.imageLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	SWIM_CHECK_EQUAL(RhiVulkan::GetTextureState(desc, Rhi::ResourceState::ShaderRead).Layout, image.imageLayout);
}
