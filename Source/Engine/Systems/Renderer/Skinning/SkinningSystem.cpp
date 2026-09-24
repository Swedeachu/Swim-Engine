#include "Engine/Systems/Renderer/Skinning/SkinningSystem.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderCommandContext.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphTransfers.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <stdexcept>

namespace Swim::Render
{
	namespace
	{
		using S = Rhi::ResourceState;
		using B = SkinningBindings;

		constexpr std::uint32_t MaxGroups = 65535;
		constexpr std::uint32_t MaxVerticesPerInstance = MaxGroups * SkinningThreadGroupSize;

		bool FiniteMatrices(std::span<const SkinMatrix> matrices)
		{
			for (const SkinMatrix& matrix : matrices)
			{
				for (const float value : matrix)
				{
					if (!std::isfinite(value))
					{
						return false;
					}
				}
			}
			return true;
		}

		bool FiniteWeights(std::span<const float> weights)
		{
			return std::all_of(weights.begin(), weights.end(),
				[](float value)
				{
					return std::isfinite(value);
				});
		}

		// One output page's instances: rows [FirstRow, FirstRow + Rows) of the dispatch buffer.
		struct PageGroup
		{
			std::uint32_t Page = 0;
			std::uint32_t FirstRow = 0;
			std::uint32_t Rows = 0;
			std::uint32_t MaxVertices = 0;
		};
	} // namespace

	SkinningSystem::SkinningSystem(Rhi::Device& deviceInput, GeometryHeap& heapInput, SkinningSystemDesc descInput)
		: device(deviceInput), heap(heapInput), desc(std::move(descInput)),
		  vertexRanges(std::max<std::uint32_t>(desc.MaxSourceVertices, 1)), deltaRanges(std::max<std::uint32_t>(desc.MaxMorphDeltas, 1))
	{
		if (!desc.Pipeline || !desc.Layout)
		{
			throw std::invalid_argument(desc.DebugName + " needs the SwimSkinning program");
		}
		if (desc.MaxSourceVertices == 0 || desc.MaxMorphDeltas == 0 || desc.MaxMeshes == 0 || desc.MaxInstances == 0)
		{
			throw std::invalid_argument(desc.DebugName + " needs nonzero pool, mesh and instance capacities");
		}
		const auto usage = Rhi::BufferUsage::Storage | Rhi::BufferUsage::TransferDestination | Rhi::BufferUsage::TransferSource;
		const auto make = [&](std::uint64_t size, const std::string& label)
		{
			auto buffer = device.CreateBuffer({ size, usage, Rhi::MemoryPreference::DeviceLocal, label });
			if (!buffer)
			{
				throw std::runtime_error(label + " could not be created");
			}
			return buffer;
		};
		sourceVertices = make(std::uint64_t(desc.MaxSourceVertices) * sizeof(StandardVertex), desc.DebugName + " source vertices");
		skinVertices = make(std::uint64_t(desc.MaxSourceVertices) * sizeof(GpuSkinVertex), desc.DebugName + " skin vertices");
		morphDeltas = make(std::uint64_t(desc.MaxMorphDeltas) * sizeof(GpuMorphDelta), desc.DebugName + " morph deltas");
		meshes = std::make_unique<MeshRegistry>(GpuResourceRegistryDesc{ desc.MaxMeshes, desc.DebugName + " meshes" });
		instances = std::make_unique<InstanceRegistry>(GpuResourceRegistryDesc{ desc.MaxInstances, desc.DebugName + " instances" });
	}

	SkinningSystem::~SkinningSystem()
	{
		try
		{
			Drain();
		}
		catch (...)
		{
		}
	}

