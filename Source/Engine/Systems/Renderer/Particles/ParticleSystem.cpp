#include "Engine/Systems/Renderer/Particles/ParticleSystem.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderCommandContext.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphTransfers.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>
#include <span>
#include <stdexcept>

namespace Swim::Render
{
	namespace
	{
		using S = Rhi::ResourceState;
		using B = ParticleSimulationBindings;

		constexpr std::uint64_t CommandBytes = sizeof(Rhi::DrawIndexedIndirectCommand);
		constexpr std::array<std::uint32_t, 6> QuadIndices{ 0, 1, 2, 2, 1, 3 };

		bool HasUsage(Rhi::TextureUsage usage, Rhi::TextureUsage required)
		{
			return (static_cast<std::uint32_t>(usage) & static_cast<std::uint32_t>(required)) != 0;
		}

		std::uint32_t Groups(std::uint32_t count, std::uint32_t size)
		{
			return (count + size - 1) / size;
		}

		// The persistent and per-frame buffers a pass may bind, by binding number.
		struct BufferSet
		{
			std::array<std::optional<GraphBuffer>, B::Count> Buffers;
		};

		template <std::size_t Count>
		void Dispatch(RenderCommandContext& c, const ParticleComputeProgram& program, const std::string& label, const BufferSet& set,
			const std::array<std::uint32_t, Count>& bindings, std::uint32_t x, std::uint32_t y, std::uint32_t z)
		{
			auto table = c.Device().CreateDescriptorTable({ program.Layout, 0, 0, label });
			if (!table)
			{
				throw std::runtime_error(label + " descriptor table could not be created");
			}
			std::array<Rhi::DescriptorWrite, Count> writes{};
			for (std::size_t i = 0; i < Count; ++i)
			{
				const auto range = c.GetRange(*set.Buffers[bindings[i]]);
				writes[i].Binding = bindings[i];
				writes[i].BufferResource = range.Buffer;
				writes[i].BufferOffset = range.Offset;
				writes[i].BufferRange = range.Size;
			}
			table->Write(writes);
			auto& retained = static_cast<Rhi::DescriptorTable&>(c.Retain(std::move(table)));
			auto& list = c.Commands();
			list.BindComputePipeline(*program.Pipeline);
			list.BindDescriptorTable(0, retained);
			list.Dispatch(x, y, z);
		}
	} // namespace

	Rhi::GraphicsPipelineDesc ParticleSystem::PipelineDesc(
		ParticleBlendMode blend, Rhi::ShaderProgram& program, Rhi::PipelineLayout& layout)
	{
		static constexpr std::array<Rhi::Format, 1> ColorFormats{ ColorFormat };
		static constexpr std::array<Rhi::BlendAttachmentState, 1> Additive{ Rhi::BlendAttachmentState{ true, Rhi::BlendFactor::One,
			Rhi::BlendFactor::One, Rhi::BlendOp::Add, Rhi::BlendFactor::Zero, Rhi::BlendFactor::One, Rhi::BlendOp::Add,
			Rhi::ColorWriteMask::All } };
		static constexpr std::array<Rhi::BlendAttachmentState, 1> AlphaBlend{ Rhi::BlendAttachmentState{ true, Rhi::BlendFactor::One,
			Rhi::BlendFactor::OneMinusSourceAlpha, Rhi::BlendOp::Add, Rhi::BlendFactor::One, Rhi::BlendFactor::OneMinusSourceAlpha,
			Rhi::BlendOp::Add, Rhi::ColorWriteMask::All } };
		if (blend != ParticleBlendMode::Additive && blend != ParticleBlendMode::AlphaBlend)
		{
			throw std::invalid_argument("Unknown particle blend mode");
		}
		Rhi::GraphicsPipelineDesc pipeline{};
		pipeline.Program = &program;
		pipeline.Layout = &layout;
		pipeline.ColorFormats = ColorFormats;
		pipeline.BlendAttachments = blend == ParticleBlendMode::Additive ? std::span<const Rhi::BlendAttachmentState>(Additive)
																		 : std::span<const Rhi::BlendAttachmentState>(AlphaBlend);
		pipeline.DepthStencilFormat = Rhi::Format::D32Float;
		pipeline.DepthStencil.DepthTest = true;
		pipeline.DepthStencil.DepthWrite = false;
		pipeline.DepthStencil.DepthCompare = Rhi::CompareOp::GreaterEqual; // Canonical reverse-Z.
		pipeline.Raster.Cull = Rhi::CullMode::None;
		pipeline.Raster.Winding = Rhi::FrontFace::CounterClockwise;
		pipeline.DebugName = blend == ParticleBlendMode::Additive ? "Particles additive" : "Particles alpha blend";
		return pipeline;
	}

