#pragma once

#include "Engine/Systems/Renderer/RHI/RhiContracts.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace Swim::Rhi
{

	struct FrameContextDesc
	{
		QueueType Queue = QueueType::Graphics;
		std::uint32_t FrameCount = 3;
	};

	class FrameContextRing
	{
	public:
		struct FrameContext
		{
			std::uint32_t Index = 0;
			std::uint64_t CompletionValue = 0;

			CommandPool& GetCommandPool() const
			{
				return *commandPool;
			}

		private:
			friend class FrameContextRing;

			std::unique_ptr<CommandPool> commandPool;
			std::vector<std::unique_ptr<CommandList>> commandLists;
			std::vector<std::unique_ptr<RhiObject>> retiredObjects;
		};

		static std::unique_ptr<FrameContextRing> Create(Device& device, const FrameContextDesc& desc = {})
		{
			if (desc.FrameCount == 0)
			{
				return nullptr;
			}

			auto timeline = device.CreateTimeline(0);
			if (!timeline)
			{
				return nullptr;
			}

			std::vector<FrameContext> contexts;
			contexts.resize(desc.FrameCount);
			for (std::uint32_t index = 0; index < desc.FrameCount; ++index)
			{
				contexts[index].Index = index;
				contexts[index].commandPool = device.CreateCommandPool(desc.Queue);
				if (!contexts[index].commandPool)
				{
					return nullptr;
				}
			}

			return std::unique_ptr<FrameContextRing>(new FrameContextRing(
				device.GetQueue(desc.Queue), std::move(timeline), std::move(contexts)));
		}

		~FrameContextRing()
		{
			try
			{
				Drain();
			}
			catch (...)
			{
			}
		}

		FrameContextRing(const FrameContextRing&) = delete;
		FrameContextRing& operator=(const FrameContextRing&) = delete;

		FrameContext& BeginFrame()
		{
			if (currentContext != nullptr)
			{
				throw std::logic_error("Cannot begin a new RHI frame before submitting the current frame");
			}

			CollectCompleted();

			FrameContext& context = contexts[nextContextIndex];
			if (context.CompletionValue != 0 && timeline->GetCompletedValue() < context.CompletionValue)
			{
				if (!timeline->Wait(context.CompletionValue))
				{
					throw std::runtime_error("Failed waiting for an RHI frame context timeline value");
				}
			}

			context.commandLists.clear();
			context.retiredObjects.clear();
			context.commandPool->Reset();
			context.CompletionValue = 0;

			currentContext = &context;
			nextContextIndex = (nextContextIndex + 1) % static_cast<std::uint32_t>(contexts.size());
			return context;
		}

		CommandList& CreateCommandList()
		{
			if (currentContext == nullptr)
			{
				throw std::logic_error("Cannot allocate an RHI frame command list before BeginFrame");
			}

			auto commandList = currentContext->commandPool->CreateCommandList();
			if (!commandList)
			{
				throw std::runtime_error("Failed to allocate an RHI frame command list");
			}

			currentContext->commandLists.push_back(std::move(commandList));
			return *currentContext->commandLists.back();
		}

		std::uint64_t SubmitCurrent(const SubmitDesc& desc)
		{
			if (currentContext == nullptr)
			{
				throw std::logic_error("Cannot submit an RHI frame before BeginFrame");
			}

			const std::uint64_t signalValue = nextSignalValue;
			std::vector<TimelinePoint> signals(desc.SignalTimelines.begin(), desc.SignalTimelines.end());
			for (const TimelinePoint& signal : signals)
			{
				if (signal.Semaphore == timeline.get())
				{
					throw std::invalid_argument("Frame timeline signaling is owned by FrameContextRing");
				}
			}
			signals.push_back({ timeline.get(), signalValue });

			SubmitDesc submit = desc;
			submit.SignalTimelines = signals;
			queue.Submit(submit);
			++nextSignalValue;

			currentContext->CompletionValue = signalValue;
			lastSubmittedValue = signalValue;
			currentContext = nullptr;
			return signalValue;
		}

		std::uint64_t SubmitCurrent()
		{
			if (currentContext == nullptr)
			{
				throw std::logic_error("Cannot submit an RHI frame before BeginFrame");
			}

			std::vector<CommandList*> commandLists;
			commandLists.reserve(currentContext->commandLists.size());
			for (const auto& commandList : currentContext->commandLists)
			{
				commandLists.push_back(commandList.get());
			}

			SubmitDesc desc{};
			desc.CommandLists = commandLists;
			return SubmitCurrent(desc);
		}

		template <typename ObjectType>
		void Retire(std::unique_ptr<ObjectType>&& object)
		{
			static_assert(std::is_base_of_v<RhiObject, ObjectType>, "Frame retirement requires an RHI object");
			if (!object)
			{
				return;
			}
			if (currentContext == nullptr)
			{
				throw std::logic_error("Frame-scoped RHI retirement requires an active frame context");
			}

			currentContext->retiredObjects.push_back(std::move(object));
		}

		template <typename ObjectType>
		void RetireAt(std::uint64_t completionValue, std::unique_ptr<ObjectType>&& object)
		{
			static_assert(std::is_base_of_v<RhiObject, ObjectType>, "Frame retirement requires an RHI object");
			if (!object)
			{
				return;
			}
			if (completionValue > lastSubmittedValue)
			{
				throw std::invalid_argument("Cannot retire an RHI object against an unscheduled frame timeline value");
			}
			if (completionValue == 0 || completionValue <= timeline->GetCompletedValue())
			{
				return;
			}

			deferredObjects.push_back({ completionValue, std::move(object) });
		}

		void CollectCompleted()
		{
			const std::uint64_t completedValue = timeline->GetCompletedValue();
			deferredObjects.erase(
				std::remove_if(
					deferredObjects.begin(),
					deferredObjects.end(),
					[completedValue](const DeferredObject& object)
					{
						return object.CompletionValue <= completedValue;
					}),
				deferredObjects.end());
		}

		void Drain()
		{
			std::uint64_t drainValue = lastSubmittedValue;
			for (const DeferredObject& object : deferredObjects)
			{
				drainValue = std::max(drainValue, object.CompletionValue);
			}
			if (drainValue != 0 && timeline->GetCompletedValue() < drainValue)
			{
				if (!timeline->Wait(drainValue))
				{
					throw std::runtime_error("Failed draining the RHI frame timeline");
				}
			}

			for (FrameContext& context : contexts)
			{
				context.commandLists.clear();
				context.retiredObjects.clear();
				context.CompletionValue = 0;
			}
			deferredObjects.clear();
			currentContext = nullptr;
		}

		Timeline& GetTimeline() const
		{
			return *timeline;
		}

		std::uint64_t GetLastSubmittedValue() const
		{
			return lastSubmittedValue;
		}

		TimelinePoint GetLastSubmittedPoint() const
		{
			return { timeline.get(), lastSubmittedValue };
		}

		FrameContext* GetCurrentContext() const
		{
			return currentContext;
		}

	private:
		struct DeferredObject
		{
			std::uint64_t CompletionValue = 0;
			std::unique_ptr<RhiObject> Object;
		};

		FrameContextRing(
			Queue& queue,
			std::unique_ptr<Timeline> timeline,
			std::vector<FrameContext> contexts)
			: queue(queue), timeline(std::move(timeline)), contexts(std::move(contexts))
		{
		}

		Queue& queue;
		std::unique_ptr<Timeline> timeline;
		std::vector<FrameContext> contexts;
		std::vector<DeferredObject> deferredObjects;
		FrameContext* currentContext = nullptr;
		std::uint32_t nextContextIndex = 0;
		std::uint64_t nextSignalValue = 1;
		std::uint64_t lastSubmittedValue = 0;
	};

} // namespace Swim::Rhi
