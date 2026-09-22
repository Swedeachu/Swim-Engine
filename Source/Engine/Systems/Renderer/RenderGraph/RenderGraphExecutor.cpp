#include "Engine/Systems/Renderer/RenderGraph/RenderGraphExecutor.h"
#include "Engine/Systems/Renderer/RenderGraph/Internal/GraphExecutionState.h"
#include "Engine/Systems/Renderer/RenderGraph/Internal/GraphValidation.h"
#include <algorithm>

namespace Swim::Render
{
	RenderGraphExecutor::RenderGraphExecutor(Rhi::Device& device, const RenderGraphExecutorDesc& desc)
		: state(std::make_unique<Internal::GraphExecutionState>(device, desc))
	{
		state->Timeline = std::shared_ptr<Rhi::Timeline>(device.CreateTimeline());
		state->CommandPool = device.CreateCommandPool(Rhi::QueueType::Graphics);

		if (!state->Timeline || !state->CommandPool)
		{
			throw std::runtime_error("RenderGraph could not create execution synchronization/command pool");
		}
		CreateInitialStaging();
	}

	RenderGraphExecutor::~RenderGraphExecutor()
	{
		try
		{
			Wait();
		}
		catch (...)
		{
			try
			{
				state->Device.GetQueue(Rhi::QueueType::Graphics).WaitIdle();
			}
			catch (...)
			{
			}
		}

		// Explicitly release recorded objects before their referenced allocations.
		state->Commands.reset();
		state->Retained.clear();
		state->Queries.reset();
		state->Pool.clear();
		state->ReadbackSlices.clear();
		state->Upload.reset();
		state->Readback.reset();
	}

	void RenderGraphExecutor::Wait()
	{
		if (state->Recording)
		{
			throw std::logic_error("RenderGraph executor cannot be reentered from a pass");
		}

		if (state->Submitted && state->Timeline->GetCompletedValue() < state->Submitted && !state->Timeline->Wait(state->Submitted))
		{
			throw std::runtime_error("RenderGraph completion wait failed; resources have not been recycled");
		}
	}

	void RenderGraphExecutor::Trim()
	{
		Wait();

		state->Commands.reset();
		state->CommandPool->Reset();
		state->Retained.clear();
		state->Queries.reset();
		state->Resources.clear();
		state->Pool.clear();
		state->Ranges.clear();
		state->ReadbackSlices.clear();
		state->Upload.reset();
		state->Readback.reset();

		state->Graph = {};
		state->HasResult = false;
	}

	std::size_t RenderGraphExecutor::GetPooledResourceCount() const
	{
		return state->Pool.size();
	}

	void RenderGraphExecutor::RecordBarrier(const GraphBarrier& barrier)
	{
		const auto& r = state->Graph.definition->Resources[barrier.Resource];
		auto* resource = state->Resources[barrier.Resource];

		if (r.Kind == GraphKind::Buffer)
		{
			state->Commands->Transition(static_cast<Rhi::Buffer&>(*resource), barrier.Before, barrier.After);
		}
		else
		{
			state->Commands->Transition(static_cast<Rhi::Texture&>(*resource), barrier.Before, barrier.After, barrier.Range);
		}
	}

