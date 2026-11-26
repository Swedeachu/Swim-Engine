#include "Engine/Systems/Renderer/RenderGraph/RenderGraph.h"
#include "Engine/Systems/Renderer/RenderGraph/Internal/GraphValidation.h"
#include "Engine/Systems/Renderer/RenderGraph/Internal/GraphHazard.h"
#include <algorithm>
#include <numeric>
#include <queue>
#include <span>

namespace Swim::Render
{

	namespace
	{
		bool SameTransition(const GraphBarrier& a, const GraphBarrier& b)
		{
			return a.Resource == b.Resource && a.Before == b.Before && a.After == b.After;
		}

		// Per-subresource barriers merged into ranges: mips of a layer, then layers with the same
		// mip range (a whole texture array becomes one barrier). A resource that takes two
		// transitions of the same subresource in one list keeps its barriers in order.
		void CoalesceBarriers(const Internal::GraphDefinition& definition, std::vector<GraphBarrier>& barriers)
		{
			if (barriers.size() < 2)
			{
				return;
			}

			std::stable_sort(barriers.begin(), barriers.end(),
				[](const GraphBarrier& a, const GraphBarrier& b)
				{
					return a.Resource < b.Resource;
				});
			std::vector<GraphBarrier> result;
			result.reserve(barriers.size());

			for (std::size_t begin = 0; begin < barriers.size();)
			{
				std::size_t end = begin + 1;

				while (end < barriers.size() && barriers[end].Resource == barriers[begin].Resource)
				{
					++end;
				}

				const auto& resource = definition.Resources[barriers[begin].Resource];
				auto group = std::span(barriers).subspan(begin, end - begin);
				bool distinct = true;

				if (group.size() > 1)
				{
					std::vector<std::uint32_t> cells;
					cells.reserve(group.size());
					const auto mips = resource.Kind == GraphKind::Texture ? resource.Texture.MipLevels : 1u;

					for (const auto& barrier : group)
					{
						cells.push_back(barrier.Range.BaseArrayLayer * mips + barrier.Range.BaseMipLevel);
					}

					std::sort(cells.begin(), cells.end());
					distinct = std::adjacent_find(cells.begin(), cells.end()) == cells.end();
				}

				if (!distinct || resource.Kind != GraphKind::Texture)
				{
					// Buffers have one subresource: identical repeats are dropped.
					for (const auto& barrier : group)
					{
						if (!distinct || result.empty() || !SameTransition(result.back(), barrier) || resource.Kind == GraphKind::Texture)
						{
							result.push_back(barrier);
						}
					}

					begin = end;
					continue;
				}

				std::sort(group.begin(), group.end(),
					[](const GraphBarrier& a, const GraphBarrier& b)
					{
						if (a.Before != b.Before)
						{
							return a.Before < b.Before;
						}

						if (a.After != b.After)
						{
							return a.After < b.After;
						}

						if (a.Range.BaseArrayLayer != b.Range.BaseArrayLayer)
						{
							return a.Range.BaseArrayLayer < b.Range.BaseArrayLayer;
						}

						return a.Range.BaseMipLevel < b.Range.BaseMipLevel;
					});
				const std::size_t first = result.size();

				for (const auto& barrier : group)
				{
					auto* last = result.size() > first ? &result.back() : nullptr;

					if (last && SameTransition(*last, barrier) && last->Range.BaseArrayLayer == barrier.Range.BaseArrayLayer &&
						last->Range.ArrayLayerCount == 1 && barrier.Range.ArrayLayerCount == 1 &&
						last->Range.BaseMipLevel + last->Range.MipLevelCount == barrier.Range.BaseMipLevel)
					{
						last->Range.MipLevelCount += barrier.Range.MipLevelCount;
					}
					else
					{
						result.push_back(barrier);
					}
				}

				std::size_t write = first;

				for (std::size_t read = first; read < result.size(); ++read)
				{
					auto& last = result[write - (write > first ? 1 : 0)];

					if (write > first && SameTransition(last, result[read]) && last.Range.BaseMipLevel == result[read].Range.BaseMipLevel &&
						last.Range.MipLevelCount == result[read].Range.MipLevelCount &&
						last.Range.BaseArrayLayer + last.Range.ArrayLayerCount == result[read].Range.BaseArrayLayer)
					{
						last.Range.ArrayLayerCount += result[read].Range.ArrayLayerCount;
					}
					else
					{
						result[write++] = result[read];
					}
				}

				result.resize(write);
				begin = end;
			}

			barriers = std::move(result);
		}
	} // namespace

