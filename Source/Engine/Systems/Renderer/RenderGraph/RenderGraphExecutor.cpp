#include "Engine/Systems/Renderer/RenderGraph/RenderGraphExecutor.h"

#include <chrono>
#include "Engine/Systems/Renderer/RenderGraph/Internal/GraphExecutionState.h"
#include "Engine/Systems/Renderer/RenderGraph/Internal/GraphValidation.h"
#include <algorithm>
#include <unordered_map>

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
		state->Tables.clear();
		state->Queries.reset();
		state->Views.clear();
		state->Pool.clear();
		state->ReadbackSlices.clear();
		state->Upload.reset();
		state->Readback.reset();
	}

	void RenderGraphExecutor::RecycleDescriptorTables(const CompiledRenderGraph& graph)
	{
		// The previous execution has completed (Wait): its tables can be rewritten. Keep them
		// while no pipeline layout has been destroyed (a new layout could reuse a cached one's
		// address); a table is only reused for the same layout and space, and passes write
		// every binding they use, so graphs of different shapes (frames alternating between a
		// few) share them too.
		const auto generation = state->Device.GetPipelineLayoutGeneration();
		const bool keep = generation != UINT64_MAX && generation == state->TablesLayoutGeneration;

		if (keep)
		{
			std::unordered_map<const Rhi::RhiObject*, std::size_t> retained;
			retained.reserve(state->Retained.size());

			for (std::size_t i = 0; i < state->Retained.size(); ++i)
			{
				retained.emplace(state->Retained[i].get(), i);
			}

			for (const auto& issued : state->IssuedTables)
			{
				const auto at = retained.find(issued.Table);

				if (at == retained.end() || !state->Retained[at->second])
				{
					continue;
				}

				auto& slot = state->Retained[at->second];
				auto* table = dynamic_cast<Rhi::DescriptorTable*>(slot.get());

				if (table && table->ReopenForWrites())
				{
					slot.release();
					state->Tables[{ issued.Layout, issued.Space }].push_back({ issued.Key, std::unique_ptr<Rhi::DescriptorTable>(table) });
				}
			}
		}
		else
		{
			state->Tables.clear();
		}

		state->IssuedTables.clear();
		state->TablesShape = graph.shape;
		state->TablesLayoutGeneration = generation;
	}

	std::size_t RenderGraphExecutor::GetCachedDescriptorTableCount() const
	{
		std::size_t count = 0;

		for (const auto& [slot, free] : state->Tables)
		{
			count += free.size();
		}

		return count;
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
		state->IssuedTables.clear();
		state->Tables.clear();
		state->TablesShape.reset();
		state->Queries.reset();
		state->Resources.clear();
		state->Views.clear();
		state->Pool.clear();
		state->PoolShape.reset();
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

	void RenderGraphExecutor::RecordBarriers(std::span<const GraphBarrier> barriers)
	{
		if (barriers.empty())
		{
			return;
		}

		// One dependency per pass instead of a barrier command per transition.
		thread_local std::vector<Rhi::CommandList::BufferBarrier> buffers;
		thread_local std::vector<Rhi::CommandList::TextureBarrier> textures;
		buffers.clear();
		textures.clear();

		for (const auto& barrier : barriers)
		{
			const auto& r = state->Graph.definition->Resources[barrier.Resource];
			auto* resource = state->Resources[barrier.Resource];

			if (r.Kind == GraphKind::Buffer)
			{
				buffers.push_back({ static_cast<Rhi::Buffer*>(resource), barrier.Before, barrier.After });
			}
			else
			{
				textures.push_back({ static_cast<Rhi::Texture*>(resource), barrier.Before, barrier.After, barrier.Range });
			}
		}

		state->Commands->Transitions(buffers, textures);
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

		using Clock = std::chrono::steady_clock;
		const auto ms = [](Clock::time_point a, Clock::time_point b)
		{
			return std::chrono::duration<double, std::milli>(b - a).count();
		};
		lastTimings = {};
		auto mark = Clock::now();
		Wait();
		lastTimings.Wait = ms(mark, Clock::now());
		mark = Clock::now();

		state->Commands.reset();
		state->CommandPool->Reset();
		RecycleDescriptorTables(graph);
		state->Retained.clear();
		// The timestamp pool is kept while it is large enough (it is reset in the command list).
		state->HasResult = false;
		lastTimings.Reset = ms(mark, Clock::now());
		mark = Clock::now();

		state->Graph = graph; // Own the immutable description and callbacks through completion.
		state->Resources.assign(graph.definition->Resources.size(), nullptr);

		std::vector<Internal::GraphPooledResource> nextPool;
		std::vector<Rhi::RhiObject*> slots;
		// Same shape as last time: the pool already holds this graph's transients, in order.
		const bool samePool = graph.shape == state->PoolShape;
		std::size_t pooled = 0;
		const auto execution = ++state->Executions;

		std::erase_if(state->Views,
			[&](const auto& entry)
			{
				return entry.second.Used + 1 < execution;
			});

		for (auto r : graph.shape->Allocations)
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

			if (samePool)
			{
				slots.push_back(state->Pool[pooled++].Object.get());
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
				creation.Size = Internal::PooledBufferSize(creation.Size);
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

		if (!samePool)
		{
			// Views of the allocations dropped below go first (moved ones stay valid).
			std::erase_if(state->Views,
				[&](const auto& entry)
				{
					return std::any_of(state->Pool.begin(), state->Pool.end(),
						[&](const auto& old)
						{
							return old.Object && old.Object.get() == entry.first.Texture;
						});
				});
			state->Pool = std::move(nextPool); // Drop incompatible old allocations, bounded by the current graph.
			state->PoolShape = graph.shape;
		}

		for (std::uint32_t r = 0; r < graph.shape->Lifetimes.size(); ++r)
		{
			if (graph.shape->Lifetimes[r].Allocation != GraphResourceLifetime::Unused)
			{
				state->Resources[r] = slots[graph.shape->Lifetimes[r].Allocation];
			}
		}

		lastTimings.Allocate = ms(mark, Clock::now());
		mark = Clock::now();
		StageBuffers(graph); // Runs upload writers; failure leaves no published result.
		lastTimings.Stage = ms(mark, Clock::now());
		mark = Clock::now();

		if (graph.shape->Schedule.empty() || !state->Device.GetQueue(Rhi::QueueType::Graphics).GetTimestampInfo().IsSupported())
		{
			state->Queries.reset();
		}
		else
		{
			if (graph.shape->Schedule.size() > UINT32_MAX / 2)
			{
				throw std::overflow_error("RenderGraph timestamp count overflow");
			}

			const auto needed = static_cast<std::uint32_t>(graph.shape->Schedule.size() * 2);

			if (!state->Queries || state->Queries->GetDesc().Count < needed)
			{
				// Room to grow, so a frame with a few more passes does not recreate it.
				state->Queries = state->Device.CreateQueryPool({ Rhi::QueryType::Timestamp, needed + needed / 2 + 16, "RenderGraph passes" });
			}

			if (!state->Queries)
			{
				throw std::runtime_error("RenderGraph timestamp pool allocation failed");
			}
		}

		lastTimings.Queries = ms(mark, Clock::now());
		mark = Clock::now();
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

			for (const auto& scheduled : graph.shape->Schedule)
			{
				const auto& pass = graph.definition->Passes[scheduled.Pass];
				state->Commands->BeginDebugLabel(pass.Name);

				if (state->Queries)
				{
					state->Commands->WriteTimestamp(*state->Queries, query++, Rhi::TimestampStage::Begin);
				}

				const auto barrierStart = Clock::now();

				RecordBarriers(scheduled.Barriers);

				const auto passStart = Clock::now();
				lastTimings.Barriers += ms(barrierStart, passStart);
				RenderCommandContext context(*state, scheduled.Pass);
				pass.Execute(context);
				lastTimings.Passes.push_back(
					{ pass.Name, std::chrono::duration<double, std::nano>(Clock::now() - passStart).count(), std::nullopt });

				if (state->Queries)
				{
					state->Commands->WriteTimestamp(*state->Queries, query++, Rhi::TimestampStage::End);
				}

				state->Commands->EndDebugLabel();
			}

			RecordBarriers(graph.shape->FinalBarriers);

			state->Commands->End();
			lastTimings.Record = ms(mark, Clock::now());
			mark = Clock::now();

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
			lastTimings.Submit = ms(mark, Clock::now());

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
		std::vector<Rhi::TimestampResult> ticks(state->Graph.shape->Schedule.size() * 2);

		if (state->Queries && state->Queries->ReadTimestamps(0, ticks) != Rhi::QueryReadStatus::Ready)
		{
			throw std::runtime_error("RenderGraph timestamps unavailable after completion");
		}

		for (std::size_t i = 0; i < state->Graph.shape->Schedule.size(); ++i)
		{
			GraphPassTiming timing{ state->Graph.definition->Passes[state->Graph.shape->Schedule[i].Pass].Name, {}, {} };

			if (state->Queries)
			{
				const auto info = state->Queries->GetTimestampInfo();
				timing.Nanoseconds = info.ElapsedNanoseconds(ticks[2 * i], ticks[2 * i + 1]);
				timing.EndOffsetNanoseconds = info.ElapsedNanoseconds(ticks[0], ticks[2 * i + 1]);
			}

			result.push_back(std::move(timing));
		}

		return result;
	}

} // namespace Swim::Render
