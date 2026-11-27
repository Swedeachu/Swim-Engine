#include "Engine/Systems/Renderer/RHI/Backends/D3D12/D3D12CommandList.h"
#include "Engine/Systems/Renderer/RHI/Backends/D3D12/D3D12Objects.h"

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace Swim::RhiD3D12
{

	bool WaitForFence(ID3D12Fence* fence, std::uint64_t value, std::uint64_t timeoutNanoseconds)
	{
		if (fence->GetCompletedValue() >= value)
		{
			return true;
		}

		if (timeoutNanoseconds == 0)
		{
			return false;
		}

		HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);

		if (!event)
		{
			throw std::runtime_error("D3D12: CreateEvent failed");
		}

		bool reached = false;

		if (SUCCEEDED(fence->SetEventOnCompletion(value, event)))
		{
			const DWORD milliseconds = timeoutNanoseconds == Rhi::InfiniteTimeout
				? INFINITE
				: static_cast<DWORD>(std::min<std::uint64_t>((timeoutNanoseconds + 999999) / 1000000, INFINITE - 1));
			reached = WaitForSingleObject(event, milliseconds) == WAIT_OBJECT_0 || fence->GetCompletedValue() >= value;
		}

		CloseHandle(event);
		return reached;
	}

	// Timeline

	D3D12Timeline::D3D12Timeline(std::shared_ptr<D3D12DeviceState> stateValue, std::uint64_t initial) : state(std::move(stateValue))
	{
		Check(state->Device->CreateFence(initial, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "CreateFence (timeline)", state.get());
	}

	std::uint64_t D3D12Timeline::GetCompletedValue() const
	{
		const auto value = fence->GetCompletedValue();

		if (value == UINT64_MAX)
		{
			state->RecordLoss("ID3D12Fence::GetCompletedValue", state->Device->GetDeviceRemovedReason());
			throw Rhi::DeviceLostError();
		}

		return value;
	}

	bool D3D12Timeline::Wait(std::uint64_t value, std::uint64_t timeoutNanoseconds)
	{
		const bool reached = WaitForFence(fence.Get(), value, timeoutNanoseconds);
		GetCompletedValue(); // Raises a device loss seen while waiting.
		return reached;
	}

	// Fence

	D3D12Fence::D3D12Fence(std::shared_ptr<D3D12DeviceState> stateValue, bool signaled) : state(std::move(stateValue))
	{
		Check(state->Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "CreateFence", state.get());

		if (signaled)
		{
			armed = true;
			SignalOnCpu();
		}
	}

	bool D3D12Fence::IsSignaled() const
	{
		return armed && fence->GetCompletedValue() >= target;
	}

	bool D3D12Fence::Wait(std::uint64_t timeoutNanoseconds)
	{
		if (!armed)
		{
			// Unsignaled and nothing queued: it can only time out.
			return false;
		}

		return WaitForFence(fence.Get(), target, timeoutNanoseconds);
	}

	void D3D12Fence::Reset()
	{
		// The next signal targets a value above every earlier one.
		target = fence->GetCompletedValue() + 1;
		armed = false;
	}

	std::uint64_t D3D12Fence::NextSignal()
	{
		armed = true;
		return target;
	}

	void D3D12Fence::SignalOnCpu()
	{
		Check(fence->Signal(NextSignal()), "ID3D12Fence::Signal", state.get());
	}

	// Semaphore

	D3D12Semaphore::D3D12Semaphore(std::shared_ptr<D3D12DeviceState> stateValue) : state(std::move(stateValue))
	{
		Check(state->Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "CreateFence (semaphore)", state.get());
	}

	// Queue

	D3D12Queue::D3D12Queue(std::shared_ptr<D3D12DeviceState> stateValue, Rhi::QueueType typeValue)
		: state(std::move(stateValue)), type(typeValue)
	{
		D3D12_COMMAND_QUEUE_DESC desc{};
		desc.Type = ToCommandListType(type);
		Check(state->Device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue)), "CreateCommandQueue", state.get());
		SetDebugName(queue.Get(),
			type == Rhi::QueueType::Graphics	  ? "Swim direct queue"
				: type == Rhi::QueueType::Compute ? "Swim compute queue"
												  : "Swim copy queue");
		Check(state->Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&idleFence)), "CreateFence (queue idle)", state.get());
		progressIndex = state->Progress.Register(idleFence, type == Rhi::QueueType::Graphics);
	}

	Rhi::TimestampInfo D3D12Queue::GetTimestampInfo() const
	{
		UINT64 frequency = 0;

		if ((type == Rhi::QueueType::Transfer && !state->Features.CopyQueueTimestamps) ||
			FAILED(queue->GetTimestampFrequency(&frequency)) || frequency == 0)
		{
			return {};
		}

		return { 1.0e9 / static_cast<double>(frequency), 64 };
	}

	void D3D12Queue::WaitSemaphores(std::span<Rhi::Semaphore* const> semaphores)
	{
		for (auto* semaphore : semaphores)
		{
			auto& native = static_cast<D3D12Semaphore&>(*semaphore);

			if (const auto value = native.ConsumeWait(); value != 0)
			{
				Check(queue->Wait(native.GetFence(), value), "ID3D12CommandQueue::Wait", state.get());
			}
		}
	}

	void D3D12Queue::Submit(const Rhi::SubmitDesc& desc)
	{
		std::scoped_lock lock(mutex);

		for (const auto& point : desc.WaitTimelines)
		{
			Check(queue->Wait(static_cast<D3D12Timeline&>(*point.Semaphore).GetFence(), point.Value), "ID3D12CommandQueue::Wait",
				state.get());
		}

		WaitSemaphores(desc.WaitSemaphores);
		std::vector<ID3D12CommandList*> lists;
		lists.reserve(desc.CommandLists.size());

		for (auto* list : desc.CommandLists)
		{
			auto& native = static_cast<D3D12CommandList&>(*list);
			lists.push_back(native.GetList());
		}

		if (!lists.empty())
		{
			queue->ExecuteCommandLists(static_cast<UINT>(lists.size()), lists.data());
		}

		state->DrainMessages(); // Polled debug-layer messages, where callbacks are unavailable.

		for (auto* semaphore : desc.SignalSemaphores)
		{
			auto& native = static_cast<D3D12Semaphore&>(*semaphore);
			Check(queue->Signal(native.GetFence(), native.SignalOnGpu()), "ID3D12CommandQueue::Signal", state.get());
		}

		for (const auto& point : desc.SignalTimelines)
		{
			Check(queue->Signal(static_cast<D3D12Timeline&>(*point.Semaphore).GetFence(), point.Value), "ID3D12CommandQueue::Signal",
				state.get());
		}

		if (desc.CompletionFence)
		{
			auto& fence = static_cast<D3D12Fence&>(*desc.CompletionFence);
			Check(queue->Signal(fence.GetFence(), fence.NextSignal()), "ID3D12CommandQueue::Signal", state.get());
		}

		// Progress for work that must outlive this submission (D3D12GpuProgress).
		Check(queue->Signal(idleFence.Get(), ++idleValue), "ID3D12CommandQueue::Signal (progress)", state.get());
		state->Progress.Submitted(progressIndex, idleValue);
		state->Deferred->Collect();
	}

	void D3D12Queue::WaitIdle()
	{
		std::uint64_t value = 0;

		{
			std::scoped_lock lock(mutex);
			value = ++idleValue;
			Check(queue->Signal(idleFence.Get(), value), "ID3D12CommandQueue::Signal (idle)", state.get());
		}

		WaitForFence(idleFence.Get(), value, Rhi::InfiniteTimeout);

		if (idleFence->GetCompletedValue() == UINT64_MAX)
		{
			state->RecordLoss("ID3D12CommandQueue::WaitIdle", state->Device->GetDeviceRemovedReason());
			throw Rhi::DeviceLostError();
		}

		state->Deferred->Collect(); // The idle signal passes everything submitted, and the frame being recorded.
	}

} // namespace Swim::RhiD3D12
