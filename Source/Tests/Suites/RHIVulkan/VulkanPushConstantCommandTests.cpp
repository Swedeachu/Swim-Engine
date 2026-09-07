#include "Tests/Fixtures/VulkanPipelineCapture.h"
#include "Tests/Framework/Test.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Resources/VulkanBuffer.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Resources/VulkanTextureView.h"

using namespace Swim;

namespace
{
	constexpr auto both = Rhi::ShaderStageMask::Vertex | Rhi::ShaderStageMask::Fragment;

	struct PushConstantCapture : Testing::VulkanPipelineCapture
	{
		RhiVulkan::VulkanTexture Target{ State, VK_NULL_HANDLE, { Rhi::TextureDimension::Texture2D, { 16, 16, 1 },
			Rhi::Format::RGBA8Unorm, Rhi::TextureUsage::ColorAttachment } };
		RhiVulkan::VulkanTextureView View{ State, Target, VK_NULL_HANDLE,
			{ Rhi::TextureViewDimension::Texture2D, Rhi::Format::RGBA8Unorm } };
		Rhi::RenderingAttachmentDesc Attachment{};
		std::array<std::byte, 32> Data{};

		PushConstantCapture()
		{
			Attachment.View = &View;
		}

		auto Pipeline(std::span<const Rhi::PushConstantRange> ranges)
		{
			return MakePipeline(Rhi::Format::RGBA8Unorm, {}, {}, { {}, ranges });
		}

		void BeginDraw(Rhi::GraphicsPipeline& pipeline)
		{
			Commands->Begin();
			Commands->BindGraphicsPipeline(pipeline);
			Commands->BeginRendering({ { &Attachment, 1 }, nullptr, { 16, 16 } });
			Commands->SetViewport({ 0, 0, 16, 16 });
			Commands->SetScissor({ 0, 0, 16, 16 });
		}
	};
}

