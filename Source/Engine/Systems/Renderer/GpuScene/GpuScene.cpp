#include "Engine/Systems/Renderer/GpuScene/GpuScene.h"

#include <algorithm>
#include <cstring>

namespace Swim::Render
{
	namespace
	{
		constexpr auto ProducerMask = static_cast<std::uint32_t>(RenderObjectFlags::ProducerMask);
		constexpr auto LiveBit = static_cast<std::uint32_t>(RenderObjectFlags::Live);
		constexpr auto HasMeshBit = static_cast<std::uint32_t>(RenderObjectFlags::HasMesh);

		void WriteBounds(GpuInstanceRecord& record, const RenderBounds& bounds)
		{
			std::copy(bounds.Center.begin(), bounds.Center.end(), record.LocalCenter);
			std::copy(bounds.Extents.begin(), bounds.Extents.end(), record.LocalExtents);
		}

		bool SameBounds(const GpuInstanceRecord& record, const RenderBounds& bounds)
		{
			return std::equal(bounds.Center.begin(), bounds.Center.end(), record.LocalCenter) &&
				std::equal(bounds.Extents.begin(), bounds.Extents.end(), record.LocalExtents);
		}

		void WriteMesh(GpuInstanceRecord& record, GpuMeshHandle mesh)
		{
			record.MeshIndex = mesh ? mesh.Index : GpuInstanceRecord::InvalidIndex;
			record.MeshGeneration = mesh ? mesh.Generation : 0;
			record.Flags = mesh ? record.Flags | HasMeshBit : record.Flags & ~HasMeshBit;
		}

		bool SameAffine(const float* rows, const RenderAffine& transform)
		{
			return std::memcmp(rows, transform.Rows.data(), sizeof(float) * 12) == 0;
		}
	} // namespace

	GpuScene::GpuScene(Rhi::Device& device, GpuSceneDesc desc)
		: objects({ desc.MaxObjects, desc.DebugName }), instances(device, desc.MaxObjects, desc.DebugName + " instances"),
		  transforms(device, desc.MaxObjects, desc.DebugName + " transforms"), name(std::move(desc.DebugName))
	{
	}

	GpuScene::~GpuScene() = default;

	std::optional<RenderObjectHandle> GpuScene::TryCreate(const RenderObjectDesc& desc)
	{
		const auto handle = objects.TryCreate({ frame });
		if (!handle)
		{
			return std::nullopt;
		}
		rowCount = std::max(rowCount, handle->Index + 1);
		auto& instance = instances.Edit(handle->Index);
		instance = {};
		WriteBounds(instance, desc.LocalBounds);
		instance.TransformIndex = handle->Index;
		instance.MaterialSet = desc.MaterialSet;
		instance.ObjectId = desc.ObjectId;
		instance.Flags = (static_cast<std::uint32_t>(desc.Flags) & ProducerMask) | LiveBit;
		WriteMesh(instance, desc.Mesh);
		instance.SkinIndex = desc.SkinIndex;
		instance.LodBias = desc.LodBias;
		instance.PreviousVertexOffset = desc.PreviousVertexOffset;
		instance.Generation = handle->Generation;
		auto& transform = transforms.Edit(handle->Index);
		std::copy(desc.Transform.Rows.begin(), desc.Transform.Rows.end(), transform.Current);
		std::copy(desc.Transform.Rows.begin(), desc.Transform.Rows.end(), transform.Previous); // No motion yet.
		return handle;
	}

	RenderObjectHandle GpuScene::Create(const RenderObjectDesc& desc)
	{
		if (auto handle = TryCreate(desc))
		{
			return *handle;
		}
		throw std::length_error(name + " has no free render object rows");
	}

	bool GpuScene::Destroy(RenderObjectHandle object, Rhi::TimelinePoint lastUse)
	{
		if (!objects.IsValid(object))
		{
			return false;
		}
		objects.Release(object, lastUse);
		instances.Edit(object.Index) = {}; // Dead row: Flags and Generation are zero.
		return true;
	}