	Rhi::TimelinePoint RenderGraphExecutor::Execute(const CompiledRenderGraph& graph, const Rhi::SubmitDesc& synchronization)
	{
		if (!graph.definition || !synchronization.CommandLists.empty())
		{
			throw std::invalid_argument("RenderGraph requires a compiled graph and owns submitted command lists");
		}

		for (const auto& point : synchronization.SignalTimelines)
		{
			if (point.Semaphore == state->Timeline.get())
			{
				throw std::invalid_argument("RenderGraph owns its completion timeline");
			}
		}

		for (const auto& point : synchronization.WaitTimelines)
		{
			if (point.Semaphore == state->Timeline.get() && point.Value > state->Submitted)
			{
				throw std::invalid_argument("RenderGraph cannot wait for its own future submission");
			}
		}

		if (state->Submitted == UINT64_MAX)
		{
			throw std::overflow_error("RenderGraph timeline exhausted");
		}

		Wait();

		state->Commands.reset();
		state->CommandPool->Reset();
		state->Retained.clear();
		state->Queries.reset();
		state->HasResult = false;

		state->Graph = graph; // Own the immutable description and callbacks through completion.
		state->Resources.assign(graph.definition->Resources.size(), nullptr);

		std::vector<Internal::GraphPooledResource> nextPool;
		std::vector<Rhi::RhiObject*> slots;
		for (auto r : graph.allocations)
		{
			const auto& desc = graph.definition->Resources[r];
			if (desc.Imported)
			{
				slots.push_back(desc.Imported);
				continue;
			}
			if (desc.Staging != Internal::GraphStaging::None)
			{
				slots.push_back(nullptr); // Bound to an arena suballocation by StageBuffers.
				continue;
			}

			auto found = std::find_if(state->Pool.begin(), state->Pool.end(),
				[&](const auto& pooled)
				{
					return pooled.Object && Internal::Compatible(pooled.Description, desc);
				});

			Internal::GraphPooledResource entry;
			entry.Description = desc;
			if (found != state->Pool.end())
			{
				entry.Object = std::move(found->Object);
			}
			else if (desc.Kind == GraphKind::Buffer)
			{
				auto creation = desc.Buffer;
				creation.DebugName = desc.Name;
				entry.Object = state->Device.CreateBuffer(creation);
			}
			else
			{
				auto creation = desc.Texture;
				creation.DebugName = desc.Name;
				entry.Object = state->Device.CreateTexture(creation);
			}

			if (!entry.Object)
			{
				throw std::runtime_error("RenderGraph resource allocation failed: " + desc.Name);
			}

			slots.push_back(entry.Object.get());
			nextPool.push_back(std::move(entry));
		}

		state->Pool = std::move(nextPool); // Drop incompatible old allocations, bounded by the current graph.
		for (std::uint32_t r = 0; r < graph.lifetimes.size(); ++r)
		{
			if (graph.lifetimes[r].Allocation != GraphResourceLifetime::Unused)
			{
				state->Resources[r] = slots[graph.lifetimes[r].Allocation];
			}
		}
		StageBuffers(graph); // Runs upload writers; failure leaves no published result.

		if (!graph.schedule.empty() && state->Device.GetQueue(Rhi::QueueType::Graphics).GetTimestampInfo().IsSupported())
		{
			if (graph.schedule.size() > UINT32_MAX / 2)
			{
				throw std::overflow_error("RenderGraph timestamp count overflow");
			}

			state->Queries = state->Device.CreateQueryPool(
				{ Rhi::QueryType::Timestamp, static_cast<std::uint32_t>(graph.schedule.size() * 2), "RenderGraph passes" });
			if (!state->Queries)
			{
				throw std::runtime_error("RenderGraph timestamp pool allocation failed");
			}
		}

		state->Commands = state->CommandPool->CreateCommandList();
		if (!state->Commands)
		{
			throw std::runtime_error("RenderGraph command list allocation failed");
		}

		state->Recording = true;
		try
		{
			state->Commands->Begin();
			if (state->Queries)
			{
				state->Commands->ResetQueries(*state->Queries, 0, state->Queries->GetDesc().Count);
			}

			std::uint32_t query = 0;
			for (const auto& scheduled : graph.schedule)
			{
				const auto& pass = graph.definition->Passes[scheduled.Pass];
				state->Commands->BeginDebugLabel(pass.Name);
				if (state->Queries)
				{
					state->Commands->WriteTimestamp(*state->Queries, query++, Rhi::TimestampStage::Begin);
				}
				for (const auto& barrier : scheduled.Barriers)
				{
					RecordBarrier(barrier);
				}

				RenderCommandContext context(*state, scheduled.Pass);
				pass.Execute(context);

				if (state->Queries)
				{
					state->Commands->WriteTimestamp(*state->Queries, query++, Rhi::TimestampStage::End);
				}
				state->Commands->EndDebugLabel();
			}

			for (const auto& barrier : graph.finalBarriers)
			{
				RecordBarrier(barrier);
			}
			state->Commands->End();

			const bool uploads = state->Upload && state->Upload->GetUsedBytes() != 0;
			const bool readbacks = state->Readback && state->Readback->GetUsedBytes() != 0;
			if (uploads)
			{
				state->Upload->Flush();
			}
			if (readbacks)
			{
				Rhi::ReadbackSubmission::Validate(*state->Readback);
			}

			std::vector<Rhi::TimelinePoint> signals(synchronization.SignalTimelines.begin(), synchronization.SignalTimelines.end());
			signals.push_back({ state->Timeline.get(), state->Submitted + 1 });
			auto* commands = state->Commands.get();
			auto submit = synchronization;
			submit.CommandLists = { &commands, 1 };
			submit.SignalTimelines = signals;
			state->Device.GetQueue(Rhi::QueueType::Graphics).Submit(submit);

			++state->Submitted;
			if (readbacks)
			{
				Rhi::ReadbackSubmission::Commit(*state->Readback, state->Timeline, state->Submitted);
			}
			state->HasResult = true;
			state->Recording = false;
			return { state->Timeline.get(), state->Submitted };
		}
		catch (...)
		{
			state->Recording = false;
			throw;
		}
	}

