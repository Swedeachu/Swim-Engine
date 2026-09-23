#include "Engine/Platform/PlatformSystem.h"
#include "Engine/Systems/Renderer/Geometry/GeometryHeap.h"
#include "Engine/Systems/Renderer/GpuMaterials/GpuMaterialTable.h"
#include "Engine/Systems/Renderer/GpuScene/GpuScene.h"
#include "Engine/Systems/Renderer/Materials/StandardMaterial.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanRhiBackend.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderCommandContext.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphExecutor.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphTransfers.h"
#include "Engine/Systems/Renderer/Resources/BindlessResourceTable.h"
#include "Engine/Systems/Renderer/Visibility/GpuVisibility.h"
#include "Engine/Systems/Renderer/Visibility/RenderViewDesc.h"
#include "Engine/Systems/Renderer/Visibility/VisibilityDraws.h"
#include "Tests/Fixtures/VulkanSmokeDiagnostics.h"
#include "Tests/Framework/Test.h"

#if defined(SWIM_GPU_VISIBILITY_SPIRV_PATH) && defined(SWIM_RHI_STANDARD_MATERIAL_DRAW_SPIRV_PATH) &&                                      \
	defined(SWIM_RHI_STANDARD_PBR_SPIRV_PATH)
#include "Tools/ShaderCompiler/ShaderRhiInterface.h"
#define SWIM_STANDARD_MATERIAL_SMOKE_AVAILABLE 1
#endif

#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <random>
#include <string_view>
#include <vector>

namespace
{
#ifdef SWIM_STANDARD_MATERIAL_SMOKE_AVAILABLE
	namespace Pbr = Swim::Render::StandardPbr;

	std::vector<std::byte> ReadSpirv(const char* path)
	{
		std::ifstream file(path, std::ios::binary | std::ios::ate);
		SWIM_REQUIRE(file);
		std::vector<std::byte> bytes(static_cast<std::size_t>(file.tellg()));
		file.seekg(0);
		file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		SWIM_REQUIRE(file && !bytes.empty());
		return bytes;
	}

	Swim::ShaderCompiler::ShaderRhiInterfaceResult ReflectProgram(const char* path)
	{
		const auto reflection = Swim::ShaderCompiler::LoadSlangReflectionJson(path);
		SWIM_REQUIRE_MESSAGE(reflection, reflection.Error);
		auto converted = Swim::ShaderCompiler::BuildRhiShaderInterface(reflection.Reflection);
		SWIM_REQUIRE_MESSAGE(converted, converted.Error);
		return converted;
	}

	float HalfToFloat(std::uint16_t half)
	{
		const std::uint32_t sign = (half >> 15) & 1u;
		const std::uint32_t exponent = (half >> 10) & 0x1fu;
		const std::uint32_t mantissa = half & 0x3ffu;
		float value = 0.0f;
		if (exponent == 0)
		{
			value = std::ldexp(float(mantissa), -24);
		}
		else if (exponent == 31)
		{
			value = mantissa ? NAN : INFINITY;
		}
		else
		{
			value = std::ldexp(float(mantissa | 0x400u), int(exponent) - 25);
		}
		return sign ? -value : value;
	}

	// Mirrors the smoke shader's ShadingView (std430, 144 bytes).
	struct ShadingView
	{
		float ViewProjection[16];
		float ViewDirection[4];
		float LightDirection[4];
		float LightRadiance[4];
		float Ambient[4];
		std::uint32_t MaterialCount = 0;
		std::uint32_t Reserved[3] = {};
	};

	static_assert(sizeof(ShadingView) == 144);

	struct PbrCase
	{
		float Normal[3];
		float Metallic;
		float View[3];
		float Roughness;
		float Light[3];
		float Reserved0;
		float BaseColor[3];
		float Reserved1;
	};

	static_assert(sizeof(PbrCase) == 64);
#endif