	SkinnedMeshHandle SkinningSystem::CreateSkinnedMesh(const SkinnedMeshDesc& meshDesc)
	{
		if (meshDesc.Vertices.size() > MaxVerticesPerInstance)
		{
			throw std::invalid_argument(desc.DebugName + ": a skinned mesh may have at most 4,194,240 vertices");
		}
		if (meshDesc.Indices.empty())
		{
			throw std::invalid_argument(desc.DebugName + ": a skinned mesh needs indices");
		}
		SkinnedSource source = Skinning::BuildSource(meshDesc.Vertices, meshDesc.Influences, meshDesc.MorphTargets, meshDesc.JointCount);

		Mesh mesh;
		mesh.VertexCount = static_cast<std::uint32_t>(meshDesc.Vertices.size());
		mesh.JointCount = meshDesc.JointCount;
		mesh.MorphTargetCount = static_cast<std::uint32_t>(meshDesc.MorphTargets.size());
		const auto vertices = vertexRanges.Allocate(mesh.VertexCount);
		if (!vertices)
		{
			throw std::length_error(desc.DebugName + " source vertex pool is full");
		}
		mesh.Vertices = *vertices;
		if (!source.MorphDeltas.empty())
		{
			const auto deltas = deltaRanges.Allocate(source.MorphDeltas.size());
			if (!deltas)
			{
				vertexRanges.Free(mesh.Vertices);
				throw std::length_error(desc.DebugName + " morph delta pool is full");
			}
			mesh.Deltas = *deltas;
		}
		mesh.Bounds = std::move(source.Bounds);
		mesh.BindVertices.assign(meshDesc.Vertices.begin(), meshDesc.Vertices.end());
		mesh.Indices.assign(meshDesc.Indices.begin(), meshDesc.Indices.end());
		mesh.IndexFormat = meshDesc.IndexFormat;
		mesh.Submeshes.assign(meshDesc.Submeshes.begin(), meshDesc.Submeshes.end());
		mesh.Lods.assign(meshDesc.Lods.begin(), meshDesc.Lods.end());
		mesh.PendingSkin = std::move(source.SkinVertices);
		mesh.PendingDeltas = std::move(source.MorphDeltas);
		mesh.Name = meshDesc.DebugName.empty() ? desc.DebugName + " mesh" : std::string(meshDesc.DebugName);
		const GeometryRange vertexRange = mesh.Vertices;
		const GeometryRange deltaRange = mesh.Deltas;
		auto handle = meshes->TryCreate(std::move(mesh));
		if (!handle)
		{
			vertexRanges.Free(vertexRange);
			if (deltaRange.Size)
			{
				deltaRanges.Free(deltaRange);
			}
			throw std::length_error(desc.DebugName + " has no free skinned mesh slots");
		}
		return *handle;
	}

	bool SkinningSystem::DestroySkinnedMesh(SkinnedMeshHandle handle, Rhi::TimelinePoint lastUse)
	{
		Mesh* mesh = meshes->Get(handle);
		if (!mesh || mesh->Instances > 0)
		{
			return false;
		}
		if (mesh->Upload == GpuUploadState::Recorded)
		{
			throw std::logic_error(desc.DebugName + ": commit or abort the frame that uploads a mesh before destroying it");
		}
		return meshes->Release(handle, lastUse);
	}

	SkinInstanceHandle SkinningSystem::CreateInstance(SkinnedMeshHandle meshHandle)
	{
		Mesh* mesh = meshes->Get(meshHandle);
		if (!mesh)
		{
			throw std::invalid_argument(desc.DebugName + ": CreateInstance needs a valid skinned mesh");
		}
		// Current vertices, then the previous-position slots, both starting at the bind pose.
		std::vector<StandardVertex> output(std::size_t(mesh->VertexCount) * 2);
		std::copy(mesh->BindVertices.begin(), mesh->BindVertices.end(), output.begin());
		std::copy(mesh->BindVertices.begin(), mesh->BindVertices.end(), output.begin() + mesh->VertexCount);
		GeometryMeshDesc geometry;
		geometry.VertexLayout = StandardVertexLayoutId();
		geometry.VertexStride = StandardVertexStride;
		geometry.Vertices = std::as_bytes(std::span(output));
		geometry.IndexFormat = mesh->IndexFormat;
		geometry.Indices = mesh->Indices;
		geometry.Submeshes = mesh->Submeshes;
		geometry.Lods = mesh->Lods;
		const std::string name = mesh->Name + " instance";
		geometry.DebugName = name;
		const GpuMeshHandle outputMesh = heap.CreateMesh(geometry);

		Instance instance;
		instance.Mesh = meshHandle;
		instance.Output = outputMesh;
		auto handle = instances->TryCreate(std::move(instance));
		if (!handle)
		{
			heap.DestroyMesh(outputMesh);
			throw std::length_error(desc.DebugName + " has no free instance slots");
		}
		++mesh->Instances;
		return *handle;
	}

