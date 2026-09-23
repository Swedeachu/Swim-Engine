#pragma once
#include "Engine/Systems/Renderer/Geometry/GeometryGraphResources.h"
#include "Engine/Systems/Renderer/GpuScene/GpuSceneGraphResources.h"
#include "Engine/Systems/Renderer/Visibility/GpuVisibilityDesc.h"
#include "Engine/Systems/Renderer/Visibility/HzbGraphResources.h"
#include "Engine/Systems/Renderer/Visibility/VisibilityFrameDesc.h"
#include "Engine/Systems/Renderer/Visibility/VisibilityGraphResources.h"

#include <memory>
#include <vector>

namespace Swim::Render
{
	// GPU-driven visibility and draw generation (critical-path items 49, 51-55, 57).
	// One compute pass per view (per phase) reads every GPU Scene row and, without
	// any CPU round trip:
	//   - skips dead/hidden/meshless rows and frustum-culls world bounding spheres;
	//   - with occlusion, splits the frame into an early phase (last frame's visible
	//     set) and a late phase tested against this frame's HZB (VisibilityPhase.h);
	//   - selects a LOD from projected mesh LOD error with per-row hysteresis;
	//   - appends one DrawIndexedIndirectCommand + GpuDrawRecord per submesh of the
	//     LOD into its bounded (material bin, index page) bin, compacting visible
	//     draws, and counts them per bin for DrawIndexedIndirectCount;
	//   - accumulates VisibilityStats, optionally read back asynchronously.
	// RunVisibilityReference (VisibilityReference.h) is the CPU definition.
	//
	// Owner thread. The pipeline/layout must outlive this object; persistent LOD and
	// occlusion history and the material-bin table are owned here and imported once
	// per graph, so the early and late phases of a frame share them.
	class GpuVisibility
	{
	  public:
		GpuVisibility(Rhi::Device& device, GpuVisibilityDesc desc);
		~GpuVisibility();
		GpuVisibility(const GpuVisibility&) = delete;
		GpuVisibility& operator=(const GpuVisibility&) = delete;

		// Routes a GPU Scene material set to a material bin (uploaded with the next Record).
		void SetMaterialBin(std::uint32_t materialSet, std::uint32_t materialBin);
		std::uint32_t GetMaterialBin(std::uint32_t materialSet) const;

		VisibilityGraphResources Record(RenderGraph& graph, const GpuSceneGraphResources& scene, const GeometryGraphResources& geometry,
			const VisibilityFrameDesc& frame);

		const VisibilityBinLayout& GetBins() const { return bins; }

		std::uint32_t GetMaxObjects() const { return maxObjects; }

	  private:
		struct PersistentImports
		{
			std::uint64_t Graph = 0;
			GraphBuffer MaterialTable;
			GraphBuffer LodState;
			GraphBuffer OcclusionHistory;
			GraphTexture NullHzb;
		};

		PersistentImports& Import(RenderGraph& graph, std::uint64_t graphId);

		Rhi::ComputePipeline* pipeline;
		Rhi::PipelineLayout* layout;
		std::uint32_t space;
		std::uint32_t maxObjects;
		VisibilityBinLayout bins;
		std::vector<std::uint32_t> materialBins;
		std::unique_ptr<Rhi::Buffer> materialBinBuffer;
		std::unique_ptr<Rhi::Buffer> lodStateBuffer;
		std::unique_ptr<Rhi::Buffer> occlusionHistoryBuffer;
		std::unique_ptr<Rhi::Texture> nullHzb; // 1x1 stand-in bound by the phases without an HZB.
		PersistentImports imports;
		bool materialBinsDirty = true;
		bool historyInitialized = false;
		std::string name;
	};
} // namespace Swim::Render