	bool GpuScene::IsValid(RenderObjectHandle object) const
	{
		return objects.IsValid(object);
	}

	bool GpuScene::SetTransform(RenderObjectHandle object, const RenderAffine& transform)
	{
		auto* record = objects.Get(object);
		if (!record)
		{
			return false;
		}
		const auto& current = transforms.Get(object.Index);
		if (SameAffine(current.Current, transform))
		{
			return true;
		}
		auto& row = transforms.Edit(object.Index);
		if (record->TransformFrame != frame)
		{
			// First move this frame: what the GPU drew last frame becomes Previous.
			std::copy(std::begin(row.Current), std::end(row.Current), row.Previous);
			record->TransformFrame = frame;
			moved.push_back(object);
		}
		std::copy(transform.Rows.begin(), transform.Rows.end(), row.Current);
		return true;
	}

	bool GpuScene::SetMesh(RenderObjectHandle object, GpuMeshHandle mesh, const RenderBounds& localBounds)
	{
		if (!objects.IsValid(object))
		{
			return false;
		}
		const auto& current = instances.Get(object.Index);
		const auto meshIndex = mesh ? mesh.Index : GpuInstanceRecord::InvalidIndex;
		if (current.MeshIndex == meshIndex && current.MeshGeneration == (mesh ? mesh.Generation : 0) && SameBounds(current, localBounds))
		{
			return true;
		}
		auto& row = EditInstance(object);
		WriteMesh(row, mesh);
		WriteBounds(row, localBounds);
		return true;
	}

	bool GpuScene::SetBounds(RenderObjectHandle object, const RenderBounds& localBounds)
	{
		if (!objects.IsValid(object))
		{
			return false;
		}
		if (!SameBounds(instances.Get(object.Index), localBounds))
		{
			WriteBounds(EditInstance(object), localBounds);
		}
		return true;
	}

	bool GpuScene::SetMaterialSet(RenderObjectHandle object, std::uint32_t materialSet)
	{
		if (!objects.IsValid(object))
		{
			return false;
		}
		if (instances.Get(object.Index).MaterialSet != materialSet)
		{
			EditInstance(object).MaterialSet = materialSet;
		}
		return true;
	}

	bool GpuScene::SetFlags(RenderObjectHandle object, RenderObjectFlags flags)
	{
		if (!objects.IsValid(object))
		{
			return false;
		}
		const auto current = instances.Get(object.Index).Flags;
		const auto updated = (current & ~ProducerMask) | (static_cast<std::uint32_t>(flags) & ProducerMask);
		if (updated != current)
		{
			EditInstance(object).Flags = updated;
		}
		return true;
	}

	bool GpuScene::SetObjectId(RenderObjectHandle object, std::uint32_t objectId)
	{
		if (!objects.IsValid(object))
		{
			return false;
		}
		if (instances.Get(object.Index).ObjectId != objectId)
		{
			EditInstance(object).ObjectId = objectId;
		}
		return true;
	}

	bool GpuScene::SetSkin(RenderObjectHandle object, std::uint32_t skinIndex)
	{
		if (!objects.IsValid(object))
		{
			return false;
		}
		if (instances.Get(object.Index).SkinIndex != skinIndex)
		{
			EditInstance(object).SkinIndex = skinIndex;
		}
		return true;
	}

	bool GpuScene::SetPreviousVertexOffset(RenderObjectHandle object, std::uint32_t vertices)
	{
		if (!objects.IsValid(object))
		{
			return false;
		}
		if (instances.Get(object.Index).PreviousVertexOffset != vertices)
		{
			EditInstance(object).PreviousVertexOffset = vertices;
		}
		return true;
	}