	ParticleSystem::ParticleSystem(Rhi::Device& device, ParticleSystemDesc descInput) : desc(std::move(descInput))
	{
		for (const auto* program : { &desc.Simulate, &desc.Emit, &desc.Compact, &desc.Finalize })
		{
			if (!program->Pipeline || !program->Layout)
			{
				throw std::invalid_argument(desc.DebugName + " needs the simulate, emit, compact and finalize programs");
			}
		}
		if (desc.Capacity < 1 || desc.Capacity > MaxCapacity || desc.MaxEmitters < 1 || desc.MaxEmitters > 65535)
		{
			throw std::invalid_argument(desc.DebugName + " needs 1 .. MaxCapacity slots and 1 .. 65535 emitters");
		}
		capacity = desc.Capacity;
		const auto storage = Rhi::BufferUsage::Storage | Rhi::BufferUsage::TransferDestination | Rhi::BufferUsage::TransferSource;
		const auto make = [&](std::uint64_t size, Rhi::BufferUsage usage, const std::string& label)
		{
			auto buffer = device.CreateBuffer({ size, usage, Rhi::MemoryPreference::DeviceLocal, label });
			if (!buffer)
			{
				throw std::runtime_error(label + " could not be created");
			}
			return buffer;
		};
		particles = make(std::uint64_t(capacity) * sizeof(GpuParticle), storage, desc.DebugName + " pool");
		freeList = make(std::uint64_t(capacity) * 4, storage, desc.DebugName + " free list");
		drawList = make(std::uint64_t(capacity) * 4, storage, desc.DebugName + " draw list");
		counters = make(std::uint64_t(desc.MaxEmitters) * sizeof(GpuParticleCounters), storage, desc.DebugName + " counters");
		drawArgs =
			make(std::uint64_t(desc.MaxEmitters) * CommandBytes, storage | Rhi::BufferUsage::Indirect, desc.DebugName + " draw arguments");
		quadIndices =
			make(sizeof(QuadIndices), Rhi::BufferUsage::Index | Rhi::BufferUsage::TransferDestination, desc.DebugName + " quad indices");
		registry = std::make_unique<Registry>(GpuResourceRegistryDesc{ desc.MaxEmitters, desc.DebugName + " emitters" });
		freeRanges.push_back({ 0, capacity });
	}

	ParticleSystem::~ParticleSystem() = default;

	std::optional<ParticleSystem::Range> ParticleSystem::Allocate(std::uint32_t count)
	{
		for (auto it = freeRanges.begin(); it != freeRanges.end(); ++it)
		{
			if (it->Count >= count)
			{
				const Range range{ it->First, count };
				it->First += count;
				it->Count -= count;
				if (it->Count == 0)
				{
					freeRanges.erase(it);
				}
				return range;
			}
		}
		return std::nullopt;
	}

	void ParticleSystem::Free(Range range)
	{
		auto it = std::lower_bound(freeRanges.begin(), freeRanges.end(), range.First,
			[](const Range& existing, std::uint32_t first)
			{
				return existing.First < first;
			});
		it = freeRanges.insert(it, range);
		// Coalesce with the next, then the previous neighbour.
		if (auto next = it + 1; next != freeRanges.end() && it->First + it->Count == next->First)
		{
			it->Count += next->Count;
			freeRanges.erase(next);
		}
		if (it != freeRanges.begin())
		{
			auto previous = it - 1;
			if (previous->First + previous->Count == it->First)
			{
				previous->Count += it->Count;
				freeRanges.erase(it);
			}
		}
	}

