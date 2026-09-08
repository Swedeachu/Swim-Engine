#include "Tests/Fixtures/VulkanComputeCapture.h"
#include "Tests/Framework/Test.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Resources/VulkanBuffer.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Resources/VulkanTextureView.h"

using namespace Swim;

namespace
{
	struct ComputeCommands : Testing::VulkanComputeCapture
	{
		Rhi::DescriptorSchemaDesc Schema{ 1, { { 7, Rhi::DescriptorType::StorageBuffer, 1, Rhi::ShaderStageMask::Compute } } };
		Rhi::PushConstantRange Range{ 0, 16, Rhi::ShaderStageMask::Compute };
		std::unique_ptr<RhiVulkan::VulkanShaderProgram> Program;
		std::unique_ptr<RhiVulkan::VulkanPipelineLayout> Layout;
		std::unique_ptr<RhiVulkan::VulkanComputePipeline> Pipeline;
		std::unique_ptr<RhiVulkan::VulkanDescriptorTable> Table;
		RhiVulkan::VulkanBuffer Buffer{ State, RhiVulkan::FromNativeHandle<VkBuffer>(1), nullptr, { 128,
			Rhi::BufferUsage::Storage | Rhi::BufferUsage::TransferSource | Rhi::BufferUsage::TransferDestination } };
		std::array<std::byte, 16> Data{};

		ComputeCommands()
		{
			Program = MakeComputeProgram({ { &Schema, 1 }, { &Range, 1 } });
			Layout = RhiVulkan::VulkanPipelineLayout::Create(State, { Program.get(), {} });
			Pipeline = RhiVulkan::VulkanComputePipeline::Create(State, { Program.get(), Layout.get(), {}, {} });
			Table = RhiVulkan::VulkanDescriptorTable::Create(State, { Layout.get(), 1 });
		}

		void WriteTable()
		{
			Rhi::DescriptorWrite write{};
			write.Binding = 7;
			write.BufferResource = &Buffer;
			write.BufferOffset = 16;
			write.BufferRange = 64;
			Table->Write({ &write, 1 });
		}

		void Bind()
		{
			Commands->BindComputePipeline(*Pipeline);
			Commands->BindDescriptorTable(1, *Table);
			Commands->PushConstants(Rhi::ShaderStageMask::Compute, 0, Data);
		}
	};
}

SWIM_TEST("RHI.Vulkan.ComputeCommand", "RequiresCompleteBindingsAndConstantsAndForwardsComputeBindPoint")
{
	ComputeCommands capture;
	SWIM_REQUIRE(capture.Pipeline && capture.Table);
	SWIM_CHECK_THROWS(capture.Commands->Dispatch(1, 1, 1), std::logic_error);
	capture.Commands->Begin();
	SWIM_CHECK_THROWS(capture.Commands->Dispatch(1, 1, 1), std::logic_error);
	capture.Commands->BindComputePipeline(*capture.Pipeline);
	SWIM_CHECK_EQUAL(capture.BindPoint, VK_PIPELINE_BIND_POINT_COMPUTE);
	SWIM_CHECK_THROWS(capture.Commands->BindDescriptorTable(1, *capture.Table), std::invalid_argument);
	capture.WriteTable();
	SWIM_CHECK_THROWS(capture.Commands->Dispatch(1, 1, 1), std::logic_error);
	capture.Commands->BindDescriptorTable(1, *capture.Table);
	SWIM_CHECK_EQUAL(capture.DescriptorBindPoint, VK_PIPELINE_BIND_POINT_COMPUTE);
	SWIM_CHECK_EQUAL(capture.BoundSpace, 1u);
	SWIM_CHECK_THROWS(capture.WriteTable(), std::logic_error);
	SWIM_CHECK_THROWS(capture.Commands->Dispatch(1, 1, 1), std::logic_error);
	capture.Commands->PushConstants(Rhi::ShaderStageMask::Compute, 0, std::span(capture.Data).first(8));
	SWIM_CHECK_THROWS(capture.Commands->Dispatch(1, 1, 1), std::logic_error);
	capture.Commands->PushConstants(Rhi::ShaderStageMask::Compute, 8, std::span(capture.Data).first(8));
	capture.Commands->Dispatch(2, 3, 4);
	SWIM_REQUIRE_EQUAL(capture.Dispatches.size(), 1u);
	SWIM_CHECK((capture.Dispatches[0] == std::array<std::uint32_t, 3>{ 2, 3, 4 }));
	SWIM_CHECK_EQUAL(capture.PushConstantWrites[0].Stages, VK_SHADER_STAGE_COMPUTE_BIT);
	SWIM_CHECK_EQUAL(capture.PushConstantWrites[0].Layout, capture.Layout->GetLayoutState()->Layout);
}