	bool SkinningSystem::DestroyInstance(SkinInstanceHandle handle, Rhi::TimelinePoint lastUse)
	{
		Instance* instance = instances->Get(handle);
		if (!instance)
		{
			return false;
		}
		heap.DestroyMesh(instance->Output, lastUse);
		if (Mesh* mesh = meshes->Get(instance->Mesh))
		{
			--mesh->Instances;
		}
		std::erase(recorded, handle);
		return instances->Release(handle, lastUse);
	}

	bool SkinningSystem::SetPose(SkinInstanceHandle handle, const SkinPose& pose)
	{
		Instance* instance = instances->Get(handle);
		if (!instance)
		{
			return false;
		}
		const Mesh& mesh = *meshes->Get(instance->Mesh);
		const auto previousPalette = pose.PreviousPalette.empty() ? pose.Palette : pose.PreviousPalette;
		const auto previousWeights = pose.PreviousMorphWeights.empty() ? pose.MorphWeights : pose.PreviousMorphWeights;
		if (pose.Palette.size() != mesh.JointCount || previousPalette.size() != mesh.JointCount ||
			pose.MorphWeights.size() != mesh.MorphTargetCount || previousWeights.size() != mesh.MorphTargetCount)
		{
			throw std::invalid_argument(desc.DebugName + ": a pose needs one matrix per joint and one weight per morph target");
		}
		if (!FiniteMatrices(pose.Palette) || !FiniteMatrices(previousPalette) || !FiniteWeights(pose.MorphWeights) ||
			!FiniteWeights(previousWeights))
		{
			throw std::invalid_argument(desc.DebugName + ": a pose must be finite");
		}
		instance->Palette.assign(pose.Palette.begin(), pose.Palette.end());
		instance->PreviousPalette.assign(previousPalette.begin(), previousPalette.end());
		instance->Weights.assign(pose.MorphWeights.begin(), pose.MorphWeights.end());
		instance->PreviousWeights.assign(previousWeights.begin(), previousWeights.end());
		instance->HasPose = true;
		instance->Dirty = true;
		return true;
	}

	GpuMeshHandle SkinningSystem::GetOutputMesh(SkinInstanceHandle handle) const
	{
		const Instance* instance = instances->Get(handle);
		return instance ? instance->Output : GpuMeshHandle{};
	}

	std::uint32_t SkinningSystem::GetPreviousVertexOffset(SkinInstanceHandle handle) const
	{
		const Instance* instance = instances->Get(handle);
		return instance ? meshes->Get(instance->Mesh)->VertexCount : 0u;
	}

	RenderBounds SkinningSystem::ComputeBounds(SkinInstanceHandle handle) const
	{
		const Instance* instance = instances->Get(handle);
		if (!instance || !instance->HasPose)
		{
			return RenderBounds::Infinite();
		}
		return Skinning::ComputeBounds(meshes->Get(instance->Mesh)->Bounds, instance->Palette, instance->Weights);
	}