	std::optional<ParticleEmitterHandle> ParticleSystem::TryCreateEmitter(
		const ParticleEmitterDesc& emitterDesc, const std::array<float, 12>& transform)
	{
		PackParticleEmitter(emitterDesc, transform, 0, 0, 0); // Validates both.
		if (registry->GetStats().Live + registry->GetStats().Retiring >= desc.MaxEmitters)
		{
			return std::nullopt;
		}
		const auto range = Allocate(emitterDesc.Capacity);
		if (!range)
		{
			return std::nullopt;
		}
		Emitter emitter;
		emitter.Desc = emitterDesc;
		emitter.Transform = transform;
		emitter.Slots = *range;
		auto handle = registry->TryCreate(std::move(emitter));
		if (!handle)
		{
			Free(*range);
		}
		return handle;
	}

	ParticleEmitterHandle ParticleSystem::CreateEmitter(const ParticleEmitterDesc& emitterDesc, const std::array<float, 12>& transform)
	{
		if (auto handle = TryCreateEmitter(emitterDesc, transform))
		{
			return *handle;
		}
		throw std::length_error(desc.DebugName + " has no free emitter row or pool range for this capacity");
	}

	bool ParticleSystem::IsValid(ParticleEmitterHandle emitter) const
	{
		return registry->IsValid(emitter);
	}

	bool ParticleSystem::SetTransform(ParticleEmitterHandle handle, const std::array<float, 12>& transform)
	{
		auto* emitter = registry->Get(handle);
		if (!emitter)
		{
			return false;
		}
		if (!std::all_of(transform.begin(), transform.end(),
				[](float v)
				{
					return std::isfinite(v);
				}))
		{
			throw std::invalid_argument(desc.DebugName + " emitter transform must be finite");
		}
		emitter->Transform = transform;
		return true;
	}

	bool ParticleSystem::SetEmitting(ParticleEmitterHandle handle, bool emitting)
	{
		auto* emitter = registry->Get(handle);
		if (!emitter)
		{
			return false;
		}
		emitter->Emitting = emitting;
		return true;
	}

	bool ParticleSystem::Release(ParticleEmitterHandle emitter, Rhi::TimelinePoint lastUse)
	{
		return registry->Release(emitter, lastUse);
	}

	std::size_t ParticleSystem::Collect()
	{
		return registry->CollectRetired(
			[&](ParticleEmitterHandle, Emitter& emitter)
			{
				Free(emitter.Slots);
			});
	}

	std::size_t ParticleSystem::Drain()
	{
		return registry->Drain(
			[&](ParticleEmitterHandle, Emitter& emitter)
			{
				Free(emitter.Slots);
			});
	}

	ParticleGraphResources ParticleSystem::Simulate(RenderGraph& graph, const ParticleView& view, float deltaTime)
	{
		if (pending)
		{
			throw std::logic_error(desc.DebugName + " has a frame awaiting CommitFrame/AbortFrame");
		}
		try
		{
			return RecordSimulation(graph, view, deltaTime);
		}
		catch (...)
		{
			// Nothing reached the GPU: rewind the clocks and keep the resets pending.
			AbortFrame();
			throw;
		}
	}

