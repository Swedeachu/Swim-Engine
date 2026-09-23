#include "Engine/Systems/Renderer/GpuMaterials/GpuMaterialTable.h"
#include "Engine/Systems/Renderer/GpuScene/GpuRecordUpload.h"

#include <cstring>
#include <stdexcept>

namespace Swim::Render
{
	GpuMaterialTable::GpuMaterialTable(Rhi::Device& device, GpuMaterialTableDesc desc)
		: materialTemplate(std::move(desc.Template)), recordSize(materialTemplate ? materialTemplate->GetRecordSize() : 0),
		  capacity(desc.Capacity), name(std::move(desc.DebugName)), dirty(desc.Capacity)
	{
		if (!materialTemplate || capacity < 2)
		{
			throw std::invalid_argument(name + " needs a material template and at least two rows (the fallback and one material)");
		}
		buffer = device.CreateBuffer({ std::uint64_t(capacity) * recordSize,
			Rhi::BufferUsage::Storage | Rhi::BufferUsage::TransferDestination | Rhi::BufferUsage::TransferSource,
			Rhi::MemoryPreference::DeviceLocal, name });
		if (!buffer)
		{
			throw std::runtime_error(name + " buffer could not be created");
		}
		registry = std::make_unique<Registry>(GpuResourceRegistryDesc{ capacity, name });
		// Every row starts as the defaults, so any index the GPU may read is valid.
		mirror.resize(std::size_t(capacity) * recordSize);
		const auto defaults = materialTemplate->GetDefaultRecord();
		for (std::uint32_t row = 0; row < capacity; ++row)
		{
			WriteRow(row, defaults);
		}
		const auto fallback = registry->Create({}); // Row 0, never released.
		if (fallback.Index != FallbackIndex)
		{
			throw std::logic_error(name + " fallback row is not row 0");
		}
	}

	GpuMaterialTable::~GpuMaterialTable() = default;

	void GpuMaterialTable::WriteRow(std::uint32_t row, std::span<const std::byte> record)
	{
		std::memcpy(mirror.data() + std::size_t(row) * recordSize, record.data(), recordSize);
		dirty.Mark(row);
	}

	std::optional<GpuMaterialHandle> GpuMaterialTable::TryCreate(std::shared_ptr<const MaterialInstance> instance)
	{
		if (!instance || &instance->GetTemplate() != materialTemplate.get())
		{
			throw std::invalid_argument(name + " only holds instances of template " + materialTemplate->GetName());
		}
		const auto version = instance->GetVersion();
		const auto record = instance->GetRecord();
		auto handle = registry->TryCreate({ std::move(instance), version });
		if (handle)
		{
			WriteRow(handle->Index, record);
		}
		return handle;
	}

	GpuMaterialHandle GpuMaterialTable::Create(std::shared_ptr<const MaterialInstance> instance)
	{
		if (auto handle = TryCreate(std::move(instance)))
		{
			return *handle;
		}
		throw std::length_error(name + " has no free material rows");
	}

	bool GpuMaterialTable::IsValid(GpuMaterialHandle material) const
	{
		return material.Index != FallbackIndex && registry->IsValid(material);
	}

	std::uint32_t GpuMaterialTable::GetIndex(GpuMaterialHandle material) const
	{
		return IsValid(material) ? material.Index : FallbackIndex;
	}

	bool GpuMaterialTable::Release(GpuMaterialHandle material, Rhi::TimelinePoint lastUse)
	{
		return IsValid(material) && registry->Release(material, lastUse);
	}

	void GpuMaterialTable::ResetRetired(GpuMaterialHandle released, Entry&)
	{
		WriteRow(released.Index, materialTemplate->GetDefaultRecord());
	}

	std::size_t GpuMaterialTable::Collect()
	{
		return registry->CollectRetired(
			[this](GpuMaterialHandle released, Entry& entry)
			{
				ResetRetired(released, entry);
			});
	}

	std::size_t GpuMaterialTable::Drain()
	{
		return registry->Drain(
			[this](GpuMaterialHandle released, Entry& entry)
			{
				ResetRetired(released, entry);
			});
	}

	GpuMaterialGraphResources GpuMaterialTable::Import(RenderGraph& graph)
	{
		if (pending)
		{
			throw std::logic_error(name + " has an import awaiting CommitUploads/AbortUploads");
		}
		// Pick up instance edits made since the last upload.
		registry->ForEach(
			[&](GpuMaterialHandle handle, Entry& entry)
			{
				if (entry.Instance && entry.Instance->GetVersion() != entry.UploadedVersion)
				{
					entry.UploadedVersion = entry.Instance->GetVersion();
					WriteRow(handle.Index, entry.Instance->GetRecord());
				}
			});

		GpuMaterialGraphResources resources;
		resources.Materials = graph.ImportBuffer(*buffer, Rhi::ResourceState::ShaderRead);
		resources.MaterialCount = capacity;
		resources.RecordSize = recordSize;
		lastRows = lastRuns = 0;
		lastBytes = 0;
		auto rows = dirty.Take();
		if (rows.empty())
		{
			return resources;
		}
		try
		{
			auto snapshot = std::make_shared<std::vector<std::byte>>(rows.size() * recordSize);
			for (std::size_t i = 0; i < rows.size(); ++i)
			{
				std::memcpy(snapshot->data() + i * recordSize, mirror.data() + std::size_t(rows[i]) * recordSize, recordSize);
			}
			auto runs = BuildRecordRuns(rows);
			lastRuns = static_cast<std::uint32_t>(runs.size());
			lastRows = static_cast<std::uint32_t>(rows.size());
			lastBytes = snapshot->size();
			resources.UploadPass =
				RecordRowRunsUpload(graph, name + " upload", resources.Materials, recordSize, std::move(runs), std::move(snapshot));
		}
		catch (...)
		{
			for (auto row : rows)
			{
				dirty.Mark(row);
			}
			throw;
		}
		recorded = std::move(rows);
		pending = true;
		return resources;
	}

	void GpuMaterialTable::CommitUploads()
	{
		recorded.clear();
		pending = false;
	}

	void GpuMaterialTable::AbortUploads()
	{
		for (auto row : recorded)
		{
			dirty.Mark(row);
		}
		CommitUploads();
	}

	GpuMaterialTableStats GpuMaterialTable::GetStats() const
	{
		const auto registryStats = registry->GetStats();
		GpuMaterialTableStats stats;
		stats.Capacity = capacity;
		stats.RecordSize = recordSize;
		stats.LiveMaterials = registryStats.Live - 1;
		stats.RetiringMaterials = registryStats.Retiring;
		stats.DirtyRows = dirty.Size();
		stats.LastUploadRows = lastRows;
		stats.LastUploadRuns = lastRuns;
		stats.LastUploadBytes = lastBytes;
		return stats;
	}
} // namespace Swim::Render
