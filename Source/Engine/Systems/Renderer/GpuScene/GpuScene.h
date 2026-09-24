#pragma once
#include "Engine/Systems/Renderer/GpuScene/GpuInstanceRecord.h"
#include "Engine/Systems/Renderer/GpuScene/GpuRecordBuffer.h"
#include "Engine/Systems/Renderer/GpuScene/GpuSceneDesc.h"
#include "Engine/Systems/Renderer/GpuScene/GpuSceneGraphResources.h"
#include "Engine/Systems/Renderer/GpuScene/GpuSceneStats.h"
#include "Engine/Systems/Renderer/GpuScene/GpuTransformRecord.h"
#include "Engine/Systems/Renderer/GpuScene/Internal/RenderObjectRecord.h"
#include "Engine/Systems/Renderer/GpuScene/RenderObjectDesc.h"
#include "Engine/Systems/Renderer/Resources/GpuResourceRegistry.h"

#include <optional>

namespace Swim::Render
{
	// The persistent render-facing object database (critical-path item 46). It is
	// not EnTT: any producer (scene extraction, procedural systems, tools) creates
	// RenderObjects through this API and gets a stable RenderObjectHandle whose
	// Index is the object's row in both GPU buffers.
	//
	//   Create/Set*/Destroy  edit CPU mirrors and mark only the touched rows
	//   Import(graph)        uploads dirty rows (contiguous runs, one staged pass
	//                        per buffer) and ends the frame
	//   Commit/AbortUploads  after the graph's Execute succeeds / fails
	//   Collect/Drain        reuse rows of destroyed objects after their lastUse
	//
	// Transforms keep the previous frame's value for motion vectors: the first
	// SetTransform in a frame moves Current into Previous, and an object that
	// moved in frame N but not in N+1 gets Previous = Current re-uploaded once in
	// N+1 ("settling"). Transform-only changes never touch instance rows.
	//
	// Owner-thread only. Destroyed rows are rewritten as dead (Flags == 0) at the
	// next import; their index is reused only after lastUse completes.
	class GpuScene
	{
	  public:
		GpuScene(Rhi::Device& device, GpuSceneDesc desc = {});
		~GpuScene();
		GpuScene(const GpuScene&) = delete;
		GpuScene& operator=(const GpuScene&) = delete;

		// Empty when every row is live or retiring.
		std::optional<RenderObjectHandle> TryCreate(const RenderObjectDesc& desc);
		// Throws std::length_error when full.
		RenderObjectHandle Create(const RenderObjectDesc& desc);
		// lastUse must cover every submission that may read the rows. False for
		// invalid/stale handles.
		bool Destroy(RenderObjectHandle object, Rhi::TimelinePoint lastUse = {});
		bool IsValid(RenderObjectHandle object) const;

		// Setters return false for invalid/stale handles and mark only the rows
		// whose contents actually change.
		bool SetTransform(RenderObjectHandle object, const RenderAffine& transform);
		// An invalid mesh makes the object undrawable (HasMesh cleared).
		bool SetMesh(RenderObjectHandle object, GpuMeshHandle mesh, const RenderBounds& localBounds);
		bool SetBounds(RenderObjectHandle object, const RenderBounds& localBounds);
		bool SetMaterialSet(RenderObjectHandle object, std::uint32_t materialSet);
		// Producer bits only (RenderObjectFlags::ProducerMask); Live/HasMesh are kept.
		bool SetFlags(RenderObjectHandle object, RenderObjectFlags flags);
		bool SetObjectId(RenderObjectHandle object, std::uint32_t objectId);
		bool SetSkin(RenderObjectHandle object, std::uint32_t skinIndex);
		// GpuInstanceRecord::PreviousVertexOffset (a GPU-skinned mesh's previous positions).
		bool SetPreviousVertexOffset(RenderObjectHandle object, std::uint32_t vertices);
		bool SetLodBias(RenderObjectHandle object, float lodBias);

		// Mirror contents (what the GPU will hold after the next import).
		const GpuInstanceRecord* GetInstance(RenderObjectHandle object) const;
		const GpuTransformRecord* GetTransform(RenderObjectHandle object) const;

		const GpuInstanceRecord& GetInstanceRow(std::uint32_t row) const { return instances.Get(row); }

		const GpuTransformRecord& GetTransformRow(std::uint32_t row) const { return transforms.Get(row); }

		GpuSceneGraphResources Import(RenderGraph& graph);
		void CommitUploads();
		void AbortUploads();

		std::size_t Collect();
		std::size_t Drain();

		Rhi::Buffer& GetInstanceBuffer() const { return instances.GetBuffer(); }

		Rhi::Buffer& GetTransformBuffer() const { return transforms.GetBuffer(); }

		GpuSceneStats GetStats() const;

	  private:
		using Registry = GpuResourceRegistry<RenderObjectTag, Internal::RenderObjectRecord>;

		GpuInstanceRecord& EditInstance(RenderObjectHandle object) { return instances.Edit(object.Index); }

		void Settle();

		Registry objects;
		GpuRecordBuffer<GpuInstanceRecord> instances;
		GpuRecordBuffer<GpuTransformRecord> transforms;
		std::vector<RenderObjectHandle> moved;	  // SetTransform this frame.
		std::vector<RenderObjectHandle> settling; // Moved last frame.
		std::uint32_t rowCount = 0;
		std::uint64_t frame = 1;
		bool importPending = false;
		GpuSceneStats last;
		std::uint64_t totalBytes = 0;
		std::string name;
	};
} // namespace Swim::Render
