#include "Engine/Systems/Renderer/Visibility/VisibilityReference.h"
#include "Engine/Systems/Renderer/GpuScene/RenderObjectFlags.h"
#include "Engine/Systems/Renderer/Visibility/VisibilityMath.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Swim::Render
{
	VisibilityReferenceResult RunVisibilityReference(const VisibilityReferenceInputs& inputs, std::vector<GpuLodState>& lodState)
	{
		if (inputs.Phase != VisibilityPhase::Single)
		{
			throw std::invalid_argument("Early/late visibility phases need an occlusion history");
		}
		std::vector<std::uint32_t> unused;
		return RunVisibilityReference(inputs, lodState, unused);
	}

	VisibilityReferenceResult RunVisibilityReference(
		const VisibilityReferenceInputs& inputs, std::vector<GpuLodState>& lodState, std::vector<std::uint32_t>& occlusionHistory)
	{
		if (!inputs.Bins || inputs.IndexPages.size() > inputs.Bins->GetPageSlots())
		{
			throw std::invalid_argument("Visibility reference needs a bin layout covering every index-page slot");
		}
		const bool late = inputs.Phase == VisibilityPhase::Late;
		if (late && (!inputs.Hzb || inputs.Hzb->GetConvention() != VisibilityMath::ViewDepthConvention(inputs.View)))
		{
			throw std::invalid_argument("The late visibility phase needs this frame's HZB, built with the view's depth convention");
		}
		occlusionHistory.resize(std::max(occlusionHistory.size(), inputs.Instances.size()), 0u);
		const bool resetOcclusion = (inputs.View.Flags & std::uint32_t(GpuViewFlags::ResetOcclusionHistory)) != 0;
		const bool disableOcclusion = (inputs.View.Flags & std::uint32_t(GpuViewFlags::DisableOcclusion)) != 0;
		VisibilityMath::HzbDims dims;
		if (late)
		{
			dims = { inputs.Hzb->GetWidth(), inputs.Hzb->GetHeight(), inputs.Hzb->GetMipCount() };
		}
		VisibilityReferenceResult result;
		result.Bins.resize(inputs.Bins->GetBinCount());
		lodState.resize(std::max(lodState.size(), inputs.Instances.size()));
		auto& stats = result.Stats;
		const auto live = static_cast<std::uint32_t>(RenderObjectFlags::Live);
		const auto drawable = static_cast<std::uint32_t>(RenderObjectFlags::Live | RenderObjectFlags::HasMesh | RenderObjectFlags::Visible);
		const bool reset = (inputs.View.Flags & std::uint32_t(GpuViewFlags::ResetLodHistory)) != 0;
		for (std::uint32_t row = 0; row < inputs.Instances.size(); ++row)
		{
			const auto& instance = inputs.Instances[row];
			if ((instance.Flags & live) == 0)
			{
				continue;
			}
			++stats.Tested;
			if ((instance.Flags & drawable) != drawable || instance.MeshIndex >= inputs.Meshes.size())
			{
				++stats.NotDrawable;
				if (late)
				{
					occlusionHistory[row] = 0;
				}
				continue;
			}
			const auto sphere = VisibilityMath::WorldSphere(instance, inputs.Transforms[instance.TransformIndex]);
			if (!VisibilityMath::InsideFrustum(inputs.View, sphere))
			{
				++stats.FrustumCulled;
				if (late)
				{
					occlusionHistory[row] = 0;
				}
				continue;
			}
			// Two-phase occlusion: early draws last frame's visible set, late the rest.
			const bool visibleLastFrame = resetOcclusion || occlusionHistory[row] == instance.Generation;
			if (inputs.Phase == VisibilityPhase::Early && !visibleLastFrame)
			{
				++stats.Deferred;
				continue;
			}
			if (late)
			{
				const bool occluded = !disableOcclusion &&
					VisibilityMath::OccludedByHzb(inputs.View, sphere, dims,
						[&](std::uint32_t mip, std::uint32_t x, std::uint32_t y)
						{
							return inputs.Hzb->Fetch(mip, x, y);
						});
				occlusionHistory[row] = occluded ? 0u : instance.Generation;
				if (visibleLastFrame)
				{
					++stats.AlreadyDrawn;
					continue;
				}
				if (occluded)
				{
					++stats.Occluded;
					continue;
				}
			}
			++stats.Visible;
			const auto& mesh = inputs.Meshes[instance.MeshIndex];
			auto& history = lodState[row];
			const float threshold = inputs.View.LodPixelError * std::exp2(instance.LodBias);
			const auto lod = VisibilityMath::SelectLodWithHistory(mesh, VisibilityMath::ErrorScale(inputs.View, sphere), threshold,
				inputs.View.LodHysteresis, !reset && history.Generation == instance.Generation, history.Lod);
			history = { instance.Generation, lod };
			++stats.LodCounts[lod];

			// Unmapped material sets and out-of-range bins use material bin 0.
			auto materialBin = instance.MaterialSet < inputs.MaterialBins.size() ? inputs.MaterialBins[instance.MaterialSet] : 0u;
			materialBin = materialBin < inputs.Bins->GetMaterialBins() ? materialBin : 0u;
			std::uint32_t slot = UINT32_MAX;
			for (std::uint32_t i = 0; i < inputs.IndexPages.size(); ++i)
			{
				if (inputs.IndexPages[i] == mesh.IndexPage)
				{
					slot = i;
					break;
				}
			}
			const auto [first, count] = VisibilityMath::LodSubmeshes(mesh, lod);
			for (std::uint32_t s = first; s < first + count; ++s)
			{
				if (slot == UINT32_MAX)
				{
					++stats.OtherPage;
					continue;
				}
				const auto bin = inputs.Bins->GetBin(materialBin, slot);
				auto& draws = result.Bins[bin];
				if (draws.size() >= inputs.Bins->GetRange(bin).Capacity)
				{
					++stats.Dropped;
					continue;
				}
				const auto& submesh = inputs.Submeshes[s];
				draws.push_back(
					{ { submesh.IndexCount, 1, submesh.FirstIndex, submesh.VertexOffset, static_cast<std::uint32_t>(draws.size()) },
						{ row, s } });
				++stats.Draws;
			}
		}
		return result;
	}
} // namespace Swim::Render
