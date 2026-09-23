#include "Engine/Systems/Renderer/Lights/GpuLightBuffer.h"
#include "Engine/Systems/Renderer/GpuScene/GpuRecordUpload.h"
#include "Engine/Systems/Renderer/Lights/LightMath.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphTransfers.h"

#include <cstring>
#include <stdexcept>

namespace Swim::Render
{
	GpuLightBuffer::GpuLightBuffer(Rhi::Device& device, GpuLightBufferDesc desc)
		: directionalCapacity(desc.MaxDirectionalLights), localCapacity(desc.MaxLocalLights), name(std::move(desc.DebugName)),
		  dirty(desc.MaxDirectionalLights + desc.MaxLocalLights)
	{
		if (directionalCapacity == 0 || localCapacity == 0)
		{
			throw std::invalid_argument(name + " needs room for at least one directional and one local light");
		}
		const std::uint32_t rows = directionalCapacity + localCapacity;
		Rhi::BufferDesc lights;
		lights.Size = std::uint64_t(rows) * sizeof(GpuLightRecord);
		lights.Usage = Rhi::BufferUsage::Storage | Rhi::BufferUsage::TransferDestination | Rhi::BufferUsage::TransferSource;
		lights.DebugName = name;
		buffer = device.CreateBuffer(lights);
		const std::string headerName = name + " header";
		Rhi::BufferDesc headerDesc = lights;
		headerDesc.Size = sizeof(GpuLightHeader);
		headerDesc.DebugName = headerName;
		headerBuffer = device.CreateBuffer(headerDesc);
		if (!buffer || !headerBuffer)
		{
			throw std::runtime_error(name + " buffers could not be created");
		}
		mirror.resize(rows);
		rowOwners.assign(rows, UINT32_MAX);
		header.FirstLocalRow = directionalCapacity;
		header.LocalCapacity = localCapacity;
	}

	GpuLightBuffer::~GpuLightBuffer() = default;

	GpuLightBuffer::Slot* GpuLightBuffer::Resolve(GpuLightHandle light)
	{
		return const_cast<Slot*>(static_cast<const GpuLightBuffer*>(this)->Resolve(light));
	}

	const GpuLightBuffer::Slot* GpuLightBuffer::Resolve(GpuLightHandle light) const
	{
		if (!light.IsValid() || light.Index >= slots.size())
		{
			return nullptr;
		}
		const auto& slot = slots[light.Index];
		return slot.Live && slot.Generation == light.Generation ? &slot : nullptr;
	}

	void GpuLightBuffer::WriteRow(std::uint32_t row, const GpuLightRecord& record, std::uint32_t owner)
	{
		mirror[row] = record;
		rowOwners[row] = owner;
		slots[owner].Row = row;
		dirty.Mark(row);
	}

	bool GpuLightBuffer::Insert(std::uint32_t slot, const GpuLightRecord& record, bool directional)
	{
		auto& count = directional ? header.DirectionalCount : header.LocalCount;
		if (count == (directional ? directionalCapacity : localCapacity))
		{
			return false;
		}
		WriteRow((directional ? 0 : header.FirstLocalRow) + count, record, slot);
		++count;
		headerDirty = true;
		return true;
	}

	void GpuLightBuffer::Remove(std::uint32_t slot)
	{
		const std::uint32_t row = slots[slot].Row;
		const bool directional = row < header.FirstLocalRow;
		auto& count = directional ? header.DirectionalCount : header.LocalCount;
		const std::uint32_t last = (directional ? 0 : header.FirstLocalRow) + count - 1;
		if (row != last)
		{
			WriteRow(row, mirror[last], rowOwners[last]); // Swap the last row into the hole.
		}
		mirror[last] = {};
		rowOwners[last] = UINT32_MAX;
		--count;
		headerDirty = true;
	}

	std::optional<GpuLightHandle> GpuLightBuffer::TryCreate(const LightDesc& desc)
	{
		const auto record = Lights::EncodeLight(desc);
		const bool directional = desc.Type == LightType::Directional;
		if ((directional ? header.DirectionalCount == directionalCapacity : header.LocalCount == localCapacity))
		{
			return std::nullopt;
		}
		std::uint32_t slot = 0;
		if (!freeSlots.empty())
		{
			slot = freeSlots.back();
			freeSlots.pop_back();
		}
		else
		{
			slot = static_cast<std::uint32_t>(slots.size());
			slots.emplace_back();
		}
		auto& entry = slots[slot];
		entry.Generation = entry.Generation + 1 == 0 ? 1 : entry.Generation + 1;
		entry.Live = true;
		entry.Desc = desc;
		Insert(slot, record, directional);
		return GpuLightHandle{ slot, entry.Generation };
	}

	GpuLightHandle GpuLightBuffer::Create(const LightDesc& desc)
	{
		if (auto handle = TryCreate(desc))
		{
			return *handle;
		}
		throw std::length_error(name + " has no free " + (desc.Type == LightType::Directional ? "directional" : "local") + " rows");
	}