	Rhi::RhiObject& RenderGraphExecutor::GetExport(std::uint64_t graph, std::uint32_t index, GraphKind kind) const
	{
		if (!state->HasResult || state->Recording)
		{
			throw std::logic_error("RenderGraph has no successful execution result");
		}

		const auto& r = Internal::RequireResource(*state->Graph.definition, graph, index, kind);
		if (!r.Exported)
		{
			throw std::invalid_argument("Only exported RenderGraph resources may escape a pass");
		}
		if (r.Staging != Internal::GraphStaging::None)
		{
			throw std::invalid_argument("Staged RenderGraph readbacks are read through TryReadback: " + r.Name);
		}

		return *state->Resources[index];
	}

	Rhi::Buffer& RenderGraphExecutor::GetExported(GraphBuffer r) const
	{
		return static_cast<Rhi::Buffer&>(GetExport(r.Graph, r.Index, GraphKind::Buffer));
	}

	Rhi::Texture& RenderGraphExecutor::GetExported(GraphTexture r) const
	{
		return static_cast<Rhi::Texture&>(GetExport(r.Graph, r.Index, GraphKind::Texture));
	}

	std::vector<GraphPassTiming> RenderGraphExecutor::ReadTimings()
	{
		Wait();
		if (!state->HasResult)
		{
			throw std::logic_error("RenderGraph has no successful execution result");
		}

		std::vector<GraphPassTiming> result;
		std::vector<Rhi::TimestampResult> ticks(state->Graph.schedule.size() * 2);
		if (state->Queries && state->Queries->ReadTimestamps(0, ticks) != Rhi::QueryReadStatus::Ready)
		{
			throw std::runtime_error("RenderGraph timestamps unavailable after completion");
		}

		for (std::size_t i = 0; i < state->Graph.schedule.size(); ++i)
		{
			GraphPassTiming timing{ state->Graph.definition->Passes[state->Graph.schedule[i].Pass].Name, {} };
			if (state->Queries)
			{
				timing.Nanoseconds = state->Queries->GetTimestampInfo().ElapsedNanoseconds(ticks[2 * i], ticks[2 * i + 1]);
			}
			result.push_back(std::move(timing));
		}

		return result;
	}
} // namespace Swim::Render
