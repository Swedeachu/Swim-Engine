#pragma once
#include "Engine/Systems/Renderer/Geometry/GeometryGraphResources.h"
#include "Engine/Systems/Renderer/Geometry/GeometryHeap.h"
#include "Engine/Systems/Renderer/Geometry/GeometryRangeAllocator.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraph.h"
#include "Engine/Systems/Renderer/Resources/GpuResourceRegistry.h"
#include "Engine/Systems/Renderer/Skinning/SkinningBindings.h"
#include "Engine/Systems/Renderer/Skinning/SkinningGraphResources.h"
#include "Engine/Systems/Renderer/Skinning/SkinningReference.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Swim::Render
{
	struct SkinningSystemDesc
	{
		Rhi::ComputePipeline* Pipeline = nullptr; // SwimSkinning
		Rhi::PipelineLayout* Layout = nullptr;
		std::uint32_t MaxSourceVertices = 1u << 20; // Bind-pose vertices of every skinned mesh.
		std::uint32_t MaxMorphDeltas = 1u << 20;	// Sparse deltas of every skinned mesh.
		std::uint32_t MaxMeshes = 1024;
		std::uint32_t MaxInstances = 4096;
		std::string DebugName = "Skinning";
	};

	// A skinned mesh's bind-pose source: StandardVertex vertices with influences
	// and morph targets, plus the draw data every instance's output mesh copies.
	struct SkinnedMeshDesc
	{
		std::span<const StandardVertex> Vertices;
		std::span<const SkinInfluence> Influences;
		std::span<const SkinnedMorphTarget> MorphTargets;
		std::uint32_t JointCount = 0;
		Rhi::IndexType IndexFormat = Rhi::IndexType::Uint32;
		std::span<const std::byte> Indices;
		std::span<const GeometrySubmesh> Submeshes;
		std::span<const GeometryLodRange> Lods;
		std::string_view DebugName;
	};

	// One frame's pose of an instance: JointCount matrices (model x inverse bind,
	// Animation::SkeletonInstance's palette) and one weight per morph target, for
	// this frame and the previous one.
	struct SkinPose
	{
		std::span<const SkinMatrix> Palette;
		std::span<const SkinMatrix> PreviousPalette;
		std::span<const float> MorphWeights;
		std::span<const float> PreviousMorphWeights;
	};

	struct SkinningStats
	{
		std::uint32_t Meshes = 0;
		std::uint32_t Instances = 0;
		std::uint32_t SourceVertices = 0; // Allocated in the source pool.
		std::uint32_t MorphDeltas = 0;
		std::uint32_t PendingMeshes = 0; // Source data not yet uploaded.
	};

	// GPU skinning and morph targets (critical-path item 78), compute-based.
	//
	// Each skinned mesh keeps its bind-pose source in persistent pools (vertices,
	// influences and sparse per-vertex morph deltas). Each instance gets its own
	// output mesh in the GeometryHeap: the source's indices, submeshes and LODs over
	// 2 x VertexCount StandardVertex slots, the first half this frame's skinned
	// vertices and the second the previous frame's positions. Everything downstream
	// (GPU visibility, shadows, Clustered Forward+) draws it as an ordinary
	// StandardVertex mesh; the render object sets
	// GpuInstanceRecord::PreviousVertexOffset = GetPreviousVertexOffset so motion
	// vectors see the deformation, and SetBounds(ComputeBounds) for culling.
	//
	// Record (after GeometryHeap::Import on the same graph, before the passes that
	// draw the meshes) uploads pending sources and the frame's palettes and weights,
	// then records one compute pass: one dispatch per output page, one group row per
	// instance. Instances skin when SetPose was called since the last committed frame;
	// the frame after that they skin once more with previous = current ("settling"),
	// then stop until the next SetPose. Output meshes still PendingUpload in the heap
	// are skipped. Vertex choice: compute skinning once per instance per frame serves
	// every view and pass (depth, shadows, Forward+) instead of re-skinning in each
	// vertex shader, at the cost of one output copy per instance.
	//
	// Owner thread, externally synchronized. After Execute call CommitFrame (or
	// AbortFrame). The heap and device must outlive the system.
	class SkinningSystem
	{
	  public:
		// Throws std::invalid_argument for a missing program or zero capacities and
		// std::runtime_error when a pool cannot be created.
		SkinningSystem(Rhi::Device& device, GeometryHeap& heap, SkinningSystemDesc desc);
		~SkinningSystem();
		SkinningSystem(const SkinningSystem&) = delete;
		SkinningSystem& operator=(const SkinningSystem&) = delete;

		// Throws std::invalid_argument for invalid data (see Skinning::BuildSource) and
		// std::length_error when the pools or mesh slots are full.
		SkinnedMeshHandle CreateSkinnedMesh(const SkinnedMeshDesc& desc);
		// False for invalid handles or while instances of the mesh exist.
		bool DestroySkinnedMesh(SkinnedMeshHandle mesh, Rhi::TimelinePoint lastUse = {});

		bool IsValid(SkinnedMeshHandle mesh) const { return meshes->IsValid(mesh); }

		// Creates the instance's output mesh in the heap (bind pose until first skinned).
		// Throws std::invalid_argument for an invalid mesh and std::length_error when full.
		SkinInstanceHandle CreateInstance(SkinnedMeshHandle mesh);
		// Destroys the output mesh against lastUse. False for invalid handles.
		bool DestroyInstance(SkinInstanceHandle instance, Rhi::TimelinePoint lastUse = {});

		bool IsValid(SkinInstanceHandle instance) const { return instances->IsValid(instance); }

		// False for invalid handles; throws std::invalid_argument when the spans do not
		// match the mesh's joint and morph target counts or hold non-finite values.
		bool SetPose(SkinInstanceHandle instance, const SkinPose& pose);

		GpuMeshHandle GetOutputMesh(SkinInstanceHandle instance) const;
		std::uint32_t GetPreviousVertexOffset(SkinInstanceHandle instance) const; // 0 for invalid handles.
		// Local bounds of the current pose (Infinite for invalid handles or no pose yet).
		RenderBounds ComputeBounds(SkinInstanceHandle instance) const;

		// Throws std::logic_error while a frame awaits CommitFrame/AbortFrame.
		SkinningGraphResources Record(RenderGraph& graph, const GeometryGraphResources& geometry);
		void CommitFrame();
		void AbortFrame();

		std::size_t Collect();
		std::size_t Drain();

		SkinningStats GetStats() const;

		Rhi::Buffer& GetSourceVertexBuffer() const { return *sourceVertices; }

	  private:
		struct Mesh
		{
			GeometryRange Vertices; // In vertices.
			GeometryRange Deltas;	// In deltas.
			std::uint32_t VertexCount = 0;
			std::uint32_t JointCount = 0;
			std::uint32_t MorphTargetCount = 0;
			SkinnedBoundsData Bounds;
			std::vector<StandardVertex> BindVertices; // Also the initial output contents.
			std::vector<std::byte> Indices;
			Rhi::IndexType IndexFormat = Rhi::IndexType::Uint32;
			std::vector<GeometrySubmesh> Submeshes;
			std::vector<GeometryLodRange> Lods;
			std::vector<GpuSkinVertex> PendingSkin; // Cleared once resident.
			std::vector<GpuMorphDelta> PendingDeltas;
			GpuUploadState Upload = GpuUploadState::PendingUpload;
			std::uint32_t Instances = 0;
			std::string Name;
		};

		struct Instance
		{
			SkinnedMeshHandle Mesh;
			GpuMeshHandle Output;
			std::vector<SkinMatrix> Palette;
			std::vector<SkinMatrix> PreviousPalette;
			std::vector<float> Weights;
			std::vector<float> PreviousWeights;
			bool HasPose = false;
			bool Dirty = false;	 // SetPose since the last committed frame.
			bool Settle = false; // Skin once more with previous = current.
			bool RecordedDirty = false;
			bool RecordedSettle = false;
		};

		using MeshRegistry = GpuResourceRegistry<SkinnedMeshTag, Mesh>;
		using InstanceRegistry = GpuResourceRegistry<GpuSkinTag, Instance>;

		Rhi::Device& device;
		GeometryHeap& heap;
		SkinningSystemDesc desc;
		std::unique_ptr<Rhi::Buffer> sourceVertices, skinVertices, morphDeltas;
		GeometryRangeAllocator vertexRanges;
		GeometryRangeAllocator deltaRanges;
		std::unique_ptr<MeshRegistry> meshes;
		std::unique_ptr<InstanceRegistry> instances;
		std::vector<SkinnedMeshHandle> uploading;
		std::vector<SkinInstanceHandle> recorded;
		bool pending = false;
	};
} // namespace Swim::Render