	SkinningGraphResources SkinningSystem::Record(RenderGraph& graph, const GeometryGraphResources& geometry)
	{
		if (pending)
		{
			throw std::logic_error(desc.DebugName + ": the previous frame awaits CommitFrame or AbortFrame");
		}
		SkinningGraphResources resources;
		const std::string& name = desc.DebugName;

		// Instances to skin this frame, with the page their output lives in.
		struct Candidate
		{
			SkinInstanceHandle Handle;
			Instance* Data = nullptr;
			const Mesh* Source = nullptr;
			std::uint32_t Page = 0;
			std::uint32_t OutputVertex = 0;
			bool Settle = false;
		};

		std::vector<Candidate> candidates;
		instances->ForEach(
			[&](SkinInstanceHandle handle, Instance& instance)
			{
				if (!instance.HasPose || !(instance.Dirty || instance.Settle))
				{
					return;
				}
				const auto residency = heap.GetResidency(instance.Output);
				const GpuMeshMetadata* metadata = heap.GetMetadata(instance.Output);
				if (residency == GpuUploadState::Invalid || residency == GpuUploadState::PendingUpload || !metadata ||
					metadata->VertexPage >= geometry.Pages.size())
				{
					++resources.SkippedInstances;
					return;
				}
				candidates.push_back(
					{ handle, &instance, meshes->Get(instance.Mesh), metadata->VertexPage, metadata->VertexOffset, !instance.Dirty });
			});
		std::stable_sort(candidates.begin(), candidates.end(),
			[](const Candidate& a, const Candidate& b)
			{
				return a.Page < b.Page;
			});

		std::vector<SkinnedMeshHandle> uploads;
		meshes->ForEach(
			[&](SkinnedMeshHandle handle, Mesh& mesh)
			{
				if (mesh.Upload == GpuUploadState::PendingUpload)
				{
					uploads.push_back(handle);
				}
			});
		if (candidates.empty() && uploads.empty())
		{
			return resources;
		}

		const auto sources = graph.ImportBuffer(*sourceVertices, S::ShaderRead);
		const auto skins = graph.ImportBuffer(*skinVertices, S::ShaderRead);
		const auto deltas = graph.ImportBuffer(*morphDeltas, S::ShaderRead);
		resources.SourceVertices = sources;
		resources.SkinVertices = skins;
		resources.MorphDeltas = deltas;
		for (const SkinnedMeshHandle handle : uploads)
		{
			Mesh& mesh = *meshes->Get(handle);
			AddBufferUpload(graph, name + " source vertices", std::as_bytes(std::span(mesh.BindVertices)), sources,
				mesh.Vertices.Offset * sizeof(StandardVertex));
			AddBufferUpload(graph, name + " skin vertices", std::as_bytes(std::span(mesh.PendingSkin)), skins,
				mesh.Vertices.Offset * sizeof(GpuSkinVertex));
			if (!mesh.PendingDeltas.empty())
			{
				AddBufferUpload(graph, name + " morph deltas", std::as_bytes(std::span(mesh.PendingDeltas)), deltas,
					mesh.Deltas.Offset * sizeof(GpuMorphDelta));
			}
			mesh.Upload = GpuUploadState::Recorded;
		}
		uploading = uploads;
		resources.UploadedMeshes = static_cast<std::uint32_t>(uploads.size());
		pending = true;
		if (candidates.empty())
		{
			return resources;
		}

		std::vector<GpuSkinDispatch> rows;
		std::vector<float> palettes;
		std::vector<float> weights;
		std::vector<PageGroup> groups;
		for (const Candidate& candidate : candidates)
		{
			const Mesh& mesh = *candidate.Source;
			Instance& instance = *candidate.Data;
			GpuSkinDispatch row;
			row.SourceVertex = static_cast<std::uint32_t>(mesh.Vertices.Offset);
			row.VertexCount = mesh.VertexCount;
			row.OutputVertex = candidate.OutputVertex;
			row.PreviousOffset = mesh.VertexCount;
			row.Palette = static_cast<std::uint32_t>(palettes.size() / 12);
			row.JointCount = mesh.JointCount;
			row.MorphWeights = static_cast<std::uint32_t>(weights.size());
			row.MorphTargetCount = mesh.MorphTargetCount;
			row.SourceMorph = static_cast<std::uint32_t>(mesh.Deltas.Offset);
			// Settling re-skins with previous = current, so the motion stops.
			const auto& previousPalette = candidate.Settle ? instance.Palette : instance.PreviousPalette;
			const auto& previousWeights = candidate.Settle ? instance.Weights : instance.PreviousWeights;
			const std::array<const std::vector<SkinMatrix>*, 2> poses{ &instance.Palette, &previousPalette };
			for (const std::vector<SkinMatrix>* palette : poses)
			{
				for (const SkinMatrix& matrix : *palette)
				{
					palettes.insert(palettes.end(), matrix.begin(), matrix.end());
				}
			}
			weights.insert(weights.end(), instance.Weights.begin(), instance.Weights.end());
			weights.insert(weights.end(), previousWeights.begin(), previousWeights.end());

			if (groups.empty() || groups.back().Page != candidate.Page || groups.back().Rows == MaxGroups)
			{
				groups.push_back({ candidate.Page, static_cast<std::uint32_t>(rows.size()), 0, 0 });
			}
			++groups.back().Rows;
			groups.back().MaxVertices = std::max(groups.back().MaxVertices, mesh.VertexCount);
			rows.push_back(row);
			resources.Instances.push_back({ candidate.Handle, row, candidate.Page, candidate.Settle });

			if (candidate.Settle)
			{
				instance.Settle = false;
				instance.RecordedSettle = true;
			}
			else
			{
				instance.Dirty = false;
				instance.Settle = false;
				instance.RecordedDirty = true;
			}
			recorded.push_back(candidate.Handle);
		}
		if (weights.empty())
		{
			weights.assign(4, 0.0f); // Bound but never read.
		}

		const auto dispatchBuffer = graph.CreateUpload(std::as_bytes(std::span(rows)), name + " dispatches", Rhi::BufferUsage::Storage, 16);
		const auto paletteBuffer =
			graph.CreateUpload(std::as_bytes(std::span(palettes)), name + " palettes", Rhi::BufferUsage::Storage, 16);
		const auto weightBuffer =
			graph.CreateUpload(std::as_bytes(std::span(weights)), name + " morph weights", Rhi::BufferUsage::Storage, 16);
		resources.Dispatches = dispatchBuffer;
		resources.Palettes = paletteBuffer;
		resources.MorphWeights = weightBuffer;

		// Groups are sorted by page, so each written page appears in one run.
		std::vector<GraphBuffer> pages;
		for (std::size_t index = 0; index < groups.size(); ++index)
		{
			if (index == 0 || groups[index].Page != groups[index - 1].Page)
			{
				pages.push_back(geometry.Pages[groups[index].Page]);
			}
		}
		std::vector<GraphBuffer> groupPages;
		for (const PageGroup& group : groups)
		{
			groupPages.push_back(geometry.Pages[group.Page]);
		}

		resources.SkinPass = graph.AddPass(
			name + " skin", Rhi::QueueType::Compute,
			[&](RenderGraphBuilder& b)
			{
				b.Read(dispatchBuffer, S::ShaderRead);
				b.Read(sources, S::ShaderRead);
				b.Read(skins, S::ShaderRead);
				b.Read(deltas, S::ShaderRead);
				b.Read(paletteBuffer, S::ShaderRead);
				b.Read(weightBuffer, S::ShaderRead);
				for (const GraphBuffer& page : pages)
				{
					b.ReadWrite(page, S::ShaderRead | S::ShaderWrite);
				}
			},
			[pipeline = desc.Pipeline, layout = desc.Layout, label = name + " skin", groups, groupPages, dispatchBuffer, sources, skins,
				deltas, paletteBuffer, weightBuffer](RenderCommandContext& c)
			{
				auto& list = c.Commands();
				list.BindComputePipeline(*pipeline);
				for (std::size_t index = 0; index < groups.size(); ++index)
				{
					auto table = c.Device().CreateDescriptorTable({ layout, 0, 0, label });
					if (!table)
					{
						throw std::runtime_error(label + " descriptor table could not be created");
					}
					const std::array<std::pair<std::uint32_t, GraphBuffer>, B::Count> buffers{ { { B::Dispatches, dispatchBuffer },
						{ B::SourceVertices, sources }, { B::SkinVertices, skins }, { B::MorphDeltas, deltas },
						{ B::Palettes, paletteBuffer }, { B::MorphWeights, weightBuffer }, { B::Output, groupPages[index] } } };
					std::array<Rhi::DescriptorWrite, B::Count> writes{};
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
					list.BindDescriptorTable(0, retained);
					const std::uint32_t firstRow = groups[index].FirstRow;
					list.PushConstants(Rhi::ShaderStageMask::Compute, 0, std::as_bytes(std::span(&firstRow, 1)));
					list.Dispatch(
						(groups[index].MaxVertices + SkinningThreadGroupSize - 1) / SkinningThreadGroupSize, groups[index].Rows, 1);
				}
			});
		return resources;
	}