	ParticleGraphResources ParticleSystem::RecordSimulation(RenderGraph& graph, const ParticleView& view, float deltaTime)
	{
		const auto& name = desc.DebugName;
		ParticleGraphResources resources;
		resources.FrameRecord = BuildParticleFrame(view, deltaTime, 0, 0); // Validates the view first.

		std::vector<ParticleEmitterHandle> live;
		registry->ForEach(
			[&](ParticleEmitterHandle handle, Emitter&)
			{
				live.push_back(handle);
			});
		std::sort(live.begin(), live.end());
		if (live.empty())
		{
			return resources;
		}

		std::vector<GpuParticleEmitter> records;
		std::uint32_t maxSpawn = 0;
		std::uint32_t maxCapacity = 0;
		std::vector<ParticleEmitterHandle> resets;
		for (const auto handle : live)
		{
			auto& emitter = *registry->Get(handle);
			emitter.SavedClock = emitter.Clock;
			std::uint32_t spawn = 0;
			if (emitter.Emitting)
			{
				spawn = Particles::AdvanceEmission(emitter.Desc, emitter.Clock, deltaTime);
			}
			else
			{
				emitter.Clock.Time += deltaTime;
			}
			auto record = PackParticleEmitter(emitter.Desc, emitter.Transform, emitter.Slots.First, spawn, emitter.Clock.NextId - spawn);
			record.Row = handle.Index;
			maxSpawn = std::max(maxSpawn, record.SpawnCount);
			maxCapacity = std::max(maxCapacity, record.Capacity);
			ParticleFrameEmitter entry;
			entry.Handle = handle;
			entry.Record = record;
			entry.Blend = emitter.Desc.Blend;
			records.push_back(record);
			resources.Emitters.push_back(entry);
			if (emitter.NeedsReset)
			{
				resets.push_back(handle);
			}
		}
		const auto count = static_cast<std::uint32_t>(records.size());
		resources.FrameRecord = BuildParticleFrame(view, deltaTime, count, maxSpawn);
		const auto& frameRecord = resources.FrameRecord;
		for (auto& entry : resources.Emitters)
		{
			const auto& t = entry.Record.Transform;
			entry.OriginDepth = (t[3] - frameRecord.CameraPosition[0]) * frameRecord.CameraForward[0] +
				(t[7] - frameRecord.CameraPosition[1]) * frameRecord.CameraForward[1] +
				(t[11] - frameRecord.CameraPosition[2]) * frameRecord.CameraForward[2];
		}

		BufferSet set;
		const auto frameBuffer =
			graph.CreateUpload(std::as_bytes(std::span(&resources.FrameRecord, 1)), name + " frame", Rhi::BufferUsage::Storage, 16);
		const auto emitterBuffer = graph.CreateUpload(std::as_bytes(std::span(records)), name + " emitters", Rhi::BufferUsage::Storage, 16);
		const auto pool = graph.ImportBuffer(*particles, S::ShaderRead);
		const auto free = graph.ImportBuffer(*freeList, S::ShaderRead);
		const auto draws = graph.ImportBuffer(*drawList, S::ShaderRead);
		const auto counterBuffer = graph.ImportBuffer(*counters, S::ShaderRead);
		const auto args = graph.ImportBuffer(*drawArgs, S::IndirectArgument);
		const auto indices = graph.ImportBuffer(*quadIndices, S::IndexBuffer);
		set.Buffers[B::Frame] = frameBuffer;
		set.Buffers[B::Emitters] = emitterBuffer;
		set.Buffers[B::Particles] = pool;
		set.Buffers[B::FreeList] = free;
		set.Buffers[B::DrawList] = draws;
		set.Buffers[B::Counters] = counterBuffer;
		set.Buffers[B::DrawArgs] = args;
		resources.Frame = frameBuffer;
		resources.EmitterRecords = emitterBuffer;
		resources.Particles = pool;
		resources.FreeList = free;
		resources.DrawList = draws;
		resources.Counters = counterBuffer;
		resources.DrawArgs = args;
		resources.QuadIndices = indices;

		// New ranges start empty: free slots, a full free list and fresh counters.
		resetInFlight = resets;
		for (const auto handle : resets)
		{
			const auto& emitter = *registry->Get(handle);
			const auto first = emitter.Slots.First;
			const auto slots = emitter.Slots.Count;
			AddBufferUpload(
				graph, name + " range reset", std::uint64_t(slots) * sizeof(GpuParticle),
				[](std::span<std::byte> bytes)
				{
					std::fill(bytes.begin(), bytes.end(), std::byte{ 0 });
				},
				pool, std::uint64_t(first) * sizeof(GpuParticle));
			AddBufferUpload(
				graph, name + " free list reset", std::uint64_t(slots) * 4,
				[first](std::span<std::byte> bytes)
				{
					for (std::size_t i = 0; i < bytes.size() / 4; ++i)
					{
						const auto slot = static_cast<std::uint32_t>(first + i);
						std::memcpy(bytes.data() + i * 4, &slot, 4);
					}
				},
				free, std::uint64_t(first) * 4);
			const GpuParticleCounters fresh{ slots, 0, 0, 0 };
			AddBufferUpload(graph, name + " counter reset", std::as_bytes(std::span(&fresh, 1)), counterBuffer,
				std::uint64_t(handle.Index) * sizeof(GpuParticleCounters));
			registry->Get(handle)->NeedsReset = false;
		}
		resources.InitializedEmitters = static_cast<std::uint32_t>(resets.size());
		if (!quadIndicesReady)
		{
			AddBufferUpload(graph, name + " quad indices", std::as_bytes(std::span(QuadIndices)), indices);
			quadIndicesInFlight = true;
		}
		pending = true; // From here on only passes are declared; CommitFrame/AbortFrame settles the frame.

		constexpr std::uint32_t group = ParticleThreadGroupSize;
		resources.SimulatePass = graph.AddPass(
			name + " simulate", Rhi::QueueType::Compute,
			[&](RenderGraphBuilder& b)
			{
				b.Read(frameBuffer, S::ShaderRead);
				b.Read(emitterBuffer, S::ShaderRead);
				b.ReadWrite(pool, S::ShaderRead | S::ShaderWrite);
				b.ReadWrite(free, S::ShaderRead | S::ShaderWrite);
				b.ReadWrite(counterBuffer, S::ShaderRead | S::ShaderWrite);
			},
			[program = desc.Simulate, label = name + " simulate", set, maxCapacity, count](RenderCommandContext& c)
			{
				Dispatch(c, program, label, set, ParticleProgramBindings::Simulate, Groups(maxCapacity, group), count, 1);
			});
		resources.EmitPass = graph.AddPass(
			name + " emit", Rhi::QueueType::Compute,
			[&](RenderGraphBuilder& b)
			{
				b.Read(emitterBuffer, S::ShaderRead);
				b.ReadWrite(pool, S::ShaderRead | S::ShaderWrite);
				b.ReadWrite(free, S::ShaderRead | S::ShaderWrite);
				b.ReadWrite(counterBuffer, S::ShaderRead | S::ShaderWrite);
			},
			[program = desc.Emit, label = name + " emit", set, maxSpawn, count](RenderCommandContext& c)
			{
				// At least one group per emitter: its first thread resets the live count.
				Dispatch(c, program, label, set, ParticleProgramBindings::Emit, std::max(1u, Groups(maxSpawn, group)), count, 1);
			});
		resources.CompactPass = graph.AddPass(
			name + " compact", Rhi::QueueType::Compute,
			[&](RenderGraphBuilder& b)
			{
				b.Read(emitterBuffer, S::ShaderRead);
				b.Read(pool, S::ShaderRead);
				b.ReadWrite(draws, S::ShaderRead | S::ShaderWrite);
				b.ReadWrite(counterBuffer, S::ShaderRead | S::ShaderWrite);
			},
			[program = desc.Compact, label = name + " compact", set, maxCapacity, count](RenderCommandContext& c)
			{
				Dispatch(c, program, label, set, ParticleProgramBindings::Compact, Groups(maxCapacity, group), count, 1);
			});
		resources.FinalizePass = graph.AddPass(
			name + " finalize", Rhi::QueueType::Compute,
			[&](RenderGraphBuilder& b)
			{
				b.Read(frameBuffer, S::ShaderRead);
				b.Read(emitterBuffer, S::ShaderRead);
				b.Read(pool, S::ShaderRead);
				b.ReadWrite(draws, S::ShaderRead | S::ShaderWrite);
				b.Read(counterBuffer, S::ShaderRead);
				b.ReadWrite(args, S::ShaderRead | S::ShaderWrite);
			},
			[program = desc.Finalize, label = name + " finalize", set, count](RenderCommandContext& c)
			{
				Dispatch(c, program, label, set, ParticleProgramBindings::Finalize, count, 1, 1);
			});
		return resources;
	}

