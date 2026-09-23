#include "Engine/Systems/Renderer/Environment/EnvironmentBuilder.h"
#include "Engine/Systems/Renderer/Environment/EnvironmentReference.h"
#include "Engine/Systems/Renderer/Geometry/GeometryHeap.h"
#include "Engine/Systems/Renderer/GpuMaterials/GpuMaterialTable.h"
#include "Engine/Systems/Renderer/Materials/MaterialInstance.h"
#include "Engine/Systems/Renderer/Materials/StandardMaterial.h"
#include "Engine/Systems/Renderer/Geometry/GeometryRangeAllocator.h"
#include "Engine/Systems/Renderer/GpuScene/GpuScene.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphTransfers.h"
#include "Engine/Systems/Renderer/Resources/BindlessResourceTable.h"
#include "Engine/Systems/Renderer/Resources/GpuResourceRegistry.h"
#include "Engine/Systems/Renderer/Resources/GpuSamplerCache.h"
#include "Engine/Systems/Renderer/Residency/AssetResidencyService.h"
#include "Engine/Systems/Renderer/Residency/MeshGeometryPayload.h"
#include "Engine/Systems/Renderer/Residency/TextureResidency.h"
#include "Engine/Systems/Renderer/Visibility/GpuVisibility.h"
#include "Engine/Systems/Renderer/Visibility/HzbBuilder.h"
#include "Engine/Systems/Renderer/Visibility/HzbPyramid.h"
#include "Engine/Systems/Renderer/Visibility/VisibilityDraws.h"
#include "Engine/Systems/Renderer/Visibility/RenderViewDesc.h"
#include "Engine/Systems/Renderer/Visibility/VisibilityReference.h"
#include <memory>
#include <type_traits>

namespace
{
	struct Record
	{
		std::unique_ptr<Swim::Rhi::Buffer> Buffer;
	};
} // namespace

// Registries/GeometryHeap compile against only backend-neutral RHI + RenderGraph headers.
static_assert(!std::is_copy_constructible_v<Swim::Render::GeometryHeap>);
static_assert(!std::is_copy_constructible_v<Swim::Render::GpuResourceRegistry<Swim::Render::GpuMeshTag, Record>>);
static_assert(!std::is_same_v<Swim::Render::GpuMeshHandle, Swim::Render::GpuTextureHandle>);
static_assert(sizeof(Swim::Render::GpuMeshHandle) == sizeof(std::uint64_t));
static_assert(sizeof(Swim::Render::GpuMeshMetadata) % 16 == 0);
static_assert(sizeof(Swim::Render::GpuSubmeshRecord) == 16);
static_assert(!std::is_copy_constructible_v<Swim::Render::TextureResidency>);
static_assert(!std::is_copy_constructible_v<Swim::Render::AssetResidencyService>);
static_assert(!std::is_copy_constructible_v<Swim::Render::BindlessResourceTable>);
static_assert(sizeof(Swim::Render::Environment::ProceduralSkyConstants) == Swim::Render::EnvironmentSkyBindings::PushConstantBytes);
static_assert(sizeof(Swim::Render::EnvironmentPrefilterConstants) == Swim::Render::EnvironmentPrefilterBindings::PushConstantBytes);
static_assert(!std::is_copy_constructible_v<Swim::Render::GpuSamplerCache>);
static_assert(!std::is_same_v<Swim::Render::BindlessTextureHandle, Swim::Render::BindlessSamplerHandle>);
static_assert(Swim::Render::BindlessResourceTable::FallbackIndex == 0);
static_assert(!std::is_copy_constructible_v<Swim::Render::GpuScene>);
static_assert(sizeof(Swim::Render::GpuInstanceRecord) == 64 && sizeof(Swim::Render::GpuTransformRecord) == 96);
static_assert(sizeof(Swim::Render::RenderAffine) == 48);
static_assert(!std::is_copy_constructible_v<Swim::Render::GpuVisibility>);
static_assert(sizeof(Swim::Render::GpuViewRecord) == 192);
static_assert(Swim::Render::CanonicalDepthConvention == Swim::Render::DepthConvention::ReverseZ);
static_assert(sizeof(Swim::Render::VisibilityStats) == 80);
static_assert(Swim::Render::SelectVisibilityDrawPath(Swim::Rhi::GraphicsCapabilities{}) == Swim::Render::VisibilityDrawPath::ZeroFilledIndirect);
static_assert(Swim::Render::MaterialParameterAlignment(Swim::Render::MaterialParameterType::Float3) == 16);
static_assert(!std::is_copy_constructible_v<Swim::Render::GpuMaterialTable>);
static_assert(Swim::Render::StandardMaterialRecordSize == 80);