SWIM_TEST("RHI.Vulkan.ComputeCommand", "DispatchAxesAndZeroCountsOnDedicatedFamily")
{
	ComputeCommands capture;
	capture.Pool->FamilyIndex = 1;
	capture.WriteTable();
	capture.Commands->Begin();
	capture.Bind();
	SWIM_CHECK_THROWS(capture.Commands->Dispatch(65, 1, 1), std::invalid_argument);
	SWIM_CHECK_THROWS(capture.Commands->Dispatch(1, 64, 1), std::invalid_argument);
	SWIM_CHECK_THROWS(capture.Commands->Dispatch(1, 1, 63), std::invalid_argument);
	capture.Commands->Dispatch(64, 63, 62);
	capture.Commands->Dispatch(0, 1, 1);
	capture.Commands->Dispatch(1, 0, 1);
	capture.Commands->Dispatch(1, 1, 0);
	SWIM_CHECK_EQUAL(capture.Dispatches.size(), 4u);
	capture.Commands->End();
	SWIM_CHECK_THROWS(capture.Commands->Dispatch(1, 1, 1), std::logic_error);
}

SWIM_TEST("RHI.Vulkan.ComputeCommand", "PipelineSwitchRequiresExplicitRebindAndRenderingRejectsCompute")
{
	ComputeCommands capture;
	auto graphics = capture.MakePipeline();
	SWIM_REQUIRE(graphics);
	capture.WriteTable();
	capture.Commands->Begin();
	capture.Bind();
	capture.Commands->BindGraphicsPipeline(*graphics);
	SWIM_CHECK_THROWS(capture.Commands->Dispatch(1, 1, 1), std::logic_error);
	capture.Commands->BindComputePipeline(*capture.Pipeline);
	SWIM_CHECK_THROWS(capture.Commands->Dispatch(1, 1, 1), std::logic_error);
	capture.Commands->BindDescriptorTable(1, *capture.Table);
	capture.Commands->Dispatch(1, 1, 1); // Compatible constants survive binding alone.
	RhiVulkan::VulkanTexture target{ capture.State, VK_NULL_HANDLE, { Rhi::TextureDimension::Texture2D,
		{ 16, 16, 1 }, Rhi::Format::RGBA8Unorm, Rhi::TextureUsage::ColorAttachment } };
	RhiVulkan::VulkanTextureView view{ capture.State, target, VK_NULL_HANDLE,
		{ Rhi::TextureViewDimension::Texture2D, Rhi::Format::RGBA8Unorm } };
	Rhi::RenderingAttachmentDesc attachment{};
	attachment.View = &view;
	capture.Commands->BeginRendering({ { &attachment, 1 }, nullptr, { 16, 16 } });
	SWIM_CHECK_THROWS(capture.Commands->BindComputePipeline(*capture.Pipeline), std::logic_error);
	SWIM_CHECK_THROWS(capture.Commands->Dispatch(1, 1, 1), std::logic_error);
	SWIM_CHECK_THROWS(capture.Commands->Draw(3, 1, 0, 0), std::logic_error);
	capture.Commands->EndRendering();
	capture.Commands->Dispatch(1, 1, 1);
	SWIM_CHECK_EQUAL(capture.Dispatches.size(), 2u);
}

