#pragma once
#include "Engine/Systems/Renderer/GpuMaterials/GpuMaterialGraphResources.h"
#include "Engine/Systems/Renderer/GpuMaterials/GpuMaterialTableDesc.h"
#include "Engine/Systems/Renderer/GpuScene/Internal/GpuDirtyRowSet.h"
#include "Engine/Systems/Renderer/Materials/MaterialInstance.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraph.h"
#include "Engine/Systems/Renderer/Resources/GpuResourceRegistry.h"

#include <memory>
#include <optional>
#include <vector>

namespace Swim::Render
{
	// The GPU material parameter buffer (critical-path item 59): one persistent
	// device-local row per registered MaterialInstance, in its template's std430
	// layout. The row index is the material index a GPU Scene object stores in
	// RenderObjectDesc::MaterialSet; textures and samplers inside the records are
	// BindlessResourceTable indices.
	//
	// - Row 0 is a permanent fallback holding the template defaults, so an
	//   unassigned, released or out-of-range material index shades
	//   deterministically.
	// - Import compares each instance's version with the last uploaded one and
	//   records one batched transfer pass for the changed rows only (plus rows
	//   returned to the fallback). CommitUploads after a successful Execute;
	//   AbortUploads marks the rows dirty again.
	// - Release retires a row after its last GPU use; Collect then resets it to
	//   the defaults and frees the index (FIFO), like the bindless table.
	//
	// Owner thread, externally synchronized. Instances are shared: edits made on
	// them are picked up by the next Import.
	class GpuMaterialTable
	{
	  public:
		static constexpr std::uint32_t FallbackIndex = 0;

		// Throws std::invalid_argument without a template or with capacity < 2.
		GpuMaterialTable(Rhi::Device& device, GpuMaterialTableDesc desc);
		~GpuMaterialTable();
		GpuMaterialTable(const GpuMaterialTable&) = delete;
		GpuMaterialTable& operator=(const GpuMaterialTable&) = delete;

		// Empty when every row is live or retiring. Throws std::invalid_argument for a
		// null instance or one of another template.
		std::optional<GpuMaterialHandle> TryCreate(std::shared_ptr<const MaterialInstance> instance);
		// As above, but a full table throws std::length_error.
		GpuMaterialHandle Create(std::shared_ptr<const MaterialInstance> instance);
		bool IsValid(GpuMaterialHandle material) const;
		// FallbackIndex for invalid, stale or released handles.
		std::uint32_t GetIndex(GpuMaterialHandle material) const;
		// lastUse must cover every submission that may read the row.
		bool Release(GpuMaterialHandle material, Rhi::TimelinePoint lastUse = {});
		std::size_t Collect();
		std::size_t Drain();

		GpuMaterialGraphResources Import(RenderGraph& graph);
		void CommitUploads();
		void AbortUploads();

		const MaterialTemplate& GetTemplate() const { return *materialTemplate; }

		Rhi::Buffer& GetBuffer() const { return *buffer; }

		GpuMaterialTableStats GetStats() const;

	  private:
		struct Entry
		{
			std::shared_ptr<const MaterialInstance> Instance;
			std::uint64_t UploadedVersion = 0;
		};

		using Registry = GpuResourceRegistry<GpuMaterialTag, Entry>;

		void WriteRow(std::uint32_t row, std::span<const std::byte> record);
		void ResetRetired(GpuMaterialHandle released, Entry&);

		std::shared_ptr<const MaterialTemplate> materialTemplate;
		std::uint32_t recordSize;
		std::uint32_t capacity;
		std::string name;
		std::unique_ptr<Rhi::Buffer> buffer;
		std::unique_ptr<Registry> registry;
		std::vector<std::byte> mirror;
		Internal::GpuDirtyRowSet dirty;
		std::vector<std::uint32_t> recorded;
		bool pending = false;
		std::uint32_t lastRows = 0;
		std::uint32_t lastRuns = 0;
		std::uint64_t lastBytes = 0;
	};
} // namespace Swim::Render
