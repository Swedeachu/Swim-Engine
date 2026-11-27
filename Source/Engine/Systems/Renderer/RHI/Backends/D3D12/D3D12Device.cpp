#include "Engine/Systems/Renderer/RHI/Backends/D3D12/D3D12Device.h"

#include "Engine/Systems/Renderer/RHI/Backends/D3D12/D3D12CommandList.h"
#include "Engine/Systems/Renderer/RHI/Backends/D3D12/D3D12Pipelines.h"
#include "Engine/Systems/Renderer/RHI/Backends/D3D12/D3D12Swapchain.h"

#include "Engine/Logging/Log.h"

#include <iostream>
#include <stdexcept>

namespace Swim::RhiD3D12
{

	namespace
	{

		void CALLBACK OnDebugMessage(
			D3D12_MESSAGE_CATEGORY, D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID id, LPCSTR description, void* context) noexcept
		{
			const auto* system = static_cast<const D3D12SystemState*>(context);
			RecordDebugMessage(*system->Log, system->Echo, severity, id, description ? description : "");
		}

	} // namespace

	std::unique_ptr<Rhi::Device> CreateD3D12Device(std::shared_ptr<D3D12SystemState> system, ComPtr<IDXGIAdapter4> adapter,
		const Rhi::AdapterInfo& info, const D3D12Features& features)
	{
		auto state = std::make_shared<D3D12DeviceState>();
		state->Factory = system->Factory;
		state->Adapter = adapter;
		state->Features = features;
		state->Log = system->Log;
		state->EchoMessages = system->Echo;
		state->Debug = system->Validation;
		state->Uma = info.Integrated;
		Check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&state->Device)), "D3D12CreateDevice");

		// The adapter and its queried features, once per device (as the Vulkan back end reports its adapter).
		{
			const std::string summary = info.Name + "; D3D12 " + DescribeFeatures(features);
			system->Log->Record(Rhi::DiagnosticSeverity::Info, "Adapter", summary);

			if (Engine::Logging::GetDebugLevel() >= 2)
			{
				Engine::Logging::Write(Engine::Logging::Level::Debug, "[Swim D3D12] " + summary);
			}
		}

		if (features.EnhancedBarriers)
		{
			state->Device.As(&state->Device10);
		}

		auto* device = state->Device.Get();
		state->RenderTargets =
			std::make_unique<D3D12DescriptorHeap>(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 4096, false, "Swim render target views");
		state->DepthStencils =
			std::make_unique<D3D12DescriptorHeap>(device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 2048, false, "Swim depth views");
		state->CpuViews =
			std::make_unique<D3D12DescriptorHeap>(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 65536, false, "Swim staging views");
		state->CpuSamplers =
			std::make_unique<D3D12DescriptorHeap>(device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 2048, false, "Swim staging samplers");
		const std::uint32_t views = features.BindingTier >= D3D12_RESOURCE_BINDING_TIER_3 ? 1000000u : 65536u;
		state->Views =
			std::make_unique<D3D12DescriptorHeap>(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, views, true, "Swim shader views");
		state->Samplers =
			std::make_unique<D3D12DescriptorHeap>(device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 2048, true, "Swim shader samplers");
		return std::make_unique<D3D12Device>(std::move(system), std::move(state), info);
	}

	D3D12Device::D3D12Device(
		std::shared_ptr<D3D12SystemState> systemValue, std::shared_ptr<D3D12DeviceState> stateValue, Rhi::AdapterInfo infoValue)
		: system(std::move(systemValue)), state(std::move(stateValue)), info(std::move(infoValue))
	{
		SetDebugName(state->Device.Get(), "Swim device");
		queues[0] = std::make_unique<D3D12Queue>(state, Rhi::QueueType::Graphics);
		queues[1] = std::make_unique<D3D12Queue>(state, Rhi::QueueType::Compute);
		queues[2] = std::make_unique<D3D12Queue>(state, Rhi::QueueType::Transfer);
		HookDebugMessages();
	}

	D3D12Device::~D3D12Device()
	{
		try
		{
			if (!state->Diagnostics->IsLost())
			{
				WaitIdle();
			}
		}
		catch (...)
		{
		}

		if (infoQueue)
		{
			infoQueue->UnregisterMessageCallback(messageCookie);
		}
	}

	void D3D12Device::HookDebugMessages()
	{
		ComPtr<ID3D12InfoQueue> queue;

		if (!system->Validation || FAILED(state->Device.As(&queue)))
		{
			return;
		}

		// Messages that describe intended use, not errors: clearing without a
		// matching optimized clear value, mapping a readback buffer for reading, and
		// a pipeline cache miss.
		D3D12_MESSAGE_ID quiet[] = { D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,
			D3D12_MESSAGE_ID_CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE, D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,
			D3D12_MESSAGE_ID_LOADPIPELINE_NAMENOTFOUND };
		// Info and plain messages are never logged: denied here, the debug layer neither stores
		// them nor calls back for them.
		D3D12_MESSAGE_SEVERITY unlogged[] = { D3D12_MESSAGE_SEVERITY_INFO, D3D12_MESSAGE_SEVERITY_MESSAGE };
		D3D12_INFO_QUEUE_FILTER filter{};
		filter.DenyList.NumIDs = static_cast<UINT>(std::size(quiet));
		filter.DenyList.pIDList = quiet;
		filter.DenyList.NumSeverities = static_cast<UINT>(std::size(unlogged));
		filter.DenyList.pSeverityList = unlogged;
		queue->PushStorageFilter(&filter);

		// ID3D12InfoQueue1 (a newer debug layer) calls back as each message is produced. The
		// filter applies to callbacks too; Info messages are dropped in RecordDebugMessage.
		if (SUCCEEDED(queue.As(&infoQueue)) &&
			SUCCEEDED(infoQueue->RegisterMessageCallback(&OnDebugMessage, D3D12_MESSAGE_CALLBACK_FLAG_NONE, system.get(), &messageCookie)))
		{
			return;
		}

		infoQueue.Reset();
		state->PolledMessages = queue;
	}

	Rhi::MemoryBudgetSnapshot D3D12Device::GetMemoryBudgetSnapshot() const
	{
		Rhi::MemoryBudgetSnapshot snapshot;

		for (const auto group : { DXGI_MEMORY_SEGMENT_GROUP_LOCAL, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL })
		{
			DXGI_QUERY_VIDEO_MEMORY_INFO memory{};

			if (SUCCEEDED(state->Adapter->QueryVideoMemoryInfo(0, group, &memory)))
			{
				Rhi::MemoryHeapBudget heap;
				heap.HeapIndex = static_cast<std::uint32_t>(snapshot.Heaps.size());
				heap.DeviceLocal = group == DXGI_MEMORY_SEGMENT_GROUP_LOCAL && info.Discrete;
				heap.HostVisible = group == DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL || info.Integrated;
				heap.CapacityBytes = group == DXGI_MEMORY_SEGMENT_GROUP_LOCAL ? info.DedicatedVideoMemory : memory.Budget;
				heap.Source = Rhi::MemoryBudgetSource::DriverEstimate;
				heap.UsageBytes = memory.CurrentUsage;
				heap.BudgetBytes = memory.Budget;
				heap.AllocationCount = static_cast<std::uint32_t>(state->AllocationCount[heap.HeapIndex].load());
				heap.AllocationBytes = state->AllocationBytes[heap.HeapIndex].load();
				// Every resource is committed: each allocation is its own block.
				heap.BlockCount = heap.AllocationCount;
				heap.BlockBytes = heap.AllocationBytes;
				snapshot.Heaps.push_back(heap);
			}
		}

		return snapshot;
	}

	std::unique_ptr<Rhi::Buffer> D3D12Device::CreateBuffer(const Rhi::BufferDesc& desc)
	{
		return std::make_unique<D3D12Buffer>(state, desc);
	}

	std::unique_ptr<Rhi::Texture> D3D12Device::CreateTexture(const Rhi::TextureDesc& desc)
	{
		return std::make_unique<D3D12Texture>(state, desc);
	}

	std::unique_ptr<Rhi::TextureView> D3D12Device::CreateTextureView(Rhi::Texture& texture, const Rhi::TextureViewDesc& desc)
	{
		return std::make_unique<D3D12TextureView>(state, static_cast<D3D12Texture&>(texture), desc);
	}

	std::unique_ptr<Rhi::Sampler> D3D12Device::CreateSampler(const Rhi::SamplerDesc& desc)
	{
		return std::make_unique<D3D12Sampler>(state, desc);
	}

	Rhi::PipelineCacheLoadStatus D3D12Device::LoadPipelineCache(std::span<const std::byte> data)
	{
		return LoadD3D12PipelineCache(*state, data);
	}

	Rhi::PipelineCacheData D3D12Device::GetPipelineCacheData() const
	{
		return GetD3D12PipelineCacheData(*state);
	}

	std::unique_ptr<Rhi::ShaderProgram> D3D12Device::CreateShaderProgram(const Rhi::ShaderProgramDesc& desc)
	{
		return D3D12ShaderProgram::Create(state, desc);
	}

	std::unique_ptr<Rhi::PipelineLayout> D3D12Device::CreatePipelineLayout(const Rhi::PipelineLayoutDesc& desc)
	{
		return D3D12PipelineLayout::Create(state, desc);
	}

	std::unique_ptr<Rhi::GraphicsPipeline> D3D12Device::CreateGraphicsPipeline(const Rhi::GraphicsPipelineDesc& desc)
	{
		return D3D12GraphicsPipeline::Create(state, desc);
	}

	std::unique_ptr<Rhi::ComputePipeline> D3D12Device::CreateComputePipeline(const Rhi::ComputePipelineDesc& desc)
	{
		return D3D12ComputePipeline::Create(state, desc);
	}

	std::unique_ptr<Rhi::DescriptorTable> D3D12Device::CreateDescriptorTable(const Rhi::DescriptorTableDesc& desc)
	{
		return D3D12DescriptorTable::Create(state, desc);
	}

	std::unique_ptr<Rhi::QueryPool> D3D12Device::CreateQueryPool(const Rhi::QueryPoolDesc& desc)
	{
		return std::make_unique<D3D12QueryPool>(state, desc, queues.at(static_cast<std::size_t>(desc.Queue))->GetTimestampInfo());
	}

	std::unique_ptr<Rhi::CommandPool> D3D12Device::CreateCommandPool(Rhi::QueueType queueType)
	{
		return std::make_unique<D3D12CommandPool>(state, queueType);
	}

	std::unique_ptr<Rhi::Semaphore> D3D12Device::CreateGpuSemaphore()
	{
		return std::make_unique<D3D12Semaphore>(state);
	}

	std::unique_ptr<Rhi::Fence> D3D12Device::CreateFence(bool signaled)
	{
		return std::make_unique<D3D12Fence>(state, signaled);
	}

	std::unique_ptr<Rhi::Timeline> D3D12Device::CreateTimeline(std::uint64_t initialValue)
	{
		return std::make_unique<D3D12Timeline>(state, initialValue);
	}

	Rhi::SwapchainSupport D3D12Device::QuerySwapchainSupport(Platform::Window& window) const
	{
		return QueryD3D12SwapchainSupport(*state, window);
	}

	std::unique_ptr<Rhi::Swapchain> D3D12Device::CreateSwapchain(Platform::Window& window, const Rhi::SwapchainDesc& desc)
	{
		if (!D3D12SwapchainSupportsColorMode(*state, window, desc))
		{
			return nullptr;
		}

		return std::make_unique<D3D12Swapchain>(state, *queues[0], window, desc);
	}

	void D3D12Device::WaitIdle()
	{
		for (auto& queue : queues)
		{
			queue->WaitIdle();
		}
	}

} // namespace Swim::RhiD3D12