SWIM_TEST("RHI.Vulkan.ComputeCommand", "ForeignPipelineWrongTablesAndBadPushDoNotDisturbGoodState")
{
	ComputeCommands capture;
	capture.WriteTable();
	capture.Commands->Begin();
	capture.Bind();
	auto foreignState = std::make_shared<RhiVulkan::VulkanDeviceState>();
	RhiVulkan::VulkanComputePipeline foreign(foreignState, capture.Layout->GetLayoutState());
	SWIM_CHECK_THROWS(capture.Commands->BindComputePipeline(foreign), std::invalid_argument);
	SWIM_CHECK_THROWS(capture.Commands->BindDescriptorTable(0, *capture.Table), std::invalid_argument);
	auto otherLayout = RhiVulkan::VulkanPipelineLayout::Create(capture.State, { capture.Program.get(), {} });
	auto otherTable = RhiVulkan::VulkanDescriptorTable::Create(capture.State, { otherLayout.get(), 1 });
	SWIM_CHECK_THROWS(capture.Commands->BindDescriptorTable(1, *otherTable), std::invalid_argument);
	SWIM_CHECK_THROWS(capture.Commands->PushConstants(Rhi::ShaderStageMask::Fragment, 0, capture.Data), std::invalid_argument);
	SWIM_CHECK_THROWS(capture.Commands->PushConstants(Rhi::ShaderStageMask::Compute, 4, capture.Data), std::invalid_argument);
	SWIM_CHECK_THROWS(capture.Commands->PushConstants(Rhi::ShaderStageMask::Compute, 0, {}), std::invalid_argument);
	capture.Commands->Dispatch(1, 1, 1);
	SWIM_CHECK_EQUAL(capture.Dispatches.size(), 1u);
	SWIM_CHECK_EQUAL(capture.PushConstantWrites.size(), 1u);
	SWIM_CHECK_EQUAL(capture.DescriptorBinds, 1u);
}

SWIM_TEST("RHI.Vulkan.ComputeCommand", "PoolReuseAndQueueCapabilitiesAndDeviceLossStopStaleCommands")
{
	ComputeCommands capture;
	capture.WriteTable();
	capture.Commands->Begin();
	capture.Bind();
	++capture.Pool->Generation;
	SWIM_CHECK_THROWS(capture.Commands->Dispatch(1, 1, 1), std::logic_error);
	capture.Commands->Begin();
	SWIM_CHECK_THROWS(capture.Commands->Dispatch(1, 1, 1), std::logic_error);
	capture.Bind();
	for (auto family : { 2u, 99u })
	{
		capture.Pool->FamilyIndex = family;
		SWIM_CHECK_THROWS(capture.Commands->Dispatch(1, 1, 1), std::logic_error);
		SWIM_CHECK_THROWS(capture.Commands->BindComputePipeline(*capture.Pipeline), std::logic_error);
	}
	capture.Pool->FamilyIndex = 1;
	capture.State->QueueProperties[1].queueCount = 0;
	SWIM_CHECK_THROWS(capture.Commands->Dispatch(1, 1, 1), std::logic_error);
	capture.State->QueueProperties[1].queueCount = 1;
	capture.Commands->Dispatch(1, 1, 1);
	capture.State->Diagnostics->TryRecordLoss("compute commands test", VK_ERROR_DEVICE_LOST);
	SWIM_CHECK_THROWS(capture.Commands->Dispatch(1, 1, 1), Rhi::DeviceLostError);
	SWIM_CHECK_THROWS(capture.Commands->BindComputePipeline(*capture.Pipeline), Rhi::DeviceLostError);
	SWIM_CHECK_EQUAL(capture.Dispatches.size(), 1u);
}

