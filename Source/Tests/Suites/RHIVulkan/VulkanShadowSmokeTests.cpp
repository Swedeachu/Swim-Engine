#include "Engine/Platform/PlatformSystem.h"
#include "Engine/Systems/Renderer/ClusteredLighting/ClusteredLightAssigner.h"
#include "Engine/Systems/Renderer/ForwardPlus/ForwardPlusRenderer.h"
#include "Engine/Systems/Renderer/Geometry/GeometryHeap.h"
#include "Engine/Systems/Renderer/GpuMaterials/GpuMaterialTable.h"
#include "Engine/Systems/Renderer/GpuScene/GpuScene.h"
#include "Engine/Systems/Renderer/Lights/GpuLightBuffer.h"
#include "Engine/Systems/Renderer/Lights/LightMath.h"
#include "Engine/Systems/Renderer/Materials/StandardMaterial.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanRhiBackend.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphExecutor.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphTransfers.h"
#include "Engine/Systems/Renderer/Resources/BindlessResourceTable.h"
#include "Engine/Systems/Renderer/Shadows/ShadowRenderer.h"
#include "Engine/Systems/Renderer/Visibility/GpuVisibility.h"
#include "Engine/Systems/Renderer/Visibility/RenderViewDesc.h"
#include "Tests/Fixtures/ClusterFixture.h"
#include "Tests/Fixtures/ShadowFixture.h"
#include "Tests/Fixtures/VulkanSmokeDiagnostics.h"
#include "Tests/Framework/Test.h"

#if defined(SWIM_SHADOW_DEPTH_SPIRV_PATH) && defined(SWIM_SHADOW_MASKED_SPIRV_PATH) && defined(SWIM_FORWARD_OPAQUE_SPIRV_PATH) &&          \
	defined(SWIM_FORWARD_TRANSPARENT_SPIRV_PATH) && defined(SWIM_FORWARD_TRANSPARENT_SORT_SPIRV_PATH) &&                                   \
	defined(SWIM_GPU_VISIBILITY_SPIRV_PATH) && defined(SWIM_CLUSTER_LIGHT_CULL_SPIRV_PATH) && defined(SWIM_CLUSTER_BOUNDS_SPIRV_PATH) &&   \
	defined(SWIM_CLUSTER_ASSIGN_SPIRV_PATH) && defined(SWIM_CLUSTER_SCAN_SPIRV_PATH) && defined(SWIM_ENVIRONMENT_SKY_SPIRV_PATH) &&        \
	defined(SWIM_ENVIRONMENT_DOWNSAMPLE_SPIRV_PATH) && defined(SWIM_ENVIRONMENT_PREFILTER_SPIRV_PATH) &&                                   \
	defined(SWIM_ENVIRONMENT_IRRADIANCE_SPIRV_PATH) && defined(SWIM_ENVIRONMENT_BRDF_LUT_SPIRV_PATH)