	void SkinningSystem::CommitFrame()
	{
		for (const SkinnedMeshHandle handle : uploading)
		{
			if (Mesh* mesh = meshes->Get(handle))
			{
				mesh->Upload = GpuUploadState::Resident;
				mesh->PendingSkin = {};
				mesh->PendingDeltas = {};
			}
		}
		uploading.clear();
		for (const SkinInstanceHandle handle : recorded)
		{
			Instance* instance = instances->Get(handle);
			if (!instance)
			{
				continue;
			}
			if (instance->RecordedDirty && !instance->Dirty)
			{
				instance->Settle = true;
			}
			if (instance->RecordedSettle)
			{
				instance->PreviousPalette = instance->Palette;
				instance->PreviousWeights = instance->Weights;
			}
			instance->RecordedDirty = instance->RecordedSettle = false;
		}
		recorded.clear();
		pending = false;
	}

	void SkinningSystem::AbortFrame()
	{
		for (const SkinnedMeshHandle handle : uploading)
		{
			if (Mesh* mesh = meshes->Get(handle))
			{
				mesh->Upload = GpuUploadState::PendingUpload;
			}
		}
		uploading.clear();
		for (const SkinInstanceHandle handle : recorded)
		{
			Instance* instance = instances->Get(handle);
			if (!instance)
			{
				continue;
			}
			instance->Dirty = instance->Dirty || instance->RecordedDirty;
			instance->Settle = instance->Settle || instance->RecordedSettle;
			instance->RecordedDirty = instance->RecordedSettle = false;
		}
		recorded.clear();
		pending = false;
	}