SWIM_TEST("RHI.Vulkan.ComputeCommand", "ComputeBufferBarriersCoverStorageHazardsWithoutOwnershipTransfer")
{
	ComputeCommands capture;
	capture.Pool->FamilyIndex = 1;
	capture.Commands->Begin();
	const auto rw = Rhi::ResourceState::ShaderRead | Rhi::ResourceState::ShaderWrite;
	capture.Commands->Transition(capture.Buffer, Rhi::ResourceState::CopyDestination, rw);
	capture.Commands->Transition(capture.Buffer, rw, rw);
	capture.Commands->Transition(capture.Buffer, rw, Rhi::ResourceState::CopySource);
	SWIM_REQUIRE_EQUAL(capture.Buffers.size(), 3u);
	SWIM_CHECK((capture.Buffers[1].srcAccessMask & VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT) != 0);
	SWIM_CHECK((capture.Buffers[1].dstAccessMask & VK_ACCESS_2_SHADER_STORAGE_READ_BIT) != 0);
	SWIM_CHECK_EQUAL(capture.Buffers[1].srcStageMask, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
	SWIM_CHECK_EQUAL(capture.Buffers[1].srcQueueFamilyIndex, VK_QUEUE_FAMILY_IGNORED);
	SWIM_CHECK_EQUAL(capture.Buffers[1].dstQueueFamilyIndex, VK_QUEUE_FAMILY_IGNORED);
	SWIM_CHECK_THROWS(capture.Commands->Transition(capture.Buffer, rw, Rhi::ResourceState::VertexBuffer), std::invalid_argument);
	SWIM_CHECK_THROWS(capture.Commands->Transition(capture.Buffer, Rhi::ResourceState::IndexBuffer, rw), std::invalid_argument);
	RhiVulkan::VulkanBuffer other(capture.State, VK_NULL_HANDLE, nullptr, { 128, Rhi::BufferUsage::TransferDestination });
	capture.Commands->CopyBuffer(capture.Buffer, other, { 0, 0, 64 });
	SWIM_CHECK_EQUAL(capture.CopyCount, 1u);
	capture.Pool->FamilyIndex = 2;
	SWIM_CHECK_THROWS(capture.Commands->Transition(capture.Buffer, rw, rw), std::logic_error);
	SWIM_CHECK_EQUAL(capture.Buffers.size(), 3u);
}

SWIM_TEST("RHI.Vulkan.ComputeCommand", "WritableBufferDescriptorsValidateBatchBeforeNativeMutation")
{
	ComputeCommands capture;
	Rhi::DescriptorWrite write{};
	write.Binding = 7;
	write.BufferResource = &capture.Buffer;
	write.BufferOffset = 3;
	SWIM_CHECK_THROWS(capture.Table->Write({ &write, 1 }), std::invalid_argument);
	write.BufferOffset = 16;
	write.BufferRange = 129;
	SWIM_CHECK_THROWS(capture.Table->Write({ &write, 1 }), std::invalid_argument);
	write.BufferRange = 64;
	RhiVulkan::VulkanBuffer invalid(capture.State, RhiVulkan::FromNativeHandle<VkBuffer>(2), nullptr, { 128, Rhi::BufferUsage::Uniform });
	write.BufferResource = &invalid;
	SWIM_CHECK_THROWS(capture.Table->Write({ &write, 1 }), std::invalid_argument);
	write.BufferResource = &capture.Buffer;
	std::array<Rhi::DescriptorWrite, 2> batch{ write, write };
	batch[1].Binding = 99;
	SWIM_CHECK_THROWS(capture.Table->Write(batch), std::invalid_argument);
	SWIM_CHECK_EQUAL(capture.Updates, 0u);
	SWIM_CHECK(!capture.Table->IsComplete());
	capture.WriteTable();
	SWIM_CHECK(capture.Table->IsComplete());
	SWIM_REQUIRE_EQUAL(capture.BuffersWritten.size(), 1u);
	SWIM_CHECK_EQUAL(capture.BuffersWritten[0].offset, 16u);
	SWIM_CHECK_EQUAL(capture.BuffersWritten[0].range, 64u);
	SWIM_CHECK_EQUAL(capture.Writes[0].descriptorType, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
}

SWIM_TEST("RHI.Vulkan.ComputeCommand", "GraphicsPushRequiresComputeConstantsToBeInitializedAgain")
{
	ComputeCommands capture;
	const Rhi::PushConstantRange range{ 0, 16, Rhi::ShaderStageMask::Fragment };
	auto graphics = capture.MakePipeline(Rhi::Format::RGBA8Unorm, {}, {}, { {}, { &range, 1 } });
	SWIM_REQUIRE(graphics);
	capture.WriteTable();
	capture.Commands->Begin();
	capture.Bind();
	capture.Commands->BindGraphicsPipeline(*graphics);
	capture.Commands->PushConstants(Rhi::ShaderStageMask::Fragment, 0, capture.Data);
	capture.Commands->BindComputePipeline(*capture.Pipeline);
	capture.Commands->BindDescriptorTable(1, *capture.Table);
	SWIM_CHECK_THROWS(capture.Commands->Dispatch(1, 1, 1), std::logic_error);
	capture.Commands->PushConstants(Rhi::ShaderStageMask::Compute, 0, std::span(capture.Data).first(8));
	SWIM_CHECK_THROWS(capture.Commands->Dispatch(1, 1, 1), std::logic_error);
	capture.Commands->PushConstants(Rhi::ShaderStageMask::Compute, 8, std::span(capture.Data).first(8));
	capture.Commands->Dispatch(1, 1, 1);
	SWIM_CHECK_EQUAL(capture.Dispatches.size(), 1u);
}
