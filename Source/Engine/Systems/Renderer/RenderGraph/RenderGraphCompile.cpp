#include "Engine/Systems/Renderer/RenderGraph/RenderGraph.h"
#include "Engine/Systems/Renderer/RenderGraph/Internal/GraphValidation.h"
#include "Engine/Systems/Renderer/RenderGraph/Internal/GraphHazard.h"
#include <algorithm>
#include <numeric>
#include <queue>

namespace Swim::Render
{
	CompiledRenderGraph RenderGraph::Compile() const
	{
		using namespace Internal;
		constexpr auto unused = GraphResourceLifetime::Unused;

		CompiledRenderGraph result;
		result.definition = std::make_shared<GraphDefinition>(definition);
		const auto passCount = static_cast<std::uint32_t>(definition.Passes.size());
		const auto resourceCount = static_cast<std::uint32_t>(definition.Resources.size());

		auto& edges = result.dependencies;
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

				for (auto cell : Cells(r, use.Range))
				{
					auto& hazard = hazards[use.Resource][cell];
					add(edges[p], hazard.Writer);

					if (use.Access != GraphAccess::Write)
					{
						if (hazard.Writer == unused && (!r.Imported || r.Initial == Rhi::ResourceState::Undefined))
						{
							throw std::invalid_argument("RenderGraph read before write: pass '" + pass.Name + "', resource '" + r.Name +
								"', subresource " + std::to_string(cell));
						}
						add(inputs[p], hazard.Writer);
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
				}
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
				else if (!definition.Resources[r].Imported || definition.Resources[r].Initial == Rhi::ResourceState::Undefined)
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
				result.schedule.push_back({ p, {} });
			}
		}

		result.lifetimes.resize(resourceCount);
		for (std::uint32_t position = 0; position < result.schedule.size(); ++position)
		{
			for (const auto& use : definition.Passes[result.schedule[position].Pass].Uses)
			{
				auto& life = result.lifetimes[use.Resource];
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
				auto& life = result.lifetimes[r];
				if (life.First == unused)
				{
					life.First = 0;
				}
				life.Last = static_cast<std::uint32_t>(result.schedule.size());
			}
		}

		std::vector<std::uint32_t> resources(resourceCount);
		std::iota(resources.begin(), resources.end(), 0);
		std::stable_sort(resources.begin(), resources.end(),
			[&](auto a, auto b)
			{
				return result.lifetimes[a].First < result.lifetimes[b].First;
			});

		std::vector<std::uint32_t> slotLast;
		for (auto r : resources)
		{
			auto& life = result.lifetimes[r];
			if (life.First == unused)
			{
				continue;
			}
			for (std::uint32_t slot = 0; slot < result.allocations.size(); ++slot)
			{
				const auto& candidate = definition.Resources[result.allocations[slot]];
				if (!candidate.Imported && !definition.Resources[r].Imported && slotLast[slot] < life.First &&
					Compatible(candidate, definition.Resources[r]))
				{
					life.Allocation = slot;
					break;
				}
			}
			if (life.Allocation == unused)
			{
				life.Allocation = static_cast<std::uint32_t>(result.allocations.size());
				result.allocations.push_back(r);
				slotLast.push_back(0);
			}
			slotLast[life.Allocation] = life.Last;
		}

		std::vector<std::vector<Rhi::ResourceState>> states;
		std::vector<std::vector<bool>> writeHazards;
		for (auto r : result.allocations)
		{
			states.emplace_back(CellCount(definition.Resources[r]), definition.Resources[r].Initial);
			// The external producer may have written even when the import state is a
			// read state. A first-use barrier provides conservative visibility.
			writeHazards.emplace_back(CellCount(definition.Resources[r]), true);
		}

		const auto transition = [&](std::uint32_t r, std::uint32_t cell, Rhi::ResourceState after, bool writes, auto& barriers)
		{
			const auto slot = result.lifetimes[r].Allocation;
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

		for (auto& scheduled : result.schedule)
		{
			for (const auto& use : definition.Passes[scheduled.Pass].Uses)
			{
				for (auto cell : Cells(definition.Resources[use.Resource], use.Range))
				{
					transition(use.Resource, cell, use.State, use.Access != GraphAccess::Read, scheduled.Barriers);
				}
			}
		}
		for (std::uint32_t r = 0; r < resourceCount; ++r)
		{
			const auto& resource = definition.Resources[r];
			if (result.lifetimes[r].First == unused || (!resource.Exported && !resource.Imported))
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
				transition(r, cell, final, false, result.finalBarriers);
			}
		}

		return result;
	}
} // namespace Swim::Render
