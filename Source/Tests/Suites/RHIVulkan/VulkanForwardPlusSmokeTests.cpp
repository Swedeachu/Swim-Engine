#include "Engine/Platform/PlatformSystem.h"
#include "Engine/Systems/Renderer/ClusteredLighting/ClusterReference.h"
#include "Engine/Systems/Renderer/ClusteredLighting/ClusteredLightAssigner.h"
#include "Engine/Systems/Renderer/ForwardPlus/ForwardPlusRenderer.h"
#include "Engine/Systems/Renderer/Geometry/GeometryHeap.h"
#include "Engine/Systems/Renderer/GpuMaterials/GpuMaterialTable.h"
#include "Engine/Systems/Renderer/GpuScene/GpuScene.h"
#include "Engine/Systems/Renderer/Lights/GpuLightBuffer.h"
#include "Engine/Systems/Renderer/Materials/StandardMaterial.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanRhiBackend.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderCommandContext.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphExecutor.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphTransfers.h"
#include "Engine/Systems/Renderer/Resources/BindlessResourceTable.h"
#include "Engine/Systems/Renderer/Visibility/GpuVisibility.h"
#include "Engine/Systems/Renderer/Visibility/RenderViewDesc.h"
#include "Tests/Fixtures/ClusterFixture.h"
#include "Tests/Fixtures/ForwardPlusFixture.h"
#include "Tests/Fixtures/VulkanSmokeDiagnostics.h"
#include "Tests/Framework/Test.h"

#if defined(SWIM_FORWARD_OPAQUE_SPIRV_PATH) && defined(SWIM_FORWARD_TRANSPARENT_SPIRV_PATH) &&                                             \
	defined(SWIM_FORWARD_TRANSPARENT_SORT_SPIRV_PATH) && defined(SWIM_GPU_VISIBILITY_SPIRV_PATH) &&                                        \
	defined(SWIM_CLUSTER_LIGHT_CULL_SPIRV_PATH) && defined(SWIM_CLUSTER_BOUNDS_SPIRV_PATH) && defined(SWIM_CLUSTER_ASSIGN_SPIRV_PATH) &&   \
	defined(SWIM_CLUSTER_SCAN_SPIRV_PATH) && defined(SWIM_ENVIRONMENT_SKY_SPIRV_PATH) &&                                                   \
	defined(SWIM_ENVIRONMENT_DOWNSAMPLE_SPIRV_PATH) && defined(SWIM_ENVIRONMENT_PREFILTER_SPIRV_PATH) &&                                   \
	defined(SWIM_ENVIRONMENT_IRRADIANCE_SPIRV_PATH) && defined(SWIM_ENVIRONMENT_BRDF_LUT_SPIRV_PATH)