#include "Tests/Fixtures/VulkanEnvironmentFixture.h"
#define SWIM_SHADOW_SMOKE_AVAILABLE 1
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace
{
#ifdef SWIM_SHADOW_SMOKE_AVAILABLE
	namespace Smoke = Swim::Testing::EnvironmentSmoke;

	// A graphics program, its layout (optionally with the shared bindless space) and a pipeline.
	struct GraphicsProgram
	{
		std::unique_ptr<Swim::Rhi::ShaderProgram> Program;
		std::unique_ptr<Swim::Rhi::PipelineLayout> Layout;
		std::unique_ptr<Swim::Rhi::GraphicsPipeline> Pipeline;
	};

	template <typename MakePipelineDesc>
	GraphicsProgram LoadGraphics(Swim::Rhi::Device& device, const char* spirvPath, const char* reflectionPath,
		const Swim::Rhi::DescriptorSchemaDesc* bindlessSpace, const char* label, MakePipelineDesc&& pipelineDesc)
	{
		using namespace Swim;
		const auto reflected = Smoke::ReflectProgram(reflectionPath);
		const auto bytes = Smoke::ReadSpirv(spirvPath);
		const auto& programInterface = reflected.Interface;
		const std::array<Rhi::ShaderStageArtifact, 2> stages{ { { Rhi::ShaderStageMask::Vertex, "vertexMain", bytes },
			{ Rhi::ShaderStageMask::Fragment, "fragmentMain", bytes } } };
		GraphicsProgram program;
		program.Program =
			device.CreateShaderProgram({ stages, { programInterface.DescriptorSchemas, programInterface.PushConstants }, label });
		SWIM_REQUIRE(program.Program);
		if (bindlessSpace)
		{
			program.Layout = device.CreatePipelineLayout({ program.Program.get(), label, { bindlessSpace, 1 } });
		}
		else
		{
			program.Layout = device.CreatePipelineLayout({ program.Program.get(), label });
		}
		SWIM_REQUIRE(program.Layout);
		program.Pipeline = device.CreateGraphicsPipeline(pipelineDesc(*program.Program, *program.Layout));
		SWIM_REQUIRE_MESSAGE(program.Pipeline, std::string(label) + " pipeline");
		return program;
	}

	Swim::Render::ClusterProgram ClusterProgramOf(const Smoke::ComputeProgram& program)
	{
		return { program.Pipeline.get(), program.Layout.get(), program.Space };
	}

	// GPU time of the passes whose names start with `prefix`, charged from the previous
	// pass's end timestamp to their own (see VulkanForwardPlusSmokeTests.cpp).
	double PassMilliseconds(const std::vector<Swim::Render::GraphPassTiming>& timings, std::string_view prefix, bool* measured = nullptr)
	{
		double total = 0.0;
		bool any = false;
		double previousEnd = 0.0;
		for (const auto& timing : timings)
		{
			if (!timing.EndOffsetNanoseconds)
			{
				continue;
			}
			const double end = *timing.EndOffsetNanoseconds;
			if (timing.Name.starts_with(prefix))
			{
				total += std::max(end - previousEnd, 0.0) * 1.0e-6;
				any = true;
			}
			previousEnd = std::max(previousEnd, end);
		}
		if (measured)
		{
			*measured = any;
		}
		return total;
	}

	float RelativeError(float actual, float expected)
	{
		return std::abs(actual - expected) / std::max(std::abs(expected), 0.05f);
	}
#endif

	// Phase 16 (items 70-72) on a real device. A ground plane, an opaque cube, an
	// alpha-masked cube that survives its cutoff, a masked cube that is cut away
	// entirely, a blended glass quad and a floating cube without CastShadows are lit by
	// a cascaded sun, a spot and a point light (plus unshadowed clustered lights):
	//  - ShadowPlanner places three cascades, one spot tile and six cube faces in one
	//    D32 atlas; ShadowRenderer culls casters per view on the GPU and draws the opaque
	//    and masked variants into each tile;
	//  - the atlas is read back and every interior texel compared with an exact CPU ray
	//    cast of the casters (so masked-away, blended and non-casting objects must be
	//    absent, and the masked cube present);
	//  - the Forward+ image is compared per pixel with ForwardPlus::Shade sampling the
	//    GPU's own atlas through Shadows::ShadowFactor, and shadows must be visible;
	//  - a small atlas with a second point light shows budget and eviction policy
	//    (the evicted light is unshadowed on both sides), with pass timings printed.
	void RunShadowSmoke(const Swim::Rhi::GraphicsSystemDesc& graphicsDesc)
	{
#ifndef SWIM_SHADOW_SMOKE_AVAILABLE
		SWIM_REQUIRE_MESSAGE(false, "Shadow smoke requires generated Slang artifacts");
#else
		using namespace Swim;
		using namespace Swim::Render;
		namespace Fp = Swim::Render::ForwardPlus;
		namespace Fs = Swim::Testing::ForwardScene;
		namespace Pbr = Swim::Render::StandardPbr;
		namespace Scene = Swim::Testing::ClusterScene;
		namespace Sh = Swim::Render::Shadows;
		namespace Ss = Swim::Testing::ShadowScene;

		Platform::PlatformSystem platform;
		SWIM_REQUIRE_MESSAGE(platform.Initialize(), "Shadow smoke requires working SDL Vulkan platform services");
		auto graphics = RhiVulkan::CreateGraphicsSystem(graphicsDesc);
		SWIM_REQUIRE(graphics);
		Testing::RequireVulkanSmokeValidation(*graphics, graphicsDesc.Checks);
		const auto& capabilities = graphics->GetAdapter(0).GetInfo().Capabilities;
		SWIM_REQUIRE_MESSAGE(capabilities.BindlessDescriptors, "Adapter lacks bindless descriptor support");
		auto device = graphics->GetAdapter(0).CreateDevice();
		SWIM_REQUIRE(device);

		const auto cull =
			Smoke::MakeCompute(*device, SWIM_CLUSTER_LIGHT_CULL_SPIRV_PATH, SWIM_CLUSTER_LIGHT_CULL_REFLECTION_PATH, "Cluster cull");
		const auto bounds =
			Smoke::MakeCompute(*device, SWIM_CLUSTER_BOUNDS_SPIRV_PATH, SWIM_CLUSTER_BOUNDS_REFLECTION_PATH, "Cluster bounds");
		const auto assign =
			Smoke::MakeCompute(*device, SWIM_CLUSTER_ASSIGN_SPIRV_PATH, SWIM_CLUSTER_ASSIGN_REFLECTION_PATH, "Cluster assign");
		const auto scan = Smoke::MakeCompute(*device, SWIM_CLUSTER_SCAN_SPIRV_PATH, SWIM_CLUSTER_SCAN_REFLECTION_PATH, "Cluster scan");
		const auto visibilityProgram =
			Smoke::MakeCompute(*device, SWIM_GPU_VISIBILITY_SPIRV_PATH, SWIM_GPU_VISIBILITY_REFLECTION_PATH, "GPU visibility");
		const auto sort = Smoke::MakeCompute(
			*device, SWIM_FORWARD_TRANSPARENT_SORT_SPIRV_PATH, SWIM_FORWARD_TRANSPARENT_SORT_REFLECTION_PATH, "Forward+ sort");
		const auto bindlessSpace = ForwardPlusBindlessSpace(16, 4);
		const auto shadowBindlessSpace = ShadowBindlessSpace(16, 4);
		const auto forwardPipeline = [](ForwardPlusBin bin)
		{
			return [bin](Rhi::ShaderProgram& program, Rhi::PipelineLayout& layout)
			{
				return ForwardPlusRenderer::PipelineDesc(bin, program, layout);
			};
		};
		const auto shadowPipeline = [](Rhi::ShaderProgram& program, Rhi::PipelineLayout& layout)
		{
			return ShadowRenderer::PipelineDesc(program, layout);
		};
		const auto opaque = LoadGraphics(*device, SWIM_FORWARD_OPAQUE_SPIRV_PATH, SWIM_FORWARD_OPAQUE_REFLECTION_PATH, &bindlessSpace,
			"Forward+ opaque", forwardPipeline(ForwardPlusBin::Opaque));
		const auto transparent = LoadGraphics(*device, SWIM_FORWARD_TRANSPARENT_SPIRV_PATH, SWIM_FORWARD_TRANSPARENT_REFLECTION_PATH,
			&bindlessSpace, "Forward+ transparent", forwardPipeline(ForwardPlusBin::Transparent));
		const auto shadowDepth =
			LoadGraphics(*device, SWIM_SHADOW_DEPTH_SPIRV_PATH, SWIM_SHADOW_DEPTH_REFLECTION_PATH, nullptr, "Shadow depth", shadowPipeline);
		const auto shadowMasked = LoadGraphics(*device, SWIM_SHADOW_MASKED_SPIRV_PATH, SWIM_SHADOW_MASKED_REFLECTION_PATH,
			&shadowBindlessSpace, "Shadow masked", shadowPipeline);

		const auto path = SelectVisibilityDrawPath(capabilities);
		ForwardPlusRendererDesc rendererDesc;
		rendererDesc.Opaque = { opaque.Pipeline.get(), opaque.Layout.get() };
		rendererDesc.Transparent = { transparent.Pipeline.get(), transparent.Layout.get() };
		rendererDesc.SortPipeline = sort.Pipeline.get();
		rendererDesc.SortLayout = sort.Layout.get();
		rendererDesc.DrawPath = path;
		const ForwardPlusRenderer renderer(*device, rendererDesc);
		ShadowRendererDesc shadowDesc;
		shadowDesc.Opaque = { shadowDepth.Pipeline.get(), shadowDepth.Layout.get() };
		shadowDesc.Masked = { shadowMasked.Pipeline.get(), shadowMasked.Layout.get() };
		shadowDesc.DrawPath = path;
		const ShadowRenderer shadowRenderer(shadowDesc);
		ClusteredLightAssignerDesc assignerDesc;
		assignerDesc.Cull = ClusterProgramOf(cull);
		assignerDesc.Bounds = ClusterProgramOf(bounds);
		assignerDesc.Assign = ClusterProgramOf(assign);
		assignerDesc.Scan = ClusterProgramOf(scan);
		assignerDesc.DebugName = "Clusters";
		const ClusteredLightAssigner assigner(assignerDesc);
		RenderGraphExecutor executor(*device);

		// Textures: 4x4 uniform texels; 0 is the fallback, 1 opaque white, 2 alpha 0.3.
		const std::array<std::array<std::uint8_t, 4>, 3> texelSpecs{ { { 255, 255, 255, 255 }, { 255, 255, 255, 255 },
			{ 250, 250, 250, 76 } } };
		std::vector<std::unique_ptr<Rhi::Texture>> textures;
		std::vector<std::unique_ptr<Rhi::TextureView>> views;
		for (std::size_t i = 0; i < texelSpecs.size(); ++i)
		{
			Rhi::TextureDesc desc{};
			desc.Extent = { 4, 4, 1 };
			desc.PixelFormat = Rhi::Format::RGBA8UnormSrgb;
			desc.Usage = Rhi::TextureUsage::Sampled | Rhi::TextureUsage::TransferDestination;
			textures.push_back(device->CreateTexture(desc));
			SWIM_REQUIRE(textures.back());
			Rhi::TextureViewDesc view{};
			view.PixelFormat = desc.PixelFormat;
			views.push_back(device->CreateTextureView(*textures.back(), view));
			SWIM_REQUIRE(views.back());
		}
		Rhi::SamplerDesc samplerDesc{};
		auto sampler = device->CreateSampler(samplerDesc);
		SWIM_REQUIRE(sampler);
		BindlessTableDesc bindlessDesc;
		bindlessDesc.Layout = opaque.Layout.get();
		bindlessDesc.Space = ForwardPlusDrawBindings::BindlessSpace;
		bindlessDesc.FallbackTexture = views[0].get();
		bindlessDesc.FallbackSampler = sampler.get();
		BindlessResourceTable bindless(*device, bindlessDesc);
		std::vector<BindlessTextureHandle> textureHandles;
		std::vector<std::uint32_t> textureIndex{ BindlessResourceTable::FallbackIndex };
		for (std::size_t i = 1; i < texelSpecs.size(); ++i)
		{
			textureHandles.push_back(bindless.RegisterTexture(*views[i]));
			textureIndex.push_back(bindless.GetIndex(textureHandles.back()));
		}
		const auto samplerHandle = bindless.RegisterSampler(*sampler);
		const auto samplerIndex = bindless.GetIndex(samplerHandle);
		{
			RenderGraph uploads;
			for (std::size_t i = 0; i < textures.size(); ++i)
			{
				const auto texture = uploads.ImportTexture(*textures[i], Rhi::ResourceState::Undefined);
				std::array<std::uint8_t, 64> texels{};
				for (std::size_t t = 0; t < 16; ++t)
				{
					std::memcpy(texels.data() + t * 4, texelSpecs[i].data(), 4);
				}
				AddTextureUpload(uploads, "Texture upload", std::as_bytes(std::span(texels)), texture, { 0, {}, {}, { 4, 4, 1 } });
				uploads.Export(texture, Rhi::ResourceState::ShaderRead);
			}
			executor.Execute(uploads.Compile());
			executor.Wait();
		}
		const auto texelsOf = [&](const MaterialInstance& instance)
		{
			const auto indices = ReadStandardTextures(instance);
			std::size_t spec = 0;
			for (std::size_t i = 1; i < textureIndex.size(); ++i)
			{
				spec = textureIndex[i] == indices.BaseColor ? i : spec;
			}
			Pbr::Texels texels;
			for (int c = 0; c < 4; ++c)
			{
				const float encoded = float(texelSpecs[spec][c]) / 255.0f;
				texels.BaseColor[c] = c < 3 ? Pbr::SrgbToLinear(encoded) : encoded;
			}
			return texels;
		};

		// Materials.
		const auto materialTemplate = CreateStandardMaterialTemplate();
		GpuMaterialTable materials(*device, { materialTemplate, 16, "Shadow materials" });
		const auto material = [&](std::array<float, 4> baseColor, float roughness, std::uint32_t flags, std::uint32_t texture)
		{
			auto instance = std::make_shared<MaterialInstance>(materialTemplate);
			instance->SetVector("BaseColorFactor", baseColor);
			instance->SetFloat("MetallicFactor", 0.0f);
			instance->SetFloat("RoughnessFactor", roughness);
			instance->SetUint("Flags", flags);
			instance->SetSampler("MaterialSampler", samplerIndex);
			if (texture != 0)
			{
				instance->SetTexture("BaseColorTexture", textureIndex[texture]);
			}
			return instance;
		};
		std::vector<std::shared_ptr<MaterialInstance>> instances{
			material({ 0.6f, 0.6f, 0.55f, 1.0f }, 0.85f, 0, 0),										   // 0 ground.
			material({ 0.7f, 0.3f, 0.2f, 1.0f }, 0.6f, 0, 0),										   // 1 opaque cube.
			material({ 0.2f, 0.6f, 0.3f, 1.0f }, 0.5f, Pbr::FlagAlphaMask, 1),						   // 2 masked, kept (alpha 1).
			material({ 0.9f, 0.9f, 0.9f, 1.0f }, 0.5f, Pbr::FlagAlphaMask, 2),						   // 3 masked away (alpha 0.3).
			material({ 0.2f, 0.4f, 0.9f, 0.5f }, 0.3f, Pbr::FlagAlphaBlend | Pbr::FlagDoubleSided, 0), // 4 glass.
			material({ 0.9f, 0.8f, 0.2f, 1.0f }, 0.5f, 0, 0),										   // 5 floating non-caster.
		};
		std::vector<GpuMaterialHandle> materialHandles;
		for (const auto& instance : instances)
		{
			materialHandles.push_back(materials.Create(instance));
		}

		// Geometry.
		GeometryHeapDesc heapDesc;
		heapDesc.VertexPageSize = 64 * 1024;
		heapDesc.IndexPageSize = 64 * 1024;
		heapDesc.MeshletPageSize = 4096;
		heapDesc.MaxMeshes = 4;
		heapDesc.MaxSubmeshes = 8;
		heapDesc.MaxPages = 8;
		GeometryHeap heap(*device, heapDesc);
		const auto upload = [&](const Fs::Mesh& mesh, const char* name)
		{
			const std::array<GeometrySubmesh, 1> submeshes{ { { 0, std::uint32_t(mesh.Indices.size()), 0, 0 } } };
			const std::array<GeometryLodRange, 1> lods{ { { 0, 1, 0.0f } } };
			GeometryMeshDesc desc;
			desc.VertexLayout = StandardVertexLayoutId();
			desc.VertexStride = StandardVertexStride;
			desc.Vertices = std::as_bytes(std::span(mesh.Vertices));
			desc.IndexFormat = Rhi::IndexType::Uint32;
			desc.Indices = std::as_bytes(std::span(mesh.Indices));
			desc.Submeshes = submeshes;
			desc.Lods = lods;
			desc.DebugName = name;
			return heap.CreateMesh(desc);
		};
		const auto cubeMesh = upload(Fs::MakeCube(), "Cube");
		const auto quadMesh = upload(Fs::MakeQuad(), "Quad");
		const auto& cubeMeta = *heap.GetMetadata(cubeMesh);
		SWIM_REQUIRE(
			cubeMeta.VertexPage == heap.GetMetadata(quadMesh)->VertexPage && cubeMeta.IndexPage == heap.GetMetadata(quadMesh)->IndexPage);
		const ForwardPlusPageSlot pageSlot{ cubeMeta.IndexPage, cubeMeta.VertexPage };

		// Scene.
		constexpr float halfPi = 1.57079633f;

		struct Placement
		{
			Fs::Shape Kind;
			Fs::Float3 Translation;
			float Yaw;
			float Pitch;
			Fs::Float3 Scale;
			std::uint32_t Material;
			bool CastsShadows;
		};

		const std::vector<Placement> placements{
			{ Fs::Shape::Quad, { 0, 0, 0 }, 0.0f, -halfPi, { 12, 12, 1 }, 0, true },				// 0 ground (+Y).
			{ Fs::Shape::Cube, { -2.5f, 1.0f, 0.0f }, 0.5f, 0.0f, { 1, 1, 1 }, 1, true },			// 1 opaque.
			{ Fs::Shape::Cube, { 1.5f, 0.8f, -1.5f }, -0.3f, 0.0f, { 0.8f, 0.8f, 0.8f }, 2, true }, // 2 masked, kept.
			{ Fs::Shape::Cube, { 0.3f, 0.6f, 2.4f }, 0.2f, 0.0f, { 0.6f, 0.6f, 0.6f }, 3, true },	// 3 masked away.
			{ Fs::Shape::Quad, { 3.0f, 1.4f, 2.2f }, 0.4f, 0.0f, { 1.0f, 1.0f, 1 }, 4, true },		// 4 glass (excluded).
			{ Fs::Shape::Cube, { -0.8f, 2.6f, 2.8f }, 0.0f, 0.0f, { 0.4f, 0.4f, 0.4f }, 5, false }, // 5 non-caster.
		};
		std::vector<Fs::Object> objects;
		std::vector<bool> casts; // What the CPU shadow maps contain.
		GpuScene scene(*device, { 16, "Shadow scene" });
		std::vector<RenderObjectHandle> handles;
		for (std::uint32_t i = 0; i < placements.size(); ++i)
		{
			const auto& p = placements[i];
			objects.push_back({ p.Kind, Fs::MakeTransform(p.Translation, p.Yaw, p.Pitch, p.Scale) });
			const auto parameters = ReadStandardParameters(*instances[p.Material]);
			// Uniform textures: a masked material is kept or cut away everywhere.
			const bool maskedAway = (parameters.Flags & Pbr::FlagAlphaMask) != 0 &&
				texelsOf(*instances[p.Material]).BaseColor[3] * parameters.BaseColorFactor[3] < parameters.AlphaCutoff;
			casts.push_back(p.CastsShadows && ShadowRenderer::MaterialBin(parameters) != ShadowBin::Excluded && !maskedAway);
			RenderObjectDesc desc;
			std::copy(
				std::begin(objects.back().Transform.Current), std::end(objects.back().Transform.Current), desc.Transform.Rows.begin());
			desc.Mesh = p.Kind == Fs::Shape::Cube ? cubeMesh : quadMesh;
			desc.LocalBounds = p.Kind == Fs::Shape::Cube ? RenderBounds::FromMinMax({ -1, -1, -1 }, { 1, 1, 1 })
														 : RenderBounds::FromMinMax({ -1, -1, 0 }, { 1, 1, 0 });
			desc.MaterialSet = materials.GetIndex(materialHandles[p.Material]);
			desc.ObjectId = i;
			desc.Flags = p.CastsShadows ? RenderObjectFlags::Default : RenderObjectFlags::Visible;
			handles.push_back(scene.Create(desc));
		}

		const auto makeVisibility = [&](std::vector<std::uint32_t> capacities, const char* name)
		{
			GpuVisibilityDesc desc;
			desc.CullPipeline = visibilityProgram.Pipeline.get();
			desc.Layout = visibilityProgram.Layout.get();
			desc.Space = visibilityProgram.Space;
			desc.MaxObjects = 16;
			desc.MaxMaterialSets = 16;
			desc.MaterialBinCapacities = std::move(capacities);
			desc.DebugName = name;
			return std::make_unique<GpuVisibility>(*device, std::move(desc));
		};
		auto visibility = makeVisibility(ForwardPlusRenderer::VisibilityBinCapacities(32, 16), "Main visibility");
		auto shadowVisibility = makeVisibility(ShadowRenderer::VisibilityBinCapacities(32, 16), "Shadow visibility");
		for (std::size_t i = 0; i < materialHandles.size(); ++i)
		{
			const auto set = materials.GetIndex(materialHandles[i]);
			const auto parameters = ReadStandardParameters(*instances[i]);
			ForwardPlusRenderer::RouteMaterial(*visibility, set, parameters);
			ShadowRenderer::RouteMaterial(*shadowVisibility, set, parameters);
		}

		// Lights: three shadowed lights (slots 0-2) and unshadowed clustered fill lights.
		GpuLightBuffer lights(*device, { 2, 1024, "Shadow lights" });
		std::vector<LightDesc> shadowed(3);
		shadowed[0].Type = LightType::Directional;
		shadowed[0].Direction = { 0.45f, -1.0f, 0.25f };
		shadowed[0].Intensity = 2.5f;
		shadowed[0].Color = { 1.0f, 0.95f, 0.85f };
		shadowed[1].Type = LightType::Spot;
		shadowed[1].Position = { -1.0f, 6.5f, 2.0f };
		shadowed[1].Direction = { -0.2f, -1.0f, -0.25f };
		shadowed[1].Intensity = 60.0f;
		shadowed[1].Range = 20.0f;
		shadowed[1].InnerConeAngle = 0.45f;
		shadowed[1].OuterConeAngle = 0.7f;
		shadowed[1].Color = { 0.9f, 0.9f, 1.0f };
		shadowed[2].Type = LightType::Point;
		shadowed[2].Position = { 2.5f, 3.0f, 0.5f };
		shadowed[2].Intensity = 25.0f;
		shadowed[2].Range = 15.0f;
		shadowed[2].Color = { 1.0f, 0.8f, 0.6f };
		for (std::uint32_t slot = 0; slot < shadowed.size(); ++slot)
		{
			shadowed[slot].ShadowIndex = slot;
			shadowed[slot].Flags = LightFlags::CastsShadows;
			lights.Create(shadowed[slot]);
		}
		std::mt19937 random(700);
		std::uniform_real_distribution<float> unit(0.0f, 1.0f);
		for (int i = 0; i < 40; ++i)
		{
			LightDesc fill;
			fill.Type = LightType::Point;
			fill.Position = { 14 * unit(random) - 7, 0.3f + 3.0f * unit(random), 12 * unit(random) - 6 };
			fill.Color = { 0.5f + 0.5f * unit(random), 0.5f + 0.5f * unit(random), 0.5f + 0.5f * unit(random) };
			fill.Intensity = 0.5f + 1.5f * unit(random);
			fill.Range = 1.5f + 2.0f * unit(random);
			lights.Create(fill);
		}

		constexpr std::uint32_t width = 480;
		constexpr std::uint32_t height = 270;
		ClusterGridDesc gridDesc;
		gridDesc.ViewportWidth = width;
		gridDesc.ViewportHeight = height;
		gridDesc.TileSize = 32;
		gridDesc.SliceCount = 16;
		gridDesc.Near = 0.1f;
		gridDesc.Far = 60.0f;
		gridDesc.MaxLightsPerCluster = 256;
		gridDesc.LightCapacity = 1024;

		const Fs::Float3 eye{ 1.0f, 5.5f, 11.0f };
		const Fs::Float3 target{ 0.0f, 0.8f, 0.0f };
		const float verticalFov = 0.9f;
		const float aspect = float(width) / float(height);

		struct FrameSpec
		{
			const char* Name;
			ShadowSettings Settings;
			std::vector<ShadowCasterDesc> Casters;
		};

		struct FrameResult
		{
			ShadowPlan Plan;
			std::uint32_t Shadowed = 0; // Pixels a shadow visibly darkens.
		};

		const auto frame = [&](const FrameSpec& spec)
		{
			ClusterView clusterView;
			clusterView.View = Scene::LookAt(eye, target, { 0, 1, 0 });
			clusterView.Projection = PerspectiveReverseZRowMajor(verticalFov, aspect, 0.1f);
			const auto viewProjection = MultiplyRowMajor(clusterView.Projection, clusterView.View);
			Sh::ShadowCamera camera;
			camera.View = clusterView.View;
			camera.VerticalFov = verticalFov;
			camera.Aspect = aspect;
			camera.Near = 0.1f;
			ShadowAtlasAllocator atlasAllocator(spec.Settings.AtlasSize, spec.Settings.MinTile);
			FrameResult result;
			result.Plan = PlanShadows(spec.Settings, camera, spec.Casters, atlasAllocator);
			const auto& plan = result.Plan;

			RenderGraph graph;
			const auto materialResources = materials.Import(graph);
			const auto sceneResources = scene.Import(graph);
			const auto geometry = heap.Import(graph);
			const auto lightResources = lights.Import(graph);

			ShadowFrame shadowFrame;
			shadowFrame.Scene = &sceneResources;
			shadowFrame.Geometry = &geometry;
			shadowFrame.Visibility = shadowVisibility.get();
			shadowFrame.PageSlots = { { pageSlot.IndexPage, pageSlot.VertexPage } };
			shadowFrame.Materials = &materialResources;
			shadowFrame.Bindless = &bindless.GetTable();
			shadowFrame.Plan = &plan;
			shadowFrame.ZeroUnusedCommands = NeedsZeroedCommands(path);
			const auto shadows = shadowRenderer.Record(graph, shadowFrame);

			RenderViewDesc viewDesc;
			viewDesc.ViewProjection = viewProjection;
			viewDesc.CameraPosition = eye;
			VisibilityFrameDesc visibilityFrame;
			visibilityFrame.View = BuildGpuViewRecord(viewDesc);
			visibilityFrame.IndexPages = { pageSlot.IndexPage };
			visibilityFrame.ZeroUnusedCommands = NeedsZeroedCommands(path);
			const auto visible = visibility->Record(graph, sceneResources, geometry, visibilityFrame);
			const auto clusters = assigner.Record(graph, lightResources, gridDesc, clusterView);

			const auto targetTexture = [&](Rhi::Format format, Rhi::TextureUsage usage, const char* name)
			{
				Rhi::TextureDesc desc;
				desc.Extent = { width, height, 1 };
				desc.PixelFormat = format;
				desc.Usage = usage;
				desc.DebugName = name;
				return graph.CreateTexture(desc);
			};
			ForwardPlusTargets targets;
			targets.Color = targetTexture(
				ForwardPlusRenderer::ColorFormat, Rhi::TextureUsage::ColorAttachment | Rhi::TextureUsage::TransferSource, "Shadowed color");
			targets.ObjectId = targetTexture(ForwardPlusRenderer::ObjectIdFormat,
				Rhi::TextureUsage::ColorAttachment | Rhi::TextureUsage::TransferSource, "Shadowed object id");
			targets.Depth = targetTexture(CanonicalDepthFormat, Rhi::TextureUsage::DepthStencilAttachment, "Shadowed depth");
			ForwardPlusFrame forwardFrame;
			forwardFrame.Scene = &sceneResources;
			forwardFrame.Geometry = &geometry;
			forwardFrame.Visibility = &visible;
			forwardFrame.PageSlots = { pageSlot };
			forwardFrame.Materials = &materialResources;
			forwardFrame.Bindless = &bindless.GetTable();
			forwardFrame.Lights = &lightResources;
			forwardFrame.Clusters = &clusters;
			forwardFrame.Shadows = &shadows;
			forwardFrame.View.ViewProjection = viewProjection;
			forwardFrame.View.CameraPosition = eye;
			forwardFrame.View.CameraForward = { target[0] - eye[0], target[1] - eye[1], target[2] - eye[2] };
			forwardFrame.View.Ambient = { 0.03f, 0.03f, 0.04f };
			const auto forward = renderer.Record(graph, forwardFrame, targets);
			SWIM_CHECK((forward.ViewRecord.Flags & ForwardViewFlagShadows) != 0u);
			SWIM_CHECK(!forward.ShadowFallback);

			const std::uint32_t atlasSize = plan.AtlasSize;
			const std::uint32_t clusterCount = clusters.Layout.ClusterCount;
			const auto atlasReadback = AddTextureReadback(graph, "Shadow atlas", shadows.Atlas, { 0, {}, {}, { atlasSize, atlasSize, 1 } });
			const auto colorReadback = AddTextureReadback(graph, "Color", targets.Color, { 0, {}, {}, { width, height, 1 } });
			const auto idReadback = AddTextureReadback(graph, "Object id", targets.ObjectId, { 0, {}, {}, { width, height, 1 } });
			const auto recordsReadback = AddBufferReadback(graph, "Cluster records", clusters.Records, 0, std::uint64_t(clusterCount) * 16);
			const auto indicesReadback = AddBufferReadback(
				graph, "Cluster indices", clusters.Indices, 0, std::uint64_t(clusterCount) * ClusterBlockWords(clusters.GridRecord) * 4);
			const auto completion = executor.Execute(graph.Compile());
			scene.CommitUploads();
			heap.CommitUploads(completion);
			materials.CommitUploads();
			lights.CommitUploads();
			const auto timings = executor.ReadTimings();

			const auto read = [&](const GraphReadback& readback, auto& destination)
			{
				SWIM_REQUIRE(
					executor.TryReadback(readback.Buffer, std::as_writable_bytes(std::span(destination))) == Rhi::ReadbackStatus::Ready);
			};
			Sh::ShadowAtlasImage gpuAtlas;
			gpuAtlas.Size = atlasSize;
			gpuAtlas.Depth.resize(std::size_t(atlasSize) * atlasSize);
			std::vector<std::uint16_t> colorHalves(std::size_t(width) * height * 4);
			std::vector<float> ids(std::size_t(width) * height);
			std::vector<ClusterRecord> clusterRecords(clusterCount);
			std::vector<std::uint32_t> clusterIndices(std::size_t(clusterCount) * ClusterBlockWords(clusters.GridRecord));
			read(atlasReadback, gpuAtlas.Depth);
			read(colorReadback, colorHalves);
			read(idReadback, ids);
			read(recordsReadback, clusterRecords);
			read(indicesReadback, clusterIndices);

			// 1. The atlas against CPU shadow maps of the casters.
			Sh::ShadowAtlasImage cpuAtlas;
			cpuAtlas.Size = atlasSize;
			cpuAtlas.Depth.assign(gpuAtlas.Depth.size(), 0.0f);
			std::vector<std::uint32_t> signature(gpuAtlas.Depth.size(), 0);
			for (const auto& view : plan.Views)
			{
				Ss::RenderView(objects, casts, view, cpuAtlas, &signature);
			}
			std::uint32_t texelInterior = 0, texelMismatch = 0, texelCovered = 0;
			std::array<std::uint32_t, 8> objectTexels{};
			float worstDepth = 0.0f;
			for (const auto& draw : plan.Draws)
			{
				const auto& tile = draw.Tile;
				for (std::uint32_t y = tile.Y + 1; y + 1 < tile.Y + tile.Size; ++y)
				{
					for (std::uint32_t x = tile.X + 1; x + 1 < tile.X + tile.Size; ++x)
					{
						const std::size_t index = std::size_t(y) * atlasSize + x;
						bool interior = true;
						for (int dy = -1; dy <= 1 && interior; ++dy)
						{
							for (int dx = -1; dx <= 1 && interior; ++dx)
							{
								interior = signature[std::size_t(int(y) + dy) * atlasSize + std::size_t(int(x) + dx)] == signature[index];
							}
						}
						if (!interior)
						{
							continue;
						}
						++texelInterior;
						const float expected = cpuAtlas.Depth[index];
						const float actual = gpuAtlas.Depth[index];
						const float error = std::abs(actual - expected);
						worstDepth = std::max(worstDepth, error / std::max(expected, 1.0e-3f));
						texelMismatch += error > 1.0e-5f + 1.0e-4f * expected ? 1u : 0u;
						if (signature[index] != 0)
						{
							++texelCovered;
							++objectTexels[std::min<std::size_t>((signature[index] - 1) / 8, 7)];
						}
					}
				}
			}
			std::printf(
				"             [shadows %s] %zu views, atlas %u: %u interior texels (%u covered), %u mismatches, worst relative %.2e; "
				"texels per object %u %u %u %u %u %u\n",
				spec.Name, plan.Views.size(), atlasSize, texelInterior, texelCovered, texelMismatch, double(worstDepth), objectTexels[0],
				objectTexels[1], objectTexels[2], objectTexels[3], objectTexels[4], objectTexels[5]);
			SWIM_CHECK(texelInterior > 0u);
			SWIM_CHECK(texelMismatch <= texelInterior / 1000);
			SWIM_CHECK(objectTexels[1] > 100u && objectTexels[2] > 100u); // Opaque and masked-kept casters.
			SWIM_CHECK(objectTexels[3] == 0u && objectTexels[4] == 0u && objectTexels[5] == 0u);

			// 2. The image against ForwardPlus::Shade over the GPU atlas.
			const auto& grid = clusters.GridRecord;
			const Sh::ShadowSampleInputs shadowInputs{ &gpuAtlas, plan.Records, plan.Views };
			const Fp::LightingInputs inputs{ lights.GetRecords(), lights.GetHeader(), &grid, clusterRecords, clusterIndices, nullptr,
				&shadowInputs };
			const Fp::LightingInputs unshadowedInputs{ lights.GetRecords(), lights.GetHeader(), &grid, clusterRecords, clusterIndices,
				nullptr, nullptr };

			struct PixelTruth
			{
				std::uint32_t Signature = 0;
				std::uint32_t Id = 0;
				std::optional<Fs::Hit> Opaque;
				std::vector<Fs::Hit> Layers;
			};

			std::vector<PixelTruth> truth(std::size_t(width) * height);
			for (std::uint32_t y = 0; y < height; ++y)
			{
				for (std::uint32_t x = 0; x < width; ++x)
				{
					const auto through = Scene::ViewToWorld(grid, Scene::ViewPoint(grid, float(x) + 0.5f, float(y) + 0.5f, 1.0f));
					const Fs::Float3 direction{ through[0] - eye[0], through[1] - eye[1], through[2] - eye[2] };
					auto& pixel = truth[std::size_t(y) * width + x];
					for (const auto& hit : Fs::CastRay(objects, eye, direction))
					{
						const auto& instance = *instances[placements[hit.Object].Material];
						const auto parameters = ReadStandardParameters(instance);
						const auto axes = Fp::BuildFrame(hit.Normal, hit.Tangent, hit.FrontFacing != hit.Mirrored, hit.Mirrored);
						if (Fp::CullsFace(parameters, axes.FrontFacing) || !Pbr::Resolve(parameters, texelsOf(instance), axes))
						{
							continue;
						}
						if (Fp::MaterialBin(parameters) == ForwardPlusBin::Transparent)
						{
							pixel.Layers.push_back(hit);
							pixel.Signature |= 1u << (16 + hit.Object);
							continue;
						}
						pixel.Opaque = hit;
						pixel.Id = hit.Object + 1;
						pixel.Signature |= (hit.Object + 1) * 8 + hit.Face;
						break;
					}
					std::reverse(pixel.Layers.begin(), pixel.Layers.end()); // Back to front (one glass quad here).
				}
			}
			const auto shade = [&](const Fp::LightingInputs& lighting, const Fs::Hit& hit, float px, float py)
			{
				const auto& instance = *instances[placements[hit.Object].Material];
				const auto parameters = ReadStandardParameters(instance);
				const auto axes = Fp::BuildFrame(hit.Normal, hit.Tangent, hit.FrontFacing != hit.Mirrored, hit.Mirrored);
				const auto surface = *Pbr::Resolve(parameters, texelsOf(instance), axes);
				return Fp::Shade(lighting, forward.ViewRecord, surface, hit.Position, px, py);
			};

			std::uint32_t interior = 0, idMismatch = 0, compared = 0, outliers = 0;
			double errorSum = 0.0;
			float worst = 0.0f;
			for (std::uint32_t y = 1; y + 1 < height; ++y)
			{
				for (std::uint32_t x = 1; x + 1 < width; ++x)
				{
					const std::size_t index = std::size_t(y) * width + x;
					const auto& pixel = truth[index];
					bool same = true;
					for (int dy = -1; dy <= 1 && same; ++dy)
					{
						for (int dx = -1; dx <= 1 && same; ++dx)
						{
							same = truth[std::size_t(int(y) + dy) * width + std::size_t(int(x) + dx)].Signature == pixel.Signature;
						}
					}
					if (!same)
					{
						continue;
					}
					++interior;
					idMismatch += static_cast<std::uint32_t>(ids[index]) != pixel.Id ? 1u : 0u;
					const float px = float(x) + 0.5f;
					const float py = float(y) + 0.5f;
					Fp::Float4 expected{ 0, 0, 0, 0 };
					if (pixel.Opaque)
					{
						const auto color = shade(inputs, *pixel.Opaque, px, py);
						expected = { color[0], color[1], color[2], 1.0f };
						const auto lit = shade(unshadowedInputs, *pixel.Opaque, px, py);
						float gap = 0.0f;
						for (int c = 0; c < 3; ++c)
						{
							gap = std::max(gap, lit[c] - color[c]);
						}
						result.Shadowed += gap > 0.05f ? 1u : 0u;
					}
					for (const auto& layer : pixel.Layers)
					{
						expected = Fp::Over(shade(inputs, layer, px, py), expected);
					}
					float pixelError = 0.0f;
					bool outlier = false;
					for (int c = 0; c < 4; ++c)
					{
						const float actual = Smoke::HalfToFloat(colorHalves[index * 4 + c]);
						pixelError = std::max(pixelError, RelativeError(actual, expected[c]));
						outlier = outlier || std::abs(actual - expected[c]) > 0.01f + 0.03f * std::abs(expected[c]);
					}
					++compared;
					outliers += outlier ? 1u : 0u;
					errorSum += pixelError;
					worst = std::max(worst, pixelError);
				}
			}
			const double mean = compared ? errorSum / compared : 0.0;
			const auto& stats = plan.Stats;
			std::printf("             [shadows %s] %u interior pixels, %u id mismatches; %u compared, %u outliers, mean %.2e, worst %.2e; "
						"%u visibly shadowed; plan: %u casters, %u placed, %u over budget, %u evicted, %u downgraded\n",
				spec.Name, interior, idMismatch, compared, outliers, mean, double(worst), result.Shadowed, stats.Casters, stats.Placed,
				stats.OverBudget, stats.Evicted, stats.Downgraded);
			SWIM_CHECK(interior > width * height / 2);
			SWIM_CHECK(idMismatch <= interior / 500);
			SWIM_CHECK(outliers <= compared / 100);
			SWIM_CHECK(mean < 5.0e-3);

			bool measured = false;
			const double depthMs = PassMilliseconds(timings, "Shadows depth", &measured);
			if (measured)
			{
				std::printf("             [shadows %s] GPU: caster culls %.3f ms, depth %.3f ms (%zu views), forward opaque %.3f ms\n",
					spec.Name, PassMilliseconds(timings, "Shadow visibility"), depthMs, plan.Views.size(),
					PassMilliseconds(timings, "Forward+ opaque"));
			}
			return result;
		};

		const auto casterOf = [&](std::uint32_t slot, float priority)
		{
			return ShadowCasterDesc{ slot, Lights::EncodeLight(shadowed[slot]), priority, 0 };
		};

		// Frame 1: every shadow placed.
		ShadowSettings settings;
		settings.AtlasSize = 2048;
		settings.MinTile = 64;
		settings.CascadeResolution = 512;
		settings.SpotResolution = 512;
		settings.PointResolution = 256;
		settings.Cascades.MaxDistance = 30.0f;
		const auto full = frame({ "full", settings, { casterOf(0, 10.0f), casterOf(1, 5.0f), casterOf(2, 3.0f) } });
		SWIM_CHECK_EQUAL(full.Plan.Views.size(), std::size_t(3 + 1 + 6));
		SWIM_CHECK_EQUAL(full.Plan.Stats.Evicted + full.Plan.Stats.OverBudget, 0u);
		SWIM_CHECK(full.Shadowed > 1000u);

		// Frame 2: a small atlas and a second point light. Three 512 cascades and the
		// 512 spot fill a 1024 atlas: the first point light is evicted, the second is
		// over the one-point budget; both are unshadowed on the GPU and the CPU alike.
		shadowed.push_back(shadowed[2]);
		shadowed[3].Position = { -3.5f, 2.5f, 3.0f };
		shadowed[3].ShadowIndex = 3;
		lights.Create(shadowed[3]);
		auto tight = settings;
		tight.AtlasSize = 1024;
		tight.MinTile = 256;
		tight.MaxPointShadows = 1;
		const auto evicted = frame({ "evicted", tight, { casterOf(0, 10.0f), casterOf(1, 5.0f), casterOf(2, 3.0f), casterOf(3, 1.0f) } });
		SWIM_CHECK_EQUAL(evicted.Plan.Stats.OverBudget, 1u);
		SWIM_CHECK_EQUAL(evicted.Plan.Stats.Evicted, 1u);
		SWIM_CHECK_EQUAL(evicted.Plan.Records[2].Kind, std::uint32_t(ShadowKind::None));
		SWIM_CHECK_EQUAL(evicted.Plan.Records[3].Kind, std::uint32_t(ShadowKind::None));
		SWIM_CHECK_EQUAL(evicted.Plan.Views.size(), std::size_t(4));
		SWIM_CHECK(evicted.Shadowed > 500u);

		scene.Collect();
		scene.Drain();
		heap.Drain();
		materials.Drain();
		bindless.Drain();
		executor.Trim();
#endif
	}

	[[maybe_unused]] const bool registered = []
	{
		const char* enabled = std::getenv("SWIM_RUN_RHI_SMOKE");
		if (enabled != nullptr && std::string_view(enabled) == "1")
		{
			Swim::Testing::TestRegistry::Get().Add({ "RHI.Vulkan.Smoke", "ShadowedForwardPlusMatchesTheCpuReference", SWIM_TEST_LOCATION,
				+[]
				{
					Swim::Testing::RunValidatedVulkanSmoke(&RunShadowSmoke);
				} });
		}
		return true;
	}();
} // namespace
