#include "Engine/Platform/PlatformSystem.h"
#include "Engine/Systems/Renderer/Geometry/GeometryHeap.h"
#include "Engine/Systems/Renderer/GpuScene/GpuScene.h"
#include "Engine/Systems/Renderer/RHI/Backends/Vulkan/VulkanRhiBackend.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderCommandContext.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphExecutor.h"
#include "Engine/Systems/Renderer/RenderGraph/RenderGraphTransfers.h"
#include "Engine/Systems/Renderer/Visibility/GpuVisibility.h"
#include "Engine/Systems/Renderer/Visibility/HzbBuilder.h"
#include "Engine/Systems/Renderer/Visibility/HzbPyramid.h"
#include "Engine/Systems/Renderer/Visibility/RenderViewDesc.h"
#include "Engine/Systems/Renderer/Visibility/VisibilityReference.h"
#include "Tests/Fixtures/VulkanSmokeDiagnostics.h"
#include "Tests/Framework/Test.h"

#if defined(SWIM_GPU_VISIBILITY_SPIRV_PATH) && defined(SWIM_RHI_GPU_DRIVEN_DRAW_SPIRV_PATH) && defined(SWIM_HZB_REDUCE_SPIRV_PATH)
#include "Tools/ShaderCompiler/ShaderRhiInterface.h"
#define SWIM_GPU_OCCLUSION_SMOKE_AVAILABLE 1
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <string_view>
#include <vector>

namespace
{
#ifdef SWIM_GPU_OCCLUSION_SMOKE_AVAILABLE
	std::vector<std::byte> ReadProgram(const char* path)
	{
		std::ifstream file(path, std::ios::binary | std::ios::ate);
		SWIM_REQUIRE(file);
		std::vector<std::byte> bytes(static_cast<std::size_t>(file.tellg()));
		file.seekg(0);
		file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		SWIM_REQUIRE(file && !bytes.empty());
		return bytes;
	}

	Swim::ShaderCompiler::ShaderRhiInterfaceResult LoadProgramInterface(const char* path)
	{
		const auto reflection = Swim::ShaderCompiler::LoadSlangReflectionJson(path);
		SWIM_REQUIRE_MESSAGE(reflection, reflection.Error);
		auto converted = Swim::ShaderCompiler::BuildRhiShaderInterface(reflection.Reflection);
		SWIM_REQUIRE_MESSAGE(converted, converted.Error);
		return converted;
	}

	std::set<std::uint32_t> DrawnRows(const Swim::Render::VisibilityReferenceResult& result)
	{
		std::set<std::uint32_t> rows;
		for (const auto& bin : result.Bins)
		{
			for (const auto& draw : bin)
			{
				rows.insert(draw.Record.InstanceRow);
			}
		}
		return rows;
	}
#endif