	std::shared_ptr<const Internal::GraphShape> Internal::BuildGraphShape(const GraphDefinition& definition)
	{
		constexpr auto unused = GraphResourceLifetime::Unused;

		auto shape = std::make_shared<GraphShape>();
		auto& result = *shape;
		const auto passCount = static_cast<std::uint32_t>(definition.Passes.size());
		const auto resourceCount = static_cast<std::uint32_t>(definition.Resources.size());

		auto& edges = result.Dependencies;
		edges.resize(passCount);
		std::vector<std::vector<std::uint32_t>> inputs(passCount);
		std::vector<bool> live(passCount, false);
		std::vector<std::vector<GraphHazard>> hazards;

		for (const auto& r : definition.Resources)
		{
			hazards.emplace_back(CellCount(r));
		}

		const auto add = [](auto& list, std::uint32_t value)
		{
			if (value != unused && std::find(list.begin(), list.end(), value) == list.end())
			{
				list.push_back(value);
			}
		};

		for (std::uint32_t p = 0; p < passCount; ++p)
		{
			const auto& pass = definition.Passes[p];
			live[p] = pass.SideEffect;

			for (auto dependency : pass.Dependencies)
			{
				add(edges[p], dependency);
				add(inputs[p], dependency);
			}

			for (const auto& use : pass.Uses)
			{
				const auto& r = definition.Resources[use.Resource];

				if (r.Imported && use.Access != GraphAccess::Read)
				{
					live[p] = true;
				}

				// Neighbouring cells usually share a writer: skip repeated lookups.
				auto lastWriter = unused;
				ForEachCell(r, use.Range,
					[&](std::uint32_t cell)
					{
						auto& hazard = hazards[use.Resource][cell];

						if (hazard.Writer != lastWriter)
						{
							lastWriter = hazard.Writer;
							add(edges[p], hazard.Writer);

							if (use.Access != GraphAccess::Write)
							{
								add(inputs[p], hazard.Writer);
							}
						}

						if (use.Access != GraphAccess::Write && hazard.Writer == unused && !r.InitiallyDefined())
						{
							throw std::invalid_argument("RenderGraph read before write: pass '" + pass.Name + "', resource '" + r.Name +
								"', subresource " + std::to_string(cell));
						}

						if (use.Access == GraphAccess::Read)
						{
							add(hazard.Readers, p);
						}
						else
						{
							for (auto reader : hazard.Readers)
							{
								add(edges[p], reader);
							}

							hazard.Readers.clear();
							hazard.Writer = p;
						}
					});
			}
		}

		for (std::uint32_t r = 0; r < resourceCount; ++r)
		{
			if (!definition.Resources[r].Exported)
			{
				continue;
			}

			for (const auto& hazard : hazards[r])
			{
				if (hazard.Writer != unused)
				{
					live[hazard.Writer] = true;
				}
				else if (!definition.Resources[r].InitiallyDefined())
				{
					throw std::invalid_argument("RenderGraph exports uninitialized contents: " + definition.Resources[r].Name);
				}
			}
		}

		// Validate the complete DAG, including dead passes, before culling. A min-heap
		// makes otherwise independent passes deterministic in declaration order.
		std::vector<std::vector<std::uint32_t>> successors(passCount);
		std::vector<std::uint32_t> indegree(passCount);
		std::priority_queue<std::uint32_t, std::vector<std::uint32_t>, std::greater<>> ready;

		for (std::uint32_t p = 0; p < passCount; ++p)
		{
			indegree[p] = static_cast<std::uint32_t>(edges[p].size());

			for (auto dependency : edges[p])
			{
				successors[dependency].push_back(p);
			}

			if (!indegree[p])
			{
				ready.push(p);
			}
		}

		std::vector<std::uint32_t> order;

		while (!ready.empty())
		{
			const auto p = ready.top();
			ready.pop();
			order.push_back(p);

			for (auto next : successors[p])
			{
				if (!--indegree[next])
				{
					ready.push(next);
				}
			}
		}

		if (order.size() != passCount)
		{
			std::string names;

			for (std::uint32_t p = 0; p < passCount; ++p)
			{
				if (indegree[p])
				{
					names += " '" + definition.Passes[p].Name + "'";
				}
			}

			throw std::invalid_argument("RenderGraph dependency cycle involving:" + names);
		}

		// Only data producers and explicit prerequisites keep dead work alive. WAR
		// and WAW ordering edges alone do not keep overwritten transient writes.
		std::vector<std::uint32_t> stack;

		for (std::uint32_t p = 0; p < passCount; ++p)
		{
			if (live[p])
			{
				stack.push_back(p);
			}
		}

		while (!stack.empty())
		{
			const auto p = stack.back();
			stack.pop_back();

			for (auto input : inputs[p])
			{
				if (!live[input])
				{
					live[input] = true;
					stack.push_back(input);
				}
			}
		}

		for (auto p : order)
		{
			if (live[p])
			{
				result.Schedule.push_back({ p, {} });
			}
		}

		result.Lifetimes.resize(resourceCount);

		for (std::uint32_t position = 0; position < result.Schedule.size(); ++position)
		{
			for (const auto& use : definition.Passes[result.Schedule[position].Pass].Uses)
			{
				auto& life = result.Lifetimes[use.Resource];

				if (life.First == unused)
				{
					life.First = position;
				}

				life.Last = position;
			}
		}

		for (std::uint32_t r = 0; r < resourceCount; ++r)
		{
			if (definition.Resources[r].Exported)
			{
				auto& life = result.Lifetimes[r];

				if (life.First == unused)
				{
					life.First = 0;
				}

				life.Last = static_cast<std::uint32_t>(result.Schedule.size());
			}
		}

		std::vector<std::uint32_t> resources(resourceCount);
		std::iota(resources.begin(), resources.end(), 0);
		std::stable_sort(resources.begin(), resources.end(),
			[&](auto a, auto b)
			{
				return result.Lifetimes[a].First < result.Lifetimes[b].First;
			});

		std::vector<std::uint32_t> slotLast;

		for (auto r : resources)
		{
			auto& life = result.Lifetimes[r];

			if (life.First == unused)
			{
				continue;
			}

			for (std::uint32_t slot = 0; slot < result.Allocations.size(); ++slot)
			{
				const auto& candidate = definition.Resources[result.Allocations[slot]];

				if (candidate.Poolable() && definition.Resources[r].Poolable() && slotLast[slot] < life.First &&
					Compatible(candidate, definition.Resources[r]))
				{
					life.Allocation = slot;
					break;
				}
			}

			if (life.Allocation == unused)
			{
				life.Allocation = static_cast<std::uint32_t>(result.Allocations.size());
				result.Allocations.push_back(r);
				slotLast.push_back(0);
			}

			slotLast[life.Allocation] = life.Last;
		}

		std::vector<std::vector<Rhi::ResourceState>> states;
		std::vector<std::vector<bool>> writeHazards;

		for (auto r : result.Allocations)
		{
			states.emplace_back(CellCount(definition.Resources[r]), definition.Resources[r].Initial);
			// The external producer may have written even when the import state is a
			// read state. A first-use barrier provides conservative visibility.
			writeHazards.emplace_back(CellCount(definition.Resources[r]), true);
		}

		const auto transition = [&](std::uint32_t r, std::uint32_t cell, Rhi::ResourceState after, bool writes, auto& barriers)
		{
			const auto slot = result.Lifetimes[r].Allocation;
			auto& before = states[slot][cell];

			if (before != after || writeHazards[slot][cell] || writes)
			{
				Rhi::TextureSubresourceRange range{ 0, 1, 0, 1 };

				if (definition.Resources[r].Kind == GraphKind::Texture)
				{
					range = { cell % definition.Resources[r].Texture.MipLevels, 1, cell / definition.Resources[r].Texture.MipLevels, 1 };
				}

				barriers.push_back({ r, before, after, range });
			}

			before = after;
			writeHazards[slot][cell] = writes;
		};

		for (auto& scheduled : result.Schedule)
		{
			for (const auto& use : definition.Passes[scheduled.Pass].Uses)
			{
				ForEachCell(definition.Resources[use.Resource], use.Range,
					[&](std::uint32_t cell)
					{
						transition(use.Resource, cell, use.State, use.Access != GraphAccess::Read, scheduled.Barriers);
					});
			}
		}

		for (std::uint32_t r = 0; r < resourceCount; ++r)
		{
			const auto& resource = definition.Resources[r];

			if (result.Lifetimes[r].First == unused || (!resource.Exported && !resource.Imported))
			{
				continue;
			}

			const auto final = resource.Exported ? resource.Final : resource.Initial;

			if (final == Rhi::ResourceState::Undefined)
			{
				throw std::invalid_argument("An initially undefined imported resource needs an explicit export state: " + resource.Name);
			}

			for (std::uint32_t cell = 0; cell < CellCount(resource); ++cell)
			{
				transition(r, cell, final, false, result.FinalBarriers);
			}
		}

		for (auto& scheduled : result.Schedule)
		{
			CoalesceBarriers(definition, scheduled.Barriers);
		}

		CoalesceBarriers(definition, result.FinalBarriers);
		return shape;
	}

