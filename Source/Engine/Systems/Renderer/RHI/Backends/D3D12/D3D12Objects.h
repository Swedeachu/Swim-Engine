#pragma once

#include "Engine/Systems/Renderer/RHI/Backends/D3D12/Internal/D3D12DeviceState.h"

#include <mutex>
#include <vector>

namespace Swim::RhiD3D12
{

	class D3D12Buffer final : public Rhi::Buffer
	{

	  public:
		D3D12Buffer(std::shared_ptr<D3D12DeviceState> state, const Rhi::BufferDesc& desc);
		~D3D12Buffer() override;

		std::uintptr_t GetNativeHandle() const override { return reinterpret_cast<std::uintptr_t>(resource.Get()); }

		const Rhi::BufferDesc& GetDesc() const override { return desc; }

		void Write(std::uint64_t offset, std::span<const std::byte> data) override;
		void Read(std::uint64_t offset, std::span<std::byte> data) override;
		std::span<std::byte> GetMappedWriteSpan() override;
		void FlushMappedWrites(std::uint64_t offset, std::uint64_t size) override;
		std::span<const std::byte> GetMappedReadSpan() override;
		void InvalidateMappedReads(std::uint64_t offset, std::uint64_t size) override;

		ID3D12Resource* GetResource() const { return resource.Get(); }

		D3D12_GPU_VIRTUAL_ADDRESS GetAddress() const { return resource->GetGPUVirtualAddress(); }

		// Upload and readback heaps keep one fixed state; barriers skip them.
		bool IsHostHeap() const { return desc.Memory != Rhi::MemoryPreference::DeviceLocal; }

		D3D12_RESOURCE_STATES TrackedState = D3D12_RESOURCE_STATE_COMMON; // Legacy barriers only.

	  private:
		std::shared_ptr<D3D12DeviceState> state;
		Rhi::BufferDesc desc;
		std::string name;
		D3D12Placement placement; // Empty for a committed buffer.
		ComPtr<ID3D12Resource> resource;
		D3D12MemoryRecord memory;
		std::byte* mapped = nullptr;
	};

	class D3D12Texture final : public Rhi::Texture
	{

	  public:
		// Creates and owns a texture (placed or committed).
		D3D12Texture(std::shared_ptr<D3D12DeviceState> state, const Rhi::TextureDesc& desc);
		// Wraps a texture owned elsewhere (a swapchain buffer).
		D3D12Texture(std::shared_ptr<D3D12DeviceState> state, ComPtr<ID3D12Resource> resource, const Rhi::TextureDesc& desc);
		// An owned texture outlives the GPU work submitted so far (D3D12DeferredReleases).
		~D3D12Texture() override;

		std::uintptr_t GetNativeHandle() const override { return reinterpret_cast<std::uintptr_t>(resource.Get()); }

		const Rhi::TextureDesc& GetDesc() const override { return desc; }

		ID3D12Resource* GetResource() const { return resource.Get(); }

		std::uint32_t GetSubresourceCount() const;
		std::uint32_t Subresource(std::uint32_t mip, std::uint32_t layer, std::uint32_t plane = 0) const;

		std::uint32_t GetPlaneCount() const { return Rhi::HasStencil(desc.PixelFormat) ? 2u : 1u; }

		// Enhanced barriers: a texture only sampled and copied stays in D3D12_BARRIER_LAYOUT_COMMON,
		// which allows SHADER_RESOURCE, COPY_DEST and COPY_SOURCE access (Enhanced Barriers spec,
		// "Layout Access Compatibility"), so its transitions need no layout change.
		bool KeepsCommonLayout() const { return commonLayout; }

		// Legacy barriers: the state of every subresource as recorded so far.
		std::vector<D3D12_RESOURCE_STATES> TrackedStates;
		// The state of the latest transition recorded (chooses read-only depth views).
		Rhi::ResourceState LastState = Rhi::ResourceState::Undefined;

	  private:
		std::shared_ptr<D3D12DeviceState> state;
		Rhi::TextureDesc desc;
		std::string name;
		D3D12Placement placement; // Empty for a committed or wrapped texture.
		ComPtr<ID3D12Resource> resource;
		D3D12MemoryRecord memory; // Empty for a wrapped texture.
		bool commonLayout = false;
	};

	class D3D12TextureView final : public Rhi::TextureView
	{

	  public:
		D3D12TextureView(std::shared_ptr<D3D12DeviceState> state, D3D12Texture& texture, const Rhi::TextureViewDesc& desc);
		~D3D12TextureView() override;

		std::uintptr_t GetNativeHandle() const override
		{
			return renderTarget != D3D12DescriptorHeap::Invalid ? renderTarget : depthStencil;
		}

		Rhi::Texture& GetTexture() const override { return texture; }

		const Rhi::TextureViewDesc& GetDesc() const override { return desc; }

		D3D12Texture& GetD3D12Texture() const { return texture; }