	// Critical-path items 50 and 51 end to end on a real device. A 20x20 grid of GPU
	// Scene quads sits behind a large wall quad; every frame runs the full two-phase
	// pipeline with reverse-Z depth:
	//   early cull -> indirect draw (clear color/depth) -> HZB (HzbReduce.slang)
	//   -> late cull against that HZB -> indirect draw (load color/depth).
	// Over five frames (fresh history, steady state, the wall teleporting away, the
	// wall back plus a camera cut) the early/late statistics and draw sets equal
	// RunVisibilityReference fed with the GPU's own HZB, every HZB mip equals the CPU
	// reduction of the mip below, objects hidden behind the wall stop being drawn, and
	// no object is ever missing from the image: each sample pixel shows the object or
	// the wall in front of it, and revealed objects appear in the same frame.
	void RunGpuOcclusionSmoke(const Swim::Rhi::GraphicsSystemDesc& graphicsDesc)
	{
#ifndef SWIM_GPU_OCCLUSION_SMOKE_AVAILABLE
		SWIM_REQUIRE_MESSAGE(false, "GPU occlusion smoke requires generated Slang artifacts");
#else
		using namespace Swim;
		using namespace Swim::Render;
		using S = Rhi::ResourceState;

		const auto cullInterface = LoadProgramInterface(SWIM_GPU_VISIBILITY_REFLECTION_PATH);
		const auto hzbInterface = LoadProgramInterface(SWIM_HZB_REDUCE_REFLECTION_PATH);
		const auto drawInterface = LoadProgramInterface(SWIM_RHI_GPU_DRIVEN_DRAW_REFLECTION_PATH);
		const auto cullBytes = ReadProgram(SWIM_GPU_VISIBILITY_SPIRV_PATH);
		const auto hzbBytes = ReadProgram(SWIM_HZB_REDUCE_SPIRV_PATH);
		const auto drawBytes = ReadProgram(SWIM_RHI_GPU_DRIVEN_DRAW_SPIRV_PATH);

		Platform::PlatformSystem platform;
		SWIM_REQUIRE_MESSAGE(platform.Initialize(), "GPU occlusion smoke requires working SDL Vulkan platform services");
		auto graphics = RhiVulkan::CreateGraphicsSystem(graphicsDesc);
		SWIM_REQUIRE(graphics);
		Testing::RequireVulkanSmokeValidation(*graphics, graphicsDesc.Checks);
		SWIM_REQUIRE(graphics->GetAdapter(0).GetInfo().Capabilities.IndirectCount);
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
		std::unique_ptr<Rhi::ShaderProgram> cullProgram;
		std::unique_ptr<Rhi::PipelineLayout> cullLayout;
		auto cullPipeline = makeCompute(cullInterface, cullBytes, "GPU visibility", cullProgram, cullLayout);
		std::unique_ptr<Rhi::ShaderProgram> hzbProgram;
		std::unique_ptr<Rhi::PipelineLayout> hzbLayout;
		auto hzbPipeline = makeCompute(hzbInterface, hzbBytes, "HZB reduce", hzbProgram, hzbLayout);

		// Draw program with canonical reverse-Z depth testing.
		const auto& draw = drawInterface.Interface;
		const std::array<Rhi::ShaderStageArtifact, 2> drawStages{ { { Rhi::ShaderStageMask::Vertex, "vertexMain", drawBytes },
			{ Rhi::ShaderStageMask::Fragment, "fragmentMain", drawBytes } } };
		auto drawProgram = device->CreateShaderProgram({ drawStages, { draw.DescriptorSchemas, draw.PushConstants }, "GPU-driven draw" });
		SWIM_REQUIRE(drawProgram);
		auto drawLayout = device->CreatePipelineLayout({ drawProgram.get(), "GPU-driven draw layout" });
		SWIM_REQUIRE(drawLayout);
		const Rhi::Format colorFormat = Rhi::Format::RGBA8Unorm;
		Rhi::GraphicsPipelineDesc drawDesc{};
		drawDesc.Program = drawProgram.get();
		drawDesc.Layout = drawLayout.get();
		drawDesc.ColorFormats = { &colorFormat, 1 };
		drawDesc.DepthStencilFormat = CanonicalDepthFormat;
		drawDesc.DepthStencil.DepthTest = true;
		drawDesc.DepthStencil.DepthWrite = true;
		drawDesc.DepthStencil.DepthCompare = DepthCompareOp(DepthConvention::ReverseZ);
		drawDesc.Raster.Cull = Rhi::CullMode::None;
		drawDesc.DebugName = "GPU-driven depth draw";
		auto drawPipeline = device->CreateGraphicsPipeline(drawDesc);
		SWIM_REQUIRE(drawPipeline);
		const auto drawSpace = draw.DescriptorSchemas[0].Space;

		RenderGraphExecutor executor(*device);
		{
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
			meshDesc.DebugName = "Occlusion quad";
			const auto quad = heap.CreateMesh(meshDesc);
			const auto& quadMeta = *heap.GetMetadata(quad);

			// Grid at z = 0 (ids 0..399) and a 24x24 wall at z = 5 (id 400) in front of its middle.
			constexpr std::uint32_t side = 20;
			constexpr float spacing = 2.5f;
			constexpr float wallHalf = 12.0f;
			constexpr float center = 18.75f;
			GpuScene scene(*device, { side * side + 1, "Occlusion scene" });
			for (std::uint32_t j = 0; j < side; ++j)
			{
				for (std::uint32_t i = 0; i < side; ++i)
				{
					RenderObjectDesc desc;
					desc.Transform = RenderAffine::Translation(float(i) * spacing, float(j) * spacing, 0.0f);
					desc.Mesh = quad;
					desc.LocalBounds = RenderBounds::FromMinMax({ -1, -1, 0 }, { 1, 1, 0 });
					desc.MaterialSet = 0;
					desc.ObjectId = j * side + i;
					scene.Create(desc);
				}
			}
			const auto wallAt = [&](float x)
			{
				RenderAffine transform;
				transform.Rows = { wallHalf, 0, 0, x, 0, wallHalf, 0, center, 0, 0, 1, 5.0f };
				return transform;
			};
			RenderObjectDesc wallDesc;
			wallDesc.Transform = wallAt(center);
			wallDesc.Mesh = quad;
			wallDesc.LocalBounds = RenderBounds::FromMinMax({ -1, -1, 0 }, { 1, 1, 0 });
			wallDesc.MaterialSet = 0;
			wallDesc.ObjectId = side * side;
			const auto wall = scene.Create(wallDesc);

			GpuVisibilityDesc visibilityDesc;
			visibilityDesc.CullPipeline = cullPipeline.get();
			visibilityDesc.Layout = cullLayout.get();
			visibilityDesc.Space = cullInterface.Interface.DescriptorSchemas[0].Space;
			visibilityDesc.MaxObjects = side * side + 1;
			visibilityDesc.MaxMaterialSets = 1;
			visibilityDesc.MaterialBinCapacities = { 1024 };
			visibilityDesc.IndexPageSlots = 1;
			GpuVisibility visibility(*device, visibilityDesc);
			const HzbBuilder hzbBuilder(
				{ hzbPipeline.get(), hzbLayout.get(), hzbInterface.Interface.DescriptorSchemas[0].Space, "Smoke HZB" });
			const auto& bins = visibility.GetBins();
			const std::vector<std::uint32_t> materialBins{ 0 };
			const std::vector<std::uint32_t> pages{ quadMeta.IndexPage };
			std::vector<GpuLodState> referenceLods;
			std::vector<std::uint32_t> referenceHistory;

			constexpr std::uint32_t size = 256;
			const auto hzbMips = ComputeHzbMips(size, size);
			std::uint32_t revealed = 0;
			// Returns {early drawn, late drawn, late occluded}.
			const auto frame = [&](std::uint32_t flags, float wallX)
			{
				RenderViewDesc viewDesc;
				const std::array<float, 16> view{ 1, 0, 0, -center, 0, 1, 0, -center, 0, 0, 1, -10, 0, 0, 0, 1 };
				viewDesc.ViewProjection = MultiplyRowMajor(OrthographicReverseZRowMajor(-20, 20, -20, 20, 0.1f, 100.0f), view);
				viewDesc.CameraPosition = { center, center, 10.0f };
				viewDesc.Depth = DepthConvention::ReverseZ;
				viewDesc.Flags = flags;
				VisibilityFrameDesc frameDesc;
				frameDesc.View = BuildGpuViewRecord(viewDesc);
				frameDesc.IndexPages = pages;

				RenderGraph graph;
				const auto sceneResources = scene.Import(graph);
				const auto geometry = heap.Import(graph);
				const auto viewUpload =
					graph.CreateUpload(std::as_bytes(std::span(&frameDesc.View, 1)), "Draw view", Rhi::BufferUsage::Storage, 16);
				const auto target = graph.CreateTexture({ Rhi::TextureDimension::Texture2D, { size, size, 1 }, colorFormat,
					Rhi::TextureUsage::ColorAttachment | Rhi::TextureUsage::TransferSource, 1, 1, Rhi::SampleCount::X1,
					"Occlusion target" });
				const auto depth = graph.CreateTexture({ Rhi::TextureDimension::Texture2D, { size, size, 1 }, CanonicalDepthFormat,
					Rhi::TextureUsage::DepthStencilAttachment | Rhi::TextureUsage::Sampled, 1, 1, Rhi::SampleCount::X1,
					"Occlusion depth" });
				const auto vertexPage = geometry.Pages[quadMeta.VertexPage];
				const auto indexPage = geometry.Pages[quadMeta.IndexPage];

				const auto drawPhase = [&](const char* name, const VisibilityGraphResources& visible, bool first)
				{
					graph.AddPass(
						name, Rhi::QueueType::Graphics,
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
							if (first)
							{
								b.Write(target, S::ColorAttachment);
								b.Write(depth, S::DepthStencilWrite);
							}
							else
							{
								b.ReadWrite(target, S::ColorAttachment);
								b.ReadWrite(depth, S::DepthStencilWrite);
							}
						},
						[&, visible, first](RenderCommandContext& c)
						{
							auto table = c.Device().CreateDescriptorTable({ drawLayout.get(), drawSpace, 0, "Occlusion draw table" });
							SWIM_REQUIRE(table);
							std::array<Rhi::DescriptorWrite, 5> writes{};
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
							table->Write(writes);
							auto& retained = static_cast<Rhi::DescriptorTable&>(c.Retain(std::move(table)));
							Rhi::RenderingAttachmentDesc color{};
							color.View = &c.CreateView(target);
							color.Load = first ? Rhi::LoadOp::Clear : Rhi::LoadOp::Load;
							Rhi::TextureViewDesc depthView;
							depthView.PixelFormat = CanonicalDepthFormat;
							Rhi::DepthStencilAttachmentDesc depthAttachment{ &c.CreateView(depth, depthView),
								first ? Rhi::LoadOp::Clear : Rhi::LoadOp::Load, Rhi::StoreOp::Store,
								DepthClearValue(DepthConvention::ReverseZ), 0 };
							auto& commands = c.Commands();
							commands.BeginRendering({ { &color, 1 }, &depthAttachment, { size, size } });
							commands.BindGraphicsPipeline(*drawPipeline);
							commands.BindDescriptorTable(drawSpace, retained);
							commands.SetViewport({ 0, 0, float(size), float(size) });
							commands.SetScissor({ 0, 0, size, size });
							commands.BindIndexBuffer(c.Get(indexPage), 0, Rhi::IndexType::Uint32);
							const auto& range = bins.GetRange(0);
							commands.DrawIndexedIndirectCount(c.Get(visible.Commands), 0, c.Get(visible.Counts), 0, range.Capacity);
							commands.EndRendering();
						});
				};

				frameDesc.Phase = VisibilityPhase::Early;
				const auto early = visibility.Record(graph, sceneResources, geometry, frameDesc);
				drawPhase("Early draws", early, true);
				const auto hzb = hzbBuilder.Record(graph, depth, DepthConvention::ReverseZ);
				frameDesc.Phase = VisibilityPhase::Late;
				frameDesc.Hzb = &hzb;
				const auto late = visibility.Record(graph, sceneResources, geometry, frameDesc);
				drawPhase("Late draws", late, false);

				const auto capacity = bins.GetTotalCapacity();
				const auto earlyRecords =
					AddBufferReadback(graph, "Early records", early.DrawRecords, 0, std::uint64_t(capacity) * sizeof(GpuDrawRecord));
				const auto lateRecords =
					AddBufferReadback(graph, "Late records", late.DrawRecords, 0, std::uint64_t(capacity) * sizeof(GpuDrawRecord));
				const auto earlyCounts = AddBufferReadback(graph, "Early counts", early.Counts, 0, sizeof(std::uint32_t));
				const auto lateCounts = AddBufferReadback(graph, "Late counts", late.Counts, 0, sizeof(std::uint32_t));
				const auto image = AddTextureReadback(graph, "Image readback", target, { 0, {}, {}, { size, size, 1 } });
				std::vector<GraphReadback> mipReadbacks;
				for (std::uint32_t mip = 0; mip < hzb.MipCount; ++mip)
				{
					mipReadbacks.push_back(AddTextureReadback(
						graph, "HZB readback", hzb.Pyramid, { 0, { mip, 0 }, {}, { hzbMips[mip].Width, hzbMips[mip].Height, 1 } }));
				}
				const auto completion = executor.Execute(graph.Compile());
				scene.CommitUploads();
				heap.CommitUploads(completion);
				executor.Wait();

				const auto read = [&](const GraphReadback& readback, std::span<std::byte> bytes)
				{
					SWIM_REQUIRE(executor.TryReadback(readback.Buffer, bytes) == Rhi::ReadbackStatus::Ready);
				};
				VisibilityStats earlyStats;
				VisibilityStats lateStats;
				SWIM_REQUIRE(early.StatsReadback.has_value() && late.StatsReadback.has_value());
				read(*early.StatsReadback, std::as_writable_bytes(std::span(&earlyStats, 1)));
				read(*late.StatsReadback, std::as_writable_bytes(std::span(&lateStats, 1)));
				std::uint32_t earlyCount = 0;
				std::uint32_t lateCount = 0;
				read(earlyCounts, std::as_writable_bytes(std::span(&earlyCount, 1)));
				read(lateCounts, std::as_writable_bytes(std::span(&lateCount, 1)));
				std::vector<GpuDrawRecord> earlyDraws(capacity);
				std::vector<GpuDrawRecord> lateDraws(capacity);
				read(earlyRecords, std::as_writable_bytes(std::span(earlyDraws)));
				read(lateRecords, std::as_writable_bytes(std::span(lateDraws)));
				std::vector<std::uint8_t> pixels(std::size_t(size) * size * 4);
				read(image, std::as_writable_bytes(std::span(pixels)));
				std::vector<std::vector<float>> mips;
				for (std::uint32_t mip = 0; mip < hzb.MipCount; ++mip)
				{
					mips.emplace_back(std::size_t(hzbMips[mip].Width) * hzbMips[mip].Height);
					read(mipReadbacks[mip], std::as_writable_bytes(std::span(mips.back())));
				}

				// The HZB: every mip is the exact farthest-depth reduction of the one below;
				// mip 0 only holds clear, grid or wall depths.
				const float gridDepth = (0.0f - 10.0f + 100.0f) / 99.9f;
				const float wallDepth = (5.0f - 10.0f + 100.0f) / 99.9f;
				for (const float value : mips[0])
				{
					SWIM_CHECK(value == 0.0f || std::abs(value - gridDepth) < 1.0e-5f || std::abs(value - wallDepth) < 1.0e-5f);
				}
				for (std::uint32_t mip = 1; mip < hzb.MipCount; ++mip)
				{
					SWIM_CHECK(mips[mip] == HzbReference::Reduce(mips[mip - 1], hzbMips[mip - 1], hzbMips[mip], DepthConvention::ReverseZ));
				}
				const auto gpuHzb = HzbReference::FromMips(size, size, DepthConvention::ReverseZ, mips);

				// The CPU definition over the same rows and the GPU's own HZB.
				std::vector<GpuInstanceRecord> instances;
				std::vector<GpuTransformRecord> transforms;
				for (std::uint32_t row = 0; row < sceneResources.RowCount; ++row)
				{
					instances.push_back(scene.GetInstanceRow(row));
					transforms.push_back(scene.GetTransformRow(row));
				}
				std::vector<GpuMeshMetadata> meshes(quad.Index + 1);
				meshes[quad.Index] = quadMeta;
				const auto quadSubmeshes = heap.GetSubmeshes(quad);
				std::vector<GpuSubmeshRecord> submeshRows(quadMeta.FirstSubmesh + quadSubmeshes.size());
				std::copy(quadSubmeshes.begin(), quadSubmeshes.end(), submeshRows.begin() + quadMeta.FirstSubmesh);
				VisibilityReferenceInputs inputs{ instances, transforms, meshes, submeshRows, frameDesc.View, materialBins, pages, &bins };
				inputs.Phase = VisibilityPhase::Early;
				const auto expectedEarly = RunVisibilityReference(inputs, referenceLods, referenceHistory);
				inputs.Phase = VisibilityPhase::Late;
				inputs.Hzb = &gpuHzb;
				const auto expectedLate = RunVisibilityReference(inputs, referenceLods, referenceHistory);
				SWIM_CHECK(std::memcmp(&earlyStats, &expectedEarly.Stats, sizeof(VisibilityStats)) == 0);
				SWIM_CHECK(std::memcmp(&lateStats, &expectedLate.Stats, sizeof(VisibilityStats)) == 0);
				SWIM_CHECK_EQUAL(earlyStats.Deferred, expectedEarly.Stats.Deferred);
				SWIM_CHECK_EQUAL(lateStats.Occluded, expectedLate.Stats.Occluded);
				SWIM_CHECK_EQUAL(lateStats.Visible, expectedLate.Stats.Visible);

				const auto gpuRows = [&](const std::vector<GpuDrawRecord>& records, std::uint32_t count)
				{
					std::set<std::uint32_t> rows;
					for (std::uint32_t slot = 0; slot < std::min(count, capacity); ++slot)
					{
						rows.insert(records[slot].InstanceRow);
					}
					return rows;
				};
				const auto drawnEarly = gpuRows(earlyDraws, earlyCount);
				const auto drawnLate = gpuRows(lateDraws, lateCount);
				SWIM_CHECK(drawnEarly == DrawnRows(expectedEarly));
				SWIM_CHECK(drawnLate == DrawnRows(expectedLate));
				for (const auto row : drawnLate)
				{
					SWIM_CHECK(!drawnEarly.contains(row)); // Never drawn twice.
				}

				// Pixels: each in-view grid object's sample point shows itself, or the wall when
				// the wall covers it; no pixel shows an object that was not drawn.
				const auto decode = [&](std::uint32_t x, std::uint32_t y)
				{
					const auto* p = pixels.data() + (std::size_t(y) * size + x) * 4;
					return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16);
				};
				std::set<std::uint32_t> drawnIds;
				for (const auto row : drawnEarly)
				{
					drawnIds.insert(instances[row].ObjectId);
				}
				for (const auto row : drawnLate)
				{
					drawnIds.insert(instances[row].ObjectId);
				}
				std::uint32_t samples = 0;
				for (std::uint32_t id = 0; id < side * side; ++id)
				{
					const float wx = float(id % side) * spacing - 0.5f;
					const float wy = float(id / side) * spacing - 0.5f;
					const float px = (wx - (center - 20.0f)) / 40.0f * float(size);
					const float py = (0.5f - (wy - center) / 40.0f) * float(size); // +Y-up NDC.
					if (px < 0.0f || py < 0.0f || px >= float(size) || py >= float(size))
					{
						continue;
					}
					const bool behindWall = std::abs(wx - wallX) < wallHalf && std::abs(wy - center) < wallHalf;
					SWIM_CHECK_EQUAL(decode(std::uint32_t(px), std::uint32_t(py)), (behindWall ? side * side : id) + 1);
					++samples;
				}
				SWIM_CHECK(samples > 200u);
				std::uint32_t strays = 0;
				for (std::uint32_t y = 0; y < size; ++y)
				{
					for (std::uint32_t x = 0; x < size; ++x)
					{
						const auto value = decode(x, y);
						strays += value != 0 && !drawnIds.contains(value - 1);
					}
				}
				SWIM_CHECK_EQUAL(strays, 0u);
				revealed = lateStats.Visible;
				return std::array<std::uint32_t, 3>{ earlyStats.Visible, lateStats.Visible, lateStats.Occluded };
			};

