#pragma once

#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Commands/VulkanCommandPoolState.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/Internal/VulkanNativeHandle.h"
#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

#include <volk.h>

#include <memory>
#include <stdexcept>

namespace Swim::RhiVulkan
{

	class VulkanGraphicsPipeline;
	class VulkanDescriptorTable;

	class VulkanCommandList final : public Rhi::CommandList
	{
	public:
		VulkanCommandList(std::shared_ptr<VulkanCommandPoolState> poolState, VkCommandBuffer commandBuffer)
			: poolState(std::move(poolState)), commandBuffer(commandBuffer)
		{
			SetVulkanObjectName(*this->poolState->DeviceState, VK_OBJECT_TYPE_COMMAND_BUFFER,
				ToNativeHandle(commandBuffer), "Swim command list");
		}

		~VulkanCommandList() override;
		std::uintptr_t GetNativeHandle() const override;
		void Begin() override;
		void End() override;
		void BeginDebugLabel(std::string_view name, const std::array<float, 4>& color = { 1, 1, 1, 1 }) override;
		void EndDebugLabel() override;
		void InsertDebugLabel(std::string_view name, const std::array<float, 4>& color = { 1, 1, 1, 1 }) override;
		void Transition(Rhi::Buffer& buffer, Rhi::ResourceState before, Rhi::ResourceState after) override;
		void Transition(Rhi::Texture& texture, Rhi::ResourceState before, Rhi::ResourceState after, const Rhi::TextureSubresourceRange& range) override;
		void CopyBuffer(Rhi::Buffer& source, Rhi::Buffer& destination, const Rhi::BufferCopyRegion& region) override;
		void CopyTexture(Rhi::Texture& source, Rhi::Texture& destination, const Rhi::TextureCopyRegion& region) override;
		void CopyBufferToTexture(Rhi::Buffer& source, Rhi::Texture& destination, const Rhi::BufferTextureCopyRegion& region) override;
		void CopyTextureToBuffer(Rhi::Texture& source, Rhi::Buffer& destination, const Rhi::BufferTextureCopyRegion& region) override;
		void BeginRendering(const Rhi::RenderingDesc& desc) override;
		void EndRendering() override;
		void BindGraphicsPipeline(Rhi::GraphicsPipeline&) override;
		void BindComputePipeline(Rhi::ComputePipeline&) override;
		void BindDescriptorTable(std::uint32_t, Rhi::DescriptorTable&) override;
		void SetViewport(const Rhi::Viewport& viewport) override;
		void SetScissor(const Rhi::ScissorRect& scissor) override;
		void BindVertexBuffer(std::uint32_t, Rhi::Buffer&, std::uint64_t) override;
		void BindIndexBuffer(Rhi::Buffer&, std::uint64_t, Rhi::IndexType) override;
		void Draw(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) override;
		void DrawIndexed(std::uint32_t, std::uint32_t, std::uint32_t, std::int32_t, std::uint32_t) override;
		void Dispatch(std::uint32_t, std::uint32_t, std::uint32_t) override;
		void WriteTimestamp(Rhi::QueryPool&, std::uint32_t) override;

		VkCommandBuffer GetCommandBuffer() const
		{
			return commandBuffer;
		}

		std::uint32_t GetQueueFamilyIndex() const
		{
			return poolState->FamilyIndex;
		}

		const std::shared_ptr<VulkanDeviceState>& GetState() const
		{
			return poolState->DeviceState;
		}

		bool IsExecutable() const
		{
			return executable && generation == poolState->Generation;
		}

		void MarkSubmitted()
		{
			executable = false;
		}

	private:
		std::shared_ptr<VulkanCommandPoolState> poolState;
		VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
		void RequireRecording(bool outsideRendering = false) const;
		void RequireGraphicsQueue() const;

		std::uint64_t generation = UINT64_MAX;
		const VulkanGraphicsPipeline* graphicsPipeline = nullptr;
		std::vector<Rhi::Format> renderingColors;
		Rhi::Format renderingDepth = Rhi::Format::Undefined;
		Rhi::SampleCount renderingSamples = Rhi::SampleCount::X1;
		std::uint64_t availableIndices = 0;
		bool viewportSet = false;
		bool scissorSet = false;
		void RequireDraw() const;
		void RequireDescriptorTables() const;
		std::vector<const VulkanDescriptorTable*> boundTables;

		std::uint32_t debugLabelDepth = 0;
		bool executable = false;
		bool recording = false;
		bool rendering = false;
	};

} // namespace Swim::RhiVulkan