		D3D12_CPU_DESCRIPTOR_HANDLE GetRenderTarget() const;
		D3D12_CPU_DESCRIPTOR_HANDLE GetDepthStencil(bool readOnly) const;
		// Writes this view's shader resource / unordered access descriptor into a heap slot.
		void WriteShaderResource(D3D12_CPU_DESCRIPTOR_HANDLE destination) const;
		void WriteUnorderedAccess(D3D12_CPU_DESCRIPTOR_HANDLE destination) const;
		// The subresources the view covers.
		Rhi::TextureSubresourceRange GetRange() const;

	  private:
		std::shared_ptr<D3D12DeviceState> state;
		D3D12Texture& texture;
		Rhi::TextureViewDesc desc;
		std::uint32_t renderTarget = D3D12DescriptorHeap::Invalid;
		std::uint32_t depthStencil = D3D12DescriptorHeap::Invalid; // Two: writable, then read-only.
	};

	class D3D12Sampler final : public Rhi::Sampler
	{

	  public:
		D3D12Sampler(std::shared_ptr<D3D12DeviceState> state, const Rhi::SamplerDesc& desc);
		~D3D12Sampler() override;

		std::uintptr_t GetNativeHandle() const override { return slot; }

		const Rhi::SamplerDesc& GetDesc() const override { return desc; }

		D3D12_CPU_DESCRIPTOR_HANDLE GetCpu() const { return state->CpuSamplers->Cpu(slot); }

	  private:
		std::shared_ptr<D3D12DeviceState> state;
		Rhi::SamplerDesc desc;
		std::uint32_t slot = D3D12DescriptorHeap::Invalid;
	};

	// Timelines, fences and binary semaphores are all ID3D12Fence values.
	class D3D12Timeline final : public Rhi::Timeline
	{

	  public:
		D3D12Timeline(std::shared_ptr<D3D12DeviceState> state, std::uint64_t initial);

		std::uintptr_t GetNativeHandle() const override { return reinterpret_cast<std::uintptr_t>(fence.Get()); }

		std::uint64_t GetCompletedValue() const override;
		bool Wait(std::uint64_t value, std::uint64_t timeoutNanoseconds) override;

		ID3D12Fence* GetFence() const { return fence.Get(); }

	  private:
		std::shared_ptr<D3D12DeviceState> state;
		ComPtr<ID3D12Fence> fence;
	};

	class D3D12Fence final : public Rhi::Fence
	{

	  public:
		D3D12Fence(std::shared_ptr<D3D12DeviceState> state, bool signaled);

		std::uintptr_t GetNativeHandle() const override { return reinterpret_cast<std::uintptr_t>(fence.Get()); }

		bool IsSignaled() const override;
		bool Wait(std::uint64_t timeoutNanoseconds) override;
		void Reset() override;

		// The value the next signal writes; IsSignaled compares against it.
		std::uint64_t NextSignal();
		void SignalOnCpu();

		ID3D12Fence* GetFence() const { return fence.Get(); }

	  private:
		std::shared_ptr<D3D12DeviceState> state;
		ComPtr<ID3D12Fence> fence;
		std::uint64_t target = 1;
		bool armed = false;
	};

	class D3D12Semaphore final : public Rhi::Semaphore
	{

	  public:
		explicit D3D12Semaphore(std::shared_ptr<D3D12DeviceState> state);

		std::uintptr_t GetNativeHandle() const override { return reinterpret_cast<std::uintptr_t>(fence.Get()); }

		ID3D12Fence* GetFence() const { return fence.Get(); }

		// A signal on the GPU (a submission) or a signal that needs no wait (swapchain acquire,
		// which DXGI orders itself). Waits consume the latest signal.
		std::uint64_t SignalOnGpu() { return pending = ++value; }

		void SignalReady() { pending = 0; }

		std::uint64_t ConsumeWait() { return std::exchange(pending, 0); }

	  private:
		std::shared_ptr<D3D12DeviceState> state;
		ComPtr<ID3D12Fence> fence;
		std::uint64_t value = 0;
		std::uint64_t pending = 0;
	};

	bool WaitForFence(ID3D12Fence* fence, std::uint64_t value, std::uint64_t timeoutNanoseconds);

	class D3D12Queue final : public Rhi::Queue
	{

	  public:
		D3D12Queue(std::shared_ptr<D3D12DeviceState> state, Rhi::QueueType type);

		std::uintptr_t GetNativeHandle() const override { return reinterpret_cast<std::uintptr_t>(queue.Get()); }

		Rhi::QueueType GetType() const override { return type; }

		Rhi::TimestampInfo GetTimestampInfo() const override;
		void Submit(const Rhi::SubmitDesc& desc) override;
		void WaitIdle() override;

		ID3D12CommandQueue* GetQueue() const { return queue.Get(); }

		std::mutex& GetMutex() { return mutex; }

		// Waits on this queue (GPU side) for the semaphores, consuming their signals.
		void WaitSemaphores(std::span<Rhi::Semaphore* const> semaphores);

	  private:
		std::shared_ptr<D3D12DeviceState> state;
		Rhi::QueueType type;
		ComPtr<ID3D12CommandQueue> queue;
		ComPtr<ID3D12Fence> idleFence;
		std::uint64_t idleValue = 0;
		std::size_t progressIndex = 0; // In the device's D3D12GpuProgress.
		std::mutex mutex;
	};

} // namespace Swim::RhiD3D12