	void Internal::AppendGraphShapeKey(const GraphDefinition& definition, std::vector<std::uint64_t>& key)
	{
		const auto u = [](auto value)
		{
			return static_cast<std::uint64_t>(value);
		};
		key.push_back(definition.Resources.size());
		key.push_back(definition.Passes.size());

		for (const auto& r : definition.Resources)
		{
			key.push_back(u(r.Kind) | u(r.Staging) << 8 | u(r.Imported != nullptr) << 16 | u(r.Exported) << 17);
			key.push_back(u(r.Initial) | u(r.Final) << 32);

			if (r.Kind == GraphKind::Buffer)
			{
				// Only pooled buffers are allocated from the shape; staged and imported sizes
				// (uploads that grow and shrink every frame) would otherwise miss the cache.
				key.push_back(r.Poolable() ? Internal::PooledBufferSize(r.Buffer.Size) : 0);
				key.push_back(u(r.Buffer.Usage) | u(r.Buffer.Memory) << 32 | u(r.Buffer.PersistentMap) << 40);
			}
			else
			{
				const auto& x = r.Texture;
				key.push_back(u(x.Extent.Width) | u(x.Extent.Height) << 32);
				key.push_back(u(x.Extent.Depth) | u(x.PixelFormat) << 32);
				key.push_back(u(x.Usage) | u(x.Dimension) << 32 | u(x.Samples) << 40);
				key.push_back(u(x.MipLevels) | u(x.ArrayLayers) << 32);
			}
		}

		for (const auto& pass : definition.Passes)
		{
			key.push_back(u(pass.Type) | u(pass.SideEffect) << 8 | u(pass.Uses.size()) << 16 | u(pass.Dependencies.size()) << 40);

			for (const auto& use : pass.Uses)
			{
				key.push_back(u(use.Resource) | u(use.Access) << 32);
				key.push_back(u(use.State));
				key.push_back(u(use.Range.BaseMipLevel) | u(use.Range.MipLevelCount) << 32);
				key.push_back(u(use.Range.BaseArrayLayer) | u(use.Range.ArrayLayerCount) << 32);
			}

			for (const auto dependency : pass.Dependencies)
			{
				key.push_back(dependency);
			}
		}
	}