	void ParticleSystem::CommitFrame()
	{
		registry->ForEach(
			[](ParticleEmitterHandle, Emitter& emitter)
			{
				emitter.SavedClock = emitter.Clock; // AbortFrame never rewinds past a committed frame.
			});
		if (quadIndicesInFlight)
		{
			quadIndicesReady = true;
		}
		quadIndicesInFlight = false;
		resetInFlight.clear();
		pending = false;
	}

	void ParticleSystem::AbortFrame()
	{
		registry->ForEach(
			[](ParticleEmitterHandle, Emitter& emitter)
			{
				emitter.Clock = emitter.SavedClock;
			});
		for (const auto handle : resetInFlight)
		{
			if (auto* emitter = registry->Get(handle))
			{
				emitter->NeedsReset = true;
			}
		}
		quadIndicesInFlight = false;
		resetInFlight.clear();
		pending = false;
	}

	std::optional<GraphPass> ParticleSystem::Draw(RenderGraph& graph, const ParticleGraphResources& frame,
		const ParticleRenderProgram& program, const ParticleDrawTargets& targets, Rhi::DescriptorTable& bindless) const
	{
		const auto& name = desc.DebugName;
		if (!program.Additive || !program.AlphaBlend || !program.Layout)
		{
			throw std::invalid_argument(name + " draw needs both pipelines and their layout");
		}
		const auto colorDesc = graph.GetDesc(targets.Color);
		const auto depthDesc = graph.GetDesc(targets.Depth);
		if (colorDesc.PixelFormat != ColorFormat || !HasUsage(colorDesc.Usage, Rhi::TextureUsage::ColorAttachment) ||
			depthDesc.PixelFormat != Rhi::Format::D32Float || !HasUsage(depthDesc.Usage, Rhi::TextureUsage::DepthStencilAttachment) ||
			colorDesc.Extent.Width != depthDesc.Extent.Width || colorDesc.Extent.Height != depthDesc.Extent.Height)
		{
			throw std::invalid_argument(name + " draw needs same-sized RGBA16Float color and D32Float depth attachments");
		}
		if (frame.Emitters.empty() || !frame.Frame)
		{
			return std::nullopt;
		}
		// Additive emitters first (order independent), then blended ones back to front.
		std::vector<std::uint32_t> order(frame.Emitters.size());
		std::iota(order.begin(), order.end(), 0u);
		std::stable_sort(order.begin(), order.end(),
			[&](std::uint32_t a, std::uint32_t b)
			{
				const auto& ea = frame.Emitters[a];
				const auto& eb = frame.Emitters[b];
				if (ea.Blend != eb.Blend)
				{
					return ea.Blend == ParticleBlendMode::Additive;
				}
				return ea.Blend == ParticleBlendMode::AlphaBlend && ea.OriginDepth > eb.OriginDepth;
			});
		std::vector<std::pair<std::uint32_t, std::uint32_t>> draws; // (emitter index, row)
		std::vector<ParticleBlendMode> blends;
		for (const auto index : order)
		{
			draws.push_back({ index, frame.Emitters[index].Record.Row });
			blends.push_back(frame.Emitters[index].Blend);
		}
		const std::uint32_t width = colorDesc.Extent.Width;
		const std::uint32_t height = colorDesc.Extent.Height;
		const auto frameBuffer = *frame.Frame;
		const auto emitterBuffer = *frame.EmitterRecords;
		const auto pool = *frame.Particles;
		const auto drawListBuffer = *frame.DrawList;
		const auto args = *frame.DrawArgs;
		const auto indices = *frame.QuadIndices;
		return graph.AddPass(
			name + " draw", Rhi::QueueType::Graphics,
			[&](RenderGraphBuilder& b)
			{
				b.Read(frameBuffer, S::ShaderRead);
				b.Read(emitterBuffer, S::ShaderRead);
				b.Read(pool, S::ShaderRead);
				b.Read(drawListBuffer, S::ShaderRead);
				b.Read(args, S::IndirectArgument);
				b.Read(indices, S::IndexBuffer);
				b.ReadWrite(targets.Color, S::ColorAttachment);
				// The attachment layout is the writable one; the pipelines never write depth.
				b.ReadWrite(targets.Depth, S::DepthStencilWrite);
			},
			[program, label = name + " draw", targets, frameBuffer, emitterBuffer, pool, drawListBuffer, args, indices, draws, blends,
				bindlessTable = &bindless, width, height](RenderCommandContext& c)
			{
				using R = ParticleRenderBindings;
				auto table = c.Device().CreateDescriptorTable({ program.Layout, 0, 0, label });
				if (!table)
				{
					throw std::runtime_error(label + " descriptor table could not be created");
				}
				std::array<Rhi::DescriptorWrite, R::Count> writes{};
				const std::array<std::pair<std::uint32_t, GraphBuffer>, R::Count> buffers{ { { R::Frame, frameBuffer },
					{ R::Emitters, emitterBuffer }, { R::Particles, pool }, { R::DrawList, drawListBuffer } } };
				for (std::size_t i = 0; i < buffers.size(); ++i)
				{
					const auto range = c.GetRange(buffers[i].second);
					writes[i].Binding = buffers[i].first;
					writes[i].BufferResource = range.Buffer;
					writes[i].BufferOffset = range.Offset;
					writes[i].BufferRange = range.Size;
				}
				table->Write(writes);
				auto& retained = static_cast<Rhi::DescriptorTable&>(c.Retain(std::move(table)));
				std::array<Rhi::RenderingAttachmentDesc, 1> colors{};
				colors[0].View = &c.CreateView(targets.Color);
				colors[0].Load = Rhi::LoadOp::Load;
				Rhi::TextureViewDesc depthView;
				depthView.PixelFormat = Rhi::Format::D32Float;
				const Rhi::DepthStencilAttachmentDesc depth{ &c.CreateView(targets.Depth, depthView), Rhi::LoadOp::Load,
					Rhi::StoreOp::Store, 0.0f, 0 };
				auto& list = c.Commands();
				list.BeginRendering({ colors, &depth, { width, height } });
				list.SetViewport({ 0, 0, float(width), float(height) });
				list.SetScissor({ 0, 0, width, height });
				list.BindIndexBuffer(c.Get(indices), 0, Rhi::IndexType::Uint32);
				auto& argumentBuffer = c.Get(args);
				std::optional<ParticleBlendMode> bound;
				for (std::size_t i = 0; i < draws.size(); ++i)
				{
					if (bound != blends[i])
					{
						list.BindGraphicsPipeline(blends[i] == ParticleBlendMode::Additive ? *program.Additive : *program.AlphaBlend);
						list.BindDescriptorTable(0, retained);
						list.BindDescriptorTable(R::BindlessSpace, *bindlessTable);
						bound = blends[i];
					}
					const std::uint32_t emitter = draws[i].first;
					list.PushConstants(
						Rhi::ShaderStageMask::Vertex | Rhi::ShaderStageMask::Fragment, 0, std::as_bytes(std::span(&emitter, 1)));
					list.DrawIndexedIndirect(argumentBuffer, std::uint64_t(draws[i].second) * CommandBytes, 1);
				}
				list.EndRendering();
			});
	}

	std::optional<std::pair<std::uint32_t, std::uint32_t>> ParticleSystem::GetRange(ParticleEmitterHandle handle) const
	{
		const auto* emitter = registry->Get(handle);
		if (!emitter)
		{
			return std::nullopt;
		}
		return std::pair{ emitter->Slots.First, emitter->Slots.Count };
	}

	ParticleSystemStats ParticleSystem::GetStats() const
	{
		ParticleSystemStats stats;
		const auto registryStats = registry->GetStats();
		stats.LiveEmitters = static_cast<std::uint32_t>(registryStats.Live);
		stats.RetiringEmitters = static_cast<std::uint32_t>(registryStats.Retiring);
		std::uint32_t freeSlots = 0;
		for (const auto& range : freeRanges)
		{
			freeSlots += range.Count;
			stats.LargestFreeRange = std::max(stats.LargestFreeRange, range.Count);
		}
		stats.AllocatedSlots = capacity - freeSlots;
		return stats;
	}
} // namespace Swim::Render
