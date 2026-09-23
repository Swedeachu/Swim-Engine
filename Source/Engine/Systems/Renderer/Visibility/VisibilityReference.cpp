#include "Engine/Systems/Renderer/Visibility/VisibilityReference.h"
#include "Engine/Systems/Renderer/GpuScene/RenderObjectFlags.h"
#include "Engine/Systems/Renderer/Visibility/VisibilityMath.h"

#include <cmath>
#include <stdexcept>

namespace Swim::Render
{
	VisibilityReferenceResult RunVisibilityReference(const VisibilityReferenceInputs& inputs, std::vector<GpuLodState>& lodState)
	{
		if (!inputs.Bins || inputs.IndexPages.size() > inputs.Bins->GetPageSlots())
		{
			throw std::invalid_argument("Visibility reference needs a bin layout covering every index-page slot");
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
				continue;
			}
			const auto sphere = VisibilityMath::WorldSphere(instance, inputs.Transforms[instance.TransformIndex]);
			if (!VisibilityMath::InsideFrustum(inputs.View, sphere))
			{
				++stats.FrustumCulled;
				continue;
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