SWIM_TEST("RHI.Vulkan.PushConstantCommand", "UpdatesCopyBytesWithNonzeroOffsetAndUnalignedHostPointer")
{
	PushConstantCapture capture;
	const Rhi::PushConstantRange range{ 8, 16, both };
	auto pipeline = capture.Pipeline({ &range, 1 });
	SWIM_REQUIRE(pipeline);
	capture.Commands->Begin();
	capture.Commands->BindGraphicsPipeline(*pipeline);
	capture.Data[1] = std::byte{ 17 };
	capture.Commands->PushConstants(both, 8, { capture.Data.data() + 1, 16 });
	capture.Data[1] = std::byte{ 34 };
	capture.Commands->PushConstants(both, 12, { capture.Data.data() + 1, 4 });
	SWIM_REQUIRE_EQUAL(capture.PushConstantWrites.size(), 2u);
	SWIM_CHECK_EQUAL(capture.PushConstantWrites[0].Bytes[0], std::byte{ 17 });
	SWIM_CHECK_EQUAL(capture.PushConstantWrites[1].Bytes[0], std::byte{ 34 });
	SWIM_CHECK_EQUAL(capture.PushConstantWrites[0].Bytes.size(), 16u);
	SWIM_CHECK_EQUAL(capture.PushConstantWrites[0].Stages, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
	SWIM_CHECK_EQUAL(capture.PushConstantWrites[0].Offset, 8u);
	SWIM_CHECK_EQUAL(capture.PushConstantWrites[1].Offset, 12u);
	SWIM_CHECK_EQUAL(capture.PushConstantWrites[0].Layout, pipeline->GetLayoutState()->Layout);
}

SWIM_TEST("RHI.Vulkan.PushConstantCommand", "BadArgumentsNeverRecordOrReplaceInitializedState")
{
	PushConstantCapture capture;
	const Rhi::PushConstantRange range{ 0, 32, both };
	auto pipeline = capture.Pipeline({ &range, 1 });
	SWIM_REQUIRE(pipeline);
	SWIM_CHECK_THROWS(capture.Commands->PushConstants(both, 0, capture.Data), std::logic_error);
	capture.Commands->Begin();
	SWIM_CHECK_THROWS(capture.Commands->PushConstants(both, 0, capture.Data), std::logic_error);
	capture.Commands->BindGraphicsPipeline(*pipeline);
	capture.Commands->PushConstants(both, 0, capture.Data);
	SWIM_CHECK_THROWS(capture.Commands->PushConstants(both, 0, {}), std::invalid_argument);
	SWIM_CHECK_THROWS(capture.Commands->PushConstants(both, 2, capture.Data), std::invalid_argument);
	SWIM_CHECK_THROWS(capture.Commands->PushConstants(both, 0, { capture.Data.data(), 3 }), std::invalid_argument);
	SWIM_CHECK_THROWS(capture.Commands->PushConstants(both, 4, capture.Data), std::invalid_argument);
	SWIM_CHECK_THROWS(capture.Commands->PushConstants(both, UINT32_MAX - 3, capture.Data), std::invalid_argument);
	SWIM_CHECK_THROWS(capture.Commands->PushConstants(Rhi::ShaderStageMask::None, 0, capture.Data), std::invalid_argument);
	SWIM_CHECK_THROWS(capture.Commands->PushConstants(Rhi::ShaderStageMask::Compute, 0, capture.Data), std::invalid_argument);
	SWIM_CHECK_THROWS(capture.Commands->PushConstants(Rhi::ShaderStageMask::Vertex, 0, capture.Data), std::invalid_argument);
	SWIM_CHECK_EQUAL(capture.PushConstantWrites.size(), 1u);
	capture.Commands->BeginRendering({ { &capture.Attachment, 1 }, nullptr, { 16, 16 } });
	capture.Commands->SetViewport({ 0, 0, 16, 16 });
	capture.Commands->SetScissor({ 0, 0, 16, 16 });
	capture.Commands->Draw(3, 1, 0, 0);
	SWIM_CHECK_EQUAL(capture.DrawCount, 1u);
}

SWIM_TEST("RHI.Vulkan.PushConstantCommand", "PartialUpdatesMustInitializeEveryReflectedWordBeforeAnyDraw")
{
	PushConstantCapture capture;
	const Rhi::PushConstantRange range{ 0, 32, both };
	auto pipeline = capture.Pipeline({ &range, 1 });
	SWIM_REQUIRE(pipeline);
	capture.BeginDraw(*pipeline);
	SWIM_CHECK_THROWS(capture.Commands->Draw(3, 1, 0, 0), std::logic_error);
	capture.Commands->PushConstants(both, 0, { capture.Data.data(), 16 });
	SWIM_CHECK_THROWS(capture.Commands->Draw(0, 0, 0, 0), std::logic_error);
	capture.Commands->PushConstants(both, 20, { capture.Data.data(), 12 });
	SWIM_CHECK_THROWS(capture.Commands->Draw(3, 1, 0, 0), std::logic_error);
	capture.Commands->PushConstants(both, 16, { capture.Data.data(), 4 });
	capture.Commands->Draw(3, 1, 0, 0);
	RhiVulkan::VulkanBuffer indices(capture.State, VK_NULL_HANDLE, nullptr, { 6, Rhi::BufferUsage::Index });
	capture.Commands->BindIndexBuffer(indices, 0, Rhi::IndexType::Uint16);
	capture.Commands->DrawIndexed(3, 1, 0, 0, 0);
	SWIM_CHECK_EQUAL(capture.DrawCount, 1u);
	SWIM_CHECK_EQUAL(capture.IndexedDrawCount, 1u);
}

SWIM_TEST("RHI.Vulkan.PushConstantCommand", "OverlappingRangesRequireAllStagesAndDisjointTailsUpdateSeparately")
{
	PushConstantCapture capture;
	const std::array<Rhi::PushConstantRange, 2> ranges{{
		{ 0, 16, Rhi::ShaderStageMask::Vertex }, { 8, 16, Rhi::ShaderStageMask::Fragment }
	}};
	auto pipeline = capture.Pipeline(ranges);
	SWIM_REQUIRE(pipeline);
	capture.BeginDraw(*pipeline);
	const auto bytes = std::span(capture.Data).first(8);
	SWIM_CHECK_THROWS(capture.Commands->PushConstants(both, 0, std::span(capture.Data).first(24)), std::invalid_argument);
	SWIM_CHECK_THROWS(capture.Commands->PushConstants(Rhi::ShaderStageMask::Vertex, 8, bytes), std::invalid_argument);
	capture.Commands->PushConstants(Rhi::ShaderStageMask::Vertex, 0, bytes);
	capture.Commands->PushConstants(Rhi::ShaderStageMask::Fragment, 16, bytes);
	SWIM_CHECK_THROWS(capture.Commands->Draw(3, 1, 0, 0), std::logic_error);
	capture.Commands->PushConstants(both, 8, bytes);
	capture.Commands->Draw(3, 1, 0, 0);
	SWIM_CHECK_EQUAL(capture.PushConstantWrites.size(), 3u);
	SWIM_CHECK_EQUAL(capture.DrawCount, 1u);
}

SWIM_TEST("RHI.Vulkan.PushConstantCommand", "CompatibleLayoutsPreserveValuesAndIncompatiblePushRequiresReinitialization")
{
	PushConstantCapture capture;
	const std::array<Rhi::PushConstantRange, 2> ranges{{
		{ 0, 16, Rhi::ShaderStageMask::Vertex }, { 16, 16, Rhi::ShaderStageMask::Fragment }
	}};
	const std::array<Rhi::PushConstantRange, 2> reordered{ ranges[1], ranges[0] };
	const Rhi::PushConstantRange incompatibleRange{ 0, 32, both };
	auto first = capture.Pipeline(ranges);
	auto compatible = capture.Pipeline(reordered);
	auto incompatible = capture.Pipeline({ &incompatibleRange, 1 });
	auto empty = capture.MakePipeline();
	SWIM_REQUIRE(first && compatible && incompatible && empty);
	capture.BeginDraw(*first);
	capture.Commands->PushConstants(Rhi::ShaderStageMask::Vertex, 0, std::span(capture.Data).first(16));
	capture.Commands->PushConstants(Rhi::ShaderStageMask::Fragment, 16, std::span(capture.Data).first(16));
	capture.Commands->BindGraphicsPipeline(*compatible);
	capture.Commands->Draw(3, 1, 0, 0);
	capture.Commands->BindGraphicsPipeline(*incompatible);
	SWIM_CHECK_THROWS(capture.Commands->Draw(3, 1, 0, 0), std::logic_error);
	capture.Commands->BindGraphicsPipeline(*empty);
	SWIM_CHECK_THROWS(capture.Commands->PushConstants(both, 0, capture.Data), std::invalid_argument);
	capture.Commands->Draw(3, 1, 0, 0);
	capture.Commands->BindGraphicsPipeline(*first); // Binding alone never disturbs constants.
	capture.Commands->Draw(3, 1, 0, 0);
	capture.Commands->BindGraphicsPipeline(*incompatible);
	capture.Commands->PushConstants(both, 0, std::span(capture.Data).first(16));
	SWIM_CHECK_THROWS(capture.Commands->Draw(3, 1, 0, 0), std::logic_error);
	capture.Commands->PushConstants(both, 16, std::span(capture.Data).first(16));
	capture.Commands->Draw(3, 1, 0, 0);
	capture.Commands->BindGraphicsPipeline(*first);
	SWIM_CHECK_THROWS(capture.Commands->Draw(3, 1, 0, 0), std::logic_error);
	SWIM_CHECK_EQUAL(capture.DrawCount, 4u);
}

SWIM_TEST("RHI.Vulkan.PushConstantCommand", "RenderingScopesPreserveValuesAndPoolReuseClearsThem")
{
	PushConstantCapture capture;
	const Rhi::PushConstantRange range{ 0, 32, both };
	auto pipeline = capture.Pipeline({ &range, 1 });
	SWIM_REQUIRE(pipeline);
	capture.BeginDraw(*pipeline);
	capture.Commands->PushConstants(both, 0, capture.Data);
	capture.Commands->EndRendering();
	capture.Commands->BeginRendering({ { &capture.Attachment, 1 }, nullptr, { 16, 16 } });
	capture.Commands->Draw(3, 1, 0, 0);
	capture.Commands->EndRendering();
	capture.Commands->End();
	SWIM_CHECK_THROWS(capture.Commands->PushConstants(both, 0, capture.Data), std::logic_error);
	++capture.Pool->Generation;
	capture.BeginDraw(*pipeline);
	SWIM_CHECK_THROWS(capture.Commands->Draw(3, 1, 0, 0), std::logic_error);
	capture.Commands->PushConstants(both, 0, capture.Data);
	capture.Commands->Draw(3, 1, 0, 0);
	SWIM_CHECK_EQUAL(capture.DrawCount, 2u);
}

SWIM_TEST("RHI.Vulkan.PushConstantCommand", "WrongQueueStaleGenerationAndDeviceLossPreventNativeWrites")
{
	PushConstantCapture capture;
	const Rhi::PushConstantRange range{ 0, 32, both };
	auto pipeline = capture.Pipeline({ &range, 1 });
	SWIM_REQUIRE(pipeline);
	capture.BeginDraw(*pipeline);
	capture.Pool->FamilyIndex = 1;
	SWIM_CHECK_THROWS(capture.Commands->PushConstants(both, 0, capture.Data), std::logic_error);
	capture.Pool->FamilyIndex = 0;
	++capture.Pool->Generation;
	SWIM_CHECK_THROWS(capture.Commands->PushConstants(both, 0, capture.Data), std::logic_error);
	capture.BeginDraw(*pipeline);
	capture.State->Diagnostics->TryRecordLoss("push constants test", VK_ERROR_DEVICE_LOST);
	SWIM_CHECK_THROWS(capture.Commands->PushConstants(both, 0, capture.Data), Rhi::DeviceLostError);
	SWIM_CHECK(capture.PushConstantWrites.empty());
}