#include "Tests/Fixtures/VulkanEnvironmentFixture.h"
#define SWIM_FORWARD_PLUS_SMOKE_AVAILABLE 1
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace
{
#ifdef SWIM_FORWARD_PLUS_SMOKE_AVAILABLE
	namespace Smoke = Swim::Testing::EnvironmentSmoke;

	// One Forward+ variant: its program, the layout with the shared bindless space and
	// a pipeline from ForwardPlusRenderer::PipelineDesc.
	struct DrawProgram
	{
		std::unique_ptr<Swim::Rhi::ShaderProgram> Program;
		std::unique_ptr<Swim::Rhi::PipelineLayout> Layout;
		std::unique_ptr<Swim::Rhi::GraphicsPipeline> Pipeline;
	};

	DrawProgram LoadDraw(Swim::Rhi::Device& device, const char* spirvPath, const char* reflectionPath, Swim::Render::ForwardPlusBin bin,
		const Swim::Rhi::DescriptorSchemaDesc& bindlessSpace, const char* label)
	{
		using namespace Swim;
		const auto reflected = Smoke::ReflectProgram(reflectionPath);
		const auto bytes = Smoke::ReadSpirv(spirvPath);
		const auto& draw = reflected.Interface;
		const std::array<Rhi::ShaderStageArtifact, 2> stages{ { { Rhi::ShaderStageMask::Vertex, "vertexMain", bytes },
			{ Rhi::ShaderStageMask::Fragment, "fragmentMain", bytes } } };
		DrawProgram program;
		program.Program = device.CreateShaderProgram({ stages, { draw.DescriptorSchemas, draw.PushConstants }, label });
		SWIM_REQUIRE(program.Program);
		program.Layout = device.CreatePipelineLayout({ program.Program.get(), label, { &bindlessSpace, 1 } });
		SWIM_REQUIRE(program.Layout);
		program.Pipeline = device.CreateGraphicsPipeline(Render::ForwardPlusRenderer::PipelineDesc(bin, *program.Program, *program.Layout));
		SWIM_REQUIRE_MESSAGE(program.Pipeline, std::string(label) + " pipeline");
		return program;
	}

	Swim::Render::ClusterProgram ClusterProgramOf(const Smoke::ComputeProgram& program)
	{
		return { program.Pipeline.get(), program.Layout.get(), program.Space };
	}

	Swim::Render::ClusteredLightAssignerDesc AssignerDesc(const Smoke::ComputeProgram& cull, const Smoke::ComputeProgram& bounds,
		const Smoke::ComputeProgram& assign, const Smoke::ComputeProgram& scan, const char* name)
	{
		Swim::Render::ClusteredLightAssignerDesc desc;
		desc.Cull = ClusterProgramOf(cull);
		desc.Bounds = ClusterProgramOf(bounds);
		desc.Assign = ClusterProgramOf(assign);
		desc.Scan = ClusterProgramOf(scan);
		desc.DebugName = name;
		return desc;
	}

	// GPU time of the passes whose names start with `prefix`. Each pass is charged the
	// interval between the previous pass's end timestamp and its own (end timestamps are
	// written after completion and are monotonic in schedule order; begin timestamps are
	// top-of-pipe and would overlap earlier passes).
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

	// Critical-path items 66 and 67 on a real device. A GPU Scene of StandardVertex
	// cubes and quads (a ground plane; gold, normal-mapped, mirrored, alpha-masked and
	// occluded/emissive cubes; five alpha-blended quads, one behind a cube and one
	// single-sided facing away) is culled and binned by GpuVisibility, lit by 1 + 300
	// clustered lights and the GPU-built environment, and drawn by ForwardPlusRenderer.
	// Every pixel is compared with an exact CPU ray cast of the same shapes shaded by
	// ForwardPlus::Shade over the GPU's own cluster lists and environment maps:
	//  - the object-id target against the ray-cast object;
	//  - opaque color, then the transparent layers composited back to front with
	//    ForwardPlus::Over in the order the GPU sort produced, which must equal
	//    ForwardPlus::SortTransparentDraws (and whose reverse must visibly differ);
	//  - the cluster-heatmap debug view against ForwardPlus::DebugColor;
	//  - a moved camera and reordered quads without an environment (stand-ins bound);
	//  - 10,000 lights, sampled, with the pass timings printed.
	void RunForwardPlusSmoke(const Swim::Rhi::GraphicsSystemDesc& graphicsDesc)
	{
#ifndef SWIM_FORWARD_PLUS_SMOKE_AVAILABLE
		SWIM_REQUIRE_MESSAGE(false, "Forward+ smoke requires generated Slang artifacts");
#else
		using namespace Swim;
		using namespace Swim::Render;
		namespace Fp = Swim::Render::ForwardPlus;
		namespace Fs = Swim::Testing::ForwardScene;
		namespace Pbr = Swim::Render::StandardPbr;
		namespace Env = Swim::Render::Environment;
		namespace Scene = Swim::Testing::ClusterScene;

		Platform::PlatformSystem platform;
		SWIM_REQUIRE_MESSAGE(platform.Initialize(), "Forward+ smoke requires working SDL Vulkan platform services");
		auto graphics = RhiVulkan::CreateGraphicsSystem(graphicsDesc);
		SWIM_REQUIRE(graphics);
		Testing::RequireVulkanSmokeValidation(*graphics, graphicsDesc.Checks);
		const auto& capabilities = graphics->GetAdapter(0).GetInfo().Capabilities;
		SWIM_REQUIRE_MESSAGE(capabilities.BindlessDescriptors, "Adapter lacks bindless descriptor support");
		auto device = graphics->GetAdapter(0).CreateDevice();
		SWIM_REQUIRE(device);

		Smoke::EnvironmentPrograms environmentPrograms(*device);
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
		const auto bindlessSpace = ForwardPlusBindlessSpace(32, 8);
		const auto opaque = LoadDraw(*device, SWIM_FORWARD_OPAQUE_SPIRV_PATH, SWIM_FORWARD_OPAQUE_REFLECTION_PATH, ForwardPlusBin::Opaque,
			bindlessSpace, "Forward+ opaque");
		const auto transparent = LoadDraw(*device, SWIM_FORWARD_TRANSPARENT_SPIRV_PATH, SWIM_FORWARD_TRANSPARENT_REFLECTION_PATH,
			ForwardPlusBin::Transparent, bindlessSpace, "Forward+ transparent");
		const auto path = SelectVisibilityDrawPath(capabilities);
		ForwardPlusRendererDesc rendererDesc;
		rendererDesc.Opaque = { opaque.Pipeline.get(), opaque.Layout.get() };
		rendererDesc.Transparent = { transparent.Pipeline.get(), transparent.Layout.get() };
		rendererDesc.SortPipeline = sort.Pipeline.get();
		rendererDesc.SortLayout = sort.Layout.get();
		rendererDesc.DrawPath = path;
		rendererDesc.DebugName = "Forward+";
		const ForwardPlusRenderer renderer(*device, rendererDesc);
		const ClusteredLightAssigner assigner(AssignerDesc(cull, bounds, assign, scan, "Clusters"));
		RenderGraphExecutor executor(*device);

		// Textures: 4x4 uniform texels so filtering never changes a sample.
		struct TextureSpec
		{
			Rhi::Format Format;
			std::array<std::uint8_t, 4> Texel;
		};

		const std::vector<TextureSpec> specs{
			{ Rhi::Format::RGBA8Unorm, { 255, 255, 255, 255 } },	// 0: fallback.
			{ Rhi::Format::RGBA8UnormSrgb, { 230, 180, 90, 255 } }, // 1: gold base color.
			{ Rhi::Format::RGBA8Unorm, { 0, 90, 255, 255 } },		// 2: metallic-roughness (G 0.35, B 1).
			{ Rhi::Format::RGBA8Unorm, { 166, 128, 230, 255 } },	// 3: tangent-space normal tilted toward +T.
			{ Rhi::Format::RGBA8Unorm, { 100, 0, 0, 255 } },		// 4: occlusion.
			{ Rhi::Format::RGBA8UnormSrgb, { 255, 140, 40, 255 } }, // 5: emissive.
			{ Rhi::Format::RGBA8UnormSrgb, { 250, 250, 250, 76 } }, // 6: alpha 0.3 (masked away).
		};
		std::vector<std::unique_ptr<Rhi::Texture>> textures;
		std::vector<std::unique_ptr<Rhi::TextureView>> views;
		for (const auto& spec : specs)
		{
			Rhi::TextureDesc desc{};
			desc.Extent = { 4, 4, 1 };
			desc.PixelFormat = spec.Format;
			desc.Usage = Rhi::TextureUsage::Sampled | Rhi::TextureUsage::TransferDestination;
			textures.push_back(device->CreateTexture(desc));
			SWIM_REQUIRE(textures.back());
			Rhi::TextureViewDesc view{};
			view.PixelFormat = spec.Format;
			views.push_back(device->CreateTextureView(*textures.back(), view));
			SWIM_REQUIRE(views.back());
		}
		Rhi::SamplerDesc samplerDesc{};
		samplerDesc.AddressU = samplerDesc.AddressV = Rhi::SamplerAddressMode::Repeat;
		auto sampler = device->CreateSampler(samplerDesc);
		SWIM_REQUIRE(sampler);
		BindlessTableDesc bindlessDesc;
		bindlessDesc.Layout = opaque.Layout.get();
		bindlessDesc.Space = ForwardPlusDrawBindings::BindlessSpace;
		bindlessDesc.FallbackTexture = views[0].get();
		bindlessDesc.FallbackSampler = sampler.get();
		BindlessResourceTable bindless(*device, bindlessDesc);
		std::vector<std::uint32_t> textureIndex(specs.size(), BindlessResourceTable::FallbackIndex);
		std::vector<BindlessTextureHandle> textureHandles;
		for (std::size_t i = 1; i < specs.size(); ++i)
		{
			textureHandles.push_back(bindless.RegisterTexture(*views[i]));
			textureIndex[i] = bindless.GetIndex(textureHandles.back());
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
					std::memcpy(texels.data() + t * 4, specs[i].Texel.data(), 4);
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
			const auto decode = [&](std::uint32_t bindlessIndex, int channel)
			{
				const TextureSpec* spec = &specs[0];
				for (std::size_t i = 1; i < specs.size(); ++i)
				{
					spec = textureIndex[i] == bindlessIndex ? &specs[i] : spec;
				}
				const float encoded = float(spec->Texel[channel]) / 255.0f;
				return spec->Format == Rhi::Format::RGBA8UnormSrgb && channel < 3 ? Pbr::SrgbToLinear(encoded) : encoded;
			};
			Pbr::Texels texels;
			for (int c = 0; c < 4; ++c)
			{
				texels.BaseColor[c] = decode(indices.BaseColor, c);
			}
			for (int c = 0; c < 3; ++c)
			{
				texels.MetallicRoughness[c] = decode(indices.MetallicRoughness, c);
				texels.Emissive[c] = decode(indices.Emissive, c);
			}
			if (indices.Normal != 0)
			{
				texels.TangentNormal =
					Pbr::Float3{ decode(indices.Normal, 0) * 2 - 1, decode(indices.Normal, 1) * 2 - 1, decode(indices.Normal, 2) * 2 - 1 };
			}
			texels.Occlusion = decode(indices.Occlusion, 0);
			return texels;
		};

		// Materials.
		const auto materialTemplate = CreateStandardMaterialTemplate();
		GpuMaterialTable materials(*device, { materialTemplate, 16, "Forward+ materials" });
		const auto material = [&](std::array<float, 4> baseColor, float metallic, float roughness, std::uint32_t flags)
		{
			auto instance = std::make_shared<MaterialInstance>(materialTemplate);
			instance->SetVector("BaseColorFactor", baseColor);
			instance->SetFloat("MetallicFactor", metallic);
			instance->SetFloat("RoughnessFactor", roughness);
			instance->SetUint("Flags", flags);
			instance->SetSampler("MaterialSampler", samplerIndex);
			return instance;
		};
		const std::uint32_t blend = Pbr::FlagAlphaBlend;
		const std::uint32_t blendBothSides = Pbr::FlagAlphaBlend | Pbr::FlagDoubleSided;
		std::vector<std::shared_ptr<MaterialInstance>> instances{
			material({ 0.55f, 0.55f, 0.5f, 1.0f }, 0.0f, 0.8f, 0),				  // 0 ground.
			material({ 1.0f, 1.0f, 1.0f, 1.0f }, 1.0f, 1.0f, 0),				  // 1 gold (textured).
			material({ 0.8f, 0.1f, 0.1f, 1.0f }, 0.0f, 0.5f, 0),				  // 2 red, normal-mapped.
			material({ 0.1f, 0.3f, 0.9f, 1.0f }, 0.0f, 0.6f, 0),				  // 3 blue, on a mirrored cube.
			material({ 1.0f, 1.0f, 1.0f, 1.0f }, 0.0f, 0.5f, Pbr::FlagAlphaMask), // 4 masked away entirely.
			material({ 0.6f, 0.6f, 0.6f, 1.0f }, 0.0f, 0.7f, 0),				  // 5 occlusion + emission.
			material({ 0.9f, 0.1f, 0.1f, 0.5f }, 0.0f, 0.3f, blendBothSides),	  // 6 red glass.
			material({ 0.1f, 0.9f, 0.2f, 0.4f }, 0.0f, 0.3f, blendBothSides),	  // 7 green glass.
			material({ 0.1f, 0.2f, 0.9f, 0.6f }, 0.0f, 0.3f, blendBothSides),	  // 8 blue glass.
			material({ 0.9f, 0.8f, 0.1f, 0.7f }, 0.0f, 0.4f, blend),			  // 9 yellow, behind a cube.
			material({ 0.9f, 0.9f, 0.9f, 0.8f }, 0.0f, 0.4f, blend),			  // 10 single-sided, facing away.
		};
		instances[1]->SetTexture("BaseColorTexture", textureIndex[1]);
		instances[1]->SetTexture("MetallicRoughnessTexture", textureIndex[2]);
		instances[2]->SetTexture("NormalTexture", textureIndex[3]);
		instances[3]->SetVector("EmissiveFactor", std::array<float, 3>{ 0.05f, 0.05f, 0.1f });
		instances[4]->SetTexture("BaseColorTexture", textureIndex[6]);
		instances[5]->SetTexture("OcclusionTexture", textureIndex[4]);
		instances[5]->SetFloat("OcclusionStrength", 0.9f);
		instances[5]->SetTexture("EmissiveTexture", textureIndex[5]);
		instances[5]->SetVector("EmissiveFactor", std::array<float, 3>{ 0.4f, 0.4f, 0.4f });
		std::vector<GpuMaterialHandle> materialHandles;
		for (const auto& instance : instances)
		{
			materialHandles.push_back(materials.Create(instance));
		}

		// Geometry: one StandardVertex cube and quad.
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
		const auto& quadMeta = *heap.GetMetadata(quadMesh);
		SWIM_REQUIRE(cubeMeta.VertexPage == quadMeta.VertexPage && cubeMeta.IndexPage == quadMeta.IndexPage);
		const ForwardPlusPageSlot pageSlot{ cubeMeta.IndexPage, cubeMeta.VertexPage };

		// Scene: shape, transform, material per object; ObjectId = index.
		constexpr float halfPi = 1.57079633f;

		struct Placement
		{
			Fs::Shape Kind;
			Fs::Float3 Translation;
			float Yaw;
			float Pitch;
			Fs::Float3 Scale;
			std::uint32_t Material;
		};

		std::vector<Placement> placements{
			{ Fs::Shape::Quad, { 0, 0, 0 }, 0.0f, -halfPi, { 12, 12, 1 }, 0 },				 // 0 ground (+Y).
			{ Fs::Shape::Cube, { -3.2f, 1.0f, 0.0f }, 0.5f, 0.0f, { 1, 1, 1 }, 1 },			 // 1 gold.
			{ Fs::Shape::Cube, { 0.0f, 1.3f, -2.0f }, 0.7f, 0.3f, { 1.5f, 1.2f, 0.8f }, 2 }, // 2 red, non-uniform scale.
			{ Fs::Shape::Cube, { 3.2f, 1.0f, 0.5f }, 1.0f, 0.0f, { -1, 1, 1 }, 3 },			 // 3 mirrored.
			{ Fs::Shape::Cube, { -1.6f, 0.5f, 3.0f }, 0.2f, 0.0f, { 0.5f, 0.5f, 0.5f }, 4 }, // 4 masked (invisible).
			{ Fs::Shape::Cube, { 1.6f, 0.6f, 3.0f }, -0.4f, 0.0f, { 0.6f, 0.6f, 0.6f }, 5 }, // 5 occlusion + emission.
			// Transparent, created out of depth order.
			{ Fs::Shape::Quad, { 0.6f, 1.6f, 6.0f }, 0.0f, 0.0f, { 1.2f, 1.2f, 1 }, 7 },		  // 6 green, nearest.
			{ Fs::Shape::Quad, { -0.6f, 1.4f, 4.0f }, 0.3f, 0.0f, { 1.2f, 1.2f, 1 }, 8 },		  // 7 blue, farthest of the three.
			{ Fs::Shape::Quad, { 0.0f, 1.5f, 5.0f }, 0.0f, 0.1f, { 1.2f, 1.2f, 1 }, 6 },		  // 8 red, between.
			{ Fs::Shape::Quad, { 0.9f, 1.4f, -3.4f }, 0.0f, 0.0f, { 1.4f, 1.0f, 1 }, 9 },		  // 9 yellow, partly behind cube 2.
			{ Fs::Shape::Quad, { -3.5f, 2.6f, 4.0f }, 3.14159265f, 0.0f, { 0.8f, 0.8f, 1 }, 10 }, // 10 faces away: culled.
		};
		std::vector<Fs::Object> objects;
		GpuScene scene(*device, { 32, "Forward+ scene" });
		std::vector<RenderObjectHandle> handles;
		for (std::uint32_t i = 0; i < placements.size(); ++i)
		{
			const auto& p = placements[i];
			objects.push_back({ p.Kind, Fs::MakeTransform(p.Translation, p.Yaw, p.Pitch, p.Scale) });
			RenderObjectDesc desc;
			std::copy(
				std::begin(objects.back().Transform.Current), std::end(objects.back().Transform.Current), desc.Transform.Rows.begin());
			desc.Mesh = p.Kind == Fs::Shape::Cube ? cubeMesh : quadMesh;
			desc.LocalBounds = p.Kind == Fs::Shape::Cube ? RenderBounds::FromMinMax({ -1, -1, -1 }, { 1, 1, 1 })
														 : RenderBounds::FromMinMax({ -1, -1, 0 }, { 1, 1, 0 });
			desc.MaterialSet = materials.GetIndex(materialHandles[p.Material]);
			desc.ObjectId = i;
			handles.push_back(scene.Create(desc));
		}
		const auto materialOf = [&](std::uint32_t object) -> const MaterialInstance&
		{
			return *instances[placements[object].Material];
		};

		GpuVisibilityDesc visibilityDesc;
		visibilityDesc.CullPipeline = visibilityProgram.Pipeline.get();
		visibilityDesc.Layout = visibilityProgram.Layout.get();
		visibilityDesc.Space = visibilityProgram.Space;
		visibilityDesc.MaxObjects = 32;
		visibilityDesc.MaxMaterialSets = 16;
		visibilityDesc.MaterialBinCapacities = ForwardPlusRenderer::VisibilityBinCapacities(64, 16);
		GpuVisibility visibility(*device, visibilityDesc);
		for (std::size_t i = 0; i < materialHandles.size(); ++i)
		{
			ForwardPlusRenderer::RouteMaterial(visibility, materials.GetIndex(materialHandles[i]), ReadStandardParameters(*instances[i]));
		}

		// Lights: a sun and 300 point/spot lights around the objects.
		GpuLightBuffer lights(*device, { 2, 16384, "Forward+ lights" });
		std::mt19937 random(669);
		std::uniform_real_distribution<float> unit(0.0f, 1.0f);
		const auto localLight = [&](float spread)
		{
			LightDesc desc;
			desc.Type = unit(random) < 0.6f ? LightType::Point : LightType::Spot;
			desc.Position = { spread * (16 * unit(random) - 8), 0.3f + 4.5f * unit(random), spread * (14 * unit(random) - 6) };
			desc.Direction = { unit(random) - 0.5f, -1.0f, unit(random) - 0.5f };
			desc.Color = { 0.4f + 0.6f * unit(random), 0.4f + 0.6f * unit(random), 0.4f + 0.6f * unit(random) };
			desc.Intensity = 2.0f + 6.0f * unit(random);
			desc.Range = 1.5f + 2.5f * unit(random);
			desc.OuterConeAngle = 0.4f + 1.0f * unit(random);
			desc.InnerConeAngle = desc.OuterConeAngle * 0.7f * unit(random);
			return desc;
		};
		{
			LightDesc sun;
			sun.Type = LightType::Directional;
			sun.Direction = { -0.4f, -1.0f, -0.3f };
			sun.Intensity = 1.5f;
			sun.Color = { 1.0f, 0.95f, 0.85f };
			lights.Create(sun);
		}
		for (int i = 0; i < 300; ++i)
		{
			lights.Create(localLight(1.0f));
		}

		EnvironmentMapDesc map;
		map.SourceSize = 64;
		map.PrefilteredSize = 32;
		map.PrefilteredMipCount = 5;
		map.PrefilterSampleCount = 64;
		map.IrradianceFaceSize = 16;
		constexpr std::uint32_t lutSize = 32;
		constexpr std::uint32_t lutSamples = 256;
		const Env::ProceduralSky sky;

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
		gridDesc.IndexCapacity = 1u << 20;

		struct FrameSpec
		{
			const char* Name;
			Fs::Float3 Eye;
			Fs::Float3 Target;
			bool Environment;
			ForwardPlusDebugMode Debug;
			std::array<float, 3> Ambient;
			std::uint32_t PixelStride;		   // Compare every n-th pixel (row-major).
			bool ExpectExact;				   // No cluster overflow allowed.
			std::array<float, 2> JitterPixels; // TAA jitter (item 75), in pixels (x right, y down).
		};

		struct FrameResult
		{
			std::vector<std::uint32_t> SortedObjects;
			std::uint32_t Overflow = 0;
			std::uint32_t MovingPixels = 0; // Opaque pixels with motion vectors above 1e-3 UV.
		};

		Rhi::TimelinePoint lastCompletion{};
		std::optional<std::array<float, 16>> previousViewProjection; // The last frame's, for motion vectors.
		const auto frame = [&](const FrameSpec& spec)
		{
			const float aspect = float(width) / float(height);
			ClusterView clusterView;
			clusterView.View = Scene::LookAt(spec.Eye, spec.Target, { 0, 1, 0 });
			clusterView.Projection = PerspectiveReverseZRowMajor(0.9f, aspect, 0.1f);
			const auto viewProjection = MultiplyRowMajor(clusterView.Projection, clusterView.View);
			RenderViewDesc viewDesc;
			viewDesc.ViewProjection = viewProjection;
			viewDesc.CameraPosition = spec.Eye;

			RenderGraph graph;
			const auto materialResources = materials.Import(graph);
			const auto sceneResources = scene.Import(graph);
			const auto geometry = heap.Import(graph);
			const auto lightResources = lights.Import(graph);
			std::optional<EnvironmentGraphResources> environment;
			std::optional<GraphTexture> lut;
			if (spec.Environment)
			{
				environment = environmentPrograms.Builder->Record(graph, sky, map);
				lut = environmentPrograms.Builder->RecordBrdfLut(graph, lutSize, lutSamples);
			}
			VisibilityFrameDesc visibilityFrame;
			visibilityFrame.View = BuildGpuViewRecord(viewDesc);
			visibilityFrame.IndexPages = { pageSlot.IndexPage };
			visibilityFrame.ZeroUnusedCommands = NeedsZeroedCommands(path);
			const auto visible = visibility.Record(graph, sceneResources, geometry, visibilityFrame);
			const auto clusters = assigner.Record(graph, lightResources, gridDesc, clusterView);

			const auto target = [&](Rhi::Format format, Rhi::TextureUsage usage, const char* name)
			{
				Rhi::TextureDesc desc;
				desc.Extent = { width, height, 1 };
				desc.PixelFormat = format;
				desc.Usage = usage;
				desc.DebugName = name;
				return graph.CreateTexture(desc);
			};
			ForwardPlusTargets targets;
			targets.Color = target(
				ForwardPlusRenderer::ColorFormat, Rhi::TextureUsage::ColorAttachment | Rhi::TextureUsage::TransferSource, "Forward+ color");
			targets.ObjectId = target(ForwardPlusRenderer::ObjectIdFormat,
				Rhi::TextureUsage::ColorAttachment | Rhi::TextureUsage::TransferSource, "Forward+ object id");
			targets.Depth = target(CanonicalDepthFormat, Rhi::TextureUsage::DepthStencilAttachment, "Forward+ depth");
			targets.Velocity = target(ForwardPlusRenderer::VelocityFormat,
				Rhi::TextureUsage::ColorAttachment | Rhi::TextureUsage::TransferSource, "Forward+ velocity");
			targets.Normal = target(ForwardPlusRenderer::NormalFormat,
				Rhi::TextureUsage::ColorAttachment | Rhi::TextureUsage::TransferSource, "Forward+ normal");
			targets.Indirect = target(ForwardPlusRenderer::IndirectFormat,
				Rhi::TextureUsage::ColorAttachment | Rhi::TextureUsage::TransferSource, "Forward+ indirect");

			ForwardPlusFrame forwardFrame;
			forwardFrame.Scene = &sceneResources;
			forwardFrame.Geometry = &geometry;
			forwardFrame.Visibility = &visible;
			forwardFrame.PageSlots = { pageSlot };
			forwardFrame.Materials = &materialResources;
			forwardFrame.Bindless = &bindless.GetTable();
			forwardFrame.Lights = &lightResources;
			forwardFrame.Clusters = &clusters;
			forwardFrame.Environment = environment ? &*environment : nullptr;
			forwardFrame.BrdfLut = lut;
			forwardFrame.View.ViewProjection = viewProjection;
			forwardFrame.View.CameraPosition = spec.Eye;
			forwardFrame.View.CameraForward = { spec.Target[0] - spec.Eye[0], spec.Target[1] - spec.Eye[1], spec.Target[2] - spec.Eye[2] };
			forwardFrame.View.Ambient = spec.Ambient;
			forwardFrame.View.EnvironmentIntensity = 0.8f;
			forwardFrame.View.EnvironmentRotation = 0.6f;
			forwardFrame.View.DebugMode = spec.Debug;
			forwardFrame.View.PreviousViewProjection = previousViewProjection;
			// Pixels to NDC: x right; NDC y is up, pixel y down.
			forwardFrame.View.Jitter = { spec.JitterPixels[0] * 2.0f / float(width), -spec.JitterPixels[1] * 2.0f / float(height) };
			const auto forward = renderer.Record(graph, forwardFrame, targets);
			previousViewProjection = viewProjection;

			const std::uint32_t clusterCount = clusters.Layout.ClusterCount;
			const auto colorReadback = AddTextureReadback(graph, "Color", targets.Color, { 0, {}, {}, { width, height, 1 } });
			const auto idReadback = AddTextureReadback(graph, "Object id", targets.ObjectId, { 0, {}, {}, { width, height, 1 } });
			const auto velocityReadback = AddTextureReadback(graph, "Velocity", *targets.Velocity, { 0, {}, {}, { width, height, 1 } });
			const auto normalReadback = AddTextureReadback(graph, "Normal", *targets.Normal, { 0, {}, {}, { width, height, 1 } });
			const auto indirectReadback = AddTextureReadback(graph, "Indirect", *targets.Indirect, { 0, {}, {}, { width, height, 1 } });
			const auto sortedReadback =
				AddBufferReadback(graph, "Sorted commands", forward.SortedCommands, 0, std::uint64_t(forward.TransparentCapacity) * 20);
			const auto sortedCountReadback = AddBufferReadback(graph, "Sorted count", forward.SortedCounts, 0, 4);
			const auto drawRecordReadback =
				AddBufferReadback(graph, "Draw records", visible.DrawRecords, 0, std::uint64_t(visible.Bins->GetTotalCapacity()) * 8);
			const auto recordsReadback = AddBufferReadback(graph, "Cluster records", clusters.Records, 0, std::uint64_t(clusterCount) * 16);
			const auto indicesReadback =
				AddBufferReadback(graph, "Cluster indices", clusters.Indices, 0, std::uint64_t(gridDesc.IndexCapacity) * 4);
			const auto statsReadback = AddBufferReadback(graph, "Cluster stats", clusters.Stats, 0, sizeof(ClusterStats));
			std::optional<Smoke::CubeReadback> prefilteredReadback;
			std::optional<GraphReadback> irradianceReadback;
			std::optional<GraphReadback> lutReadback;
			if (environment)
			{
				prefilteredReadback = Smoke::AddCubeReadback(graph, environment->Prefiltered, map.PrefilteredSize, map.PrefilteredMipCount);
				irradianceReadback =
					AddBufferReadback(graph, "Irradiance", environment->Irradiance, 0, EnvironmentIrradianceBindings::OutputBytes);
				lutReadback = AddTextureReadback(graph, "LUT", *lut, { 0, {}, {}, { lutSize, lutSize, 1 } });
			}
			const auto completion = executor.Execute(graph.Compile());
			lastCompletion = completion;
			scene.CommitUploads();
			heap.CommitUploads(completion);
			materials.CommitUploads();
			lights.CommitUploads();
			const auto timings = executor.ReadTimings();

			// Readbacks.
			const auto read = [&](const GraphReadback& readback, auto& target)
			{
				SWIM_REQUIRE(
					executor.TryReadback(readback.Buffer, std::as_writable_bytes(std::span(target))) == Rhi::ReadbackStatus::Ready);
			};
			std::vector<std::uint16_t> colorHalves(std::size_t(width) * height * 4);
			std::vector<float> ids(std::size_t(width) * height);
			std::vector<std::uint16_t> velocityHalves(std::size_t(width) * height * 2);
			std::vector<std::uint16_t> normalHalves(std::size_t(width) * height * 4);
			std::vector<std::uint16_t> indirectHalves(std::size_t(width) * height * 4);
			std::vector<Rhi::DrawIndexedIndirectCommand> sorted(forward.TransparentCapacity);
			std::array<std::uint32_t, 1> sortedCount{};
			std::vector<GpuDrawRecord> drawRecords(visible.Bins->GetTotalCapacity());
			std::vector<ClusterRecord> clusterRecords(clusterCount);
			std::vector<std::uint32_t> clusterIndices(gridDesc.IndexCapacity);
			std::array<ClusterStats, 1> stats{};
			read(colorReadback, colorHalves);
			read(idReadback, ids);
			read(velocityReadback, velocityHalves);
			read(normalReadback, normalHalves);
			read(indirectReadback, indirectHalves);
			read(sortedReadback, sorted);
			read(sortedCountReadback, sortedCount);
			read(drawRecordReadback, drawRecords);
			read(recordsReadback, clusterRecords);
			read(indicesReadback, clusterIndices);
			read(statsReadback, stats);
			std::optional<Env::EnvironmentProbe> probe;
			if (environment)
			{
				probe.emplace(Smoke::ReadIrradiance(executor, *irradianceReadback), Smoke::ReadCube(executor, *prefilteredReadback),
					Smoke::ReadImage(executor, *lutReadback, lutSize, lutSize));
			}
			FrameResult result;
			result.Overflow = stats[0].OverflowClusters;
			if (spec.ExpectExact)
			{
				SWIM_CHECK_EQUAL(stats[0].OverflowClusters, 0u);
				SWIM_CHECK_EQUAL(stats[0].DroppedIndices, 0u);
			}

			// 1. The transparent sort: GPU order == ForwardPlus::SortTransparentDraws.
			const auto& bins = *visible.Bins;
			const auto& range = bins.GetRange(bins.GetBin(static_cast<std::uint32_t>(ForwardPlusBin::Transparent), 0));
			std::vector<GpuInstanceRecord> instanceRows;
			std::vector<GpuTransformRecord> transformRows;
			for (std::uint32_t row = 0; row < scene.GetStats().RowCount; ++row)
			{
				instanceRows.push_back(scene.GetInstanceRow(row));
			}
			std::uint32_t maxTransform = 0;
			for (const auto& row : instanceRows)
			{
				maxTransform = std::max(maxTransform, row.TransformIndex + 1);
			}
			for (std::uint32_t row = 0; row < maxTransform; ++row)
			{
				transformRows.push_back(scene.GetTransformRow(row));
			}
			std::map<std::uint32_t, GpuTransformRecord> transformOf; // ObjectId -> its transform row.
			for (const auto& row : instanceRows)
			{
				transformOf[row.ObjectId] = transformRows[row.TransformIndex];
			}
			const std::uint32_t transparentCount = sortedCount[0];
			SWIM_CHECK_EQUAL(transparentCount, 5u); // Frustum-visible transparent quads (the culled one is culled per pixel).
			const std::span<const GpuDrawRecord> transparentRecords(drawRecords.data() + range.First, transparentCount);
			const auto expectedOrder =
				Fp::SortTransparentDraws(transparentRecords, range.First, instanceRows, transformRows, forward.ViewRecord);
			for (std::uint32_t i = 0; i < forward.TransparentCapacity; ++i)
			{
				const auto& command = sorted[i];
				if (i >= transparentCount)
				{
					SWIM_CHECK(command.IndexCount == 0 && command.InstanceCount == 0); // Zero-filled.
					continue;
				}
				SWIM_CHECK_EQUAL(command.FirstInstance, expectedOrder[i]);
				SWIM_CHECK_EQUAL(command.IndexCount, 6u);
				SWIM_CHECK_EQUAL(command.InstanceCount, 1u);
				const auto row = drawRecords[command.FirstInstance].InstanceRow;
				result.SortedObjects.push_back(scene.GetInstanceRow(row).ObjectId);
			}

			// 2. Per pixel against the CPU ray cast.
			const auto& grid = clusters.GridRecord;
			Fp::LightingInputs inputs{ lights.GetRecords(), lights.GetHeader(), &grid, clusterRecords, clusterIndices,
				probe ? &*probe : nullptr };
			std::vector<std::uint32_t> drawRank(objects.size(), 0);
			for (std::uint32_t i = 0; i < result.SortedObjects.size(); ++i)
			{
				drawRank[result.SortedObjects[i]] = i + 1;
			}
			struct PixelTruth
			{
				std::uint32_t Signature = 0; // Opaque object/face and transparent layers.
				std::uint32_t Id = 0;
				std::optional<Fs::Hit> Opaque;
				std::vector<Fs::Hit> Layers; // In draw order.
			};
			std::vector<PixelTruth> truth(std::size_t(width) * height);
			for (std::uint32_t y = 0; y < height; ++y)
			{
				for (std::uint32_t x = 0; x < width; ++x)
				{
					// The jitter moves the geometry by JitterPixels, so this pixel's centre sees
					// what the unjittered projection shows at centre - jitter.
					const float px = float(x) + 0.5f - spec.JitterPixels[0];
					const float py = float(y) + 0.5f - spec.JitterPixels[1];
					const auto through = Scene::ViewToWorld(grid, Scene::ViewPoint(grid, px, py, 1.0f));
					const Fs::Float3 direction{ through[0] - spec.Eye[0], through[1] - spec.Eye[1], through[2] - spec.Eye[2] };
					auto& pixel = truth[std::size_t(y) * width + x];
					std::vector<Fs::Hit> layers;
					for (const auto& hit : Fs::CastRay(objects, spec.Eye, direction))
					{
						const auto& instance = materialOf(hit.Object);
						const auto parameters = ReadStandardParameters(instance);
						const auto frameAxes = Fp::BuildFrame(hit.Normal, hit.Tangent, hit.FrontFacing != hit.Mirrored, hit.Mirrored);
						if (Fp::CullsFace(parameters, frameAxes.FrontFacing) || !Pbr::Resolve(parameters, texelsOf(instance), frameAxes))
						{
							continue; // Culled or alpha-masked: the ray continues.
						}
						if (Fp::MaterialBin(parameters) == ForwardPlusBin::Transparent)
						{
							layers.push_back(hit);
							continue;
						}
						pixel.Opaque = hit;
						pixel.Id = hit.Object + 1;
						pixel.Signature = (hit.Object + 1) * 8 + hit.Face;
						break;
					}
					std::sort(layers.begin(), layers.end(),
						[&](const Fs::Hit& a, const Fs::Hit& b)
						{
							return drawRank[a.Object] < drawRank[b.Object];
						});
					for (const auto& layer : layers)
					{
						pixel.Signature |= 1u << (16 + layer.Object);
					}
					pixel.Layers = std::move(layers);
				}
			}
			const auto surfaceOf = [&](const Fs::Hit& hit)
			{
				const auto& instance = materialOf(hit.Object);
				const auto parameters = ReadStandardParameters(instance);
				const auto axes = Fp::BuildFrame(hit.Normal, hit.Tangent, hit.FrontFacing != hit.Mirrored, hit.Mirrored);
				return *Pbr::Resolve(parameters, texelsOf(instance), axes);
			};
			const auto shade = [&](const Fs::Hit& hit, float px, float py)
			{
				return Fp::Shade(inputs, forward.ViewRecord, surfaceOf(hit), hit.Position, px, py);
			};
			// Item 76: the normal + roughness and indirect targets of opaque pixels.
			std::uint32_t surfaceCompared = 0, normalOutliers = 0, indirectOutliers = 0;
			float normalWorst = 0.0f;

			std::uint32_t idInterior = 0, idMismatch = 0, compared = 0, outliers = 0, layered = 0, orderSensitive = 0;
			std::uint32_t velocityCompared = 0, velocityOutliers = 0, moving = 0;
			float velocityWorst = 0.0f;
			std::map<std::uint32_t, std::uint32_t> comparedPerObject;
			double errorSum = 0.0;
			float worst = 0.0f;
			std::uint32_t maskedSeen = 0;
			for (std::uint32_t y = 1; y + 1 < height; ++y)
			{
				for (std::uint32_t x = 1; x + 1 < width; ++x)
				{
					const std::size_t index = std::size_t(y) * width + x;
					const auto& pixel = truth[index];
					const auto gpuId = static_cast<std::uint32_t>(ids[index]);
					maskedSeen += gpuId == 5u ? 1u : 0u; // Object 4 (masked) + 1.
					bool interior = true;
					for (int dy = -1; dy <= 1 && interior; ++dy)
					{
						for (int dx = -1; dx <= 1 && interior; ++dx)
						{
							interior = truth[std::size_t(int(y) + dy) * width + std::size_t(int(x) + dx)].Signature == pixel.Signature;
						}
					}
					if (!interior)
					{
						continue;
					}
					++idInterior;
					idMismatch += gpuId != pixel.Id ? 1u : 0u;
					// Motion vectors: ForwardPlus::MotionVector of the surface point this pixel sees.
					if (pixel.Opaque && spec.Debug == ForwardPlusDebugMode::None)
					{
						const auto& transform = transformOf.at(pixel.Opaque->Object);
						const auto inverse = Fs::InverseLinear(transform.Current);
						const Fs::Float3 relative{ pixel.Opaque->Position[0] - transform.Current[3],
							pixel.Opaque->Position[1] - transform.Current[7], pixel.Opaque->Position[2] - transform.Current[11] };
						Fs::Float3 local{};
						for (int r = 0; r < 3; ++r)
						{
							local[r] = inverse[r * 3] * relative[0] + inverse[r * 3 + 1] * relative[1] + inverse[r * 3 + 2] * relative[2];
						}
						const auto motion = Fp::MotionVector(forward.ViewRecord, transform.Current, transform.Previous, local);
						bool outlier = false;
						for (int c = 0; c < 2; ++c)
						{
							const float actual = Smoke::HalfToFloat(velocityHalves[index * 2 + c]);
							const float error = std::abs(actual - motion[c]);
							velocityWorst = std::max(velocityWorst, error);
							outlier = outlier || error > 1.0e-4f + 2.0e-3f * std::abs(motion[c]);
						}
						++velocityCompared;
						velocityOutliers += outlier ? 1u : 0u;
						moving += std::abs(motion[0]) + std::abs(motion[1]) > 1.0e-3f ? 1u : 0u;
					}
					if (index % spec.PixelStride != 0)
					{
						continue;
					}
					const float px = float(x) + 0.5f;
					const float py = float(y) + 0.5f;
					Fp::Float4 expected{ 0, 0, 0, 0 };
					if (pixel.Opaque)
					{
						if (spec.Debug == ForwardPlusDebugMode::ClusterHeatmap)
						{
							expected = Fp::DebugColor(grid, clusterRecords, px, py, Fp::ViewDepth(grid, pixel.Opaque->Position));
						}
						else
						{
							const auto color = shade(*pixel.Opaque, px, py);
							expected = { color[0], color[1], color[2], 1.0f };
						}
						++comparedPerObject[pixel.Opaque->Object];
					}
					if (spec.Debug == ForwardPlusDebugMode::ClusterHeatmap && !pixel.Layers.empty())
					{
						continue; // The heatmap covers opaque pixels only.
					}
					auto reversed = expected;
					std::vector<Fp::Float4> layerColors;
					for (const auto& layer : pixel.Layers)
					{
						layerColors.push_back(shade(layer, px, py));
						expected = Fp::Over(layerColors.back(), expected);
						++comparedPerObject[layer.Object];
					}
					for (auto it = layerColors.rbegin(); it != layerColors.rend(); ++it)
					{
						reversed = Fp::Over(*it, reversed);
					}
					if (pixel.Opaque)
					{
						// Normal + roughness as resolved; indirect radiance times every layer's transmittance
						// (0 in the heatmap view).
						const auto surface = surfaceOf(*pixel.Opaque);
						Fp::Float3 indirect = spec.Debug == ForwardPlusDebugMode::ClusterHeatmap
							? Fp::Float3{ 0, 0, 0 }
							: Fp::IndirectRadiance(inputs, forward.ViewRecord, surface, pixel.Opaque->Position);
						for (const auto& layer : layerColors)
						{
							for (auto& value : indirect)
							{
								value *= 1.0f - layer[3];
							}
						}
						bool normalOutlier = false, indirectOutlier = false;
						for (int c = 0; c < 4; ++c)
						{
							const float expectedNormal = c < 3 ? surface.Normal[c] : surface.PerceptualRoughness;
							const float error = std::abs(Smoke::HalfToFloat(normalHalves[index * 4 + c]) - expectedNormal);
							normalWorst = std::max(normalWorst, error);
							normalOutlier = normalOutlier || error > 0.02f;
							if (c < 3)
							{
								const float actualIndirect = Smoke::HalfToFloat(indirectHalves[index * 4 + c]);
								indirectOutlier =
									indirectOutlier || std::abs(actualIndirect - indirect[c]) > 0.01f + 0.03f * std::abs(indirect[c]);
							}
						}
						++surfaceCompared;
						normalOutliers += normalOutlier ? 1u : 0u;
						indirectOutliers += indirectOutlier ? 1u : 0u;
					}
					std::array<float, 4> actual{};
					for (int c = 0; c < 4; ++c)
					{
						actual[c] = Smoke::HalfToFloat(colorHalves[index * 4 + c]);
					}
					float pixelError = 0.0f;
					bool outlier = false;
					for (int c = 0; c < 4; ++c)
					{
						const float error = RelativeError(actual[c], expected[c]);
						pixelError = std::max(pixelError, error);
						outlier = outlier || std::abs(actual[c] - expected[c]) > 0.01f + 0.03f * std::abs(expected[c]);
					}
					++compared;
					outliers += outlier ? 1u : 0u;
					errorSum += pixelError;
					worst = std::max(worst, pixelError);
					if (pixel.Layers.size() >= 2)
					{
						++layered;
						float gap = 0.0f;
						for (int c = 0; c < 3; ++c)
						{
							gap = std::max(gap, std::abs(reversed[c] - expected[c]));
						}
						orderSensitive += gap > 0.05f ? 1u : 0u;
					}
				}
			}
			const double mean = compared ? errorSum / compared : 0.0;
			std::printf("             [forward+ %s] %u interior pixels, %u id mismatches; %u compared, %u outliers, mean %.2e, worst %.2e; "
						"%u layered (%u order-sensitive); %u clusters overflowing\n",
				spec.Name, idInterior, idMismatch, compared, outliers, mean, double(worst), layered, orderSensitive, result.Overflow);
			std::printf("             [forward+ %s] surface targets: %u compared, %u normal outliers (worst %.2e), %u indirect outliers\n",
				spec.Name, surfaceCompared, normalOutliers, double(normalWorst), indirectOutliers);
			SWIM_CHECK(surfaceCompared > 0u);
			SWIM_CHECK(normalOutliers <= surfaceCompared / 200);
			SWIM_CHECK(indirectOutliers <= surfaceCompared / 200);
			if (velocityCompared)
			{
				std::printf("             [forward+ %s] velocity: %u compared (%u moving), %u outliers, worst %.2e\n", spec.Name,
					velocityCompared, moving, velocityOutliers, double(velocityWorst));
				SWIM_CHECK(velocityOutliers <= velocityCompared / 500);
			}
			result.MovingPixels = moving;
			SWIM_CHECK(idInterior > width * height / 2);
			SWIM_CHECK(idMismatch <= idInterior / 500);
			SWIM_CHECK_EQUAL(maskedSeen, 0u);
			SWIM_CHECK(compared > 0u);
			SWIM_CHECK(outliers <= compared / 200);
			SWIM_CHECK(mean < (spec.Debug == ForwardPlusDebugMode::ClusterHeatmap ? 1.0e-2 : 5.0e-3));
			if (spec.PixelStride == 1 && spec.Debug == ForwardPlusDebugMode::None)
			{
				for (const std::uint32_t object : { 0u, 1u, 2u, 3u, 5u })
				{
					SWIM_CHECK(comparedPerObject[object] > 100u); // Every visible opaque object was compared.
				}
				SWIM_CHECK(layered > 100u);
				SWIM_CHECK(orderSensitive > 50u);		 // The order is observable, and the GPU's matched.
				SWIM_CHECK(comparedPerObject[10] == 0u); // Single-sided, facing away.
			}

			// Pass timings (GPU timestamps when the queue supports them).
			bool measured = false;
			const double clusterMs = PassMilliseconds(timings, "Clusters", &measured);
			if (measured)
			{
				std::printf("             [forward+ %s] %u lights: clusters %.3f ms, opaque %.3f ms, sort %.3f ms, transparent %.3f ms\n",
					spec.Name, lights.GetHeader().LocalCount, clusterMs, PassMilliseconds(timings, "Forward+ opaque"),
					PassMilliseconds(timings, "Forward+ transparent sort"),
					PassMilliseconds(timings, "Forward+ transparent") - PassMilliseconds(timings, "Forward+ transparent sort"));
			}
			return result;
		};

		const Fs::Float3 eye{ 0.0f, 3.5f, 14.0f };
		const Fs::Float3 target{ 0.0f, 1.2f, 0.0f };
		const auto lit = frame({ "lit", eye, target, true, ForwardPlusDebugMode::None, { 0.02f, 0.02f, 0.03f }, 1, true, { 0.0f, 0.0f } });
		const auto position = [](const std::vector<std::uint32_t>& order, std::uint32_t object)
		{
			return std::find(order.begin(), order.end(), object) - order.begin();
		};
		// Back to front: the yellow quad (z = -3.4) first, then blue (z = 4), red (z = 5), green (z = 6).
		SWIM_CHECK_EQUAL(position(lit.SortedObjects, 9), 0);
		SWIM_CHECK(position(lit.SortedObjects, 7) < position(lit.SortedObjects, 8));
		SWIM_CHECK(position(lit.SortedObjects, 8) < position(lit.SortedObjects, 6));

		frame({ "heatmap", eye, target, true, ForwardPlusDebugMode::ClusterHeatmap, { 0.02f, 0.02f, 0.03f }, 1, true, { 0.0f, 0.0f } });

		// Move the red quad in front of the green one and the camera to the side; no environment.
		// Jittered by (0.37, -0.21) pixels, and every opaque pixel moves (camera and quad motion).
		{
			const auto& p = placements[8];
			objects[8].Transform = Fs::MakeTransform({ p.Translation[0], p.Translation[1], 6.8f }, p.Yaw, p.Pitch, p.Scale);
			RenderAffine moved;
			std::copy(std::begin(objects[8].Transform.Current), std::end(objects[8].Transform.Current), moved.Rows.begin());
			SWIM_CHECK(scene.SetTransform(handles[8], moved));
		}
		const auto side = frame({ "moved", { 5.0f, 3.0f, 12.5f }, { 0.0f, 1.2f, 0.0f }, false, ForwardPlusDebugMode::None,
			{ 0.08f, 0.08f, 0.1f }, 1, true, { 0.37f, -0.21f } });
		SWIM_CHECK(position(lit.SortedObjects, 8) < position(lit.SortedObjects, 6));   // Red before green...
		SWIM_CHECK(position(side.SortedObjects, 8) > position(side.SortedObjects, 6)); // ...until it moved in front.
		SWIM_CHECK_EQUAL(lit.MovingPixels, 0u);										   // No history: the previous matrix is this frame's.
		SWIM_CHECK(side.MovingPixels > width * height / 4);							   // The camera moved.

		// 10,000 lights: clusters may truncate; the CPU uses the GPU's own lists.
		for (int i = 0; i < 9700; ++i)
		{
			lights.Create(localLight(1.4f));
		}
		frame({ "10k lights", eye, target, true, ForwardPlusDebugMode::None, { 0.02f, 0.02f, 0.03f }, 7, false, { 0.0f, 0.0f } });

		scene.Collect();
		scene.Drain();
		heap.Drain();
		materials.Drain();
		bindless.Drain();
		executor.Trim();
#endif
	}

	// Critical-path item 69: clustered light assignment at 0 / 1k / 10k / 32k lights
	// spread through a 1280x720 view, 10k lights behind the camera and 10k packed
	// into a 4 m ball. Each scenario runs three frames; the last frame's GPU pass
	// times are printed, and its statistics are checked against the CPU cull and
	// ClusterStats' own identities (bounded, counted overflow).
	void RunClusteredLightingBenchmark(const Swim::Rhi::GraphicsSystemDesc& graphicsDesc)
	{
#ifndef SWIM_FORWARD_PLUS_SMOKE_AVAILABLE
		SWIM_REQUIRE_MESSAGE(false, "Clustered lighting benchmark requires generated Slang artifacts");
#else
		using namespace Swim;
		using namespace Swim::Render;
		namespace Cl = Swim::Render::Clustering;
		namespace Scene = Swim::Testing::ClusterScene;

		Platform::PlatformSystem platform;
		SWIM_REQUIRE_MESSAGE(platform.Initialize(), "Clustered lighting benchmark requires working SDL Vulkan platform services");
		auto graphics = RhiVulkan::CreateGraphicsSystem(graphicsDesc);
		SWIM_REQUIRE(graphics);
		Testing::RequireVulkanSmokeValidation(*graphics, graphicsDesc.Checks);
		auto device = graphics->GetAdapter(0).CreateDevice();
		SWIM_REQUIRE(device);
		const auto cull =
			Smoke::MakeCompute(*device, SWIM_CLUSTER_LIGHT_CULL_SPIRV_PATH, SWIM_CLUSTER_LIGHT_CULL_REFLECTION_PATH, "Cluster cull");
		const auto bounds =
			Smoke::MakeCompute(*device, SWIM_CLUSTER_BOUNDS_SPIRV_PATH, SWIM_CLUSTER_BOUNDS_REFLECTION_PATH, "Cluster bounds");
		const auto assign =
			Smoke::MakeCompute(*device, SWIM_CLUSTER_ASSIGN_SPIRV_PATH, SWIM_CLUSTER_ASSIGN_REFLECTION_PATH, "Cluster assign");
		const auto scan = Smoke::MakeCompute(*device, SWIM_CLUSTER_SCAN_SPIRV_PATH, SWIM_CLUSTER_SCAN_REFLECTION_PATH, "Cluster scan");
		const ClusteredLightAssigner assigner(AssignerDesc(cull, bounds, assign, scan, "Clusters"));
		RenderGraphExecutor executor(*device);
		GpuLightBuffer lights(*device, { 1, 32768, "Benchmark lights" });
		{
			LightDesc sun;
			sun.Type = LightType::Directional;
			lights.Create(sun);
		}

		ClusterGridDesc gridDesc;
		gridDesc.ViewportWidth = 1280;
		gridDesc.ViewportHeight = 720;
		gridDesc.TileSize = 64;
		gridDesc.SliceCount = 24;
		gridDesc.Near = 0.1f;
		gridDesc.Far = 200.0f;
		gridDesc.MaxLightsPerCluster = 128;
		gridDesc.IndexCapacity = 1u << 20;
		const auto view = Scene::Camera(16.0f / 9.0f);
		const auto grid = MakeClusterGridRecord(gridDesc, view);
		const auto layout = ComputeClusterGridLayout(gridDesc);

		enum class Layout
		{
			Uniform,
			OffScreen,
			Dense,
		};

		struct Scenario
		{
			const char* Name;
			Layout Shape;
			std::uint32_t Count;
		};

		std::mt19937 random(690);
		std::uniform_real_distribution<float> unit(0.0f, 1.0f);
		std::vector<GpuLightHandle> handles;
		for (const auto& scenario : { Scenario{ "empty", Layout::Uniform, 0 }, Scenario{ "uniform", Layout::Uniform, 1000 },
				 Scenario{ "uniform", Layout::Uniform, 10000 }, Scenario{ "uniform", Layout::Uniform, 32768 },
				 Scenario{ "off-screen", Layout::OffScreen, 10000 }, Scenario{ "dense", Layout::Dense, 10000 } })
		{
			for (const auto handle : handles)
			{
				SWIM_CHECK(lights.Release(handle));
			}
			handles.clear();
			for (std::uint32_t i = 0; i < scenario.Count; ++i)
			{
				LightDesc desc;
				desc.Type = unit(random) < 0.5f ? LightType::Point : LightType::Spot;
				desc.Direction = { unit(random) - 0.5f, -1.0f, unit(random) - 0.5f };
				desc.Intensity = 1.0f + 5.0f * unit(random);
				desc.OuterConeAngle = 0.3f + 1.2f * unit(random);
				desc.InnerConeAngle = desc.OuterConeAngle * 0.5f * unit(random);
				if (scenario.Shape == Layout::Uniform)
				{
					desc.Position = { 120 * unit(random) - 60, 22 * unit(random) - 2, 165 * unit(random) - 150 };
					desc.Range = 1.0f + 7.0f * unit(random);
				}
				else if (scenario.Shape == Layout::OffScreen)
				{
					desc.Position = { 120 * unit(random) - 60, 22 * unit(random) - 2, 30.0f + 50.0f * unit(random) };
					desc.Range = 1.0f + 7.0f * unit(random);
				}
				else
				{
					desc.Position = { 4 * unit(random) - 2, 4 * unit(random), 4 * unit(random) - 2 };
					desc.Range = 3.0f + 3.0f * unit(random);
				}
				handles.push_back(lights.Create(desc));
			}

			ClusterStats stats{};
			std::vector<GraphPassTiming> timings;
			for (int repeat = 0; repeat < 3; ++repeat)
			{
				RenderGraph graph;
				const auto lightResources = lights.Import(graph);
				const auto clusters = assigner.Record(graph, lightResources, gridDesc, view);
				const auto statsReadback = AddBufferReadback(graph, "Stats", clusters.Stats, 0, sizeof(ClusterStats));
				executor.Execute(graph.Compile());
				lights.CommitUploads();
				timings = executor.ReadTimings();
				SWIM_REQUIRE(
					executor.TryReadback(statsReadback.Buffer, std::as_writable_bytes(std::span(&stats, 1))) == Rhi::ReadbackStatus::Ready);
			}

			// Statistics: the CPU cull and ClusterStats' identities.
			const auto rows = lights.GetRecords();
			const auto& header = lights.GetHeader();
			std::uint32_t visible = 0;
			for (std::uint32_t i = 0; i < header.LocalCount; ++i)
			{
				visible += Cl::CullLight(grid, rows[header.FirstLocalRow + i]).Radius >= 0.0f ? 1u : 0u;
			}
			SWIM_CHECK(
				std::uint32_t(std::abs(int(stats.VisibleLights) - int(visible))) <= std::max(2u, visible / 1000)); // Grazing spheres.
			SWIM_CHECK_EQUAL(stats.ClusterCount, layout.ClusterCount);
			SWIM_CHECK_EQUAL(stats.DroppedIndices, stats.RequestedIndices - stats.WrittenIndices);
			SWIM_CHECK(stats.WrittenIndices <= gridDesc.IndexCapacity);
			SWIM_CHECK(stats.OverflowClusters <= stats.NonEmptyClusters && stats.NonEmptyClusters <= stats.ClusterCount);
			SWIM_CHECK((stats.OverflowClusters > 0) == (stats.MaxRawLightsPerCluster > gridDesc.MaxLightsPerCluster));
			if (scenario.Count == 0 || scenario.Shape == Layout::OffScreen)
			{
				SWIM_CHECK_EQUAL(stats.VisibleLights, 0u);
				SWIM_CHECK_EQUAL(stats.RequestedIndices, 0u);
				SWIM_CHECK_EQUAL(stats.NonEmptyClusters, 0u);
			}
			if (scenario.Shape == Layout::Dense)
			{
				SWIM_CHECK(stats.OverflowClusters > 0u);
			}
			if (scenario.Shape == Layout::Uniform && scenario.Count > 0)
			{
				SWIM_CHECK(stats.VisibleLights > 0u);
			}

			bool measured = false;
			const double total = PassMilliseconds(timings, "Clusters", &measured);
			std::printf("             [clusters GPU] %-10s %5u lights: %5u visible, %7u indices (%u dropped), %4u overflowing, max %5u",
				scenario.Name, scenario.Count, stats.VisibleLights, stats.WrittenIndices, stats.DroppedIndices, stats.OverflowClusters,
				stats.MaxRawLightsPerCluster);
			if (measured)
			{
				std::printf("; cull %.3f, bounds %.3f, count %.3f, scan %.3f, write %.3f = %.3f ms",
					PassMilliseconds(timings, "Clusters cull"), PassMilliseconds(timings, "Clusters bounds"),
					PassMilliseconds(timings, "Clusters count"), PassMilliseconds(timings, "Clusters scan"),
					PassMilliseconds(timings, "Clusters write"), total);
			}
			std::printf("\n");
		}
		executor.Trim();
#endif
	}

	[[maybe_unused]] const bool registered = []
	{
		const char* enabled = std::getenv("SWIM_RUN_RHI_SMOKE");
		if (enabled != nullptr && std::string_view(enabled) == "1")
		{
			Swim::Testing::TestRegistry::Get().Add({ "RHI.Vulkan.Smoke", "ClusteredForwardPlusMatchesTheCpuReference", SWIM_TEST_LOCATION,
				+[]
				{
					Swim::Testing::RunValidatedVulkanSmoke(&RunForwardPlusSmoke);
				} });
			Swim::Testing::TestRegistry::Get().Add(
				{ "RHI.Vulkan.Smoke", "ClusteredLightingScalesToTensOfThousandsOfLights", SWIM_TEST_LOCATION,
					+[]
					{
						Swim::Testing::RunValidatedVulkanSmoke(&RunClusteredLightingBenchmark);
					} });
		}
		return true;
	}();
} // namespace