	bool GpuLightBuffer::Update(GpuLightHandle light, const LightDesc& desc)
	{
		auto* slot = Resolve(light);
		if (!slot)
		{
			return false;
		}
		const auto record = Lights::EncodeLight(desc);
		const bool wasDirectional = slot->Desc.Type == LightType::Directional;
		const bool directional = desc.Type == LightType::Directional;
		if (wasDirectional == directional)
		{
			WriteRow(slot->Row, record, light.Index);
		}
		else
		{
			if ((directional ? header.DirectionalCount == directionalCapacity : header.LocalCount == localCapacity))
			{
				throw std::length_error(name + " cannot move a light into a full range");
			}
			Remove(light.Index);
			Insert(light.Index, record, directional);
		}
		slots[light.Index].Desc = desc;
		return true;
	}

	bool GpuLightBuffer::Release(GpuLightHandle light)
	{
		auto* slot = Resolve(light);
		if (!slot)
		{
			return false;
		}
		Remove(light.Index);
		slot->Live = false;
		slot->Desc = {};
		freeSlots.push_back(light.Index);
		return true;
	}

	bool GpuLightBuffer::IsValid(GpuLightHandle light) const
	{
		return Resolve(light) != nullptr;
	}

	const LightDesc* GpuLightBuffer::Find(GpuLightHandle light) const
	{
		const auto* slot = Resolve(light);
		return slot ? &slot->Desc : nullptr;
	}

	std::optional<std::uint32_t> GpuLightBuffer::GetRow(GpuLightHandle light) const
	{
		const auto* slot = Resolve(light);
		return slot ? std::optional(slot->Row) : std::nullopt;
	}

	GpuLightGraphResources GpuLightBuffer::Import(RenderGraph& graph)
	{
		if (pending)
		{
			throw std::logic_error(name + " has an import awaiting CommitUploads/AbortUploads");
		}
		GpuLightGraphResources resources;
		resources.Lights = graph.ImportBuffer(*buffer, Rhi::ResourceState::ShaderRead);
		resources.Header = graph.ImportBuffer(*headerBuffer, Rhi::ResourceState::ShaderRead);
		resources.DirectionalCount = header.DirectionalCount;
		resources.LocalCount = header.LocalCount;
		resources.FirstLocalRow = header.FirstLocalRow;
		resources.RowCount = directionalCapacity + localCapacity;
		lastRows = lastRuns = 0;
		lastBytes = 0;
		lastHeader = false;
		auto rows = dirty.Take();
		const bool uploadHeader = headerDirty;
		try
		{
			if (!rows.empty())
			{
				auto snapshot = std::make_shared<std::vector<std::byte>>(rows.size() * sizeof(GpuLightRecord));
				for (std::size_t i = 0; i < rows.size(); ++i)
				{
					std::memcpy(snapshot->data() + i * sizeof(GpuLightRecord), &mirror[rows[i]], sizeof(GpuLightRecord));
				}
				auto runs = BuildRecordRuns(rows);
				lastRuns = static_cast<std::uint32_t>(runs.size());
				lastRows = static_cast<std::uint32_t>(rows.size());
				lastBytes = snapshot->size();
				resources.UploadPass = RecordRowRunsUpload(
					graph, name + " upload", resources.Lights, sizeof(GpuLightRecord), std::move(runs), std::move(snapshot));
			}
			if (uploadHeader)
			{
				resources.HeaderUploadPass =
					AddBufferUpload(graph, name + " header upload", std::as_bytes(std::span(&header, 1)), resources.Header);
				lastBytes += sizeof(GpuLightHeader);
				lastHeader = true;
			}
		}
		catch (...)
		{
			for (auto row : rows)
			{
				dirty.Mark(row);
			}
			lastRows = lastRuns = 0;
			lastBytes = 0;
			lastHeader = false;
			throw;
		}
		headerDirty = false;
		recorded = std::move(rows);
		recordedHeader = uploadHeader;
		pending = !recorded.empty() || recordedHeader;
		return resources;
	}

	void GpuLightBuffer::CommitUploads()
	{
		recorded.clear();
		recordedHeader = false;
		pending = false;
	}

	void GpuLightBuffer::AbortUploads()
	{
		for (auto row : recorded)
		{
			dirty.Mark(row);
		}
		headerDirty = headerDirty || recordedHeader;
		CommitUploads();
	}

	GpuLightBufferStats GpuLightBuffer::GetStats() const
	{
		GpuLightBufferStats stats;
		stats.DirectionalCapacity = directionalCapacity;
		stats.LocalCapacity = localCapacity;
		stats.DirectionalLights = header.DirectionalCount;
		stats.LocalLights = header.LocalCount;
		stats.DirtyRows = dirty.Size();
		stats.LastUploadRows = lastRows;
		stats.LastUploadRuns = lastRuns;
		stats.LastUploadBytes = lastBytes;
		stats.LastUploadHeader = lastHeader;
		return stats;
	}
} // namespace Swim::Render