	// Critical-path items 59 and 60 on a real device.
	//  1. StandardPbr.slang's BRDF, evaluated by a compute probe for 96 random
	//     inputs, equals StandardPbr::EvaluateBrdf.
	//  2. Six GPU Scene quads are drawn GPU-driven with their GpuMaterialTable
	//     materials: sRGB base-color textures, metallic-roughness channels, a normal
	//     map with a derivative tangent frame, occlusion + emissive textures, an
	//     alpha-masked material and an out-of-range material index (the fallback).
	//     Every object's pixel equals StandardPbr::Shade within half-float tolerance.
	//  3. The next frame edits one material, releases another and adds a new one;
	//     only the changed rows upload and the image follows.
	void RunStandardMaterialSmoke(const Swim::Rhi::GraphicsSystemDesc& graphicsDesc)
	{
#ifndef SWIM_STANDARD_MATERIAL_SMOKE_AVAILABLE
		SWIM_REQUIRE_MESSAGE(false, "Standard material smoke requires generated Slang artifacts");
#else
		using namespace Swim;
		using namespace Swim::Render;
		using S = Rhi::ResourceState;

		const auto probeInterface = ReflectProgram(SWIM_RHI_STANDARD_PBR_REFLECTION_PATH);
		const auto cullInterface = ReflectProgram(SWIM_GPU_VISIBILITY_REFLECTION_PATH);
		const auto drawInterface = ReflectProgram(SWIM_RHI_STANDARD_MATERIAL_DRAW_REFLECTION_PATH);
		const auto probeBytes = ReadSpirv(SWIM_RHI_STANDARD_PBR_SPIRV_PATH);
		const auto cullBytes = ReadSpirv(SWIM_GPU_VISIBILITY_SPIRV_PATH);
		const auto drawBytes = ReadSpirv(SWIM_RHI_STANDARD_MATERIAL_DRAW_SPIRV_PATH);

		Platform::PlatformSystem platform;
		SWIM_REQUIRE_MESSAGE(platform.Initialize(), "Standard material smoke requires working SDL Vulkan platform services");
		auto graphics = RhiVulkan::CreateGraphicsSystem(graphicsDesc);
		SWIM_REQUIRE(graphics);
		Testing::RequireVulkanSmokeValidation(*graphics, graphicsDesc.Checks);
		const auto& capabilities = graphics->GetAdapter(0).GetInfo().Capabilities;
		SWIM_REQUIRE_MESSAGE(capabilities.BindlessDescriptors, "Adapter lacks bindless descriptor support");
		auto device = graphics->GetAdapter(0).CreateDevice();
		SWIM_REQUIRE(device);

		const auto makeCompute = [&](const ShaderCompiler::ShaderRhiInterfaceResult& reflected, const std::vector<std::byte>& bytes,
									 const char* label, std::unique_ptr<Rhi::ShaderProgram>& program,
									 std::unique_ptr<Rhi::PipelineLayout>& layout)
		{
			const auto& programInterface = reflected.Interface;
			const Rhi::ShaderStageArtifact stage{ Rhi::ShaderStageMask::Compute, "computeMain", bytes };
			program = device->CreateShaderProgram({ { &stage, 1 },
				{ programInterface.DescriptorSchemas, programInterface.PushConstants, programInterface.ComputeThreadGroupSize }, label });
			SWIM_REQUIRE(program);
			layout = device->CreatePipelineLayout({ program.get(), label });
			SWIM_REQUIRE(layout);
			auto pipeline = device->CreateComputePipeline({ program.get(), layout.get(), {}, label });
			SWIM_REQUIRE(pipeline);
			return pipeline;
		};
		std::unique_ptr<Rhi::ShaderProgram> probeProgram;
		std::unique_ptr<Rhi::PipelineLayout> probeLayout;
		auto probePipeline = makeCompute(probeInterface, probeBytes, "Standard PBR probe", probeProgram, probeLayout);
		std::unique_ptr<Rhi::ShaderProgram> cullProgram;
		std::unique_ptr<Rhi::PipelineLayout> cullLayout;
		auto cullPipeline = makeCompute(cullInterface, cullBytes, "GPU visibility", cullProgram, cullLayout);

		// Draw program: space 0 per draw, space 1 the shared bindless arrays.
		constexpr std::uint32_t textureCapacity = 32;
		constexpr std::uint32_t samplerCapacity = 8;
		Rhi::DescriptorSchemaDesc bindlessSpace{ 1,
			{ { 0, Rhi::DescriptorType::Sampler, samplerCapacity, Rhi::ShaderStageMask::None },
				{ 1, Rhi::DescriptorType::SampledTexture, textureCapacity, Rhi::ShaderStageMask::None } } };
		for (auto& binding : bindlessSpace.Bindings)
		{
			binding.Stages = Rhi::ShaderStageMask::Vertex | Rhi::ShaderStageMask::Fragment | Rhi::ShaderStageMask::Compute;
			binding.PartiallyBound = binding.UpdateAfterBind = true;
		}
		const auto& draw = drawInterface.Interface;
		const std::array<Rhi::ShaderStageArtifact, 2> drawStages{ { { Rhi::ShaderStageMask::Vertex, "vertexMain", drawBytes },
			{ Rhi::ShaderStageMask::Fragment, "fragmentMain", drawBytes } } };
		auto drawProgram =
			device->CreateShaderProgram({ drawStages, { draw.DescriptorSchemas, draw.PushConstants }, "Standard material draw" });
		SWIM_REQUIRE(drawProgram);
		auto drawLayout = device->CreatePipelineLayout({ drawProgram.get(), "Standard material layout", { &bindlessSpace, 1 } });
		SWIM_REQUIRE(drawLayout);
		const Rhi::Format colorFormat = Rhi::Format::RGBA16Float;
		Rhi::GraphicsPipelineDesc drawDesc{};
		drawDesc.Program = drawProgram.get();
		drawDesc.Layout = drawLayout.get();
		drawDesc.ColorFormats = { &colorFormat, 1 };
		drawDesc.DepthStencilFormat = CanonicalDepthFormat;
		drawDesc.DepthStencil.DepthTest = true;
		drawDesc.DepthStencil.DepthWrite = true;
		drawDesc.DepthStencil.DepthCompare = DepthCompareOp(DepthConvention::ReverseZ);
		drawDesc.Raster.Cull = Rhi::CullMode::None;
		drawDesc.DebugName = "Standard material draw";
		auto drawPipeline = device->CreateGraphicsPipeline(drawDesc);
		SWIM_REQUIRE(drawPipeline);

		RenderGraphExecutor executor(*device);

		// 1. BRDF probe against the CPU definition.
		{
			std::mt19937 random(60);
			std::uniform_real_distribution<float> unit(0.0f, 1.0f);
			const auto direction = [&](float maxTheta)
			{
				const float theta = unit(random) * maxTheta;
				const float phi = unit(random) * 6.2831853f;
				return Pbr::Float3{ std::sin(theta) * std::cos(phi), std::sin(theta) * std::sin(phi), std::cos(theta) };
			};
			std::vector<PbrCase> cases(96);
			for (auto& c : cases)
			{
				const auto n = direction(0.0f); // +Z normal; the other vectors vary.
				const auto v = direction(1.3f);
				const auto l = direction(1.3f);
				c = { { n[0], n[1], n[2] }, unit(random), { v[0], v[1], v[2] }, 0.05f + 0.95f * unit(random), { l[0], l[1], l[2] }, 0,
					{ unit(random), unit(random), unit(random) }, 0 };
			}
			RenderGraph graph;
			const auto input = graph.CreateUpload(std::as_bytes(std::span(cases)), "PBR cases", Rhi::BufferUsage::Storage, 16);
			const auto output = graph.CreateBuffer({ cases.size() * 16, Rhi::BufferUsage::Storage | Rhi::BufferUsage::TransferSource,
				Rhi::MemoryPreference::DeviceLocal, "PBR results" });
			graph.AddPass(
				"PBR probe", Rhi::QueueType::Compute,
				[&](RenderGraphBuilder& b)
				{
					b.Read(input, S::ShaderRead);
					b.Write(output, S::ShaderWrite);
				},
				[&](RenderCommandContext& c)
				{
					auto table = c.Device().CreateDescriptorTable({ probeLayout.get(), 0, 0, "PBR probe table" });
					SWIM_REQUIRE(table);
					const auto range = c.GetRange(input);
					std::array<Rhi::DescriptorWrite, 2> writes{};
					writes[0] = { 0, 0, range.Buffer, nullptr, nullptr, range.Offset, range.Size };
					writes[1] = { 1, 0, &c.Get(output), nullptr, nullptr, 0, 0 };
					table->Write(writes);
					auto& retained = static_cast<Rhi::DescriptorTable&>(c.Retain(std::move(table)));
					const std::array<std::uint32_t, 4> settings{ std::uint32_t(cases.size()), 0, 0, 0 };
					auto& list = c.Commands();
					list.BindComputePipeline(*probePipeline);
					list.BindDescriptorTable(0, retained);
					list.PushConstants(Rhi::ShaderStageMask::Compute, 0, std::as_bytes(std::span(settings)));
					list.Dispatch(2, 1, 1);
				});
			const auto readback = AddBufferReadback(graph, "PBR readback", output, 0, cases.size() * 16);
			executor.Execute(graph.Compile());
			executor.Wait();
			std::vector<std::array<float, 4>> results(cases.size());
			SWIM_REQUIRE(executor.TryReadback(readback.Buffer, std::as_writable_bytes(std::span(results))) == Rhi::ReadbackStatus::Ready);
			std::uint32_t nonzero = 0;
			for (std::size_t i = 0; i < cases.size(); ++i)
			{
				const auto& c = cases[i];
				const auto expected = Pbr::EvaluateBrdf({ { c.BaseColor[0], c.BaseColor[1], c.BaseColor[2] }, c.Metallic, c.Roughness },
					{ c.Normal[0], c.Normal[1], c.Normal[2] }, { c.View[0], c.View[1], c.View[2] }, { c.Light[0], c.Light[1], c.Light[2] });
				for (int channel = 0; channel < 3; ++channel)
				{
					SWIM_CHECK(std::abs(results[i][channel] - expected[channel]) <= 1.0e-5f + 2.0e-3f * std::abs(expected[channel]));
				}
				nonzero += expected[0] > 0.0f;
			}
			SWIM_CHECK(nonzero > 60u);
		}

		{
			// 2. Textures: 4x4 uniform colors, so filtering cannot change a sample.
			struct TextureSpec
			{
				Rhi::Format Format;
				std::array<std::uint8_t, 4> Texel;
			};

			const std::vector<TextureSpec> specs{
				{ Rhi::Format::RGBA8Unorm, { 255, 255, 255, 255 } },	// 0: fallback (white).
				{ Rhi::Format::RGBA8UnormSrgb, { 200, 120, 60, 255 } }, // 1: base color A.
				{ Rhi::Format::RGBA8UnormSrgb, { 40, 180, 220, 255 } }, // 2: base color B.
				{ Rhi::Format::RGBA8Unorm, { 0, 128, 200, 255 } },		// 3: metallic-roughness (G roughness, B metallic).
				{ Rhi::Format::RGBA8Unorm, { 166, 128, 230, 255 } },	// 4: tangent-space normal tilted toward +X.
				{ Rhi::Format::RGBA8Unorm, { 64, 0, 0, 255 } },			// 5: occlusion (R).
				{ Rhi::Format::RGBA8UnormSrgb, { 255, 128, 0, 255 } },	// 6: emissive.
				{ Rhi::Format::RGBA8UnormSrgb, { 250, 250, 250, 76 } }, // 7: base color with alpha 0.3.
			};
			std::vector<std::unique_ptr<Rhi::Texture>> textures;
			std::vector<std::unique_ptr<Rhi::TextureView>> views;
			for (std::size_t i = 0; i < specs.size(); ++i)
			{
				Rhi::TextureDesc desc{};
				desc.Extent = { 4, 4, 1 };
				desc.PixelFormat = specs[i].Format;
				desc.Usage = Rhi::TextureUsage::Sampled | Rhi::TextureUsage::TransferDestination;
				textures.push_back(device->CreateTexture(desc));
				SWIM_REQUIRE(textures.back());
				Rhi::TextureViewDesc view{};
				view.PixelFormat = specs[i].Format;
				views.push_back(device->CreateTextureView(*textures.back(), view));
				SWIM_REQUIRE(views.back());
			}
			Rhi::SamplerDesc samplerDesc{};
			samplerDesc.AddressU = samplerDesc.AddressV = Rhi::SamplerAddressMode::Repeat;
			auto sampler = device->CreateSampler(samplerDesc);
			SWIM_REQUIRE(sampler);
			BindlessTableDesc bindlessDesc;
			bindlessDesc.Layout = drawLayout.get();
			bindlessDesc.Space = 1;
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

			// Upload the textures once, before any frame samples them through the table.
			{
				RenderGraph uploads;
				for (std::size_t i = 0; i < textures.size(); ++i)
				{
					const auto texture = uploads.ImportTexture(*textures[i], S::Undefined);
					std::array<std::uint8_t, 64> texels{};
					for (std::size_t t = 0; t < 16; ++t)
					{
						std::memcpy(texels.data() + t * 4, specs[i].Texel.data(), 4);
					}
					AddTextureUpload(
						uploads, "Material texture upload", std::as_bytes(std::span(texels)), texture, { 0, {}, {}, { 4, 4, 1 } });
					uploads.Export(texture, S::ShaderRead);
				}
				executor.Execute(uploads.Compile());
				executor.Wait();
			}

			// Materials.
			const auto materialTemplate = CreateStandardMaterialTemplate();
			GpuMaterialTable materials(*device, { materialTemplate, 16, "Smoke materials" });
			const auto makeMaterial = [&]()
			{
				return std::make_shared<MaterialInstance>(materialTemplate);
			};
			auto textured = makeMaterial(); // Dielectric with an sRGB base-color texture.
			textured->SetTexture("BaseColorTexture", textureIndex[1]);
			textured->SetFloat("MetallicFactor", 0.0f);
			textured->SetFloat("RoughnessFactor", 0.6f);
			textured->SetSampler("MaterialSampler", samplerIndex);
			auto metal = makeMaterial(); // Metallic-roughness texture channels.
			metal->SetVector("BaseColorFactor", std::array<float, 4>{ 0.9f, 0.6f, 0.3f, 1.0f });
			metal->SetTexture("MetallicRoughnessTexture", textureIndex[3]);
			metal->SetSampler("MaterialSampler", samplerIndex);
			auto normalMapped = makeMaterial(); // Normal map.
			normalMapped->SetTexture("BaseColorTexture", textureIndex[2]);
			normalMapped->SetTexture("NormalTexture", textureIndex[4]);
			normalMapped->SetFloat("MetallicFactor", 0.0f);
			normalMapped->SetFloat("RoughnessFactor", 0.35f);
			normalMapped->SetSampler("MaterialSampler", samplerIndex);
			auto glowing = makeMaterial(); // Occlusion + emission.
			glowing->SetVector("BaseColorFactor", std::array<float, 4>{ 0.5f, 0.5f, 0.5f, 1.0f });
			glowing->SetFloat("MetallicFactor", 0.0f);
			glowing->SetFloat("RoughnessFactor", 0.8f);
			glowing->SetTexture("OcclusionTexture", textureIndex[5]);
			glowing->SetFloat("OcclusionStrength", 0.8f);
			glowing->SetTexture("EmissiveTexture", textureIndex[6]);
			glowing->SetVector("EmissiveFactor", std::array<float, 3>{ 0.5f, 0.5f, 0.5f });
			glowing->SetSampler("MaterialSampler", samplerIndex);
			auto masked = makeMaterial(); // Alpha 0.3 below the 0.5 cutoff: discarded.
			masked->SetTexture("BaseColorTexture", textureIndex[7]);
			masked->SetUint("Flags", Pbr::FlagAlphaMask);
			masked->SetSampler("MaterialSampler", samplerIndex);
			std::vector<std::shared_ptr<MaterialInstance>> instances{ textured, metal, normalMapped, glowing, masked };
			std::vector<GpuMaterialHandle> handles;
			for (const auto& instance : instances)
			{
				handles.push_back(materials.Create(instance));
			}

			// Geometry and GPU Scene: six 6x6 quads in a 3x2 grid; object 5 uses an
			// out-of-range material index and shades with the fallback.
			GeometryHeapDesc heapDesc;
			heapDesc.VertexPageSize = 64 * 1024;
			heapDesc.IndexPageSize = 64 * 1024;
			heapDesc.MeshletPageSize = 4096;
			heapDesc.MaxMeshes = 4;
			heapDesc.MaxSubmeshes = 8;
			heapDesc.MaxPages = 8;
			GeometryHeap heap(*device, heapDesc);
			const std::array<float, 12> positions{ -1, -1, 0, 1, -1, 0, 1, 1, 0, -1, 1, 0 };
			const std::array<std::uint32_t, 6> indices{ 0, 1, 2, 0, 2, 3 };
			const std::array<GeometrySubmesh, 1> submeshes{ { { 0, 6, 0, 0 } } };
			const std::array<GeometryLodRange, 1> lods{ { { 0, 1, 0.0f } } };
			GeometryMeshDesc meshDesc;
			meshDesc.VertexStride = 12;
			meshDesc.Vertices = std::as_bytes(std::span(positions));
			meshDesc.IndexFormat = Rhi::IndexType::Uint32;
			meshDesc.Indices = std::as_bytes(std::span(indices));
			meshDesc.Submeshes = submeshes;
			meshDesc.Lods = lods;
			meshDesc.DebugName = "Material quad";
			const auto quad = heap.CreateMesh(meshDesc);
			const auto& quadMeta = *heap.GetMetadata(quad);
			constexpr std::uint32_t objectCount = 6;
			constexpr float half = 3.0f;
			const auto centerOf = [](std::uint32_t object)
			{
				return std::array<float, 2>{ float(object % 3) * 8.0f - 8.0f, object < 3 ? 4.0f : -4.0f };
			};
			GpuScene scene(*device, { objectCount, "Material scene" });
			std::vector<RenderObjectHandle> objects;
			for (std::uint32_t object = 0; object < objectCount; ++object)
			{
				const auto center = centerOf(object);
				RenderObjectDesc desc;
				desc.Transform.Rows = { half, 0, 0, center[0], 0, half, 0, center[1], 0, 0, half, 0 };
				desc.Mesh = quad;
				desc.LocalBounds = RenderBounds::FromMinMax({ -1, -1, 0 }, { 1, 1, 0 });
				desc.MaterialSet = object < 5 ? materials.GetIndex(handles[object]) : 999u;
				desc.ObjectId = object;
				objects.push_back(scene.Create(desc));
			}

			GpuVisibilityDesc visibilityDesc;
			visibilityDesc.CullPipeline = cullPipeline.get();
			visibilityDesc.Layout = cullLayout.get();
			visibilityDesc.Space = cullInterface.Interface.DescriptorSchemas[0].Space;
			visibilityDesc.MaxObjects = objectCount;
			visibilityDesc.MaxMaterialSets = 1;
			visibilityDesc.MaterialBinCapacities = { 16 };
			GpuVisibility visibility(*device, visibilityDesc);
			const auto path = SelectVisibilityDrawPath(capabilities);

			// A 24x12 orthographic window at 256x128, reverse-Z.
			constexpr std::uint32_t width = 256;
			constexpr std::uint32_t height = 128;
			RenderViewDesc viewDesc;
			const std::array<float, 16> lookDown{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, -10, 0, 0, 0, 1 };
			viewDesc.ViewProjection = MultiplyRowMajor(OrthographicReverseZRowMajor(-12, 12, -6, 6, 0.1f, 100.0f), lookDown);
			viewDesc.CameraPosition = { 0, 0, 10 };
			Pbr::Lighting lighting;
			lighting.View = { 0, 0, 1 };
			lighting.LightDirection = Pbr::Normalize({ 0.4f, 0.3f, 1.0f });
			lighting.LightRadiance = { 2.5f, 2.5f, 2.5f };
			lighting.Ambient = { 0.05f, 0.05f, 0.05f };
			ShadingView shadingView{};
			std::memcpy(shadingView.ViewProjection, viewDesc.ViewProjection.data(), sizeof(shadingView.ViewProjection));
			for (int c = 0; c < 3; ++c)
			{
				shadingView.ViewDirection[c] = lighting.View[c];
				shadingView.LightDirection[c] = lighting.LightDirection[c];
				shadingView.LightRadiance[c] = lighting.LightRadiance[c];
				shadingView.Ambient[c] = lighting.Ambient[c];
			}

			// CPU texels as the GPU decodes them.
			const auto texelsOf = [&](const MaterialInstance& instance)
			{
				const auto indices = ReadStandardTextures(instance);
				const auto spec = [&](std::uint32_t bindlessIndex) -> const TextureSpec&
				{
					for (std::size_t i = 1; i < specs.size(); ++i)
					{
						if (textureIndex[i] == bindlessIndex)
						{
							return specs[i];
						}
					}
					return specs[0];
				};
				const auto decode = [&](std::uint32_t bindlessIndex, int channel)
				{
					const auto& s = spec(bindlessIndex);
					const float encoded = float(s.Texel[channel]) / 255.0f;
					return s.Format == Rhi::Format::RGBA8UnormSrgb && channel < 3 ? Pbr::SrgbToLinear(encoded) : encoded;
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
					texels.TangentNormal = Pbr::Float3{ decode(indices.Normal, 0) * 2 - 1, decode(indices.Normal, 1) * 2 - 1,
						decode(indices.Normal, 2) * 2 - 1 };
				}
				texels.Occlusion = decode(indices.Occlusion, 0);
				return texels;
			};

			Rhi::TimelinePoint lastCompletion{};
			std::shared_ptr<MaterialInstance> fallbackInstance = makeMaterial();
			const auto frame = [&](const std::vector<std::shared_ptr<MaterialInstance>>& expectedMaterials)
			{
				RenderGraph graph;
				const auto materialResources = materials.Import(graph);
				const auto sceneResources = scene.Import(graph);
				const auto geometry = heap.Import(graph);
				VisibilityFrameDesc frameDesc;
				frameDesc.View = BuildGpuViewRecord(viewDesc);
				frameDesc.IndexPages = { quadMeta.IndexPage };
				frameDesc.ZeroUnusedCommands = NeedsZeroedCommands(path);
				const auto visible = visibility.Record(graph, sceneResources, geometry, frameDesc);
				shadingView.MaterialCount = materialResources.MaterialCount;
				const auto viewUpload =
					graph.CreateUpload(std::as_bytes(std::span(&shadingView, 1)), "Shading view", Rhi::BufferUsage::Storage, 16);
				const auto target = graph.CreateTexture({ Rhi::TextureDimension::Texture2D, { width, height, 1 }, colorFormat,
					Rhi::TextureUsage::ColorAttachment | Rhi::TextureUsage::TransferSource, 1, 1, Rhi::SampleCount::X1,
					"Material target" });
				const auto depth = graph.CreateTexture({ Rhi::TextureDimension::Texture2D, { width, height, 1 }, CanonicalDepthFormat,
					Rhi::TextureUsage::DepthStencilAttachment, 1, 1, Rhi::SampleCount::X1, "Material depth" });
				const auto vertexPage = geometry.Pages[quadMeta.VertexPage];
				const auto indexPage = geometry.Pages[quadMeta.IndexPage];
				graph.AddPass(
					"Standard material draw", Rhi::QueueType::Graphics,
					[&](RenderGraphBuilder& b)
					{
						b.Read(visible.Commands, S::IndirectArgument);
						b.Read(visible.Counts, S::IndirectArgument);
						b.Read(visible.DrawRecords, S::ShaderRead);
						b.Read(sceneResources.Instances, S::ShaderRead);
						b.Read(sceneResources.Transforms, S::ShaderRead);
						b.Read(vertexPage, S::ShaderRead);
						b.Read(indexPage, S::IndexBuffer);
						b.Read(viewUpload, S::ShaderRead);
						b.Read(materialResources.Materials, S::ShaderRead);
						b.Write(target, S::ColorAttachment);
						b.Write(depth, S::DepthStencilWrite);
					},
					[&](RenderCommandContext& c)
					{
						auto table = c.Device().CreateDescriptorTable({ drawLayout.get(), 0, 0, "Material draw table" });
						SWIM_REQUIRE(table);
						std::array<Rhi::DescriptorWrite, 6> writes{};
						for (std::uint32_t binding = 0; binding < writes.size(); ++binding)
						{
							writes[binding].Binding = binding;
						}
						writes[0].BufferResource = &c.Get(sceneResources.Instances);
						writes[1].BufferResource = &c.Get(sceneResources.Transforms);
						writes[2].BufferResource = &c.Get(visible.DrawRecords);
						writes[3].BufferResource = &c.Get(vertexPage);
						const auto viewRange = c.GetRange(viewUpload);
						writes[4].BufferResource = viewRange.Buffer;
						writes[4].BufferOffset = viewRange.Offset;
						writes[4].BufferRange = viewRange.Size;
						writes[5].BufferResource = &c.Get(materialResources.Materials);
						table->Write(writes);
						auto& retained = static_cast<Rhi::DescriptorTable&>(c.Retain(std::move(table)));
						Rhi::RenderingAttachmentDesc color{};
						color.View = &c.CreateView(target);
						color.Load = Rhi::LoadOp::Clear;
						Rhi::TextureViewDesc depthView;
						depthView.PixelFormat = CanonicalDepthFormat;
						Rhi::DepthStencilAttachmentDesc depthAttachment{ &c.CreateView(depth, depthView), Rhi::LoadOp::Clear,
							Rhi::StoreOp::Discard, DepthClearValue(DepthConvention::ReverseZ), 0 };
						auto& commands = c.Commands();
						commands.BeginRendering({ { &color, 1 }, &depthAttachment, { width, height } });
						commands.BindGraphicsPipeline(*drawPipeline);
						commands.BindDescriptorTable(0, retained);
						commands.BindDescriptorTable(1, bindless.GetTable());
						commands.SetViewport({ 0, 0, float(width), float(height) });
						commands.SetScissor({ 0, 0, width, height });
						commands.BindIndexBuffer(c.Get(indexPage), 0, Rhi::IndexType::Uint32);
						DrawVisibilityBin(commands, c.Get(visible.Commands), c.Get(visible.Counts), visibility.GetBins(), 0, path);
						commands.EndRendering();
					});
				const auto image = AddTextureReadback(graph, "Material image", target, { 0, {}, {}, { width, height, 1 } });
				const auto completion = executor.Execute(graph.Compile());
				lastCompletion = completion;
				scene.CommitUploads();
				heap.CommitUploads(completion);
				materials.CommitUploads();
				executor.Wait();
				const auto uploaded = materials.GetStats().LastUploadRows;

				std::vector<std::uint16_t> pixels(std::size_t(width) * height * 4);
				SWIM_REQUIRE(executor.TryReadback(image.Buffer, std::as_writable_bytes(std::span(pixels))) == Rhi::ReadbackStatus::Ready);
				const auto pixel = [&](float wx, float wy)
				{
					const std::uint32_t x = std::uint32_t((wx + 12.0f) / 24.0f * float(width));
					const std::uint32_t y = std::uint32_t((0.5f - wy / 12.0f) * float(height)); // +Y-up NDC.
					std::array<float, 4> value{};
					for (int c = 0; c < 4; ++c)
					{
						value[c] = HalfToFloat(pixels[(std::size_t(y) * width + x) * 4 + c]);
					}
					return value;
				};
				for (std::uint32_t object = 0; object < objectCount; ++object)
				{
					const auto& instance =
						object < expectedMaterials.size() && expectedMaterials[object] ? *expectedMaterials[object] : *fallbackInstance;
					const auto expected = Pbr::Shade(ReadStandardParameters(instance), texelsOf(instance), Pbr::Frame{}, lighting);
					const auto center = centerOf(object);
					const auto actual = pixel(center[0] + 0.3f, center[1] + 0.2f);
					if (!expected)
					{
						SWIM_CHECK((actual == std::array<float, 4>{ 0, 0, 0, 0 })); // Discarded: the clear color remains.
						continue;
					}
					for (int c = 0; c < 4; ++c)
					{
						const float tolerance = 4.0e-3f + 1.2e-2f * std::abs((*expected)[c]);
						if (std::abs(actual[c] - (*expected)[c]) > tolerance)
						{
							std::printf("             object %u channel %d: GPU %.5f, CPU %.5f\n", object, c, actual[c], (*expected)[c]);
						}
						SWIM_CHECK(std::abs(actual[c] - (*expected)[c]) <= tolerance);
					}
				}
				// Between the quads nothing is drawn.
				SWIM_CHECK((pixel(-4.0f, 0.0f) == std::array<float, 4>{ 0, 0, 0, 0 }));
				return uploaded;
			};

			// Frame 1: every row uploads (16 rows including the fallback and unused rows).
			std::vector<std::shared_ptr<MaterialInstance>> expected{ textured, metal, normalMapped, glowing, masked, nullptr };
			SWIM_CHECK_EQUAL(frame(expected), 16u);

			// Frame 2: edit one material, retire another and route its object to a new one.
			textured->SetVector("BaseColorFactor", std::array<float, 4>{ 0.5f, 1.0f, 0.5f, 1.0f });
			textured->SetTexture("BaseColorTexture", textureIndex[2]);
			auto replacement = makeMaterial();
			replacement->SetVector("BaseColorFactor", std::array<float, 4>{ 0.8f, 0.1f, 0.1f, 1.0f });
			replacement->SetFloat("MetallicFactor", 0.0f);
			replacement->SetFloat("RoughnessFactor", 0.3f);
			const auto replacementHandle = materials.Create(replacement);
			SWIM_CHECK(materials.Release(handles[1], lastCompletion));
			scene.SetMaterialSet(objects[1], materials.GetIndex(replacementHandle));
			expected[1] = replacement;
			SWIM_CHECK_EQUAL(frame(expected), 2u); // The edited and the new material only.
			SWIM_CHECK_EQUAL(materials.Collect(), 1u);
			SWIM_CHECK_EQUAL(frame(expected), 1u); // The retired row returns to the defaults.

			scene.Collect();
			scene.Drain();
			heap.Drain();
			materials.Drain();
			bindless.Drain();
		}
		executor.Trim();
#endif
	}

	[[maybe_unused]] const bool registered = []
	{
		const char* enabled = std::getenv("SWIM_RUN_RHI_SMOKE");
		if (enabled != nullptr && std::string_view(enabled) == "1")
		{
			Swim::Testing::TestRegistry::Get().Add(
				{ "RHI.Vulkan.Smoke", "StandardMaterialsShadeFromTheGpuMaterialTable", SWIM_TEST_LOCATION,
					+[]
					{
						Swim::Testing::RunValidatedVulkanSmoke(&RunStandardMaterialSmoke);
					} });
		}
		return true;
	}();
} // namespace