	bool GpuScene::SetLodBias(RenderObjectHandle object, float lodBias)
	{
		if (!objects.IsValid(object))
		{
			return false;
		}
		if (instances.Get(object.Index).LodBias != lodBias)
		{
			EditInstance(object).LodBias = lodBias;
		}
		return true;
	}

	const GpuInstanceRecord* GpuScene::GetInstance(RenderObjectHandle object) const
	{
		return objects.IsValid(object) ? &instances.Get(object.Index) : nullptr;
	}

	const GpuTransformRecord* GpuScene::GetTransform(RenderObjectHandle object) const
	{
		return objects.IsValid(object) ? &transforms.Get(object.Index) : nullptr;
	}

	void GpuScene::Settle()
	{
		for (auto object : settling)
		{
			const auto* record = objects.Get(object);
			if (!record || record->TransformFrame == frame)
			{
				continue; // Destroyed, or moved again this frame (Previous already updated).
			}
			const auto& row = transforms.Get(object.Index);
			if (std::memcmp(row.Previous, row.Current, sizeof(row.Current)) != 0)
			{
				auto& edited = transforms.Edit(object.Index);
				std::copy(std::begin(edited.Current), std::end(edited.Current), edited.Previous);
			}
		}
		settling.clear();
	}

	GpuSceneGraphResources GpuScene::Import(RenderGraph& graph)
	{
		if (importPending)
		{
			throw std::logic_error(name + " has uploads awaiting CommitUploads/AbortUploads");
		}
		Settle();
		GpuSceneGraphResources resources;
		auto instanceImport = instances.Import(graph);
		GpuRecordBuffer<GpuTransformRecord>::ImportResult transformImport;
		try
		{
			transformImport = transforms.Import(graph);
		}
		catch (...)
		{
			instances.Abort();
			throw;
		}
		resources.Instances = instanceImport.Buffer;
		resources.Transforms = transformImport.Buffer;
		resources.RowCount = rowCount;
		for (auto* pass : { &instanceImport.Pass, &transformImport.Pass })
		{
			if (*pass)
			{
				resources.UploadPasses.push_back(**pass);
			}
		}
		resources.InstanceRows = instanceImport.Rows;
		resources.TransformRows = transformImport.Rows;
		resources.UploadRuns = instanceImport.Runs + transformImport.Runs;
		resources.UploadBytes = instanceImport.Bytes + transformImport.Bytes;

		last.LastInstanceRows = resources.InstanceRows;
		last.LastTransformRows = resources.TransformRows;
		last.LastUploadRuns = resources.UploadRuns;
		last.LastUploadBytes = resources.UploadBytes;
		totalBytes += resources.UploadBytes;
		importPending = !resources.UploadPasses.empty();
		if (!importPending)
		{
			instances.Commit();
			transforms.Commit();
		}
		// The frame ends here: objects moved this frame settle during the next import.
		settling = std::move(moved);
		moved.clear();
		++frame;
		return resources;
	}

	void GpuScene::CommitUploads()
	{
		instances.Commit();
		transforms.Commit();
		importPending = false;
	}

	void GpuScene::AbortUploads()
	{
		instances.Abort();
		transforms.Abort();
		importPending = false;
	}

	std::size_t GpuScene::Collect()
	{
		return objects.CollectRetired();
	}

	std::size_t GpuScene::Drain()
	{
		return objects.Drain();
	}

	GpuSceneStats GpuScene::GetStats() const
	{
		auto stats = last;
		const auto registry = objects.GetStats();
		stats.LiveObjects = registry.Live;
		stats.RetiringObjects = registry.Retiring;
		stats.RowCount = rowCount;
		stats.Capacity = registry.MaxSlots;
		stats.DirtyInstanceRows = instances.GetDirtyRows();
		stats.DirtyTransformRows = transforms.GetDirtyRows();
		stats.SettlingTransforms = static_cast<std::uint32_t>(settling.size());
		stats.TotalUploadBytes = totalBytes;
		stats.Frame = frame - 1;
		return stats;
	}
} // namespace Swim::Render