	std::size_t SkinningSystem::Collect()
	{
		const auto release = [this](SkinnedMeshHandle, Mesh& mesh)
		{
			vertexRanges.Free(mesh.Vertices);
			if (mesh.Deltas.Size)
			{
				deltaRanges.Free(mesh.Deltas);
			}
		};
		return meshes->CollectRetired(release) +
			instances->CollectRetired(
				[](SkinInstanceHandle, Instance&)
				{
				});
	}

	std::size_t SkinningSystem::Drain()
	{
		const auto release = [this](SkinnedMeshHandle, Mesh& mesh)
		{
			vertexRanges.Free(mesh.Vertices);
			if (mesh.Deltas.Size)
			{
				deltaRanges.Free(mesh.Deltas);
			}
		};
		return meshes->Drain(release) +
			instances->Drain(
				[](SkinInstanceHandle, Instance&)
				{
				});
	}

	SkinningStats SkinningSystem::GetStats() const
	{
		SkinningStats stats;
		meshes->ForEach(
			[&](SkinnedMeshHandle, const Mesh& mesh)
			{
				++stats.Meshes;
				stats.PendingMeshes += mesh.Upload == GpuUploadState::PendingUpload ? 1u : 0u;
			});
		instances->ForEach(
			[&](SkinInstanceHandle, const Instance&)
			{
				++stats.Instances;
			});
		stats.SourceVertices = static_cast<std::uint32_t>(vertexRanges.GetAllocatedBytes());
		stats.MorphDeltas = static_cast<std::uint32_t>(deltaRanges.GetAllocatedBytes());
		return stats;
	}
} // namespace Swim::Render