	CompiledRenderGraph RenderGraph::Compile() const
	{
		CompiledRenderGraph result;
		result.shape = Internal::BuildGraphShape(definition);
		result.definition = std::make_shared<Internal::GraphDefinition>(definition);
		return result;
	}

	CompiledRenderGraph RenderGraph::Compile(RenderGraphCompileCache& cache)
	{
		auto& key = cache.scratch;
		key.clear();
		Internal::AppendGraphShapeKey(definition, key);
		CompiledRenderGraph result;

		auto& entries = cache.entries;
		const auto found = std::find_if(entries.rbegin(), entries.rend(),
			[&](const RenderGraphCompileCache::Entry& entry)
			{
				return entry.Key == key;
			});

		if (found != entries.rend())
		{
			++cache.hits;
			result.shape = found->Shape;
			std::rotate(found.base() - 1, found.base(), entries.end()); // Most recent last.
		}
		else
		{
			result.shape = Internal::BuildGraphShape(definition);
			++cache.misses;

			if (entries.size() >= RenderGraphCompileCache::Capacity)
			{
				entries.erase(entries.begin());
			}

			entries.push_back({ std::move(key), result.shape });
			key = {};
		}

		result.definition = std::make_shared<Internal::GraphDefinition>(std::move(definition));
		definition = {};
		return result;
	}

} // namespace Swim::Render
