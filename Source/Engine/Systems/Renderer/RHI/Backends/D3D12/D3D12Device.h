#pragma once

#include "Engine/Systems/Renderer/RHI/Backends/D3D12/D3D12Objects.h"

#include <array>

namespace Swim::RhiD3D12
{

	// What every adapter and device of one graphics system shares.
	struct D3D12SystemState
	{
		ComPtr<IDXGIFactory6> Factory;
		std::shared_ptr<Rhi::DiagnosticLog> Log;
		bool Validation = false;
		bool Echo = true;
		Rhi::ValidationChecks Checks{};
	};

	class D3D12Device final : public Rhi::Device
	{

	  public:
		D3D12Device(std::shared_ptr<D3D12SystemState> system, std::shared_ptr<D3D12DeviceState> state, Rhi::AdapterInfo info);
		~D3D12Device() override;

		std::uintptr_t GetNativeHandle() const override { return reinterpret_cast<std::uintptr_t>(state->Device.Get()); }

		std::shared_ptr<Rhi::DeviceDiagnostics> GetDeviceDiagnostics() const override { return state->Diagnostics; }

		Rhi::MemoryBudgetSnapshot GetMemoryBudgetSnapshot() const override;
		Rhi::PipelineCacheLoadStatus LoadPipelineCache(std::span<const std::byte> data) override;
		Rhi::PipelineCacheData GetPipelineCacheData() const override;

		const Rhi::AdapterInfo& GetAdapterInfo() const override { return info; }

		Rhi::Queue& GetQueue(Rhi::QueueType type) override { return *queues.at(static_cast<std::size_t>(type)); }

		Rhi::SwapchainSupport QuerySwapchainSupport(Platform::Window& window) const override;
		std::unique_ptr<Rhi::Swapchain> CreateSwapchain(Platform::Window& window, const Rhi::SwapchainDesc& desc) override;
		std::unique_ptr<Rhi::Buffer> CreateBuffer(const Rhi::BufferDesc& desc) override;
		std::unique_ptr<Rhi::Texture> CreateTexture(const Rhi::TextureDesc& desc) override;
		std::unique_ptr<Rhi::TextureView> CreateTextureView(Rhi::Texture& texture, const Rhi::TextureViewDesc& desc) override;
		std::unique_ptr<Rhi::Sampler> CreateSampler(const Rhi::SamplerDesc& desc) override;
		std::unique_ptr<Rhi::ShaderProgram> CreateShaderProgram(const Rhi::ShaderProgramDesc& desc) override;
		std::unique_ptr<Rhi::PipelineLayout> CreatePipelineLayout(const Rhi::PipelineLayoutDesc& desc) override;
		std::unique_ptr<Rhi::GraphicsPipeline> CreateGraphicsPipeline(const Rhi::GraphicsPipelineDesc& desc) override;
		std::unique_ptr<Rhi::ComputePipeline> CreateComputePipeline(const Rhi::ComputePipelineDesc& desc) override;
		std::unique_ptr<Rhi::DescriptorTable> CreateDescriptorTable(const Rhi::DescriptorTableDesc& desc) override;
		std::unique_ptr<Rhi::CommandPool> CreateCommandPool(Rhi::QueueType queueType) override;
		std::unique_ptr<Rhi::Semaphore> CreateGpuSemaphore() override;
		std::unique_ptr<Rhi::Fence> CreateFence(bool signaled) override;
		std::unique_ptr<Rhi::Timeline> CreateTimeline(std::uint64_t initialValue) override;
		std::unique_ptr<Rhi::QueryPool> CreateQueryPool(const Rhi::QueryPoolDesc& desc) override;
		void WaitIdle() override;

		const std::shared_ptr<D3D12DeviceState>& GetState() const { return state; }

		D3D12Queue& GetDirectQueue() { return *queues[0]; }

	  private:
		void HookDebugMessages();

		std::shared_ptr<D3D12SystemState> system;
		std::shared_ptr<D3D12DeviceState> state;
		Rhi::AdapterInfo info;
		std::array<std::unique_ptr<D3D12Queue>, 3> queues;
		ComPtr<ID3D12InfoQueue1> infoQueue;
		DWORD messageCookie = 0;
	};

	std::unique_ptr<Rhi::Device> CreateD3D12Device(std::shared_ptr<D3D12SystemState> system, ComPtr<IDXGIAdapter4> adapter,
		const Rhi::AdapterInfo& info, const D3D12Features& features);

} // namespace Swim::RhiD3D12