			// 17x17 grid spheres reach the 40x40 window, plus the wall. Of those, the 6x6 block
			// well inside the wall's footprint is occluded at HZB-texel granularity.
			const std::uint32_t inView = 17 * 17 + 1;

			// Frame 1: no history. Nothing is drawn early; the empty HZB occludes nothing,
			// so the late phase draws everything in view.
			auto counts = frame(0, center);
			SWIM_CHECK_EQUAL(counts[0], 0u);
			SWIM_CHECK_EQUAL(counts[1], inView);
			SWIM_CHECK_EQUAL(counts[2], 0u);
			// Frame 2: everything drawn early; the late test finds the covered objects.
			counts = frame(0, center);
			SWIM_CHECK_EQUAL(counts[0], inView);
			SWIM_CHECK_EQUAL(counts[1], 0u);
			// Frame 3: steady state. Covered objects are culled in both phases.
			counts = frame(0, center);
			const auto occluded = counts[2];
			SWIM_CHECK_EQUAL(occluded, 36u);
			SWIM_CHECK_EQUAL(counts[0] + occluded, inView);
			SWIM_CHECK_EQUAL(counts[1], 0u);
			// Frame 4: the wall teleports away. The revealed objects are drawn by the late
			// phase of this same frame (the pixel checks prove none is missing).
			scene.SetTransform(wall, wallAt(1000.0f));
			counts = frame(0, 1000.0f);
			SWIM_CHECK_EQUAL(revealed, occluded);
			// Frame 5: the wall returns with a camera cut: history is ignored, the early
			// phase draws everything in view and the late phase adds nothing.
			scene.SetTransform(wall, wallAt(center));
			counts = frame(std::uint32_t(GpuViewFlags::CameraCut), center);
			SWIM_CHECK_EQUAL(counts[0], inView);
			SWIM_CHECK_EQUAL(counts[1], 0u);

			scene.Collect();
			scene.Drain();
			heap.Drain();
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
				{ "RHI.Vulkan.Smoke", "GpuOcclusionTwoPhaseHzbRevealsNewlyVisibleObjects", SWIM_TEST_LOCATION,
					+[]
					{
						Swim::Testing::RunValidatedVulkanSmoke(&RunGpuOcclusionSmoke);
					} });
		}
		return true;
	}();
} // namespace
