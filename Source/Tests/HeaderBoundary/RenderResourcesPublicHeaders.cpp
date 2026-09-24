#include "Engine/Systems/Renderer/ClusteredLighting/ClusterReference.h"
#include "Engine/Systems/Renderer/ClusteredLighting/ClusteredLightAssigner.h"
#include "Engine/Systems/Renderer/Environment/EnvironmentBuilder.h"
#include "Engine/Systems/Renderer/ForwardPlus/ForwardPlusReference.h"
#include "Engine/Systems/Renderer/ForwardPlus/ForwardPlusRenderer.h"
#include "Engine/Systems/Renderer/ForwardPlus/StandardVertex.h"
#include "Engine/Systems/Renderer/Environment/EnvironmentReference.h"
#include "Engine/Systems/Renderer/Geometry/GeometryHeap.h"
#include "Engine/Systems/Renderer/Lights/GpuLightBuffer.h"
#include "Engine/Systems/Renderer/Lights/LightMath.h"
#include "Engine/Systems/Renderer/GpuMaterials/GpuMaterialTable.h"
#include "Engine/Systems/Renderer/Materials/MaterialInstance.h"
#include "Engine/Systems/Renderer/Materials/StandardMaterial.h"
#include "Engine/Systems/Renderer/Geometry/GeometryRangeAllocator.h"
#include "Engine/Systems/Renderer/GpuScene/GpuScene.h"
#include "Engine/Systems/Renderer/PostProcess/PostProcessReference.h"
#include "Engine/Systems/Renderer/PostProcess/PostProcessor.h"
#include "Engine/Systems/Renderer/RHI/RhiFormatInfo.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphTransfers.h"
#include "Engine/Systems/Renderer/Resources/BindlessResourceTable.h"
#include "Engine/Systems/Renderer/Resources/GpuResourceRegistry.h"
#include "Engine/Systems/Renderer/Resources/GpuSamplerCache.h"
#include "Engine/Systems/Renderer/Residency/AssetResidencyService.h"
#include "Engine/Systems/Renderer/Residency/MeshGeometryPayload.h"
#include "Engine/Systems/Renderer/Residency/TextureResidency.h"
#include "Engine/Systems/Renderer/Shadows/ShadowAtlasAllocator.h"
#include "Engine/Systems/Renderer/Shadows/ShadowMath.h"
#include "Engine/Systems/Renderer/Shadows/ShadowPlanner.h"
#include "Engine/Systems/Renderer/Shadows/ShadowRenderer.h"
#include "Engine/Systems/Renderer/Particles/ParticleSystem.h"
#include "Engine/Systems/Renderer/Skinning/SkinningSystem.h"
#include "Engine/Systems/Renderer/ScreenSpace/ScreenSpaceEffects.h"
#include "Engine/Systems/Renderer/Temporal/TemporalAntiAliasing.h"
#include "Engine/Systems/Renderer/Temporal/TemporalReference.h"
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
static_assert(
	Swim::Render::SelectVisibilityDrawPath(Swim::Rhi::GraphicsCapabilities{}) == Swim::Render::VisibilityDrawPath::ZeroFilledIndirect);
static_assert(Swim::Render::MaterialParameterAlignment(Swim::Render::MaterialParameterType::Float3) == 16);
static_assert(!std::is_copy_constructible_v<Swim::Render::GpuMaterialTable>);
static_assert(Swim::Render::StandardMaterialRecordSize == 80);
static_assert(sizeof(Swim::Render::GpuLightRecord) == 64 && sizeof(Swim::Render::GpuLightHeader) == 32);
static_assert(sizeof(Swim::Render::GpuShadowRecord) == 64 && sizeof(Swim::Render::GpuShadowView) == 96);
static_assert(Swim::Render::ShadowRenderer::AtlasFormat == Swim::Rhi::Format::D32Float);
static_assert(Swim::Render::ShadowDepthBindings::Count == 6 && Swim::Render::ShadowBinCount == 3);
static_assert(Swim::Rhi::GetTransferTexelBytes(Swim::Rhi::Format::D32Float) == 4);
static_assert(sizeof(Swim::Render::GpuExposureState) == 16 && sizeof(Swim::Render::GpuPostParams) == 128);
static_assert(Swim::Render::PostHistogramBins == 256 && Swim::Render::MaxBloomMips == 8);
static_assert(!std::is_copy_constructible_v<Swim::Render::PostProcessor>);
static_assert(sizeof(Swim::Render::TemporalResolveConstants) == 32 && Swim::Render::TemporalResolveBindings::Count == 5);
static_assert(!std::is_copy_constructible_v<Swim::Render::TemporalAntiAliasing>);
static_assert(sizeof(Swim::Render::ForwardViewRecord) == 208);
static_assert(Swim::Render::ForwardPlusRenderer::VelocityFormat == Swim::Rhi::Format::RG16Float);
static_assert(Swim::Render::ForwardPlusRenderer::NormalFormat == Swim::Rhi::Format::RGBA16Float &&
	Swim::Render::ForwardPlusRenderer::IndirectFormat == Swim::Rhi::Format::RGBA16Float);
static_assert(Swim::Render::ForwardPlusRenderer::ReflectanceFormat == Swim::Rhi::Format::RGBA16Float &&
	Swim::Render::ForwardPlusRenderer::SpecularFormat == Swim::Rhi::Format::RGBA16Float);
static_assert(sizeof(Swim::Render::GpuScreenSpaceParams) == 400 && Swim::Render::ScreenSpaceCompositeBindings::Count == 9 &&
	Swim::Render::ScreenSpaceReflectionBindings::Count == 7);
static_assert(Swim::Render::MaxAoSlices == 4 && Swim::Render::MaxAoSteps == 8);
static_assert(Swim::Render::MaxReflectionSteps == 256 && Swim::Render::MaxReflectionRefineSteps == 8);
static_assert(!std::is_copy_constructible_v<Swim::Render::ParticleSystem>);
static_assert(sizeof(Swim::Render::GpuParticle) == 48 && sizeof(Swim::Render::GpuParticleEmitter) == 320 &&
	sizeof(Swim::Render::GpuParticleFrame) == 128 && sizeof(Swim::Render::GpuParticleCounters) == 16);
static_assert(Swim::Render::ParticleRenderBindings::BindlessSpace == Swim::Render::ForwardPlusDrawBindings::BindlessSpace);
static_assert(!std::is_copy_constructible_v<Swim::Render::SkinningSystem>);
static_assert(
	sizeof(Swim::Render::GpuSkinVertex) == 32 && sizeof(Swim::Render::GpuMorphDelta) == 40 && sizeof(Swim::Render::GpuSkinDispatch) == 48);
static_assert(sizeof(Swim::Render::GpuInstanceRecord) == 64 && offsetof(Swim::Render::GpuInstanceRecord, PreviousVertexOffset) == 60);
